/*
 * Input: mouse/touch and keyboard directly via evdev.
 * US and German keyboard layouts; text goes into the focused text
 * field, navigation keys go to LVGL.
 * Newly plugged-in devices are detected every few seconds.
 */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#define MAX_DEV 24
#define BITS_LONG (sizeof(long) * 8)
#define NLONGS(x) (((x) + BITS_LONG - 1) / BITS_LONG)
#define TEST_BIT(bit, arr) ((arr[(bit) / BITS_LONG] >> ((bit) % BITS_LONG)) & 1)

typedef enum { DEV_KBD = 1, DEV_REL = 2, DEV_ABS = 4 } dev_kind_t;

typedef struct {
    int fd;
    char path[64];
    int kind;
    int absx_min, absx_max, absy_min, absy_max;
    int abs_x, abs_y;
} in_dev_t;

static in_dev_t s_dev[MAX_DEV];
static int s_ndev = 0;
static lv_indev_t * s_ptr;
static lv_indev_t * s_kp;
static lv_obj_t * s_cursor;
static int s_w = 1280, s_h = 800;
static int s_x, s_y, s_pressed;
static int s_wheel;
static int s_has_kbd = 0, s_has_mouse = 0;
static int s_shift = 0, s_altgr = 0, s_ctrl = 0, s_caps = 0;
static uint32_t s_keyq[64];
static int s_kq_r = 0, s_kq_w = 0;
static uint32_t s_last_scan = 0;

typedef struct {
    unsigned short code;
    const char * n;
    const char * s;
    const char * a;
} keymap_t;

/* German layout (de-latin1 without dead keys) */
static const keymap_t s_map_de[] = {
    {KEY_1, "1", "!", ""}, {KEY_2, "2", "\"", "²"}, {KEY_3, "3", "§", "³"}, {KEY_4, "4", "$", ""},
    {KEY_5, "5", "%", ""}, {KEY_6, "6", "&", ""}, {KEY_7, "7", "/", "{"}, {KEY_8, "8", "(", "["},
    {KEY_9, "9", ")", "]"}, {KEY_0, "0", "=", "}"}, {KEY_MINUS, "ß", "?", "\\"}, {KEY_EQUAL, "´", "`", ""},
    {KEY_Q, "q", "Q", "@"}, {KEY_W, "w", "W", ""}, {KEY_E, "e", "E", "€"}, {KEY_R, "r", "R", ""},
    {KEY_T, "t", "T", ""}, {KEY_Y, "z", "Z", ""}, {KEY_U, "u", "U", ""}, {KEY_I, "i", "I", ""},
    {KEY_O, "o", "O", ""}, {KEY_P, "p", "P", ""}, {KEY_LEFTBRACE, "ü", "Ü", ""}, {KEY_RIGHTBRACE, "+", "*", "~"},
    {KEY_A, "a", "A", ""}, {KEY_S, "s", "S", ""}, {KEY_D, "d", "D", ""}, {KEY_F, "f", "F", ""},
    {KEY_G, "g", "G", ""}, {KEY_H, "h", "H", ""}, {KEY_J, "j", "J", ""}, {KEY_K, "k", "K", ""},
    {KEY_L, "l", "L", ""}, {KEY_SEMICOLON, "ö", "Ö", ""}, {KEY_APOSTROPHE, "ä", "Ä", ""}, {KEY_GRAVE, "^", "°", ""},
    {KEY_BACKSLASH, "#", "'", ""}, {KEY_102ND, "<", ">", "|"}, {KEY_Z, "y", "Y", ""}, {KEY_X, "x", "X", ""},
    {KEY_C, "c", "C", ""}, {KEY_V, "v", "V", ""}, {KEY_B, "b", "B", ""}, {KEY_N, "n", "N", ""},
    {KEY_M, "m", "M", "µ"}, {KEY_COMMA, ",", ";", ""}, {KEY_DOT, ".", ":", ""}, {KEY_SLASH, "-", "_", ""},
    {KEY_SPACE, " ", " ", " "},
    {KEY_KP0, "0", "0", ""}, {KEY_KP1, "1", "1", ""}, {KEY_KP2, "2", "2", ""}, {KEY_KP3, "3", "3", ""},
    {KEY_KP4, "4", "4", ""}, {KEY_KP5, "5", "5", ""}, {KEY_KP6, "6", "6", ""}, {KEY_KP7, "7", "7", ""},
    {KEY_KP8, "8", "8", ""}, {KEY_KP9, "9", "9", ""}, {KEY_KPDOT, ",", ",", ""}, {KEY_KPMINUS, "-", "-", ""},
    {KEY_KPPLUS, "+", "+", ""}, {KEY_KPASTERISK, "*", "*", ""}, {KEY_KPSLASH, "/", "/", ""},
    {0, NULL, NULL, NULL}
};

/* US layout (default); German via SHREDOS_KBD=de or the toggle in the UI */
static const keymap_t s_map_us[] = {
    {KEY_1, "1", "!", ""}, {KEY_2, "2", "@", ""}, {KEY_3, "3", "#", ""}, {KEY_4, "4", "$", ""},
    {KEY_5, "5", "%", ""}, {KEY_6, "6", "^", ""}, {KEY_7, "7", "&", ""}, {KEY_8, "8", "*", ""},
    {KEY_9, "9", "(", ""}, {KEY_0, "0", ")", ""}, {KEY_MINUS, "-", "_", ""}, {KEY_EQUAL, "=", "+", ""},
    {KEY_Q, "q", "Q", ""}, {KEY_W, "w", "W", ""}, {KEY_E, "e", "E", ""}, {KEY_R, "r", "R", ""},
    {KEY_T, "t", "T", ""}, {KEY_Y, "y", "Y", ""}, {KEY_U, "u", "U", ""}, {KEY_I, "i", "I", ""},
    {KEY_O, "o", "O", ""}, {KEY_P, "p", "P", ""}, {KEY_LEFTBRACE, "[", "{", ""}, {KEY_RIGHTBRACE, "]", "}", ""},
    {KEY_A, "a", "A", ""}, {KEY_S, "s", "S", ""}, {KEY_D, "d", "D", ""}, {KEY_F, "f", "F", ""},
    {KEY_G, "g", "G", ""}, {KEY_H, "h", "H", ""}, {KEY_J, "j", "J", ""}, {KEY_K, "k", "K", ""},
    {KEY_L, "l", "L", ""}, {KEY_SEMICOLON, ";", ":", ""}, {KEY_APOSTROPHE, "'", "\"", ""}, {KEY_GRAVE, "`", "~", ""},
    {KEY_BACKSLASH, "\\", "|", ""}, {KEY_Z, "z", "Z", ""}, {KEY_X, "x", "X", ""},
    {KEY_C, "c", "C", ""}, {KEY_V, "v", "V", ""}, {KEY_B, "b", "B", ""}, {KEY_N, "n", "N", ""},
    {KEY_M, "m", "M", ""}, {KEY_COMMA, ",", "<", ""}, {KEY_DOT, ".", ">", ""}, {KEY_SLASH, "/", "?", ""},
    {KEY_SPACE, " ", " ", " "},
    {KEY_KP0, "0", "0", ""}, {KEY_KP1, "1", "1", ""}, {KEY_KP2, "2", "2", ""}, {KEY_KP3, "3", "3", ""},
    {KEY_KP4, "4", "4", ""}, {KEY_KP5, "5", "5", ""}, {KEY_KP6, "6", "6", ""}, {KEY_KP7, "7", "7", ""},
    {KEY_KP8, "8", "8", ""}, {KEY_KP9, "9", "9", ""}, {KEY_KPDOT, ".", ".", ""}, {KEY_KPMINUS, "-", "-", ""},
    {KEY_KPPLUS, "+", "+", ""}, {KEY_KPASTERISK, "*", "*", ""}, {KEY_KPSLASH, "/", "/", ""},
    {0, NULL, NULL, NULL}
};

static const keymap_t * s_map = s_map_us;

static void kq_push(uint32_t k)
{
    int nw = (s_kq_w + 1) % 64;
    if(nw == s_kq_r) return;
    s_keyq[s_kq_w] = k;
    s_kq_w = nw;
}

static int is_letter(unsigned short code)
{
    const char * n = NULL;
    for(int i = 0; s_map[i].n; i++) if(s_map[i].code == code) n = s_map[i].n;
    if(!n) return 0;
    unsigned char c = (unsigned char)n[0];
    return (c >= 'a' && c <= 'z') || c == 0xC3; /* incl. ä ö ü */
}

static void insert_text(const char * t)
{
    lv_group_t * grp = lv_group_get_default();
    lv_obj_t * f = grp ? lv_group_get_focused(grp) : NULL;
    if(f && lv_obj_check_type(f, &lv_textarea_class)) {
        lv_textarea_add_text(f, t);
    }
}

static void handle_key(unsigned short code, int value)
{
    int down = value != 0;
    switch(code) {
        case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: s_shift = down; return;
        case KEY_RIGHTALT: s_altgr = down; return;
        case KEY_LEFTCTRL: case KEY_RIGHTCTRL: s_ctrl = down; return;
        case KEY_CAPSLOCK: if(value == 1) s_caps = !s_caps; return;
        default: break;
    }
    if(!down) return;

    uint32_t k = 0;
    switch(code) {
        case KEY_TAB: k = s_shift ? LV_KEY_PREV : LV_KEY_NEXT; break;
        case KEY_ENTER: case KEY_KPENTER: k = LV_KEY_ENTER; break;
        case KEY_BACKSPACE: k = LV_KEY_BACKSPACE; break;
        case KEY_DELETE: k = LV_KEY_DEL; break;
        case KEY_ESC: k = LV_KEY_ESC; break;
        case KEY_LEFT: k = LV_KEY_LEFT; break;
        case KEY_RIGHT: k = LV_KEY_RIGHT; break;
        case KEY_UP: k = LV_KEY_UP; break;
        case KEY_DOWN: k = LV_KEY_DOWN; break;
        case KEY_HOME: k = LV_KEY_HOME; break;
        case KEY_END: k = LV_KEY_END; break;
        default: break;
    }
    if(k) {
        kq_push(k);
        return;
    }
    if(s_ctrl) return; /* do not insert control characters */
    for(int i = 0; s_map[i].n; i++) {
        if(s_map[i].code != code) continue;
        int shift = s_shift;
        if(s_caps && is_letter(code)) shift = !shift;
        const char * t = s_altgr ? s_map[i].a : (shift ? s_map[i].s : s_map[i].n);
        if(t && t[0] && strcmp(t, "´") != 0 && strcmp(t, "`") != 0) insert_text(t);
        return;
    }
}

static void open_dev(const char * path)
{
    for(int i = 0; i < s_ndev; i++) if(strcmp(s_dev[i].path, path) == 0) return;
    if(s_ndev >= MAX_DEV) return;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if(fd < 0) return;
    unsigned long evbits[NLONGS(EV_CNT)] = {0};
    unsigned long keybits[NLONGS(KEY_CNT)] = {0};
    unsigned long relbits[NLONGS(REL_CNT)] = {0};
    unsigned long absbits[NLONGS(ABS_CNT)] = {0};
    ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits);
    if(TEST_BIT(EV_KEY, evbits)) ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);
    if(TEST_BIT(EV_REL, evbits)) ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relbits)), relbits);
    if(TEST_BIT(EV_ABS, evbits)) ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);

    in_dev_t d;
    memset(&d, 0, sizeof(d));
    d.fd = fd;
    str_copy(d.path, path, sizeof(d.path));
    if(TEST_BIT(EV_KEY, evbits) && TEST_BIT(KEY_A, keybits) && TEST_BIT(KEY_Z, keybits) && TEST_BIT(KEY_ENTER, keybits)) d.kind |= DEV_KBD;
    if(TEST_BIT(EV_REL, evbits) && TEST_BIT(REL_X, relbits) && TEST_BIT(REL_Y, relbits)) d.kind |= DEV_REL;
    if(TEST_BIT(EV_ABS, evbits) && TEST_BIT(ABS_X, absbits) && TEST_BIT(ABS_Y, absbits)) {
        struct input_absinfo ax, ay;
        if(ioctl(fd, EVIOCGABS(ABS_X), &ax) == 0 && ioctl(fd, EVIOCGABS(ABS_Y), &ay) == 0 && ax.maximum > ax.minimum &&
           ay.maximum > ay.minimum) {
            d.kind |= DEV_ABS;
            d.absx_min = ax.minimum;
            d.absx_max = ax.maximum;
            d.absy_min = ay.minimum;
            d.absy_max = ay.maximum;
        }
    }
    if(d.kind == 0) {
        close(fd);
        return;
    }
    if(d.kind & DEV_KBD) s_has_kbd = 1;
    if(d.kind & (DEV_REL | DEV_ABS)) s_has_mouse = 1;
    s_dev[s_ndev++] = d;
}

static void scan_devices(void)
{
    DIR * dir = opendir("/dev/input");
    if(!dir) return;
    struct dirent * e;
    while((e = readdir(dir)) != NULL) {
        if(strncmp(e->d_name, "event", 5) != 0) continue;
        char p[64];
        snprintf(p, sizeof(p), "/dev/input/%s", e->d_name);
        open_dev(p);
    }
    closedir(dir);
    if(s_cursor) {
        if(s_has_mouse) lv_obj_remove_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
    }
}

static void drain(void)
{
    for(int i = 0; i < s_ndev; i++) {
        in_dev_t * d = &s_dev[i];
        struct input_event ev;
        for(;;) {
            ssize_t r = read(d->fd, &ev, sizeof(ev));
            if(r != sizeof(ev)) {
                if(r < 0 && errno == ENODEV) {
                    close(d->fd);
                    s_dev[i] = s_dev[--s_ndev];
                    i--;
                }
                break;
            }
            if(ev.type == EV_REL) {
                if(ev.code == REL_X) s_x += ev.value;
                else if(ev.code == REL_Y) s_y += ev.value;
                else if(ev.code == REL_WHEEL) s_wheel += ev.value;
            }
            else if(ev.type == EV_ABS && (d->kind & DEV_ABS)) {
                if(ev.code == ABS_X || ev.code == ABS_MT_POSITION_X)
                    s_x = (int)((long)(ev.value - d->absx_min) * (s_w - 1) / (d->absx_max - d->absx_min));
                else if(ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y)
                    s_y = (int)((long)(ev.value - d->absy_min) * (s_h - 1) / (d->absy_max - d->absy_min));
            }
            else if(ev.type == EV_KEY) {
                if(ev.code == BTN_LEFT || ev.code == BTN_TOUCH) s_pressed = ev.value != 0;
                else if(d->kind & DEV_KBD) handle_key(ev.code, ev.value);
            }
            if(s_x < 0) s_x = 0;
            if(s_y < 0) s_y = 0;
            if(s_x >= s_w) s_x = s_w - 1;
            if(s_y >= s_h) s_y = s_h - 1;
        }
    }
}

static void ptr_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    LV_UNUSED(indev);
    drain();
    data->point.x = s_x;
    data->point.y = s_y;
    data->state = s_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static uint32_t s_kp_last = 0;
static int s_kp_phase = 0;

static void kp_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    LV_UNUSED(indev);
    /* report every key as press + release */
    if(s_kp_phase == 1) {
        data->key = s_kp_last;
        data->state = LV_INDEV_STATE_RELEASED;
        s_kp_phase = 0;
        data->continue_reading = s_kq_r != s_kq_w;
        return;
    }
    drain();
    if(s_kq_r != s_kq_w) {
        s_kp_last = s_keyq[s_kq_r];
        s_kq_r = (s_kq_r + 1) % 64;
        data->key = s_kp_last;
        data->state = LV_INDEV_STATE_PRESSED;
        s_kp_phase = 1;
        data->continue_reading = true;
        return;
    }
    data->key = s_kp_last;
    data->state = LV_INDEV_STATE_RELEASED;
}

void input_init(lv_display_t * disp)
{
    const char * kb = getenv("SHREDOS_KBD");
    if(kb && strncmp(kb, "de", 2) == 0) s_map = s_map_de;
    s_w = lv_display_get_horizontal_resolution(disp);
    s_h = lv_display_get_vertical_resolution(disp);
    s_x = s_w / 2;
    s_y = s_h / 2;

    s_ptr = lv_indev_create();
    lv_indev_set_type(s_ptr, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_ptr, ptr_read);
    lv_indev_set_display(s_ptr, disp);

    /* mouse pointer: simple round marker */
    s_cursor = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(s_cursor);
    lv_obj_set_size(s_cursor, 14, 14);
    lv_obj_set_style_bg_opa(s_cursor, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_cursor, lv_color_hex(0x16181B), 0);
    lv_obj_set_style_border_color(s_cursor, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_cursor, 2, 0);
    lv_obj_set_style_radius(s_cursor, 7, 0);
    lv_obj_add_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_cursor, LV_OBJ_FLAG_CLICKABLE);
    lv_indev_set_cursor(s_ptr, s_cursor);

    s_kp = lv_indev_create();
    lv_indev_set_type(s_kp, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_kp, kp_read);
    lv_indev_set_display(s_kp, disp);
    lv_group_t * grp = lv_group_create();
    lv_group_set_default(grp);
    lv_indev_set_group(s_kp, grp);

    scan_devices();
    s_last_scan = lv_tick_get();
}

void input_poll_hotplug(void)
{
    if(lv_tick_elaps(s_last_scan) > 3000) {
        scan_devices();
        s_last_scan = lv_tick_get();
    }
}

int input_has_keyboard(void)
{
    return s_has_kbd;
}

int input_layout_is_de(void)
{
    return s_map == s_map_de;
}

void input_toggle_layout(void)
{
    s_map = (s_map == s_map_de) ? s_map_us : s_map_de;
}

int input_take_wheel(void)
{
    int w = s_wheel;
    s_wheel = 0;
    return w;
}
