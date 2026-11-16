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
    lv_display_t * disp = lv_linux_fbdev_create();
    if(!disp) {
        fprintf(stderr, "WipeOS GUI: could not open the framebuffer\n");
        return 2;
    }
    lv_linux_fbdev_set_file(disp, fbdev);
    if(lv_display_get_horizontal_resolution(disp) <= 0) {
        fprintf(stderr, "WipeOS GUI: no framebuffer at %s\n", fbdev);
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
