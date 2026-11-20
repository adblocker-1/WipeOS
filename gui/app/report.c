/*
 * Erasure report as PDF – "company letterhead" layout:
 * company logo + address on top, result stamp, customer / device,
 * drives, erasure details, signatures, small WipeOS mark in the footer.
 * Uses PDFGen (public domain), the same library nwipe uses.
 */
#include "app.h"
#include "pdfgen.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define PW 595.0f
#define PH 842.0f
#define ML 40.0f
#define MR 40.0f

#define C_NAVY PDF_RGB(0x0E, 0x1E, 0x38)
#define C_INK PDF_RGB(0x14, 0x24, 0x3D)
#define C_MUTED PDF_RGB(0x5A, 0x65, 0x78)
#define C_LINE PDF_RGB(0xD9, 0xDE, 0xE5)
#define C_SOFT PDF_RGB(0xF3, 0xF5, 0xF8)
#define C_MAG PDF_RGB(0xA2, 0x3F, 0x79)
#define C_GREEN PDF_RGB(0x1F, 0x7A, 0x3A)
#define C_RED PDF_RGB(0xC9, 0x2A, 0x2A)

/* company logo area in the letterhead (points) */
#define LOGO_BOX_W 200.0f
#define LOGO_BOX_H 52.0f

static struct pdf_doc * pdf;
static struct pdf_object * page;

/* Embedded TrueType fonts (subset of IBM Plex Sans, covers Latin, Latin Extended-A and
 * Cyrillic). Without the font files the standard Helvetica is used (Western European only). */
static char s_ttf_reg[300], s_ttf_bold[300];
static int s_ttf = 0;

static void find_fonts(void)
{
    const char * dirs[] = {getenv("SHREDOS_FONTS"), "/usr/share/wipeos/fonts", "fonts", "../fonts"};
    s_ttf = 0;
    for(size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        if(!dirs[i]) continue;
        struct stat st;
        snprintf(s_ttf_reg, sizeof(s_ttf_reg), "%s/WipeOS-Sans-Regular.ttf", dirs[i]);
        snprintf(s_ttf_bold, sizeof(s_ttf_bold), "%s/WipeOS-Sans-Bold.ttf", dirs[i]);
        if(stat(s_ttf_reg, &st) == 0 && stat(s_ttf_bold, &st) == 0) {
            s_ttf = 1;
            return;
        }
    }
}

/* selects the font and returns the name to use for width calculations */
static const char * font(const char * f)
{
    if(s_ttf) {
        const char * n = pdf_set_font_ttf(pdf, strstr(f, "Bold") ? s_ttf_bold : s_ttf_reg);
        if(n) return n;
        s_ttf = 0;
    }
    pdf_set_font(pdf, f);
    return f;
}

static void text(const char * f, float size, float x, float y, uint32_t col, const char * s)
{
    font(f);
    pdf_add_text(pdf, page, s, size, x, y, col);
}

static float text_w(const char * f, float size, const char * s)
{
    float w = 0;
    pdf_get_font_text_width(pdf, font(f), s, size, &w);
    return w;
}

static void text_right(const char * f, float size, float xr, float y, uint32_t col, const char * s)
{
    text(f, size, xr - text_w(f, size, s), y, col, s);
}

/* truncate text to a maximum width */
static void text_fit(const char * f, float size, float x, float y, float maxw, uint32_t col, const char * s)
{
    char buf[256];
    str_copy(buf, s, sizeof(buf));
    /* long text: first use a slightly smaller size (down to 80 %), then cut */
    for(int i = 0; i < 4 && text_w(f, size, buf) > maxw; i++) size *= 0.95f;
    if(text_w(f, size, buf) > maxw) {
        size_t n = strlen(buf);
        while(n > 3 && text_w(f, size, buf) > maxw) {
            n--;
            while(n > 0 && (buf[n] & 0xC0) == 0x80) n--; /* do not split UTF-8 sequences */
            buf[n] = '\0';
            strcat(buf, "…");
            if(text_w(f, size, buf) <= maxw) break;
            buf[n] = '\0';
        }
    }
    text(f, size, x, y, col, buf);
}

static void hline(float x1, float x2, float y, float w, uint32_t col)
{
    pdf_add_line(pdf, page, x1, y, x2, y, w, col);
}

static void section_title(float x, float y, const char * s)
{
    text("Helvetica-Bold", 8.5f, x, y, C_MAG, R_(s));
}

static void kv(float x, float y, float kw, float vw, const char * k, const char * v, const char * vf)
{
    text_fit("Helvetica", 9.5f, x, y, kw - 4, C_MUTED, R_(k));
    text_fit(vf ? vf : "Helvetica", 9.5f, x + kw, y, vw, C_INK, (v && v[0]) ? v : "–");
}

/* width of the label column: widest translated label plus a gap */
static float key_w(const char * const * keys, int n, float maxw)
{
    float w = 0;
    for(int i = 0; i < n; i++) {
        float t = text_w("Helvetica", 9.5f, R_(keys[i]));
        if(t > w) w = t;
    }
    w += 10;
    return w > maxw ? maxw : w;
}

static void fmt_time(time_t t, char * out, size_t len)
{
    if(t <= 0) {
        str_copy(out, "–", len);
        return;
    }
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, len, "%Y-%m-%d %H:%M", &tm);
}

static int drive_ok(const wipe_drive_t * d)
{
    return d->status == 0 && d->result == 0 && d->pass_errors == 0 && d->verify_errors == 0 && d->fsync_errors == 0;
}

/* file name part: ASCII letters, digits, - and _; every other character becomes one '-' */
static void sanitize_name(char * s)
{
    char * w = s;
    for(const char * p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if((c & 0xC0) == 0x80) continue; /* continuation byte of a UTF-8 character */
        if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') *w++ = (char)c;
        else *w++ = '-';
    }
    *w = '\0';
}

static void add_image(const img_t * im, float x, float y, float w, float h)
{
    size_t jl = 0;
    uint8_t * jpg = img_to_jpeg(im, 92, &jl);
    if(jpg && pdf_add_image_data(pdf, page, x, y, w, h, jpg, jl) >= 0) {
        free(jpg);
        return;
    }
    free(jpg);
    uint8_t * rgb = img_to_rgb_on_white(im);
    if(!rgb) return;
    pdf_add_rgb24(pdf, page, x, y, w, h, rgb, (uint32_t)im->w, (uint32_t)im->h);
    free(rgb);
}

static void footer(int pageno, int pages)
{
    hline(ML, PW - MR, 46, 0.6f, C_LINE);
    img_t wl;
    float tx = ML;
    if(img_wipeos_logo(0, &wl) == 0) {
        add_image(&wl, ML, 26, 15, 15);
        img_free(&wl);
        tx = ML + 20;
    }
    text("Helvetica", 8, tx, 30, C_MUTED,
         R_("Erased with WipeOS · device and drive details from BIOS (DMI) and drive firmware"));
    char buf[64];
    snprintf(buf, sizeof(buf), R_("Page %d of %d"), pageno, pages);
    text_right("Helvetica", 8, PW - MR, 32, C_MUTED, buf);
}

int report_create(char * out_path, size_t len, char * err, size_t errlen)
{
    const wipe_status_t * st = &g.status;
    int n = st->count;
    int ok_count = 0;
    uint64_t total_err = 0;
    for(int i = 0; i < n; i++) {
        if(drive_ok(&st->d[i])) ok_count++;
        total_err += st->d[i].pass_errors + st->d[i].verify_errors + st->d[i].fsync_errors;
    }
    int all_ok = (n > 0 && ok_count == n);
    int vo = method_is_verify_only(g.method);


    /* page count: up to 9 drives on page 1, 30 more per following page */
    int pages = 1;
    if(n > 9) pages += (n - 9 + 29) / 30;

    struct pdf_info info;
    memset(&info, 0, sizeof(info));
    str_copy(info.creator, "WipeOS GUI", sizeof(info.creator));
    str_copy(info.producer, "WipeOS / nwipe", sizeof(info.producer));
    snprintf(info.title, sizeof(info.title), R_("Erasure report %s"), g.order_no);
    str_copy(info.author, g.tech.name, sizeof(info.author));
    snprintf(info.subject, sizeof(info.subject), "%s %s", g.sysname, g.dmi.serial);
    pdf = pdf_create(PW, PH, &info);
    find_fonts();
    if(!pdf) {
        str_copy(err, R_("Could not create PDF"), errlen);
        return -1;
    }
    page = pdf_append_page(pdf);

    char buf[512], buf2[256];
    float top = PH - 36;

    /* ---- letterhead: company logo left, company details right ---- */
    float hdr_h = 0;
    img_t logo;
    char lpath[512];
    char lerr[128];
    int have_logo = storage_logo_path(lpath, sizeof(lpath)) == 0 && img_load(lpath, 720, 240, &logo, lerr, sizeof(lerr)) == 0;
    float rx = PW - MR;
    if(have_logo) {
        float dw = LOGO_BOX_W, dh = dw * logo.h / logo.w;
        if(dh > LOGO_BOX_H) {
            dh = LOGO_BOX_H;
            dw = dh * logo.w / logo.h;
        }
        add_image(&logo, ML, top - dh, dw, dh);
        img_free(&logo);
        hdr_h = dh;
    }
    else if(g.org_name[0]) {
        text_fit("Helvetica-Bold", 16, ML, top - 15, 300, C_NAVY, g.org_name);
        hdr_h = 20;
    }
    else {
        img_t wl;
        if(img_wipeos_logo(1, &wl) == 0) {
            add_image(&wl, ML, top - 34, 34, 34);
            img_free(&wl);
        }
        text("Helvetica-Bold", 16, ML + 42, top - 22, C_NAVY, "WipeOS");
        hdr_h = 34;
    }
    {
        float ry = top - 8;
        if(g.org_name[0] && have_logo) {
            text_right("Helvetica-Bold", 8.5f, rx, ry, C_INK, g.org_name);
            ry -= 11;
        }
        if(g.org_address[0]) {
            text_right("Helvetica", 8.5f, rx, ry, C_MUTED, g.org_address);
            ry -= 11;
        }
        if(g.org_contact[0]) {
            text_right("Helvetica", 8.5f, rx, ry, C_MUTED, g.org_contact);
            ry -= 11;
        }
        if(top - ry - 3 > hdr_h) hdr_h = top - ry - 3;
    }

    /* ---- title + result stamp ---- */
    float y = top - hdr_h - 40;
    text("Helvetica-Bold", 21, ML, y, C_NAVY, vo ? R_("Verification report") : R_("Erasure report"));
    fmt_time(g.wipe_end ? g.wipe_end : time(NULL), buf2, sizeof(buf2));
    snprintf(buf, sizeof(buf), R_("Job %s · %s"), g.order_no, buf2);
    text("Helvetica", 9.5f, ML, y - 16, C_MUTED, buf);
    {
        const char * st_txt = all_ok ? (vo ? R_("VERIFIED") : (g.verify ? R_("ERASED · VERIFIED") : R_("ERASED"))) : (vo ? R_("VERIFICATION FAILED") : R_("NOT ERASED"));
        uint32_t sc = all_ok ? C_GREEN : C_RED;
        float fs = 11;
        float tw = text_w("Helvetica-Bold", fs, st_txt);
        float bw = tw + 46, bh = 26;
        float cxs = rx - bw / 2 - 4, cys = y - 2;
        float ang = 3.0f * (float)M_PI / 180.0f;
        float ca = cosf(ang), sa = sinf(ang);
#define RX(px, py) (cxs + (px) * ca - (py) * sa)
#define RY(px, py) (cys + (px) * sa + (py) * ca)
        float px[4] = {RX(-bw / 2, -bh / 2), RX(bw / 2, -bh / 2), RX(bw / 2, bh / 2), RX(-bw / 2, bh / 2)};
        float py[4] = {RY(-bw / 2, -bh / 2), RY(bw / 2, -bh / 2), RY(bw / 2, bh / 2), RY(-bw / 2, bh / 2)};
        pdf_add_polygon(pdf, page, px, py, 4, 1.8f, sc);
        float ix = -bw / 2 + 12;
        if(all_ok) {
            pdf_add_line(pdf, page, RX(ix, 0), RY(ix, 0), RX(ix + 4, -4.5f), RY(ix + 4, -4.5f), 1.8f, sc);
            pdf_add_line(pdf, page, RX(ix + 4, -4.5f), RY(ix + 4, -4.5f), RX(ix + 11, 4.5f), RY(ix + 11, 4.5f), 1.8f, sc);
        }
        else {
            pdf_add_line(pdf, page, RX(ix, -4.5f), RY(ix, -4.5f), RX(ix + 9, 4.5f), RY(ix + 9, 4.5f), 1.8f, sc);
            pdf_add_line(pdf, page, RX(ix, 4.5f), RY(ix, 4.5f), RX(ix + 9, -4.5f), RY(ix + 9, -4.5f), 1.8f, sc);
        }
        font("Helvetica-Bold");
        pdf_add_text_rotate(pdf, page, st_txt, fs, RX(ix + 19, -4), RY(ix + 19, -4), ang, sc);
#undef RX
#undef RY
    }
    y -= 30;
    hline(ML, PW - MR, y, 1.5f, C_NAVY);
    y -= 14;
    snprintf(buf, sizeof(buf), vo ? R_("%d of %d drives read back and checked · %llu errors") :
             (g.verify ? R_("%d of %d drives erased and verified by read-back · %llu errors") : R_("%d of %d drives erased (no verification) · %llu errors")),
             ok_count, n, (unsigned long long)total_err);
    text("Helvetica-Bold", 9.5f, ML, y, all_ok ? C_GREEN : C_RED, buf);

    /* ---- customer | device ---- */
    y -= 26;
    float colw = (PW - ML - MR - 28) / 2;
    float c2 = ML + colw + 28;
    section_title(ML, y, "CUSTOMER");
    section_title(c2, y, "DEVICE");
    y -= 16;
    float yy = y;
    static const char * const k_cust[] = {"Customer", "Customer no.", "Address", "Contact", "Phone / email", "Reference"};
    static const char * const k_dev[] = {"System name", "Serial number", "Manufacturer", "Model", "Device type", "Asset tag", "BIOS", "UUID"};
    float kw = key_w(k_cust, 6, colw * 0.5f);
    float kwd = key_w(k_dev, 8, colw * 0.5f);
    kv(ML, yy, kw, colw - kw, "Customer", g.customer.name, "Helvetica-Bold");
    yy -= 13;
    kv(ML, yy, kw, colw - kw, "Customer no.", g.customer.no, NULL);
    yy -= 13;
    snprintf(buf, sizeof(buf), "%s%s%s %s", g.customer.street, (g.customer.street[0] && (g.customer.zip[0] || g.customer.city[0])) ? ", " : "",
             g.customer.city, g.customer.zip);
    str_trim(buf);
    kv(ML, yy, kw, colw - kw, "Address", buf, NULL);
    yy -= 13;
    kv(ML, yy, kw, colw - kw, "Contact", g.customer.contact, NULL);
    yy -= 13;
    kv(ML, yy, kw, colw - kw, "Phone / email", g.customer.phone, NULL);
    yy -= 13;
    if(g.customer.ref[0]) kv(ML, yy, kw, colw - kw, "Reference", g.customer.ref, NULL);

    yy = y;
    kv(c2, yy, kwd, colw - kwd, "System name", g.sysname, "Helvetica-Bold");
    yy -= 13;
    kv(c2, yy, kwd, colw - kwd, "Serial number", g.dmi.serial, "Helvetica-Bold");
    yy -= 13;
    kv(c2, yy, kwd, colw - kwd, "Manufacturer", g.dmi.vendor, NULL);
    yy -= 13;
    kv(c2, yy, kwd, colw - kwd, "Model", g.dmi.product, NULL);
    yy -= 13;
    kv(c2, yy, kwd, colw - kwd, "Device type", R_(g.dmi.chassis), NULL);
    yy -= 13;
    kv(c2, yy, kwd, colw - kwd, "Asset tag", g.asset, NULL);
    yy -= 13;
    snprintf(buf, sizeof(buf), "%s%s%s", g.dmi.bios, g.dmi.bios_date[0] ? " · " : "", g.dmi.bios_date);
    kv(c2, yy, kwd, colw - kwd, "BIOS", buf, NULL);
    yy -= 13;
    kv(c2, yy, kwd, colw - kwd, "UUID", g.dmi.uuid, NULL);
    y -= 8 * 13;

    /* ---- drives ---- */
    y -= 26;
    section_title(ML, y, vo ? "VERIFIED DRIVES" : "ERASED DRIVES");
    y -= 18;
    float cx[6] = {ML + 6, ML + 158, ML + 280, ML + 322, ML + 392, ML + 452};
    const char * hdr[6] = {R_("Model"), R_("Serial number"), R_("Type"), R_("Capacity"), R_("Duration"), R_("Result")};
    pdf_add_filled_rectangle(pdf, page, ML, y - 6, PW - ML - MR, 18, 0, C_SOFT, C_SOFT);
    for(int c = 0; c < 6; c++) {
        if(c == 3) text_right("Helvetica-Bold", 8.5f, cx[4] - 10, y, C_INK, hdr[c]);
        else text_fit("Helvetica-Bold", 8.5f, cx[c], y, (c < 5 ? cx[c + 1] : PW - MR) - cx[c] - 4, C_INK, hdr[c]);
    }
    y -= 6;
    hline(ML, PW - MR, y, 0.9f, C_NAVY);
    int pageno = 1;
    for(int i = 0; i < n; i++) {
        if((pageno == 1 && i == 9) || (pageno > 1 && y < 90)) {
            footer(pageno, pages);
            page = pdf_append_page(pdf);
            pageno++;
            y = PH - 60;
            snprintf(buf, sizeof(buf), R_("Erasure report %s · continued"), g.order_no);
            text("Helvetica-Bold", 12, ML, y, C_NAVY, buf);
            y -= 24;
            for(int c = 0; c < 6; c++) {
                if(c == 3) text_right("Helvetica-Bold", 8.5f, cx[4] - 10, y, C_INK, hdr[c]);
                else text_fit("Helvetica-Bold", 8.5f, cx[c], y, (c < 5 ? cx[c + 1] : PW - MR) - cx[c] - 4, C_INK, hdr[c]);
            }
            y -= 6;
            hline(ML, PW - MR, y, 0.9f, C_NAVY);
        }
        const wipe_drive_t * d = &st->d[i];
        const char * model = d->model[0] ? d->model : d->device;
        for(int k = 0; k < g.drive_count; k++)
            if(strcmp(g.drives[k].dev, d->device) == 0 && strlen(g.drives[k].model) > strlen(model)) model = g.drives[k].model;
        y -= 15;
        text_fit("Helvetica", 9, cx[0], y, cx[1] - cx[0] - 8, C_INK, model);
        text_fit("Helvetica", 8.5f, cx[1], y, cx[2] - cx[1] - 6, C_INK, d->serial[0] ? d->serial : "–");
        text_fit("Helvetica", 9, cx[2], y, cx[3] - cx[2] - 4, C_INK, d->bus);
        format_size(d->size, buf2, sizeof(buf2));
        text_right("Helvetica", 9, cx[4] - 10, y, C_INK, buf2);
        format_duration(d->end > d->start ? (long long)(d->end - d->start) : -1, buf2, sizeof(buf2));
        text_fit("Helvetica", 9, cx[4], y, cx[5] - cx[4] - 6, C_INK, buf2);
        if(drive_ok(d)) text("Helvetica-Bold", 9, cx[5], y, C_GREEN, vo ? R_("Verified") : R_("Erased"));
        else if(d->status != 0) text("Helvetica-Bold", 9, cx[5], y, C_RED, R_("Aborted"));
        else text("Helvetica-Bold", 9, cx[5], y, C_RED, R_("Failed"));
        y -= 6;
        hline(ML, PW - MR, y, 0.5f, C_LINE);
    }
    if(n == 0) {
        y -= 15;
        text("Helvetica", 9, ML, y, C_MUTED, R_("No drives erased."));
        y -= 6;
    }

    if(pageno > 1) {
        footer(pageno, pages);
        /* remaining sections continue on the last page */
    }

    /* ---- erasure / customer & technician ---- */
    if(y < 300) {
        footer(pageno, pages);
        page = pdf_append_page(pdf);
        pageno++;
        y = PH - 60;
    }
    y -= 26;
    section_title(ML, y, vo ? "VERIFICATION" : "ERASURE");
    y -= 16;
    yy = y;
    time_t t0 = 0, t1 = 0;
    for(int i = 0; i < n; i++) {
        if(st->d[i].start > 0 && (t0 == 0 || st->d[i].start < t0)) t0 = st->d[i].start;
        if(st->d[i].end > t1) t1 = st->d[i].end;
    }
    if(t0 == 0) t0 = g.wipe_start;
    if(t1 == 0) t1 = g.wipe_end;
    static const char * const k_er1[] = {"Started", "Finished", "Duration", "Method", "Passes"};
    static const char * const k_er2[] = {"Random generator", "HPA / DCO", "Errors", "Software", "Technician"};
    kw = key_w(k_er1, 5, colw * 0.5f);
    kwd = key_w(k_er2, 5, colw * 0.5f);
    fmt_time(t0, buf, sizeof(buf));
    kv(ML, yy, kw, colw - kw, "Started", buf, NULL);
    yy -= 13;
    fmt_time(t1, buf, sizeof(buf));
    kv(ML, yy, kw, colw - kw, "Finished", buf, NULL);
    yy -= 13;
    format_duration(t1 > t0 ? (long long)(t1 - t0) : 0, buf, sizeof(buf));
    kv(ML, yy, kw, colw - kw, "Duration", buf, NULL);
    yy -= 13;
    kv(ML, yy, kw, colw - kw, "Method", method_label(g.method), "Helvetica-Bold");
    yy -= 13;
    if(method_is_verify_only(g.method)) str_copy(buf, R_("read only"), sizeof(buf));
    else {
        char rr[64];
        int bl = g.blank && g.method != METHOD_OPS2;
        snprintf(rr, sizeof(rr), R_("Rounds: %d"), g.rounds);
        snprintf(buf, sizeof(buf), "%s%s%s · %s", rr, bl ? " + " : "", bl ? R_("zero blanking") : "",
                 g.verify == 2 ? R_("all passes verified") : (g.verify == 1 ? R_("last pass verified") : R_("not verified")));
    }
    kv(ML, yy, kw, colw - kw, "Passes", buf, NULL);

    yy = y;
    kv(c2, yy, kwd, colw - kwd, "Random generator", method_is_verify_only(g.method) || g.method == METHOD_ZERO || g.method == METHOD_ONE ? R_("not used") : (st->prng[0] ? st->prng : prng_label(g.prng)), NULL);
    yy -= 13;
    {
        int hpa_any = 0, hpa_none = 0;
        for(int i = 0; i < n; i++) {
            if(st->d[i].hpa_status == 1) hpa_any = 1;
            else if(st->d[i].hpa_status == 0 || strcmp(st->d[i].bus, "NVME") == 0) hpa_none = 1;
        }
        kv(c2, yy, kwd, colw - kwd, "HPA / DCO", hpa_any ? R_("detected") : (hpa_none ? R_("none") : R_("not checkable")), NULL);
    }
    yy -= 13;
    snprintf(buf, sizeof(buf), R_("%llu (write / sync / verify)"), (unsigned long long)total_err);
    kv(c2, yy, kwd, colw - kwd, "Errors", buf, NULL);
    yy -= 13;
    snprintf(buf, sizeof(buf), "nwipe %s · WipeOS", st->version[0] ? st->version : "");
    kv(c2, yy, kwd, colw - kwd, "Software", buf, NULL);
    yy -= 13;
    snprintf(buf, sizeof(buf), "%s (%s)", g.tech.name, g.tech.id);
    kv(c2, yy, kwd, colw - kwd, "Technician", buf, "Helvetica-Bold");
    y -= 5 * 13;
    if(g.note[0]) {
        y -= 8;
        {
            float nw = text_w("Helvetica", 9.5f, R_("Note")) + 10;
            text("Helvetica", 9.5f, ML, y, C_MUTED, R_("Note"));
            text_fit("Helvetica", 9.5f, ML + nw, y, PW - ML - MR - nw, C_INK, g.note);
        }
    }

    /* ---- signatures ---- */
    float sy = 110;
    float sw = (PW - ML - MR - 40) / 2;
    hline(ML, ML + sw, sy, 0.8f, C_INK);
    hline(ML + sw + 40, PW - MR, sy, 0.8f, C_INK);
    snprintf(buf, sizeof(buf), "%s (%s) · %s", g.tech.name, g.tech.id, R_("Technician"));
    text_fit("Helvetica", 9, ML, sy - 13, sw, C_MUTED, buf);
    if(g.customer.contact[0]) snprintf(buf, sizeof(buf), "%s · %s", g.customer.contact, g.customer.name);
    else snprintf(buf, sizeof(buf), "%s · %s", g.customer.name, R_("Customer"));
    text_fit("Helvetica", 9, ML + sw + 40, sy - 13, sw, C_MUTED, buf);

    footer(pageno, pages);

    /* ---- save ---- */
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", storage_report_dir());
    mkdir(dir, 0755);
    char sys[96], ser[96];
    str_copy(sys, g.sysname, sizeof(sys));
    str_copy(ser, g.dmi.serial, sizeof(ser));
    sanitize_name(sys);
    sanitize_name(ser);
    snprintf(out_path, len, "%s/%s_%s_%s.pdf", dir, g.order_no, sys, ser);
    int r = pdf_save(pdf, out_path);
    if(r < 0) {
        snprintf(err, errlen, R_("Saving failed: %s"), pdf_get_err(pdf, NULL));
    }
    pdf_destroy(pdf);
    pdf = NULL;
    storage_sync();
    return r < 0 ? -1 : 0;
}
