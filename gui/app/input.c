/*
 * Input: mouse, touchpad, touchscreen and keyboard directly via evdev.
 * Touchpads are handled like a mouse (relative movement, tap to click,
 * two-finger scrolling), touchscreens and tablets as absolute pointers.
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
#include <math.h>

#define MAX_DEV 64
#define BITS_LONG (sizeof(long) * 8)
#define NLONGS(x) (((x) + BITS_LONG - 1) / BITS_LONG)
#define TEST_BIT(bit, arr) ((arr[(bit) / BITS_LONG] >> ((bit) % BITS_LONG)) & 1)

typedef enum { DEV_KBD = 1, DEV_REL = 2, DEV_ABS = 4, DEV_PAD = 8 } dev_kind_t;

typedef struct {
    int fd;
    char path[64];
    int kind;
    int absx_min, absx_max, absy_min, absy_max;
    int abs_x, abs_y;
    /* touchpad state */
    float upmm_x, upmm_y;      /* device units per millimetre */
    int cur_x, cur_y;          /* latest position of this frame */
    int last_x, last_y, have_last;
    int touching, fingers, last_fingers;
    uint32_t down_time;
    float moved_mm;
    int max_fingers;
    float acc_x, acc_y, acc_scroll;
} in_dev_t;

static in_dev_t s_dev[MAX_DEV];
static int s_ndev = 0;
static lv_indev_t * s_ptr;
static lv_indev_t * s_kp;
static lv_obj_t * s_cursor;
static int s_w = 1280, s_h = 800;
static int s_x, s_y, s_pressed;
static int s_wheel;
static uint32_t s_tap_until = 0;   /* tap-to-click: report "pressed" until this tick */
static uint32_t s_last_key = 0;    /* for "disable tap while typing" */
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

static lv_obj_t * find_textarea(lv_obj_t * parent)
{
    uint32_t n = lv_obj_get_child_count(parent);
    for(uint32_t i = 0; i < n; i++) {
        lv_obj_t * c = lv_obj_get_child(parent, i);
        if(lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
        if(lv_obj_check_type(c, &lv_textarea_class) && !lv_obj_has_state(c, LV_STATE_DISABLED) && lv_obj_is_visible(c)) return c;
        lv_obj_t * r = find_textarea(c);
        if(r) return r;
    }
    return NULL;
}

static void insert_text(const char * t)
{
    lv_group_t * grp = lv_group_get_default();
    lv_obj_t * f = grp ? lv_group_get_focused(grp) : NULL;
    if(!(f && lv_obj_check_type(f, &lv_textarea_class) && lv_obj_is_visible(f))) {
        /* typing without a focused field: use the first visible text field */
        f = find_textarea(lv_layer_top());
        if(!f) f = find_textarea(lv_screen_active());
        if(f) lv_group_focus_obj(f);
    }
    if(f) lv_textarea_add_text(f, t);
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
    s_last_key = lv_tick_get();

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
        unsigned long props[NLONGS(INPUT_PROP_CNT)] = {0};
        ioctl(fd, EVIOCGPROP(sizeof(props)), props);
        int direct = TEST_BIT(INPUT_PROP_DIRECT, props);
        int pad = !direct && (TEST_BIT(INPUT_PROP_POINTER, props) || TEST_BIT(INPUT_PROP_BUTTONPAD, props) ||
                              (TEST_BIT(BTN_TOOL_FINGER, keybits) && !TEST_BIT(BTN_TOOL_PEN, keybits)));
        if(ioctl(fd, EVIOCGABS(ABS_X), &ax) == 0 && ioctl(fd, EVIOCGABS(ABS_Y), &ay) == 0 && ax.maximum > ax.minimum &&
           ay.maximum > ay.minimum) {
            d.kind |= pad ? DEV_PAD : DEV_ABS;
            /* units per mm; without a resolution assume a 100 mm wide pad */
            d.upmm_x = ax.resolution > 0 ? (float)ax.resolution : (float)(ax.maximum - ax.minimum) / 100.0f;
            d.upmm_y = ay.resolution > 0 ? (float)ay.resolution : d.upmm_x;
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
    if(d.kind & (DEV_REL | DEV_ABS | DEV_PAD)) s_has_mouse = 1;
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

/*
 * Touchpad: works on the single-touch emulation the kernel provides for every
 * touchpad (ABS_X/ABS_Y, BTN_TOUCH, BTN_TOOL_FINGER/DOUBLETAP/...).
 * One finger moves the pointer, a short tap clicks, two fingers scroll.
 */
#define TAP_MAX_MS 220
#define TAP_MAX_MM 1.5f
#define TYPING_GUARD_MS 400

static void pad_frame(in_dev_t * d)
{
    uint32_t now = lv_tick_get();
    if(!d->touching || d->fingers == 0) {
        if(d->have_last) {
            /* finger lifted: was it a tap? */
            if(d->max_fingers == 1 && (uint32_t)(now - d->down_time) <= TAP_MAX_MS && d->moved_mm <= TAP_MAX_MM &&
               (uint32_t)(now - s_last_key) > TYPING_GUARD_MS)
                s_tap_until = now + 90;
        }
        d->have_last = 0;
        d->last_fingers = 0;
        return;
    }
    if(!d->have_last || d->fingers != d->last_fingers) {
        /* new touch or finger count changed: no jump */
        if(!d->have_last) {
            d->down_time = now;
            d->moved_mm = 0;
            d->max_fingers = 0;
            d->acc_x = d->acc_y = d->acc_scroll = 0;
        }
        if(d->fingers > d->max_fingers) d->max_fingers = d->fingers;
        d->last_x = d->cur_x;
        d->last_y = d->cur_y;
        d->last_fingers = d->fingers;
        d->have_last = 1;
        return;
    }
    float dx = (float)(d->cur_x - d->last_x) / d->upmm_x;
    float dy = (float)(d->cur_y - d->last_y) / d->upmm_y;
    d->last_x = d->cur_x;
    d->last_y = d->cur_y;
    float dist = dx * dx + dy * dy;
    dist = dist > 0 ? sqrtf(dist) : 0;
    if(dist > 25.0f) return; /* implausible jump */
    d->moved_mm += dist;
    if(d->fingers >= 2) {
        d->acc_scroll += dy;
        while(d->acc_scroll >= 3.0f) {
            s_wheel += 1; /* fingers down = like wheel up */
            d->acc_scroll -= 3.0f;
        }
        while(d->acc_scroll <= -3.0f) {
            s_wheel -= 1;
            d->acc_scroll += 3.0f;
        }
        return;
    }
    /* px per mm: one swipe over a typical pad crosses most of the screen; faster = further */
    float gain = (float)s_w / 110.0f;
    float accel = 1.0f + (dist > 0.4f ? (dist - 0.4f) * 0.9f : 0.0f);
    if(accel > 2.6f) accel = 2.6f;
    d->acc_x += dx * gain * accel;
    d->acc_y += dy * gain * accel;
    int ix = (int)d->acc_x, iy = (int)d->acc_y;
    d->acc_x -= ix;
    d->acc_y -= iy;
    s_x += ix;
    s_y += iy;
}

static void pad_event(in_dev_t * d, const struct input_event * ev)
{
    if(ev->type == EV_ABS) {
        if(ev->code == ABS_X) d->cur_x = ev->value;
        else if(ev->code == ABS_Y) d->cur_y = ev->value;
    }
    else if(ev->type == EV_KEY) {
        switch(ev->code) {
            case BTN_TOUCH: d->touching = ev->value != 0; if(d->touching && d->fingers == 0) d->fingers = 1; break;
            case BTN_TOOL_FINGER: if(ev->value) d->fingers = 1; break;
            case BTN_TOOL_DOUBLETAP: if(ev->value) d->fingers = 2; break;
            case BTN_TOOL_TRIPLETAP: if(ev->value) d->fingers = 3; break;
            case BTN_TOOL_QUADTAP: case BTN_TOOL_QUINTTAP: if(ev->value) d->fingers = 4; break;
            default: break;
        }
        if(ev->code == BTN_TOUCH && !ev->value) d->fingers = 0;
    }
    else if(ev->type == EV_SYN && ev->code == SYN_REPORT) {
        pad_frame(d);
    }
}

/* for tests: feed raw events into a simulated touchpad */
void input_test_pad(const struct input_event * ev, int n, int * x, int * y, int * pressed, int * wheel)
{
    static in_dev_t pad;
    if(pad.kind == 0) {
        pad.kind = DEV_PAD;
        pad.upmm_x = pad.upmm_y = 12.0f;
    }
    for(int i = 0; i < n; i++) pad_event(&pad, &ev[i]);
    if(x) *x = s_x;
    if(y) *y = s_y;
    if(pressed) *pressed = s_pressed || (s_tap_until != 0 && (int32_t)(s_tap_until - lv_tick_get()) > 0);
    if(wheel) *wheel = s_wheel;
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
            if(d->kind & DEV_PAD) {
                pad_event(d, &ev);
            }
            else if(ev.type == EV_REL) {
                if(ev.code == REL_X) s_x += ev.value;
                else if(ev.code == REL_Y) s_y += ev.value;
                else if(ev.code == REL_WHEEL) s_wheel += ev.value;
            }
            else if(ev.type == EV_ABS && (d->kind & DEV_ABS)) {
                if(ev.code == ABS_X) s_x = (int)((long)(ev.value - d->absx_min) * (s_w - 1) / (d->absx_max - d->absx_min));
                else if(ev.code == ABS_Y) s_y = (int)((long)(ev.value - d->absy_min) * (s_h - 1) / (d->absy_max - d->absy_min));
            }
            if(ev.type == EV_KEY && !(d->kind & DEV_PAD && ev.code == BTN_TOUCH)) {
                if(ev.code == BTN_LEFT || (ev.code == BTN_TOUCH && (d->kind & DEV_ABS))) s_pressed = ev.value != 0;
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
    int tap = s_tap_until != 0 && (int32_t)(s_tap_until - lv_tick_get()) > 0;
    data->state = (s_pressed || tap) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
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

    /* mouse pointer: classic arrow, white with dark outline */
    {
        static const char * const arrow[] = {
            "X...............", "XX..............", "XoX.............", "XooX............", "XoooX...........",
            "XooooX..........", "XoooooX.........", "XooooooX........", "XoooooooX.......", "XooooooooX......",
            "XoooooooooX.....", "XooooooooooX....", "XoooooooooooX...", "XooooooXXXXXXX..", "XoooXooX........",
            "XooX.XooX.......", "XoX..XooX.......", "XX....XooX......", "X.....XooX......", ".......XooX.....",
            ".......XooX.....", "........XX......",
        };
        enum { AW = 16, AH = 22 };
        static uint8_t px[AW * AH * 4];
        static lv_image_dsc_t dsc;
        for(int y = 0; y < AH; y++)
            for(int x = 0; x < AW; x++) {
                char c = arrow[y][x];
                uint8_t * p = px + (y * AW + x) * 4;
                uint8_t v = c == 'o' ? 0xFF : 0x10;
                p[0] = p[1] = p[2] = v;
                p[3] = c == '.' ? 0 : 0xFF;
            }
        dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
        dsc.header.w = AW;
        dsc.header.h = AH;
        dsc.header.stride = AW * 4;
        dsc.data_size = sizeof(px);
        dsc.data = px;
        s_cursor = lv_image_create(lv_layer_sys());
        lv_image_set_src(s_cursor, &dsc);
    }
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
