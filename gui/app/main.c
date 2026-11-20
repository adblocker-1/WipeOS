/*
 * WipeOS GUI - entry point.
 * Normal mode: draws directly to the framebuffer (/dev/fb0).
 * Test mode (SHREDOS_HEADLESS=WxH): draws to memory and saves
 * screenshots as listed in SHREDOS_TEST_SCRIPT.
 */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
#include <linux/fb.h>
#include <sys/mman.h>

app_state_t g;
static volatile int s_quit = 0;
static int s_exit_code = 0;

void app_quit(int code)
{
    s_exit_code = code;
    s_quit = 1;
}
static int s_tty = -1;

/* from ui.c, for tests */
void ui_test_action(const char * cmd);

static void on_signal(int sig)
{
    (void)sig;
    s_quit = 1;
}

static uint32_t tick_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void console_graphics(int on)
{
    if(s_tty < 0) s_tty = open("/dev/tty0", O_RDWR | O_CLOEXEC);
    if(s_tty < 0) s_tty = open("/dev/tty", O_RDWR | O_CLOEXEC);
    if(s_tty >= 0) ioctl(s_tty, KDSETMODE, on ? KD_GRAPHICS : KD_TEXT);
}

/* ---- framebuffer output with integer scaling for high-resolution displays ---- */
static uint8_t * s_fb = NULL;
static int s_fb_w, s_fb_h, s_fb_bpp, s_fb_line, s_fb_xoff, s_fb_yoff;
static int s_fb_rshift = 16, s_fb_gshift = 8, s_fb_bshift = 0;
static int s_scale = 1, s_scale_fixed = 0;
static int s_ox = 0, s_oy = 0; /* centre offset when the resolution is not a multiple of the scale */

int display_scale(void)
{
    return s_scale;
}

/* a scale is usable when the interface still gets at least 1024 x 700 points */
int display_scale_ok(int sc)
{
    return sc >= 1 && sc <= 4 && s_fb_w / sc >= 1024 && s_fb_h / sc >= 700;
}

int display_scale_selectable(void)
{
    return !s_scale_fixed && s_fb && display_scale_ok(2);
}

static int auto_scale(void)
{
    /* up to 1200 lines: 1x; QHD / 4K: 2x; 5K and more: 3x */
    int sc = s_fb_h >= 2800 ? 3 : (s_fb_h >= 1400 ? 2 : 1);
    while(sc > 1 && !display_scale_ok(sc)) sc--;
    return sc;
}

static inline void fb_put(uint8_t * dst, const uint8_t * src)
{
    /* src: XRGB8888 (B, G, R, X) */
    if(s_fb_bpp == 32) {
        uint32_t v = ((uint32_t)src[2] << s_fb_rshift) | ((uint32_t)src[1] << s_fb_gshift) | ((uint32_t)src[0] << s_fb_bshift);
        memcpy(dst, &v, 4);
    }
    else if(s_fb_bpp == 24) {
        dst[s_fb_rshift / 8] = src[2];
        dst[s_fb_gshift / 8] = src[1];
        dst[s_fb_bshift / 8] = src[0];
    }
    else { /* 16 bpp RGB565 */
        uint16_t v = (uint16_t)(((src[2] >> 3) << 11) | ((src[1] >> 2) << 5) | (src[0] >> 3));
        memcpy(dst, &v, 2);
    }
}

static void fb_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px)
{
    int w = lv_area_get_width(area), h = lv_area_get_height(area);
    int bytes = s_fb_bpp / 8, sc = s_scale;
    int fast = s_fb_bpp == 32 && s_fb_rshift == 16 && s_fb_gshift == 8 && s_fb_bshift == 0;
    for(int y = 0; y < h; y++) {
        const uint8_t * src = px + (size_t)y * w * 4;
        uint8_t * row = s_fb + (size_t)(s_fb_yoff + s_oy + (area->y1 + y) * sc) * s_fb_line +
                        (size_t)(s_fb_xoff + s_ox + area->x1 * sc) * bytes;
        if(sc == 1 && fast) memcpy(row, src, (size_t)w * 4);
        else {
            uint8_t * d = row;
            for(int x = 0; x < w; x++, src += 4)
                for(int k = 0; k < sc; k++, d += bytes) {
                    if(fast) memcpy(d, src, 4);
                    else fb_put(d, src);
                }
        }
        for(int k = 1; k < sc; k++) memcpy(row + (size_t)k * s_fb_line, row, (size_t)w * sc * bytes);
    }
    lv_display_flush_ready(disp);
}

static lv_display_t * fb_open(const char * dev)
{
    int fd = open(dev, O_RDWR | O_CLOEXEC);
    if(fd < 0) return NULL;
    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    if(ioctl(fd, FBIOGET_VSCREENINFO, &vi) != 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fi) != 0) {
        close(fd);
        return NULL;
    }
    if(vi.bits_per_pixel != 32 && vi.bits_per_pixel != 24 && vi.bits_per_pixel != 16) {
        close(fd);
        return NULL;
    }
    s_fb_w = (int)vi.xres;
    s_fb_h = (int)vi.yres;
    s_fb_bpp = (int)vi.bits_per_pixel;
    s_fb_line = (int)fi.line_length;
    s_fb_xoff = (int)vi.xoffset;
    s_fb_yoff = (int)vi.yoffset;
    if(s_fb_bpp >= 24) {
        s_fb_rshift = (int)vi.red.offset;
        s_fb_gshift = (int)vi.green.offset;
        s_fb_bshift = (int)vi.blue.offset;
    }
    size_t len = fi.smem_len ? fi.smem_len : (size_t)s_fb_line * vi.yres_virtual;
    s_fb = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(s_fb == MAP_FAILED) {
        s_fb = NULL;
        close(fd);
        return NULL;
    }
    /* scale: boot parameter / environment, then saved setting, then automatic */
    const char * e = getenv("SHREDOS_SCALE");
    int want = e ? atoi(e) : 0;
    if(want > 0) s_scale_fixed = 1;
    else want = storage_get_scale();
    s_scale = display_scale_ok(want) ? want : auto_scale();

    int lw = s_fb_w / s_scale, lh = s_fb_h / s_scale;
    s_ox = (s_fb_w - lw * s_scale) / 2;
    s_oy = (s_fb_h - lh * s_scale) / 2;
    for(int y = 0; y < s_fb_h; y++) memset(s_fb + (size_t)(s_fb_yoff + y) * s_fb_line + (size_t)s_fb_xoff * (s_fb_bpp / 8), 0, (size_t)s_fb_w * (s_fb_bpp / 8));

    lv_display_t * disp = lv_display_create(lw, lh);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_XRGB8888);
    size_t bsz = (size_t)lw * lh * 4;
    void * buf = malloc(bsz);
    if(!buf) return NULL;
    lv_display_set_buffers(disp, buf, NULL, bsz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, fb_flush);
    return disp;
}

/* ---- test mode ---- */
static uint8_t * s_mem = NULL;
static int s_mw, s_mh;

static void mem_flush(lv_display_t * disp, const lv_area_t * area, uint8_t * px)
{
    int w = lv_area_get_width(area);
    for(int y = area->y1; y <= area->y2; y++) {
        memcpy(s_mem + ((size_t)y * s_mw + area->x1) * 4, px + (size_t)(y - area->y1) * w * 4, (size_t)w * 4);
    }
    lv_display_flush_ready(disp);
}

static void save_ppm(const char * path)
{
    FILE * fp = fopen(path, "wb");
    if(!fp) return;
    fprintf(fp, "P6\n%d %d\n255\n", s_mw, s_mh);
    for(int i = 0; i < s_mw * s_mh; i++) {
        uint8_t * p = s_mem + (size_t)i * 4; /* BGRA */
        uint8_t rgb[3] = {p[2], p[1], p[0]};
        fwrite(rgb, 1, 3, fp);
    }
    fclose(fp);
}

static void run_for(int ms)
{
    for(int t = 0; t < ms; t += 10) {
        lv_tick_inc(10);
        ui_tick();
        lv_timer_handler();
    }
}

/* simulated touchpad (12 units/mm): move, tap, two-finger scroll, tap while typing */
#include <linux/input.h>
void input_test_pad(const struct input_event * ev, int n, int * x, int * y, int * pressed, int * wheel);
static void pad_send(int type, int code, int value)
{
    struct input_event e;
    memset(&e, 0, sizeof(e));
    e.type = (unsigned short)type;
    e.code = (unsigned short)code;
    e.value = value;
    input_test_pad(&e, 1, NULL, NULL, NULL, NULL);
}
static void pad_touch(int x, int y, int fingers)
{
    pad_send(EV_ABS, ABS_X, x);
    pad_send(EV_ABS, ABS_Y, y);
    pad_send(EV_KEY, BTN_TOUCH, 1);
    pad_send(EV_KEY, fingers >= 2 ? BTN_TOOL_DOUBLETAP : BTN_TOOL_FINGER, 1);
    pad_send(EV_KEY, fingers >= 2 ? BTN_TOOL_FINGER : BTN_TOOL_DOUBLETAP, 0);
    pad_send(EV_SYN, SYN_REPORT, 0);
}
static void pad_lift(void)
{
    pad_send(EV_KEY, BTN_TOUCH, 0);
    pad_send(EV_KEY, BTN_TOOL_FINGER, 0);
    pad_send(EV_KEY, BTN_TOOL_DOUBLETAP, 0);
    pad_send(EV_SYN, SYN_REPORT, 0);
}
static void pad_selftest(void)
{
    int x0, y0, x, y, p, w;
    input_test_pad(NULL, 0, &x0, &y0, &p, &w);
    for(int i = 0; i <= 20; i++) { pad_touch(500 + i * 12, 500, 1); lv_tick_inc(8); } /* 20 mm right, slow */
    pad_lift();
    lv_tick_inc(500);
    input_test_pad(NULL, 0, &x, &y, &p, &w);
    printf("move 20mm right: dx=%d dy=%d pressed=%d\n", x - x0, y - y0, p);
    pad_touch(900, 900, 1); lv_tick_inc(60); pad_lift();                              /* jump to another spot + tap */
    input_test_pad(NULL, 0, &x0, &y0, &p, &w);
    printf("tap elsewhere: dx=%d dy=%d pressed=%d", x0 - x, y0 - y, p);
    lv_tick_inc(200);
    input_test_pad(NULL, 0, NULL, NULL, &p, &w);
    printf(" -> after 200ms pressed=%d\n", p);
    for(int i = 0; i <= 10; i++) { pad_touch(500, 300 + i * 24, 2); lv_tick_inc(8); } /* 20 mm down, two fingers */
    pad_lift();
    input_test_pad(NULL, 0, &x, &y, &p, &w);
    printf("two-finger 20mm: dx=%d dy=%d wheel=%d pressed=%d\n", x - x0, y - y0, w, p);
    pad_touch(500, 500, 1); lv_tick_inc(400); pad_touch(506, 500, 1); pad_lift();     /* long touch: no click */
    input_test_pad(NULL, 0, NULL, NULL, &p, NULL);
    printf("long touch: pressed=%d\n", p);
}

static int run_headless(const char * res)
{
    if(sscanf(res, "%dx%d", &s_mw, &s_mh) != 2) {
        s_mw = 1280;
        s_mh = 800;
    }
    s_mem = calloc((size_t)s_mw * s_mh, 4);
    lv_display_t * disp = lv_display_create(s_mw, s_mh);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_XRGB8888);
    size_t bsz = (size_t)s_mw * s_mh * 4;
    void * buf = malloc(bsz);
    lv_display_set_buffers(disp, buf, NULL, bsz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, mem_flush);
    input_init(disp);
    ui_init();
    run_for(300);

    const char * script = getenv("SHREDOS_TEST_SCRIPT");
    if(!script) script = "shot:/tmp/shot.ppm";
    char * s = strdup(script);
    for(char * tok = strtok(s, ";"); tok; tok = strtok(NULL, ";")) {
        if(strncmp(tok, "shot:", 5) == 0) {
            lv_obj_invalidate(lv_screen_active());
            run_for(200);
            save_ppm(tok + 5);
        }
        else if(strcmp(tok, "padtest") == 0) pad_selftest();
        else if(strncmp(tok, "wait:", 5) == 0) {
            int ms = atoi(tok + 5);
            for(int t = 0; t < ms; t += 50) {
                run_for(50);
                usleep(50000);
            }
        }
        else {
            ui_test_action(tok);
            run_for(300);
        }
    }
    free(s);
    return 0;
}

int main(int argc, char ** argv)
{
    (void)argc;
    (void)argv;
    memset(&g, 0, sizeof(g));
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    lv_init();
    storage_init();

    const char * headless = getenv("SHREDOS_HEADLESS");
    if(headless) return run_headless(headless);

    lv_tick_set_cb(tick_ms);
    const char * fbdev = getenv("SHREDOS_FBDEV");
    if(!fbdev) fbdev = "/dev/fb0";
    lv_display_t * disp = fb_open(fbdev);
    if(!disp) {
        fprintf(stderr, "WipeOS GUI: no usable framebuffer at %s\n", fbdev);
        return 2;
    }
    console_graphics(1);
    input_init(disp);
    ui_init();

    while(!s_quit) {
        input_poll_hotplug();
        uint32_t idle = lv_timer_handler();
        ui_tick();
        if(idle > 20) idle = 20;
        if(idle < 2) idle = 2;
        usleep(idle * 1000);
    }
    console_graphics(0);
    return s_exit_code;
}
