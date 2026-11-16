/*
 * Erasure report (compact, one page per device) as PDF.
 * Uses PDFGen (public domain), the same library nwipe uses.
 */
#include "app.h"
#include "pdfgen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define PW 595.0f
#define PH 842.0f
#define ML 48.0f
#define MR 48.0f

#define C_INK PDF_RGB(0x16, 0x18, 0x1B)
#define C_MUTED PDF_RGB(0x5F, 0x64, 0x6B)
#define C_LINE PDF_RGB(0xDE, 0xDC, 0xD5)
#define C_BOX PDF_RGB(0xC9, 0xC6, 0xBD)
#define C_PAPER PDF_RGB(0xF7, 0xF6, 0xF3)
#define C_GREEN PDF_RGB(0x2B, 0x8A, 0x3E)
#define C_GREEN_DARK PDF_RGB(0x1B, 0x5E, 0x2A)
#define C_GREEN_SOFT PDF_RGB(0xE6, 0xF4, 0xEA)
#define C_RED PDF_RGB(0xC9, 0x2A, 0x2A)
#define C_RED_DARK PDF_RGB(0x8F, 0x1D, 0x1D)
#define C_RED_SOFT PDF_RGB(0xFB, 0xE5, 0xE5)
#define C_ACCENT PDF_RGB(0xD9, 0x48, 0x0F)

static struct pdf_doc * pdf;
static struct pdf_object * page;

static void font(const char * f)
{
    pdf_set_font(pdf, f);
}

static void text(const char * f, float size, float x, float y, uint32_t col, const char * s)
{
    font(f);
    pdf_add_text(pdf, page, s, size, x, y, col);
}

static float text_w(const char * f, float size, const char * s)
{
    float w = 0;
    pdf_get_font_text_width(pdf, f, s, size, &w);
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
    text("Helvetica-Bold", 8.5f, x, y, C_MUTED, s);
}

static void kv(float x, float y, float kw, float vw, const char * k, const char * v, const char * vf)
{
    text("Helvetica", 9.5f, x, y, C_MUTED, k);
    text_fit(vf ? vf : "Helvetica", 9.5f, x + kw, y, vw, C_INK, (v && v[0]) ? v : "–");
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

static void sanitize_name(char * s)
{
    for(char * p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) *p = '-';
    }
}

static void footer(int pageno, int pages)
{
    hline(ML, PW - MR, 46, 0.6f, C_LINE);
    text("Helvetica", 8, ML, 32, C_MUTED,
         "Generated automatically. Device and drive details from BIOS (DMI) and drive firmware.");
    char buf[64];
    snprintf(buf, sizeof(buf), "Page %d of %d", pageno, pages);
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
    snprintf(info.title, sizeof(info.title), "Erasure report %s", g.order_no);
    str_copy(info.author, g.tech.name, sizeof(info.author));
    snprintf(info.subject, sizeof(info.subject), "%s %s", g.sysname, g.dmi.serial);
    pdf = pdf_create(PW, PH, &info);
    if(!pdf) {
        str_copy(err, "Could not create PDF", errlen);
        return -1;
    }
    page = pdf_append_page(pdf);

    char buf[512], buf2[256];
    float y = PH - 52;

    /* ---- header ---- */
    pdf_add_filled_rectangle(pdf, page, ML, y - 2, 11, 11, 0, C_ACCENT, C_ACCENT);
    text("Helvetica-Bold", 10.5f, ML + 16, y, C_INK, "WipeOS");
    text("Helvetica-Bold", 22, ML, y - 28, C_INK, vo ? "Verification report" : "Erasure report");
    text("Helvetica", 9.5f, ML, y - 44, C_MUTED, "Certificate of secure data erasure");

    float rx = PW - MR;
    text_right("Helvetica-Bold", 9.5f, rx, y, C_INK, g.order_no);
    text_right("Helvetica", 9, rx - text_w("Helvetica-Bold", 9.5f, g.order_no) - 8, y, C_MUTED, "Job no.");
    fmt_time(g.wipe_end ? g.wipe_end : time(NULL), buf, sizeof(buf));
    text_right("Helvetica", 9, rx, y - 14, C_INK, buf);
    text_right("Helvetica", 9, rx - text_w("Helvetica", 9, buf) - 8, y - 14, C_MUTED, "Created");
    snprintf(buf, sizeof(buf), "1 of %d", pages);
    text_right("Helvetica", 9, rx, y - 28, C_INK, buf);
    text_right("Helvetica", 9, rx - text_w("Helvetica", 9, buf) - 8, y - 28, C_MUTED, "Page");

    y -= 58;
    hline(ML, PW - MR, y, 1.6f, C_INK);

    /* ---- result ---- */
    y -= 50;
    uint32_t band_bg = all_ok ? C_GREEN_SOFT : C_RED_SOFT;
    uint32_t band_fg = all_ok ? C_GREEN : C_RED;
    uint32_t band_tx = all_ok ? C_GREEN_DARK : C_RED_DARK;
    pdf_add_filled_rectangle(pdf, page, ML, y, PW - ML - MR, 40, 1.5f, band_bg, band_fg);
    pdf_add_filled_rectangle(pdf, page, ML + 12, y + 11, 18, 18, 0, band_fg, band_fg);
    if(all_ok) {
        pdf_add_line(pdf, page, ML + 16, y + 20, ML + 20, y + 15.5f, 2, PDF_WHITE);
        pdf_add_line(pdf, page, ML + 20, y + 15.5f, ML + 26.5f, y + 24, 2, PDF_WHITE);
        text("Helvetica-Bold", 11.5f, ML + 40, y + 23, band_tx, vo ? "Result: all drives verified successfully" : "Result: all drives successfully erased");
    }
    else {
        pdf_add_line(pdf, page, ML + 16, y + 15, ML + 26, y + 25, 2, PDF_WHITE);
        pdf_add_line(pdf, page, ML + 16, y + 25, ML + 26, y + 15, 2, PDF_WHITE);
        text("Helvetica-Bold", 11.5f, ML + 40, y + 23, band_tx, vo ? "Result: verification failed" : "Result: not all drives were erased");
    }
    snprintf(buf, sizeof(buf), vo ? "%d of %d drives read back and checked · %llu errors" : (g.verify ? "%d of %d drives erased and verified by read-back · %llu errors" : "%d of %d drives erased (no verification) · %llu errors"), ok_count, n,
             (unsigned long long)total_err);
    text("Helvetica", 9.5f, ML + 40, y + 9, band_tx, buf);

    /* ---- device ---- */
    y -= 26;
    section_title(ML, y, "DEVICE");
    y -= 8;
    float box_h = 92;
    float bx = ML, bw = PW - ML - MR, lw = bw * 0.48f;
    pdf_add_filled_rectangle(pdf, page, bx, y - box_h, lw, box_h, 0, C_PAPER, C_PAPER);
    pdf_add_rectangle(pdf, page, bx, y - box_h, bw, box_h, 0.8f, C_BOX);
    pdf_add_line(pdf, page, bx + lw, y, bx + lw, y - box_h, 0.8f, C_BOX);
    text("Helvetica", 9, bx + 12, y - 18, C_MUTED, "System name");
    text_fit("Helvetica-Bold", 16, bx + 12, y - 36, lw - 24, C_INK, g.sysname);
    text("Helvetica", 9, bx + 12, y - 56, C_MUTED, "Device serial number");
    text_fit("Helvetica-Bold", 15, bx + 12, y - 74, lw - 24, C_INK, g.dmi.serial);
    float kx = bx + lw + 12, kw = 72, vw = bw - lw - 24 - kw;
    float ky = y - 16;
    kv(kx, ky, kw, vw, "Manufacturer", g.dmi.vendor, "Helvetica-Bold");
    kv(kx, ky - 12.5f, kw, vw, "Model", g.dmi.product, "Helvetica-Bold");
    kv(kx, ky - 25, kw, vw, "Device type", g.dmi.chassis, NULL);
    kv(kx, ky - 37.5f, kw, vw, "Asset tag", g.asset, NULL);
    snprintf(buf, sizeof(buf), "%s%s%s", g.dmi.bios, g.dmi.bios_date[0] ? " · " : "", g.dmi.bios_date);
    kv(kx, ky - 50, kw, vw, "BIOS", buf, NULL);
    kv(kx, ky - 62.5f, kw, vw, "UUID", g.dmi.uuid, "Helvetica");
    y -= box_h;

    /* ---- drives ---- */
    y -= 26;
    section_title(ML, y, vo ? "VERIFIED DRIVES" : "ERASED DRIVES");
    y -= 16;
    float cx[6] = {ML, ML + 158, ML + 282, ML + 322, ML + 384, ML + 440};
    const char * hdr[6] = {"Model", "Serial number", "Type", "Capacity", "Duration", "Result"};
    for(int c = 0; c < 6; c++) {
        if(c == 3) text_right("Helvetica-Bold", 8.5f, cx[4] - 10, y, C_INK, hdr[c]);
        else text("Helvetica-Bold", 8.5f, cx[c], y, C_INK, hdr[c]);
    }
    y -= 6;
    hline(ML, PW - MR, y, 0.9f, C_INK);
    int pageno = 1;
    for(int i = 0; i < n; i++) {
        if((pageno == 1 && i == 9) || (pageno > 1 && y < 90)) {
            footer(pageno, pages);
            page = pdf_append_page(pdf);
            pageno++;
            y = PH - 60;
            snprintf(buf, sizeof(buf), "Erasure report %s · continued", g.order_no);
            text("Helvetica-Bold", 12, ML, y, C_INK, buf);
            y -= 24;
            for(int c = 0; c < 6; c++) {
                if(c == 3) text_right("Helvetica-Bold", 8.5f, cx[4] - 10, y, C_INK, hdr[c]);
                else text("Helvetica-Bold", 8.5f, cx[c], y, C_INK, hdr[c]);
            }
            y -= 6;
            hline(ML, PW - MR, y, 0.9f, C_INK);
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
        if(drive_ok(d)) text("Helvetica-Bold", 9, cx[5], y, C_GREEN_DARK, vo ? "Verified" : "Erased");
        else if(d->status != 0) text("Helvetica-Bold", 9, cx[5], y, C_RED_DARK, "Aborted");
        else text("Helvetica-Bold", 9, cx[5], y, C_RED_DARK, "Failed");
        y -= 6;
        hline(ML, PW - MR, y, 0.5f, C_LINE);
    }
    if(n == 0) {
        y -= 15;
        text("Helvetica", 9, ML, y, C_MUTED, "No drives erased.");
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
    float colw = (PW - ML - MR - 24) / 2;
    float c2 = ML + colw + 24;
    section_title(ML, y, "ERASURE");
    section_title(c2, y, "CUSTOMER & TECHNICIAN");
    y -= 16;
    float yy = y;
    time_t t0 = 0, t1 = 0;
    for(int i = 0; i < n; i++) {
        if(st->d[i].start > 0 && (t0 == 0 || st->d[i].start < t0)) t0 = st->d[i].start;
        if(st->d[i].end > t1) t1 = st->d[i].end;
    }
    if(t0 == 0) t0 = g.wipe_start;
    if(t1 == 0) t1 = g.wipe_end;
    fmt_time(t0, buf, sizeof(buf));
    kv(ML, yy, 92, colw - 92, "Started", buf, NULL);
    yy -= 13;
    fmt_time(t1, buf, sizeof(buf));
    kv(ML, yy, 92, colw - 92, "Finished", buf, NULL);
    yy -= 13;
    format_duration(t1 > t0 ? (long long)(t1 - t0) : 0, buf, sizeof(buf));
    kv(ML, yy, 92, colw - 92, "Duration", buf, NULL);
    yy -= 13;
    kv(ML, yy, 92, colw - 92, "Method", method_label(g.method), NULL);
    yy -= 13;
    if(method_is_verify_only(g.method)) str_copy(buf, "read only", sizeof(buf));
    else snprintf(buf, sizeof(buf), "%d round%s%s · %s", g.rounds, g.rounds == 1 ? "" : "s", g.blank && g.method != METHOD_OPS2 ? " + blanking" : "",
                      g.verify == 2 ? "all passes verified" : (g.verify == 1 ? "last pass verified" : "not verified"));
    kv(ML, yy, 92, colw - 92, "Passes", buf, NULL);
    yy -= 13;
    kv(ML, yy, 92, colw - 92, "Random generator", method_is_verify_only(g.method) || g.method == METHOD_ZERO || g.method == METHOD_ONE ? "not used" : (st->prng[0] ? st->prng : prng_label(g.prng)), NULL);
    yy -= 13;
    {
        int hpa_any = 0, hpa_none = 0;
        for(int i = 0; i < n; i++) {
            if(st->d[i].hpa_status == 1) hpa_any = 1;
            else if(st->d[i].hpa_status == 0 || strcmp(st->d[i].bus, "NVME") == 0) hpa_none = 1;
        }
        kv(ML, yy, 92, colw - 92, "HPA / DCO", hpa_any ? "detected" : (hpa_none ? "none" : "not checkable"), NULL);
    }
    yy -= 13;
    snprintf(buf, sizeof(buf), "%llu (write / sync / verify)", (unsigned long long)total_err);
    kv(ML, yy, 92, colw - 92, "Errors", buf, NULL);
    yy -= 13;
    snprintf(buf, sizeof(buf), "nwipe %s · WipeOS", st->version[0] ? st->version : "");
    kv(ML, yy, 92, colw - 92, "Software", buf, NULL);

    yy = y;
    kv(c2, yy, 80, colw - 72, "Customer", g.customer.name, "Helvetica-Bold");
    yy -= 13;
    kv(c2, yy, 80, colw - 72, "Customer no.", g.customer.no, "Helvetica");
    yy -= 13;
    snprintf(buf, sizeof(buf), "%s%s%s %s", g.customer.street, (g.customer.street[0] && (g.customer.zip[0] || g.customer.city[0])) ? ", " : "",
             g.customer.city, g.customer.zip);
    str_trim(buf);
    kv(c2, yy, 80, colw - 72, "Address", buf, NULL);
    yy -= 13;
    kv(c2, yy, 80, colw - 72, "Contact person", g.customer.contact, NULL);
    yy -= 13;
    snprintf(buf, sizeof(buf), "%s (%s)", g.tech.name, g.tech.id);
    kv(c2, yy, 80, colw - 72, "Technician", buf, "Helvetica-Bold");
    yy -= 13;
    kv(c2, yy, 80, colw - 72, "Performed by", g.org_name[0] ? g.org_name : "–", NULL);
    yy -= 13;
    if(g.org_address[0]) {
        kv(c2, yy, 80, colw - 72, "", g.org_address, NULL);
        yy -= 13;
    }
    if(g.customer.ref[0]) {
        kv(c2, yy, 80, colw - 72, "Reference", g.customer.ref, NULL);
        yy -= 13;
    }
    y -= 9 * 13 + 4;
    if(g.note[0]) {
        y -= 14;
        text("Helvetica", 9.5f, ML, y, C_MUTED, "Note");
        text_fit("Helvetica", 9.5f, ML + 92, y, PW - ML - MR - 92, C_INK, g.note);
    }

    /* ---- signatures ---- */
    float sy = 110;
    float sw = (PW - ML - MR - 40) / 2;
    hline(ML, ML + sw, sy, 0.8f, C_INK);
    hline(ML + sw + 40, PW - MR, sy, 0.8f, C_INK);
    snprintf(buf, sizeof(buf), "%s (%s) · Technician", g.tech.name, g.tech.id);
    text_fit("Helvetica", 9, ML, sy - 13, sw, C_MUTED, buf);
    if(g.customer.contact[0]) snprintf(buf, sizeof(buf), "%s · %s", g.customer.contact, g.customer.name);
    else snprintf(buf, sizeof(buf), "%s · Customer", g.customer.name);
    text_fit("Helvetica", 9, ML + sw + 40, sy - 13, sw, C_MUTED, buf);

    footer(pageno, pages);

    /* ---- save ---- */
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/../reports", storage_dir());
    mkdir(dir, 0755);
    char sys[96], ser[96];
    str_copy(sys, g.sysname, sizeof(sys));
    str_copy(ser, g.dmi.serial, sizeof(ser));
    sanitize_name(sys);
    sanitize_name(ser);
    snprintf(out_path, len, "%s/%s_%s_%s.pdf", dir, g.order_no, sys, ser);
    int r = pdf_save(pdf, out_path);
    if(r < 0) {
        snprintf(err, errlen, "Saving failed: %s", pdf_get_err(pdf, NULL));
    }
    pdf_destroy(pdf);
    pdf = NULL;
    storage_sync();
    return r < 0 ? -1 : 0;
}
