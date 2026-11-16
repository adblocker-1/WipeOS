/*
 * WipeOS GUI - Bildschirme (Entwurf 4: Auftrag anlegen, Laufwerke,
 * Methode, Löschen, Fertig).
 */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

void app_quit(int code);

/* ---------- Farben ---------- */
#define COL_BG 0xF3F2EE
#define COL_SURFACE 0xFFFFFF
#define COL_LINE 0xDEDCD5
#define COL_FIELD 0xC9C6BD
#define COL_SOFT 0xECEBE6
#define COL_INK 0x16181B
#define COL_MUTED 0x5F646B
#define COL_ACCENT 0xD9480F
#define COL_ACCENT_SOFT 0xFBE9E0
#define COL_ACCENT_TEXT 0x7A2A08
#define COL_GREEN 0x2B8A3E
#define COL_GREEN_SOFT 0xE6F4EA
#define COL_RED 0xC92A2A
#define COL_RED_SOFT 0xFBE5E5
#define COL_SIDEBAR 0x16181B
#define COL_SIDE_TEXT 0xE9E8E4
#define COL_SIDE_MUTED 0x9EA3AA
#define COL_SIDE_ACTIVE 0x2A2D32

#define C(hex) lv_color_hex(hex)

/* ---------- Zustand der Oberfläche ---------- */
static int s_step = 1;
static int s_compact = 0;   /* kleine Bildschirme (< 1200 px Breite) */
static lv_obj_t * s_root;
static lv_obj_t * s_main;
static lv_obj_t * s_kb;          /* Bildschirmtastatur */
static lv_obj_t * s_modal;       /* offenes Dialogfenster */
static lv_group_t * s_modal_group;
static lv_group_t * s_main_group;

static customer_t s_customers[MAX_CUSTOMERS];
static int s_ncust;
static tech_t s_techs[MAX_TECHS];
static int s_ntech;

/* Schritt 1 */
static int s_cust_mode_new = 1;
static int s_cust_sel = -1;
static customer_t s_new_cust;
static int s_tech_sel = -1;
static lv_obj_t * s_ta_cust[7];
static lv_obj_t * s_ta_search;
static lv_obj_t * s_cust_list;
static lv_obj_t * s_cust_body;
static lv_obj_t * s_ta_sys, * s_ta_asset, * s_ta_note;
static lv_obj_t * s_err_label;
static lv_obj_t * s_tab_btn[2];
static lv_obj_t * s_tech_list;

/* Schritt 2 */
static lv_obj_t * s_drive_summary;
static lv_obj_t * s_btn_next;

/* Schritt 3 */
static lv_obj_t * s_method_list;
static lv_obj_t * s_plan_box;

/* Schritt 4 */
static lv_timer_t * s_poll_timer;
static lv_obj_t * s_arc, * s_arc_label;
static lv_obj_t * s_lbl_remaining, * s_lbl_runtime, * s_lbl_finish, * s_lbl_errors;
static lv_obj_t * s_row_status[MAX_DRIVES], * s_row_bar[MAX_DRIVES], * s_row_info[MAX_DRIVES];
static int s_rows;
static lv_obj_t * s_dim;

static void build_step(int step);

/* ======================================================================
 * Hilfsfunktionen für Widgets
 * ====================================================================== */

static lv_obj_t * box(lv_obj_t * parent)
{
    lv_obj_t * o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_OVERFLOW_VISIBLE); /* Fokusrahmen nicht abschneiden */
    return o;
}

static lv_obj_t * col(lv_obj_t * parent, int gap)
{
    lv_obj_t * o = box(parent);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(o, gap, 0);
    lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT);
    return o;
}

static lv_obj_t * row(lv_obj_t * parent, int gap)
{
    lv_obj_t * o = box(parent);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(o, gap, 0);
    lv_obj_set_size(o, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return o;
}

static lv_obj_t * spacer(lv_obj_t * parent)
{
    lv_obj_t * o = box(parent);
    lv_obj_set_flex_grow(o, 1);
    lv_obj_set_height(o, 1);
    return o;
}

static lv_obj_t * label(lv_obj_t * parent, const char * txt, const lv_font_t * font, uint32_t color)
{
    lv_obj_t * l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, C(color), 0);
    return l;
}

static lv_obj_t * card(lv_obj_t * parent, int pad)
{
    lv_obj_t * o = box(parent);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, C(COL_SURFACE), 0);
    lv_obj_set_style_border_color(o, C(COL_LINE), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 12, 0);
    lv_obj_set_style_pad_all(o, pad, 0);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(o, 12, 0);
    lv_obj_set_height(o, LV_SIZE_CONTENT);
    return o;
}

static lv_obj_t * pill(lv_obj_t * parent, const char * txt, uint32_t fg, uint32_t bg)
{
    lv_obj_t * l = label(parent, txt, &f_sans_sb14, fg);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(l, C(bg), 0);
    lv_obj_set_style_radius(l, 99, 0);
    lv_obj_set_style_pad_hor(l, 10, 0);
    lv_obj_set_style_pad_ver(l, 1, 0);
    return l;
}

typedef enum { BTN_DARK, BTN_ACCENT, BTN_GHOST, BTN_DANGER, BTN_LINK } btn_kind_t;

static lv_obj_t * button(lv_obj_t * parent, const char * txt, btn_kind_t kind, lv_event_cb_t cb, void * ud)
{
    lv_obj_t * b = lv_button_create(parent);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_pad_hor(b, 22, 0);
    lv_obj_set_style_pad_ver(b, 12, 0);
    lv_obj_set_style_min_height(b, 44, 0);
    lv_obj_set_style_outline_color(b, C(COL_ACCENT), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(b, 3, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(b, 2, LV_STATE_FOCUS_KEY);
    uint32_t bg = COL_INK, fg = 0xFFFFFF;
    switch(kind) {
        case BTN_DARK: bg = COL_INK; fg = 0xFFFFFF; break;
        case BTN_ACCENT: bg = COL_ACCENT; fg = 0xFFFFFF; break;
        case BTN_DANGER: bg = COL_RED; fg = 0xFFFFFF; break;
        case BTN_GHOST:
        case BTN_LINK:
            bg = COL_SURFACE;
            fg = COL_INK;
            break;
    }
    lv_obj_set_style_bg_color(b, C(bg), 0);
    lv_obj_set_style_bg_opa(b, kind == BTN_LINK ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    if(kind == BTN_GHOST) {
        lv_obj_set_style_border_color(b, C(COL_FIELD), 0);
        lv_obj_set_style_border_width(b, 1, 0);
    }
    lv_obj_set_style_bg_color(b, C(kind == BTN_GHOST || kind == BTN_LINK ? COL_SOFT : 0x3A3D42), LV_STATE_PRESSED);
    if(kind == BTN_ACCENT) lv_obj_set_style_bg_color(b, C(0xA8380B), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, C(0xBDBAB2), LV_STATE_DISABLED);
    lv_obj_t * l = label(b, txt, &f_sans_sb16, fg);
    lv_obj_center(l);
    if(cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

static void ta_event(lv_event_t * e);

static lv_obj_t * field(lv_obj_t * parent, const char * caption, const char * value, const char * placeholder, int grow)
{
    lv_obj_t * c = col(parent, 4);
    if(grow > 0) {
        lv_obj_set_width(c, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(c, grow);
    }
    lv_obj_set_style_min_width(c, 40, 0);
    if(caption) label(c, caption, &f_sans_sb14, COL_INK);
    lv_obj_t * ta = lv_textarea_create(c);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_style_text_font(ta, &f_sans_r16, 0);
    lv_obj_set_style_text_color(ta, C(COL_INK), 0);
    lv_obj_set_style_border_color(ta, C(COL_FIELD), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, C(COL_ACCENT), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ta, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(ta, 8, 0);
    lv_obj_set_style_pad_hor(ta, 12, 0);
    lv_obj_set_style_pad_ver(ta, 9, 0);
    lv_obj_set_style_bg_color(ta, C(COL_SURFACE), 0);
    lv_obj_set_style_text_color(ta, C(0x8A8F96), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(ta, C(COL_ACCENT), LV_PART_CURSOR);
    lv_obj_set_style_border_color(ta, C(COL_ACCENT), LV_PART_CURSOR);
    lv_obj_set_style_outline_width(ta, 0, LV_STATE_FOCUS_KEY);
    lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_OFF);
    if(placeholder) lv_textarea_set_placeholder_text(ta, placeholder);
    if(value) lv_textarea_set_text(ta, value);
    lv_obj_add_event_cb(ta, ta_event, LV_EVENT_ALL, NULL);
    return ta;
}

static void get_text(lv_obj_t * ta, char * out, size_t len)
{
    str_copy(out, ta ? lv_textarea_get_text(ta) : "", len);
    str_trim(out);
}

/* ---------- Bildschirmtastatur (nur ohne echte Tastatur) ---------- */

static const char * kb_map_lower[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "ß", LV_SYMBOL_BACKSPACE, "\n",
    "q", "w", "e", "r", "t", "z", "u", "i", "o", "p", "ü", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "ö", "ä", "\n",
    "ABC", "y", "x", "c", "v", "b", "n", "m", ",", ".", "-", "\n",
    LV_SYMBOL_CLOSE, "@", " ", "/", LV_SYMBOL_OK, ""
};
static const char * kb_map_upper[] = {
    "!", "\"", "§", "$", "%", "&", "/", "(", ")", "=", "?", LV_SYMBOL_BACKSPACE, "\n",
    "Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "Ü", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "Ö", "Ä", "\n",
    "abc", "Y", "X", "C", "V", "B", "N", "M", ";", ":", "_", "\n",
    LV_SYMBOL_CLOSE, "+", " ", "#", LV_SYMBOL_OK, ""
};
static const lv_buttonmatrix_ctrl_t kb_ctrl[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 1, 6, 1, 2
};

static void kb_hide(void)
{
    if(s_kb) {
        lv_keyboard_set_textarea(s_kb, NULL);
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void kb_event(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) kb_hide();
}

static void ta_event(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);
    if(code == LV_EVENT_CLICKED && !input_has_keyboard()) {
        if(!s_kb) {
            s_kb = lv_keyboard_create(lv_layer_top());
            lv_keyboard_set_map(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER, kb_map_lower, kb_ctrl);
            lv_keyboard_set_map(s_kb, LV_KEYBOARD_MODE_TEXT_UPPER, kb_map_upper, kb_ctrl);
            lv_obj_set_style_text_font(s_kb, &f_sans_sb18, LV_PART_ITEMS);
            lv_obj_set_size(s_kb, LV_PCT(100), LV_PCT(42));
            lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_add_event_cb(s_kb, kb_event, LV_EVENT_ALL, NULL);
            lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_CLICK_FOCUSABLE);
            lv_group_remove_obj(s_kb);
        }
        lv_keyboard_set_textarea(s_kb, ta);
        lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_kb);
        lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
    }
    else if(code == LV_EVENT_DEFOCUSED) {
        if(s_kb && lv_keyboard_get_textarea(s_kb) == ta) kb_hide();
    }
    else if(code == LV_EVENT_READY) {
        /* Enter im Textfeld: zum nächsten Feld */
        lv_group_t * grp = lv_obj_get_group(ta);
        if(grp) lv_group_focus_next(grp);
    }
}

/* ---------- Dialoge ---------- */

static lv_indev_t * keypad_indev(void)
{
    lv_indev_t * i = lv_indev_get_next(NULL);
    while(i) {
        if(lv_indev_get_type(i) == LV_INDEV_TYPE_KEYPAD) return i;
        i = lv_indev_get_next(i);
    }
    return NULL;
}

static lv_obj_t * modal_open(const char * title, int width)
{
    kb_hide();
    s_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_modal);
    lv_obj_set_size(s_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_modal, C(0x16181B), 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_50, 0);
    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_CLICKABLE);

    s_main_group = lv_group_get_default();
    s_modal_group = lv_group_create();
    lv_group_set_default(s_modal_group);
    lv_indev_t * kp = keypad_indev();
    if(kp) lv_indev_set_group(kp, s_modal_group);

    lv_obj_t * c = card(s_modal, 26);
    lv_obj_set_size(c, width, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(c, 16, 0);
    lv_obj_center(c);
    lv_obj_t * t = label(c, title, &f_sans_b24, COL_INK);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(t, LV_PCT(100));
    return c;
}

static void modal_close(void)
{
    kb_hide();
    if(s_modal) {
        lv_obj_delete(s_modal);
        s_modal = NULL;
    }
    if(s_modal_group) {
        lv_group_delete(s_modal_group);
        s_modal_group = NULL;
    }
    if(s_main_group) {
        lv_group_set_default(s_main_group);
        lv_indev_t * kp = keypad_indev();
        if(kp) lv_indev_set_group(kp, s_main_group);
    }
}

static void modal_close_cb(lv_event_t * e)
{
    (void)e;
    modal_close();
}

static void info_dialog(const char * title, const char * msg)
{
    lv_obj_t * c = modal_open(title, 520);
    lv_obj_t * l = label(c, msg, &f_sans_r16, COL_INK);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_t * r = row(c, 12);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t * b = button(r, "OK", BTN_DARK, modal_close_cb, NULL);
    lv_group_focus_obj(b);
}

/* ======================================================================
 * Grundgerüst: Seitenleiste + Inhaltsbereich
 * ====================================================================== */

static const char * s_step_names[5] = {"Auftrag anlegen", "Laufwerke wählen", "Methode wählen", "Löschen", "Bericht"};

static void classic_cb(lv_event_t * e)
{
    (void)e;
    app_quit(10);
}

static void build_sidebar(lv_obj_t * parent)
{
    lv_obj_t * sb = box(parent);
    lv_obj_set_size(sb, s_compact ? 228 : 264, LV_PCT(100));
    lv_obj_set_style_bg_opa(sb, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sb, C(COL_SIDEBAR), 0);
    lv_obj_set_style_pad_hor(sb, s_compact ? 16 : 22, 0);
    lv_obj_set_style_pad_ver(sb, 28, 0);
    lv_obj_set_flex_flow(sb, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sb, 32, 0);

    lv_obj_t * logo = row(sb, 10);
    lv_obj_t * mark = box(logo);
    lv_obj_set_size(mark, 24, 24);
    lv_obj_set_style_radius(mark, 6, 0);
    lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(mark, C(COL_ACCENT), 0);
    for(int i = 0; i < 3; i++) {
        lv_obj_t * bar = box(mark);
        lv_obj_set_size(bar, i == 2 ? 8 : 13, 2);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, C(COL_INK), 0);
        lv_obj_set_style_radius(bar, 1, 0);
        lv_obj_set_pos(bar, 6, 7 + i * 4);
    }
    label(logo, "WipeOS", &f_sans_b24, COL_SIDE_TEXT);

    lv_obj_t * nav = col(sb, 4);
    for(int i = 0; i < 5; i++) {
        int n = i + 1;
        int done = n < s_step, cur = n == s_step;
        lv_obj_t * r = row(nav, 12);
        lv_obj_set_style_pad_all(r, 9, 0);
        lv_obj_set_style_radius(r, 8, 0);
        if(cur) {
            lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(r, C(COL_SIDE_ACTIVE), 0);
        }
        lv_obj_t * dot = box(r);
        lv_obj_set_size(dot, 24, 24);
        lv_obj_set_style_radius(dot, 99, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        if(cur) lv_obj_set_style_bg_color(dot, C(COL_ACCENT), 0);
        else if(done) lv_obj_set_style_bg_color(dot, C(COL_GREEN), 0);
        else {
            lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_color(dot, C(0x4A4E55), 0);
            lv_obj_set_style_border_width(dot, 1, 0);
        }
        char num[8];
        if(done) str_copy(num, LV_SYMBOL_OK, sizeof(num));
        else snprintf(num, sizeof(num), "%d", n);
        lv_obj_t * nl = label(dot, num, &f_sans_sb14, cur ? COL_INK : (done ? 0xFFFFFF : COL_SIDE_MUTED));
        lv_obj_center(nl);
        label(r, s_step_names[i], cur ? &f_sans_sb16 : &f_sans_r16, cur ? 0xFFFFFF : (done ? 0xC9CCD1 : COL_SIDE_MUTED));
    }

    spacer(sb);
    lv_obj_set_flex_grow(lv_obj_get_child(sb, -1), 1);

    if(s_step == 1) {
        lv_obj_t * hints = col(sb, 6);
        const char * h[][2] = {{"Tab", "Nächstes Feld"}, {"Enter", "Weiter"}};
        for(int i = 0; i < 2; i++) {
            lv_obj_t * r = row(hints, 8);
            label(r, h[i][0], &f_mono_m14, COL_SIDE_TEXT);
            label(r, h[i][1], &f_sans_r14, COL_SIDE_MUTED);
        }
        lv_obj_t * b = button(sb, "Klassisches nwipe", BTN_LINK, classic_cb, NULL);
        lv_obj_set_style_pad_hor(b, 0, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, LV_STATE_PRESSED);
        lv_obj_set_style_text_color(lv_obj_get_child(b, 0), C(COL_SIDE_MUTED), 0);
        lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &f_sans_r14, 0);
    }
    else {
        lv_obj_t * info = col(sb, 10);
        lv_obj_set_style_pad_all(info, 14, 0);
        lv_obj_set_style_border_color(info, C(0x33373D), 0);
        lv_obj_set_style_border_width(info, 1, 0);
        lv_obj_set_style_radius(info, 10, 0);
        char t[160];
        snprintf(t, sizeof(t), "AUFTRAG %s", g.order_no);
        label(info, t, &f_mono_m14, COL_SIDE_MUTED);
        const char * k[3] = {"Kunde", "Techniker", "Gerät"};
        char v[3][160];
        str_copy(v[0], g.customer.name, sizeof(v[0]));
        snprintf(v[1], sizeof(v[1]), "%s · %s", g.tech.name, g.tech.id);
        str_copy(v[2], g.sysname, sizeof(v[2]));
        for(int i = 0; i < 3; i++) {
            lv_obj_t * c = col(info, 0);
            label(c, k[i], &f_sans_r14, COL_SIDE_MUTED);
            lv_obj_t * l = label(c, v[i], &f_sans_sb14, COL_SIDE_TEXT);
            lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
            lv_obj_set_width(l, LV_PCT(100));
        }
        char sn[128];
        snprintf(sn, sizeof(sn), "S/N %s", g.dmi.serial);
        lv_obj_t * l = label(lv_obj_get_child(info, -1), sn, &f_mono_m14, 0xC9CCD1);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, LV_PCT(100));
    }
}

static lv_obj_t * header(lv_obj_t * parent, const char * title, const char * right)
{
    lv_obj_t * h = row(parent, 16);
    lv_obj_set_flex_align(h, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_t * c = col(h, 4);
    lv_obj_set_width(c, LV_SIZE_CONTENT);
    char s[48];
    snprintf(s, sizeof(s), "SCHRITT %d VON 5", s_step);
    lv_obj_t * st = label(c, s, &f_sans_sb14, COL_MUTED);
    lv_obj_set_style_text_letter_space(st, 1, 0);
    label(c, title, &f_sans_b30, COL_INK);
    if(right) label(h, right, &f_mono_m14, COL_MUTED);
    return h;
}

static void build_step(int step)
{
    s_step = step;
    kb_hide();
    if(s_poll_timer && step != 4) {
        lv_timer_delete(s_poll_timer);
        s_poll_timer = NULL;
    }
    lv_obj_clean(s_root);
    build_sidebar(s_root);

    s_main = box(s_root);
    lv_obj_set_height(s_main, LV_PCT(100));
    lv_obj_set_flex_grow(s_main, 1);
    lv_obj_set_style_pad_hor(s_main, s_compact ? 24 : 44, 0);
    lv_obj_set_style_pad_top(s_main, s_compact ? 24 : 34, 0);
    lv_obj_set_style_pad_bottom(s_main, 26, 0);
    lv_obj_set_flex_flow(s_main, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_main, 18, 0);
    lv_obj_add_flag(s_main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_main, LV_SCROLLBAR_MODE_AUTO);

    extern void build_step1(void);
    extern void build_step2(void);
    extern void build_step3(void);
    extern void build_step4(void);
    extern void build_step5(void);
    switch(step) {
        case 1: build_step1(); break;
        case 2: build_step2(); break;
        case 3: build_step3(); break;
        case 4: build_step4(); break;
        default: build_step5(); break;
    }
}

/* ======================================================================
 * Schritt 1: Auftrag anlegen (Kunde, Techniker, Gerät)
 * ====================================================================== */

static void save_step1_fields(void)
{
    if(s_cust_mode_new && s_ta_cust[0]) {
        get_text(s_ta_cust[0], s_new_cust.name, sizeof(s_new_cust.name));
        get_text(s_ta_cust[1], s_new_cust.ref, sizeof(s_new_cust.ref));
        get_text(s_ta_cust[2], s_new_cust.contact, sizeof(s_new_cust.contact));
        get_text(s_ta_cust[3], s_new_cust.phone, sizeof(s_new_cust.phone));
        get_text(s_ta_cust[4], s_new_cust.street, sizeof(s_new_cust.street));
        get_text(s_ta_cust[5], s_new_cust.zip, sizeof(s_new_cust.zip));
        get_text(s_ta_cust[6], s_new_cust.city, sizeof(s_new_cust.city));
    }
    if(s_ta_sys) get_text(s_ta_sys, g.sysname, sizeof(g.sysname));
    if(s_ta_asset) get_text(s_ta_asset, g.asset, sizeof(g.asset));
    if(s_ta_note) get_text(s_ta_note, g.note, sizeof(g.note));
}

static void build_cust_body(void);

static void tab_cb(lv_event_t * e)
{
    int mode_new = (int)(intptr_t)lv_event_get_user_data(e);
    if(mode_new == s_cust_mode_new) return;
    save_step1_fields();
    s_cust_mode_new = mode_new;
    build_cust_body();
}

static void style_tab(lv_obj_t * b, int active)
{
    lv_obj_set_style_bg_opa(b, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(b, C(COL_SURFACE), 0);
    lv_obj_set_style_shadow_width(b, active ? 3 : 0, 0);
    lv_obj_set_style_shadow_opa(b, LV_OPA_20, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0), C(active ? COL_INK : COL_MUTED), 0);
}

static void cust_pick_cb(lv_event_t * e)
{
    s_cust_sel = (int)(intptr_t)lv_event_get_user_data(e);
    build_cust_body();
}

static void fill_cust_list(void)
{
    lv_obj_clean(s_cust_list);
    char q[96] = "";
    if(s_ta_search) get_text(s_ta_search, q, sizeof(q));
    int shown = 0;
    for(int i = s_ncust - 1; i >= 0; i--) { /* neueste zuerst */
        customer_t * c = &s_customers[i];
        if(q[0] && !strcasestr(c->name, q) && !strcasestr(c->city, q) && !strcasestr(c->no, q) && !strcasestr(c->contact, q)) continue;
        lv_obj_t * b = lv_button_create(s_cust_list);
        lv_obj_set_width(b, LV_PCT(100));
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_pad_hor(b, 14, 0);
        lv_obj_set_style_pad_ver(b, 8, 0);
        lv_obj_set_style_bg_color(b, C(i == s_cust_sel ? COL_ACCENT_SOFT : COL_SURFACE), 0);
        lv_obj_set_style_bg_color(b, C(COL_SOFT), LV_STATE_PRESSED);
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(b, C(COL_SOFT), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_outline_color(b, C(COL_ACCENT), LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_width(b, 2, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_pad(b, -2, LV_STATE_FOCUS_KEY);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t * cc = col(b, 0);
        lv_obj_set_width(cc, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(cc, 1);
        lv_obj_t * n = label(cc, c->name, &f_sans_sb16, COL_INK);
        lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
        lv_obj_set_width(n, LV_PCT(100));
        char sub[256];
        snprintf(sub, sizeof(sub), "%s%s%s", c->city, (c->city[0] && c->contact[0]) ? " · " : "", c->contact);
        label(cc, sub[0] ? sub : " ", &f_sans_r14, COL_MUTED);
        label(b, c->no, &f_mono_m14, COL_MUTED);
        lv_obj_add_event_cb(b, cust_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        if(++shown >= 60) break;
    }
    if(shown == 0) {
        lv_obj_t * l = label(s_cust_list, s_ncust ? "Kein Kunde passt zur Suche." : "Noch keine Kunden in der Kartei. Lege einen neuen Kunden an.",
                             &f_sans_r14, COL_MUTED);
        lv_obj_set_style_pad_all(l, 14, 0);
    }
}

static void search_cb(lv_event_t * e)
{
    if(lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) fill_cust_list();
}

static void build_cust_body(void)
{
    style_tab(s_tab_btn[0], s_cust_mode_new);
    style_tab(s_tab_btn[1], !s_cust_mode_new);
    lv_obj_clean(s_cust_body);
    memset(s_ta_cust, 0, sizeof(s_ta_cust));
    s_ta_search = NULL;
    s_cust_list = NULL;
    if(s_cust_mode_new) {
        lv_obj_t * r1 = row(s_cust_body, 12);
        s_ta_cust[0] = field(r1, "Firma / Name *", s_new_cust.name, "z. B. Muster GmbH", 4);
        s_ta_cust[1] = field(r1, "Referenz", s_new_cust.ref, "z. B. Bestell-Nr.", 2);
        lv_obj_t * r2 = row(s_cust_body, 12);
        s_ta_cust[2] = field(r2, "Ansprechpartner", s_new_cust.contact, NULL, 1);
        s_ta_cust[3] = field(r2, "Telefon / E-Mail", s_new_cust.phone, "optional", 1);
        lv_obj_t * r3 = row(s_cust_body, 12);
        s_ta_cust[4] = field(r3, "Straße, Nr.", s_new_cust.street, NULL, 3);
        s_ta_cust[5] = field(r3, "PLZ", s_new_cust.zip, NULL, 1);
        s_ta_cust[6] = field(r3, "Ort", s_new_cust.city, NULL, 2);
        char no[24], t[96];
        storage_next_customer_no(no, sizeof(no));
        snprintf(t, sizeof(t), "Wird als %s in der Kundenkartei gespeichert.", no);
        label(s_cust_body, t, &f_sans_r14, COL_MUTED);
    }
    else {
        s_ta_search = field(s_cust_body, NULL, "", "Name, Ort oder Kunden-Nr. suchen …", 0);
        lv_obj_add_event_cb(s_ta_search, search_cb, LV_EVENT_VALUE_CHANGED, NULL);
        s_cust_list = box(s_cust_body);
        lv_obj_set_size(s_cust_list, LV_PCT(100), 200);
        lv_obj_set_flex_flow(s_cust_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_flag(s_cust_list, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_color(s_cust_list, C(COL_SOFT), 0);
        lv_obj_set_style_border_width(s_cust_list, 1, 0);
        lv_obj_set_style_radius(s_cust_list, 8, 0);
        lv_obj_set_style_clip_corner(s_cust_list, true, 0);
        fill_cust_list();
    }
}

static void build_tech_list(void);

static lv_obj_t * s_ta_tname, * s_ta_tid;

static void tech_save_cb(lv_event_t * e)
{
    (void)e;
    tech_t t;
    memset(&t, 0, sizeof(t));
    get_text(s_ta_tname, t.name, sizeof(t.name));
    get_text(s_ta_tid, t.id, sizeof(t.id));
    if(t.name[0] == '\0') return;
    if(t.id[0] == '\0') snprintf(t.id, sizeof(t.id), "T-%02d", s_ntech + 1);
    storage_save_tech(&t);
    s_ntech = storage_load_techs(s_techs, MAX_TECHS);
    s_tech_sel = s_ntech - 1;
    modal_close();
    build_tech_list();
}

static void tech_add_cb(lv_event_t * e)
{
    (void)e;
    save_step1_fields();
    lv_obj_t * c = modal_open("Techniker hinzufügen", 520);
    lv_obj_t * r = row(c, 12);
    s_ta_tname = field(r, "Name *", "", "Vor- und Nachname", 3);
    char id[16];
    snprintf(id, sizeof(id), "T-%02d", s_ntech + 1);
    s_ta_tid = field(r, "ID", id, NULL, 1);
    lv_obj_t * b = row(c, 12);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    button(b, "Abbrechen", BTN_GHOST, modal_close_cb, NULL);
    button(b, "Speichern", BTN_DARK, tech_save_cb, NULL);
    lv_group_focus_obj(s_ta_tname);
}

static void tech_pick_cb(lv_event_t * e)
{
    s_tech_sel = (int)(intptr_t)lv_event_get_user_data(e);
    build_tech_list();
}

static void build_tech_list(void)
{
    lv_obj_clean(s_tech_list);
    for(int i = 0; i < s_ntech; i++) {
        int on = i == s_tech_sel;
        lv_obj_t * b = lv_button_create(s_tech_list);
        lv_obj_set_width(b, LV_PCT(100));
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_pad_all(b, 8, 0);
        lv_obj_set_style_bg_color(b, C(COL_SURFACE), 0);
        lv_obj_set_style_bg_color(b, C(COL_SOFT), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(b, C(on ? COL_ACCENT : COL_SOFT), 0);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_outline_color(b, C(COL_ACCENT), LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_width(b, 2, LV_STATE_FOCUS_KEY);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(b, 12, 0);
        lv_obj_t * av = box(b);
        lv_obj_set_size(av, 34, 34);
        lv_obj_set_style_radius(av, 99, 0);
        lv_obj_set_style_bg_opa(av, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(av, C(on ? COL_ACCENT : COL_SOFT), 0);
        char ini[8] = "";
        const char * nm = s_techs[i].name;
        int k = 0;
        for(int j = 0; nm[j] && k < 2; j++) {
            if(j == 0 || nm[j - 1] == ' ') {
                int l = 1;
                unsigned char ch = (unsigned char)nm[j];
                if(ch >= 0xC0) l = (ch >= 0xE0) ? 3 : 2;
                strncat(ini, nm + j, l);
                k++;
            }
        }
        for(char * p = ini; *p; p++) if(*p >= 'a' && *p <= 'z') *p -= 32;
        lv_obj_center(label(av, ini, &f_sans_sb14, on ? 0xFFFFFF : COL_INK));
        lv_obj_t * cc = col(b, 0);
        lv_obj_set_width(cc, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(cc, 1);
        lv_obj_t * n = label(cc, s_techs[i].name, &f_sans_sb16, COL_INK);
        lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
        lv_obj_set_width(n, LV_PCT(100));
        char id[40];
        snprintf(id, sizeof(id), "ID %s", s_techs[i].id);
        label(cc, id, &f_sans_r14, COL_MUTED);
        lv_obj_add_event_cb(b, tech_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    if(s_ntech == 0) {
        lv_obj_t * l = label(s_tech_list, "Noch keine Techniker angelegt.", &f_sans_r14, COL_MUTED);
        lv_obj_set_style_pad_ver(l, 6, 0);
    }
}

static lv_obj_t * s_ta_org, * s_ta_orgaddr;

static void org_save_cb(lv_event_t * e)
{
    (void)e;
    get_text(s_ta_org, g.org_name, sizeof(g.org_name));
    get_text(s_ta_orgaddr, g.org_address, sizeof(g.org_address));
    storage_save_org();
    modal_close();
}

static void org_cb(lv_event_t * e)
{
    (void)e;
    save_step1_fields();
    lv_obj_t * c = modal_open("Eigene Firma (erscheint im Bericht)", 620);
    s_ta_org = field(c, "Firmenname", g.org_name, "z. B. Muster IT-Service GmbH", 0);
    s_ta_orgaddr = field(c, "Anschrift", g.org_address, "Straße Nr., PLZ Ort", 0);
    char where[512];
    snprintf(where, sizeof(where), "Gespeichert in %s/firma.txt%s", storage_dir(), storage_on_usb() ? "" : " (nur bis zum Neustart – kein WipeOS-Stick gefunden)");
    lv_obj_t * l = label(c, where, &f_sans_r14, COL_MUTED);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_t * b = row(c, 12);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    button(b, "Abbrechen", BTN_GHOST, modal_close_cb, NULL);
    button(b, "Speichern", BTN_DARK, org_save_cb, NULL);
    lv_group_focus_obj(s_ta_org);
}

static void step1_next_cb(lv_event_t * e)
{
    (void)e;
    save_step1_fields();
    const char * err = NULL;
    if(s_cust_mode_new && s_new_cust.name[0] == '\0') err = "Bitte den Namen des Kunden eintragen.";
    else if(!s_cust_mode_new && (s_cust_sel < 0 || s_cust_sel >= s_ncust)) err = "Bitte einen Kunden aus der Kartei auswählen.";
    else if(s_tech_sel < 0 || s_tech_sel >= s_ntech) err = "Bitte einen Techniker auswählen oder hinzufügen.";
    else if(g.sysname[0] == '\0') err = "Bitte einen Systemnamen für das Gerät eintragen.";
    if(err) {
        lv_label_set_text(s_err_label, err);
        lv_obj_remove_flag(s_err_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if(s_cust_mode_new) {
        g.customer = s_new_cust;
        g.customer_is_new = 1;
        storage_next_customer_no(g.customer.no, sizeof(g.customer.no));
    }
    else {
        g.customer = s_customers[s_cust_sel];
        g.customer_is_new = 0;
    }
    g.tech = s_techs[s_tech_sel];
    if(g.order_no[0] == '\0') storage_next_order_no(g.order_no, sizeof(g.order_no));
    /* neuen Kunden sofort in die Kartei übernehmen */
    if(g.customer_is_new) {
        storage_save_customer(&g.customer);
        s_ncust = storage_load_customers(s_customers, MAX_CUSTOMERS);
        s_cust_mode_new = 0;
        s_cust_sel = s_ncust - 1;
        memset(&s_new_cust, 0, sizeof(s_new_cust));
        g.customer_is_new = 0;
    }
    g.drive_count = sysinfo_scan_drives(g.drives, MAX_DRIVES);
    build_step(2);
}

void build_step1(void)
{
    s_ncust = storage_load_customers(s_customers, MAX_CUSTOMERS);
    s_ntech = storage_load_techs(s_techs, MAX_TECHS);
    if(s_tech_sel >= s_ntech) s_tech_sel = -1;
    if(s_ntech == 1 && s_tech_sel < 0) s_tech_sel = 0;

    lv_obj_t * h = header(s_main, "Auftrag anlegen", NULL);
    lv_obj_t * hr = row(h, 12);
    lv_obj_set_width(hr, LV_SIZE_CONTENT);
    if(!storage_on_usb()) pill(hr, "Kein WipeOS-Stick: Daten nur bis zum Neustart", COL_ACCENT_TEXT, COL_ACCENT_SOFT);
    lv_obj_t * ob = button(hr, g.org_name[0] ? "Eigene Firma" : "Eigene Firma eintragen", BTN_GHOST, org_cb, NULL);
    lv_obj_set_style_pad_ver(ob, 8, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(ob, 0), &f_sans_sb14, 0);

    lv_obj_t * top = row(s_main, 18);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* Kunde */
    lv_obj_t * kc = card(top, 20);
    lv_obj_set_width(kc, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(kc, 1);
    lv_obj_set_height(kc, 330);
    lv_obj_t * kh = row(kc, 12);
    lv_obj_set_flex_align(kh, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(kh, "Kunde", &f_sans_sb18, COL_INK);
    lv_obj_t * tabs = row(kh, 0);
    lv_obj_set_width(tabs, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(tabs, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(tabs, C(COL_BG), 0);
    lv_obj_set_style_radius(tabs, 8, 0);
    lv_obj_set_style_pad_all(tabs, 3, 0);
    const char * tn[2] = {"Neuer Kunde", "Aus Kartei"};
    for(int i = 0; i < 2; i++) {
        s_tab_btn[i] = button(tabs, tn[i], BTN_LINK, tab_cb, (void *)(intptr_t)(i == 0));
        lv_obj_set_style_pad_ver(s_tab_btn[i], 6, 0);
        lv_obj_set_style_pad_hor(s_tab_btn[i], 14, 0);
        lv_obj_set_style_min_height(s_tab_btn[i], 34, 0);
        lv_obj_set_style_radius(s_tab_btn[i], 6, 0);
        lv_obj_set_style_text_font(lv_obj_get_child(s_tab_btn[i], 0), &f_sans_sb14, 0);
    }
    s_cust_body = col(kc, 12);
    build_cust_body();

    /* Techniker */
    lv_obj_t * tc = card(top, 20);
    lv_obj_set_size(tc, s_compact ? 236 : 280, 330);
    label(tc, "Techniker", &f_sans_sb18, COL_INK);
    s_tech_list = col(tc, 8);
    lv_obj_set_flex_grow(s_tech_list, 1);
    lv_obj_set_height(s_tech_list, 10);
    lv_obj_add_flag(s_tech_list, LV_OBJ_FLAG_SCROLLABLE);
    build_tech_list();
    lv_obj_t * ab = button(tc, "+ Techniker hinzufügen", BTN_GHOST, tech_add_cb, NULL);
    lv_obj_set_width(ab, LV_PCT(100));
    lv_obj_set_style_border_opa(ab, LV_OPA_COVER, 0);

    /* Gerät */
    lv_obj_t * gc = card(s_main, 18);
    lv_obj_set_width(gc, LV_PCT(100));
    lv_obj_t * gr = row(gc, 20);
    lv_obj_set_flex_align(gr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_t * s1 = col(gr, 0);
    lv_obj_set_width(s1, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(s1, 3);
    lv_obj_t * s1h = row(s1, 6);
    label(s1h, "Seriennummer", &f_sans_r14, COL_MUTED);
    pill(s1h, "BIOS", COL_GREEN, COL_GREEN_SOFT);
    lv_obj_t * snl = label(s1, g.dmi.serial, &f_mono_m24, COL_INK);
    lv_label_set_long_mode(snl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(snl, LV_PCT(100));
    lv_obj_t * s2 = col(gr, 0);
    lv_obj_set_width(s2, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(s2, 2);
    label(s2, "Hersteller", &f_sans_r14, COL_MUTED);
    lv_obj_t * vl = label(s2, g.dmi.vendor, &f_sans_sb16, COL_INK);
    lv_label_set_long_mode(vl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(vl, LV_PCT(100));
    lv_obj_t * s3 = col(gr, 0);
    lv_obj_set_width(s3, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(s3, 4);
    label(s3, "Modell", &f_sans_r14, COL_MUTED);
    lv_obj_t * ml = label(s3, g.dmi.product, &f_sans_sb16, COL_INK);
    lv_label_set_long_mode(ml, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ml, LV_PCT(100));
    s_ta_sys = field(gr, "Systemname *", g.sysname, "z. B. Buchhaltung-NB-07", 5);
    lv_obj_set_style_border_color(s_ta_sys, C(COL_ACCENT), 0);
    lv_obj_set_style_border_width(s_ta_sys, 2, 0);
    lv_obj_t * gr2 = row(gc, 20);
    s_ta_asset = field(gr2, "Inventar-Nr.", g.asset, "optional", 1);
    s_ta_note = field(gr2, "Notiz", g.note, "optional, erscheint im Bericht", 3);

    lv_obj_t * f = row(s_main, 16);
    lv_obj_set_flex_align(f, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_err_label = label(f, "", &f_sans_sb14, COL_RED);
    lv_obj_add_flag(s_err_label, LV_OBJ_FLAG_HIDDEN);
    button(f, "Weiter zu Laufwerken  " LV_SYMBOL_RIGHT, BTN_DARK, step1_next_cb, NULL);
}

/* ======================================================================
 * Schritt 2: Laufwerke wählen
 * ====================================================================== */

static void update_drive_summary(void)
{
    int n = 0;
    uint64_t total = 0;
    for(int i = 0; i < g.drive_count; i++) {
        if(g.drives[i].selected && !g.drives[i].is_boot) {
            n++;
            total += g.drives[i].size;
        }
    }
    char sz[32], t[96];
    format_size(total, sz, sizeof(sz));
    if(n == 0) snprintf(t, sizeof(t), "Kein Laufwerk ausgewählt");
    else snprintf(t, sizeof(t), "%d Laufwerk%s · %s ausgewählt", n, n == 1 ? "" : "e", sz);
    lv_label_set_text(s_drive_summary, t);
    if(n) lv_obj_remove_state(s_btn_next, LV_STATE_DISABLED);
    else lv_obj_add_state(s_btn_next, LV_STATE_DISABLED);
}

static void drive_toggle_cb(lv_event_t * e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if(g.drives[i].is_boot) return;
    g.drives[i].selected = !g.drives[i].selected;
    build_step(2);
}

static void select_all_cb(lv_event_t * e)
{
    (void)e;
    int all = 1;
    for(int i = 0; i < g.drive_count; i++) if(!g.drives[i].is_boot && !g.drives[i].selected) all = 0;
    for(int i = 0; i < g.drive_count; i++) if(!g.drives[i].is_boot) g.drives[i].selected = !all;
    build_step(2);
}

static void rescan_cb(lv_event_t * e)
{
    (void)e;
    drive_t old[MAX_DRIVES];
    int on = g.drive_count;
    memcpy(old, g.drives, sizeof(old));
    g.drive_count = sysinfo_scan_drives(g.drives, MAX_DRIVES);
    for(int i = 0; i < g.drive_count; i++)
        for(int j = 0; j < on; j++)
            if(strcmp(g.drives[i].dev, old[j].dev) == 0 && strcmp(g.drives[i].serial, old[j].serial) == 0)
                g.drives[i].selected = old[j].selected && !g.drives[i].is_boot;
    build_step(2);
}

static void goto_cb(lv_event_t * e)
{
    build_step((int)(intptr_t)lv_event_get_user_data(e));
}

void build_step2(void)
{
    char sub[128];
    snprintf(sub, sizeof(sub), "%d Datenträger gefunden", g.drive_count);
    lv_obj_t * h = header(s_main, "Laufwerke wählen", NULL);
    lv_obj_t * hb = row(h, 10);
    lv_obj_set_width(hb, LV_SIZE_CONTENT);
    label(hb, sub, &f_sans_r14, COL_MUTED);
    button(hb, "Neu einlesen", BTN_GHOST, rescan_cb, NULL);
    button(hb, "Alle auswählen", BTN_GHOST, select_all_cb, NULL);

    lv_obj_t * grid = box(s_main);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, 16, 0);
    lv_obj_set_style_pad_column(grid, 16, 0);
    lv_obj_set_flex_grow(grid, 1);

    for(int i = 0; i < g.drive_count; i++) {
        drive_t * d = &g.drives[i];
        lv_obj_t * c = lv_button_create(grid);
        lv_obj_set_style_shadow_width(c, 0, 0);
        lv_obj_set_width(c, LV_PCT(49));
        lv_obj_set_height(c, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(c, 12, 0);
        lv_obj_set_style_pad_all(c, 18, 0);
        lv_obj_set_style_bg_color(c, C(d->is_boot ? COL_SOFT : COL_SURFACE), 0);
        lv_obj_set_style_bg_color(c, C(COL_BG), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(c, C(d->selected ? COL_ACCENT : COL_LINE), 0);
        lv_obj_set_style_border_width(c, 2, 0);
        lv_obj_set_style_outline_color(c, C(COL_ACCENT), LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_width(c, 3, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_pad(c, 2, LV_STATE_FOCUS_KEY);
        lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(c, 12, 0);
        lv_obj_add_event_cb(c, drive_toggle_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t * top = row(c, 14);
        lv_obj_t * badge = label(top, d->bus, &f_mono_m14, d->is_boot ? COL_MUTED : 0xFFFFFF);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(badge, C(d->is_boot ? COL_LINE : COL_INK), 0);
        lv_obj_set_style_radius(badge, 8, 0);
        lv_obj_set_style_pad_ver(badge, 8, 0);
        lv_obj_set_width(badge, 72);
        lv_obj_set_style_text_align(badge, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t * tt = col(top, 0);
        lv_obj_set_width(tt, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(tt, 1);
        lv_obj_t * ml = label(tt, d->model, &f_sans_sb16, d->is_boot ? COL_MUTED : COL_INK);
        lv_label_set_long_mode(ml, LV_LABEL_LONG_DOT);
        lv_obj_set_width(ml, LV_PCT(100));
        char ln[200];
        snprintf(ln, sizeof(ln), "%s · S/N %s", d->dev, d->serial[0] ? d->serial : "–");
        lv_obj_t * sl = label(tt, ln, &f_mono_m14, COL_MUTED);
        lv_label_set_long_mode(sl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(sl, LV_PCT(100));
        lv_obj_t * chk = box(top);
        lv_obj_set_size(chk, 24, 24);
        lv_obj_set_style_radius(chk, 6, 0);
        lv_obj_set_style_border_width(chk, 2, 0);
        lv_obj_set_style_border_color(chk, C(d->selected ? COL_ACCENT : (d->is_boot ? COL_LINE : COL_FIELD)), 0);
        lv_obj_set_style_bg_opa(chk, d->selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(chk, C(COL_ACCENT), 0);
        if(d->selected) lv_obj_center(label(chk, LV_SYMBOL_OK, &f_sans_sb14, 0xFFFFFF));

        lv_obj_t * mid = row(c, 20);
        lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        char sz[32];
        format_size(d->size, sz, sizeof(sz));
        label(mid, sz, &f_sans_b30, d->is_boot ? COL_MUTED : COL_INK);
        const char * kind = strcmp(d->bus, "NVMe") == 0 ? "NVMe-SSD" : (d->rotational ? "Festplatte (HDD)" : "SSD / Flash");
        label(mid, kind, &f_sans_r14, COL_MUTED);

        lv_obj_t * note = box(c);
        lv_obj_set_size(note, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_border_side(note, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_color(note, C(COL_SOFT), 0);
        lv_obj_set_style_border_width(note, 1, 0);
        lv_obj_set_style_pad_top(note, 10, 0);
        if(d->is_boot) label(note, "Startmedium von WipeOS · geschützt", &f_sans_sb14, COL_RED);
        else if(strcmp(d->bus, "USB") == 0) label(note, "Externer USB-Datenträger", &f_sans_r14, COL_MUTED);
        else label(note, "Interner Datenträger", &f_sans_r14, COL_MUTED);
    }
    if(g.drive_count == 0) label(grid, "Keine Datenträger gefunden. Prüfe die Anschlüsse und tippe auf „Neu einlesen“.", &f_sans_r16, COL_MUTED);

    lv_obj_t * bar = card(s_main, 12);
    lv_obj_set_width(bar, LV_PCT(100));
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(bar, 22, 0);
    s_drive_summary = label(bar, "", &f_sans_sb16, COL_INK);
    lv_obj_t * br = row(bar, 10);
    lv_obj_set_width(br, LV_SIZE_CONTENT);
    button(br, "Zurück", BTN_LINK, goto_cb, (void *)(intptr_t)1);
    s_btn_next = button(br, "Weiter zur Methode  " LV_SYMBOL_RIGHT, BTN_DARK, goto_cb, (void *)(intptr_t)3);
    update_drive_summary();
}

/* ======================================================================
 * Schritt 3: Methode wählen
 * ====================================================================== */

static void build_plan(void);

static void method_cb(lv_event_t * e)
{
    g.method = (method_id_t)(intptr_t)lv_event_get_user_data(e);
    build_step(3);
}

static void build_plan(void)
{
    lv_obj_clean(s_plan_box);
    label(s_plan_box, "Plan pro Laufwerk", &f_sans_sb18, COL_INK);
    long long total = 0;
    int n = 0;
    for(int i = 0; i < g.drive_count; i++) {
        drive_t * d = &g.drives[i];
        if(!d->selected || d->is_boot) continue;
        n++;
        long long s = estimate_seconds(d, g.method);
        if(s > total) total = s;
        lv_obj_t * c = col(s_plan_box, 2);
        lv_obj_set_style_border_side(c, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(c, C(COL_SOFT), 0);
        lv_obj_set_style_border_width(c, 1, 0);
        lv_obj_set_style_pad_bottom(c, 12, 0);
        char t[200], dur[48];
        char sz[32];
        format_size(d->size, sz, sizeof(sz));
        snprintf(t, sizeof(t), "%s · %s · %s", d->dev, d->bus, sz);
        label(c, t, &f_mono_m14, COL_MUTED);
        label(c, method_short(g.method), &f_sans_sb16, COL_INK);
        format_duration(s, dur, sizeof(dur));
        snprintf(t, sizeof(t), "Dauer etwa %s", dur);
        label(c, t, &f_sans_r14, COL_MUTED);
    }
    lv_obj_t * r = row(s_plan_box, 8);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    label(r, "Gesamtdauer (parallel)", &f_sans_r14, COL_MUTED);
    char dur[48], tt[64];
    format_duration(total, dur, sizeof(dur));
    snprintf(tt, sizeof(tt), "ca. %s", dur);
    label(r, tt, &f_sans_b24, COL_INK);
    lv_obj_t * w = box(s_plan_box);
    lv_obj_set_size(w, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(w, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(w, C(COL_ACCENT_SOFT), 0);
    lv_obj_set_style_radius(w, 8, 0);
    lv_obj_set_style_pad_all(w, 12, 0);
    char t[128];
    snprintf(t, sizeof(t), LV_SYMBOL_WARNING "  Alle Daten auf %d Laufwerk%s werden endgültig gelöscht.", n, n == 1 ? "" : "en");
    lv_obj_t * wl = label(w, t, &f_sans_r14, COL_ACCENT_TEXT);
    lv_label_set_long_mode(wl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wl, LV_PCT(100));
    lv_obj_t * hint = label(s_plan_box, "Die Dauer ist eine Schätzung. Sie hängt vom Laufwerk und vom Anschluss ab.", &f_sans_r14, COL_MUTED);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, LV_PCT(100));
}

static void confirm_start_cb(lv_event_t * e)
{
    (void)e;
    modal_close();
    if(wipe_start() != 0) {
        info_dialog("Start fehlgeschlagen", "nwipe konnte nicht gestartet werden.");
        return;
    }
    build_step(4);
}

static void start_cb(lv_event_t * e)
{
    (void)e;
    lv_obj_t * c = modal_open("Wirklich endgültig löschen?", 600);
    lv_obj_t * l = label(c, "Diese Laufwerke werden vollständig überschrieben. Das lässt sich nicht rückgängig machen.", &f_sans_r16, COL_INK);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_t * lst = col(c, 6);
    lv_obj_set_style_bg_opa(lst, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(lst, C(COL_BG), 0);
    lv_obj_set_style_radius(lst, 8, 0);
    lv_obj_set_style_pad_all(lst, 12, 0);
    for(int i = 0; i < g.drive_count; i++) {
        drive_t * d = &g.drives[i];
        if(!d->selected || d->is_boot) continue;
        char t[256], sz[32];
        format_size(d->size, sz, sizeof(sz));
        snprintf(t, sizeof(t), "%s  ·  %s  ·  %s  ·  S/N %s", d->dev, d->model, sz, d->serial[0] ? d->serial : "–");
        lv_obj_t * dl = label(lst, t, &f_sans_sb14, COL_INK);
        lv_label_set_long_mode(dl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(dl, LV_PCT(100));
    }
    lv_obj_t * b = row(c, 12);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t * cancel = button(b, "Abbrechen", BTN_GHOST, modal_close_cb, NULL);
    button(b, "Endgültig löschen", BTN_DANGER, confirm_start_cb, NULL);
    lv_group_focus_obj(cancel);
}

void build_step3(void)
{
    header(s_main, "Wie soll gelöscht werden?", NULL);
    lv_obj_t * body = row(s_main, 22);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    s_method_list = col(body, 12);
    lv_obj_set_width(s_method_list, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(s_method_list, 1);
    for(int m = 0; m < METHOD_COUNT; m++) {
        int on = (int)g.method == m;
        lv_obj_t * b = lv_button_create(s_method_list);
        lv_obj_set_width(b, LV_PCT(100));
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_style_pad_all(b, 16, 0);
        lv_obj_set_style_bg_color(b, C(COL_SURFACE), 0);
        lv_obj_set_style_bg_color(b, C(COL_BG), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(b, C(on ? COL_ACCENT : COL_LINE), 0);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_outline_color(b, C(COL_ACCENT), LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_width(b, 3, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_pad(b, 2, LV_STATE_FOCUS_KEY);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(b, 16, 0);
        lv_obj_add_event_cb(b, method_cb, LV_EVENT_CLICKED, (void *)(intptr_t)m);
        lv_obj_t * radio = box(b);
        lv_obj_set_size(radio, 20, 20);
        lv_obj_set_style_radius(radio, 99, 0);
        lv_obj_set_style_border_width(radio, 2, 0);
        lv_obj_set_style_border_color(radio, C(on ? COL_ACCENT : 0x9EA3AA), 0);
        if(on) {
            lv_obj_t * dot = box(radio);
            lv_obj_set_size(dot, 10, 10);
            lv_obj_set_style_radius(dot, 99, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(dot, C(COL_ACCENT), 0);
            lv_obj_center(dot);
        }
        lv_obj_t * tc = col(b, 2);
        lv_obj_set_width(tc, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(tc, 1);
        lv_obj_t * tr = row(tc, 10);
        label(tr, method_label((method_id_t)m), &f_sans_sb16, COL_INK);
        if(m == METHOD_AUTO) pill(tr, "Empfohlen", 0xFFFFFF, COL_GREEN);
        lv_obj_t * dl = label(tc, method_desc((method_id_t)m), &f_sans_r14, COL_MUTED);
        lv_label_set_long_mode(dl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(dl, LV_PCT(100));
        lv_obj_t * pl = label(b, method_passes((method_id_t)m), &f_mono_m14, COL_MUTED);
        if(s_compact) lv_obj_add_flag(pl, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t * adv = card(s_method_list, 16);
    lv_obj_set_width(adv, LV_PCT(100));
    lv_obj_set_style_pad_row(adv, 4, 0);
    label(adv, "Immer dabei", &f_sans_sb14, COL_INK);
    lv_obj_t * al = label(adv, "Prüfung des letzten Durchgangs · kryptografisch sicherer Zufallsgenerator (ChaCha20) · "
                          "Startmedium ist geschützt", &f_sans_r14, COL_MUTED);
    lv_label_set_long_mode(al, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(al, LV_PCT(100));

    s_plan_box = card(body, 20);
    lv_obj_set_width(s_plan_box, s_compact ? 290 : 360);
    lv_obj_set_style_pad_row(s_plan_box, 14, 0);
    build_plan();

    lv_obj_t * f = row(s_main, 12);
    lv_obj_set_flex_align(f, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    button(f, "Zurück", BTN_LINK, goto_cb, (void *)(intptr_t)2);
    button(f, "Löschen starten", BTN_ACCENT, start_cb, NULL);
}

/* ======================================================================
 * Schritt 4: Löschen läuft
 * ====================================================================== */

static void finish_wipe(void)
{
    if(s_poll_timer) {
        lv_timer_delete(s_poll_timer);
        s_poll_timer = NULL;
    }
    if(s_dim) {
        lv_obj_delete(s_dim);
        s_dim = NULL;
    }
    g.report_path[0] = '\0';
    g.report_error[0] = '\0';
    report_create(g.report_path, sizeof(g.report_path), g.report_error, sizeof(g.report_error));
    build_step(5);
}

static const char * phase_text(const wipe_drive_t * d, char * buf, size_t len)
{
    if(d->status == -1) return "Wartet …";
    if(d->status == 0) {
        if(d->result == 0 && d->pass_errors + d->verify_errors + d->fsync_errors == 0) return LV_SYMBOL_OK "  Gelöscht";
        return LV_SYMBOL_CLOSE "  Fehler";
    }
    const char * what = d->passtype == 2 ? "Prüft" : (d->passtype == 3 ? "Schreibt Nullen" : "Schreibt");
    snprintf(buf, len, "%s · Durchgang %d/%d · %.0f %%", what, d->pass > 0 ? d->pass : 1, d->passes > 0 ? d->passes : 1, d->percent);
    return buf;
}

static void poll_cb(lv_timer_t * t)
{
    (void)t;
    int running = wipe_poll();
    const wipe_status_t * st = &g.status;
    if(st->valid && st->count > 0) {
        double sum = 0, wsum = 0;
        long long eta = 0;
        uint64_t errs = 0;
        for(int i = 0; i < st->count && i < s_rows; i++) {
            const wipe_drive_t * d = &st->d[i];
            double w = d->size ? (double)d->size : 1.0;
            sum += d->percent * w;
            wsum += w;
            if(d->eta > eta) eta = d->eta;
            errs += d->pass_errors + d->verify_errors + d->fsync_errors;
            char buf[128];
            lv_label_set_text(s_row_status[i], phase_text(d, buf, sizeof(buf)));
            int ok = d->status == 0 && d->result == 0;
            lv_obj_set_style_text_color(s_row_status[i], C(d->status == 0 ? (ok ? COL_GREEN : COL_RED) : COL_ACCENT), 0);
            lv_bar_set_value(s_row_bar[i], (int)(d->percent * 10), LV_ANIM_OFF);
            lv_obj_set_style_bg_color(s_row_bar[i], C(d->status == 0 ? (ok ? COL_GREEN : COL_RED) : (d->passtype == 2 ? 0x1864AB : COL_ACCENT)),
                                      LV_PART_INDICATOR);
            char spd[48], rest[48], info[160];
            if(d->status == 1 && d->throughput) snprintf(spd, sizeof(spd), "%llu MB/s", (unsigned long long)(d->throughput / 1000000));
            else str_copy(spd, "–", sizeof(spd));
            format_duration(d->status == 1 ? d->eta : -1, rest, sizeof(rest));
            snprintf(info, sizeof(info), "Tempo %s     Rest %s     Fehler %llu", spd, rest,
                     (unsigned long long)(d->pass_errors + d->verify_errors + d->fsync_errors));
            lv_label_set_text(s_row_info[i], info);
        }
        int pct = wsum > 0 ? (int)(sum / wsum) : 0;
        lv_arc_set_value(s_arc, pct);
        char b[48];
        snprintf(b, sizeof(b), "%d %%", pct);
        lv_label_set_text(s_arc_label, b);
        format_duration(running ? eta : 0, b, sizeof(b));
        lv_label_set_text(s_lbl_remaining, eta > 0 ? b : "wird berechnet …");
        format_duration((long long)(time(NULL) - g.wipe_start), b, sizeof(b));
        lv_label_set_text(s_lbl_runtime, b);
        if(eta > 0) {
            time_t fin = time(NULL) + eta;
            struct tm tm;
            localtime_r(&fin, &tm);
            strftime(b, sizeof(b), "ca. %H:%M Uhr", &tm);
            lv_label_set_text(s_lbl_finish, b);
        }
        snprintf(b, sizeof(b), "%llu", (unsigned long long)errs);
        lv_label_set_text(s_lbl_errors, errs ? b : "keine");
        lv_obj_set_style_text_color(s_lbl_errors, C(errs ? COL_RED : COL_GREEN), 0);
    }
    if(!running) finish_wipe();
}

static void dim_off_cb(lv_event_t * e)
{
    (void)e;
    if(s_dim) {
        lv_obj_delete(s_dim);
        s_dim = NULL;
    }
}

static void dim_cb(lv_event_t * e)
{
    (void)e;
    s_dim = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_dim);
    lv_obj_set_size(s_dim, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_dim, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_dim, lv_color_black(), 0);
    lv_obj_add_flag(s_dim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_dim, dim_off_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * l = label(s_dim, "Löschen läuft weiter · tippen zum Aufwecken", &f_sans_r14, 0x3A3D42);
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -20);
}

static void abort_confirm_cb(lv_event_t * e)
{
    (void)e;
    modal_close();
    wipe_abort();
}

static void abort_cb(lv_event_t * e)
{
    (void)e;
    lv_obj_t * c = modal_open("Löschen abbrechen?", 560);
    lv_obj_t * l = label(c, "Abgebrochene Laufwerke sind nur teilweise gelöscht. Der Bericht vermerkt den Abbruch.", &f_sans_r16, COL_INK);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_t * b = row(c, 12);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t * keep = button(b, "Weiter löschen", BTN_DARK, modal_close_cb, NULL);
    button(b, "Abbrechen", BTN_DANGER, abort_confirm_cb, NULL);
    lv_group_focus_obj(keep);
}

static lv_obj_t * stat(lv_obj_t * parent, const char * k, const lv_font_t * f)
{
    lv_obj_t * c = col(parent, 0);
    lv_obj_set_width(c, LV_PCT(48));
    label(c, k, &f_sans_r14, COL_MUTED);
    return label(c, "–", f, COL_INK);
}

void build_step4(void)
{
    lv_obj_t * h = header(s_main, "Löschen läuft", NULL);
    (void)h;
    lv_obj_t * sub = label(s_main, "Gerät nicht ausschalten. Du kannst den Bildschirm abdunkeln, der Vorgang läuft weiter.", &f_sans_r16,
                           COL_MUTED);
    lv_obj_set_style_margin_top(sub, -10, 0);

    lv_obj_t * top = card(s_main, 22);
    lv_obj_set_width(top, LV_PCT(100));
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, 40, 0);
    s_arc = lv_arc_create(top);
    lv_obj_set_size(s_arc, 160, 160);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_range(s_arc, 0, 100);
    lv_arc_set_value(s_arc, 0);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, C(COL_SOFT), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, C(COL_ACCENT), LV_PART_INDICATOR);
    lv_group_remove_obj(s_arc);
    lv_obj_t * ac = col(s_arc, 0);
    lv_obj_set_width(ac, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(ac, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_center(ac);
    s_arc_label = label(ac, "0 %", &f_sans_b30, COL_INK);
    label(ac, "gesamt", &f_sans_r14, COL_MUTED);

    lv_obj_t * stats = col(top, 18);
    lv_obj_set_width(stats, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(stats, 1);
    lv_obj_t * sr1 = row(stats, 16);
    lv_obj_t * sr2 = row(stats, 16);
    s_lbl_remaining = stat(sr1, "Verbleibend", &f_sans_b24);
    s_lbl_runtime = stat(sr1, "Laufzeit", &f_sans_b24);
    s_lbl_finish = stat(sr2, "Fertig voraussichtlich", &f_sans_sb18);
    s_lbl_errors = stat(sr2, "Fehler", &f_sans_sb18);

    s_rows = 0;
    for(int i = 0; i < g.drive_count && s_rows < MAX_DRIVES; i++) {
        drive_t * d = &g.drives[i];
        if(!d->selected || d->is_boot) continue;
        lv_obj_t * c = card(s_main, 16);
        lv_obj_set_width(c, LV_PCT(100));
        lv_obj_set_style_pad_row(c, 8, 0);
        lv_obj_t * r = row(c, 12);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t * rl = row(r, 12);
        lv_obj_set_width(rl, LV_SIZE_CONTENT);
        label(rl, d->model, &f_sans_sb16, COL_INK);
        label(rl, d->dev, &f_mono_m14, COL_MUTED);
        s_row_status[s_rows] = label(r, "Startet …", &f_sans_sb14, COL_ACCENT);
        s_row_bar[s_rows] = lv_bar_create(c);
        lv_obj_set_size(s_row_bar[s_rows], LV_PCT(100), 8);
        lv_bar_set_range(s_row_bar[s_rows], 0, 1000);
        lv_obj_set_style_bg_color(s_row_bar[s_rows], C(COL_SOFT), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_row_bar[s_rows], C(COL_ACCENT), LV_PART_INDICATOR);
        lv_obj_set_style_radius(s_row_bar[s_rows], 99, LV_PART_MAIN);
        lv_obj_set_style_radius(s_row_bar[s_rows], 99, LV_PART_INDICATOR);
        s_row_info[s_rows] = label(c, "", &f_sans_r14, COL_MUTED);
        s_rows++;
    }

    lv_obj_t * f = row(s_main, 12);
    lv_obj_set_flex_align(f, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    button(f, "Bildschirm abdunkeln", BTN_GHOST, dim_cb, NULL);
    lv_obj_t * ab = button(f, "Abbrechen", BTN_GHOST, abort_cb, NULL);
    lv_obj_set_style_text_color(lv_obj_get_child(ab, 0), C(COL_RED), 0);
    lv_obj_set_style_border_color(ab, C(0xE8B4B4), 0);

    if(!s_poll_timer) s_poll_timer = lv_timer_create(poll_cb, 1000, NULL);
}

/* ======================================================================
 * Schritt 5: Fertig + Bericht
 * ====================================================================== */

static void new_order_cb(lv_event_t * e)
{
    (void)e;
    /* Kunde und Techniker bleiben gewählt, neues Gerät/Auftrag */
    g.order_no[0] = '\0';
    g.sysname[0] = '\0';
    g.asset[0] = '\0';
    g.note[0] = '\0';
    for(int i = 0; i < g.drive_count; i++) g.drives[i].selected = 0;
    memset(&g.status, 0, sizeof(g.status));
    build_step(1);
}

static void poweroff_cb(lv_event_t * e)
{
    (void)e;
    app_quit(20);
}

static void reboot_cb(lv_event_t * e)
{
    (void)e;
    app_quit(21);
}

void build_step5(void)
{
    const wipe_status_t * st = &g.status;
    int ok = 0;
    for(int i = 0; i < st->count; i++) {
        const wipe_drive_t * d = &st->d[i];
        if(d->status == 0 && d->result == 0 && d->pass_errors + d->verify_errors + d->fsync_errors == 0) ok++;
    }
    int all_ok = st->count > 0 && ok == st->count;

    lv_obj_t * h = row(s_main, 20);
    lv_obj_t * icon = box(h);
    lv_obj_set_size(icon, 64, 64);
    lv_obj_set_style_radius(icon, 99, 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(icon, C(all_ok ? COL_GREEN : COL_RED), 0);
    lv_obj_center(label(icon, all_ok ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE, &f_sans_b24, 0xFFFFFF));
    lv_obj_t * hc = col(h, 2);
    lv_obj_set_width(hc, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(hc, 1);
    label(hc, "SCHRITT 5 VON 5", &f_sans_sb14, COL_MUTED);
    label(hc, all_ok ? "Alle Laufwerke sicher gelöscht" : "Nicht alle Laufwerke wurden gelöscht", &f_sans_b30, COL_INK);
    char sub[256], dur[48];
    format_duration((long long)(g.wipe_end > g.wipe_start ? g.wipe_end - g.wipe_start : 0), dur, sizeof(dur));
    snprintf(sub, sizeof(sub), "%s · %s · %s · %d von %d gelöscht", g.customer.name, g.sysname, dur, ok, st->count);
    lv_obj_t * sl = label(hc, sub, &f_sans_r16, COL_MUTED);
    lv_label_set_long_mode(sl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(sl, LV_PCT(100));

    lv_obj_t * body = row(s_main, 22);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t * tbl = card(body, 0);
    lv_obj_set_width(tbl, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(tbl, 1);
    lv_obj_set_style_pad_row(tbl, 0, 0);
    lv_obj_set_style_clip_corner(tbl, true, 0);
    lv_obj_t * th = row(tbl, 10);
    lv_obj_set_style_bg_opa(th, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(th, C(0xF7F6F3), 0);
    lv_obj_set_style_pad_all(th, 12, 0);
    lv_obj_set_style_pad_left(th, 20, 0);
    const char * hd[3] = {"Laufwerk", "Dauer", "Ergebnis"};
    int wd[3] = {55, 20, 25};
    for(int k = 0; k < 3; k++) lv_obj_set_width(label(th, hd[k], &f_sans_sb14, COL_MUTED), LV_PCT(wd[k] - 2));
    for(int i = 0; i < st->count; i++) {
        const wipe_drive_t * d = &st->d[i];
        lv_obj_t * r = row(tbl, 10);
        lv_obj_set_style_pad_all(r, 12, 0);
        lv_obj_set_style_pad_left(r, 20, 0);
        lv_obj_set_style_border_side(r, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_color(r, C(COL_SOFT), 0);
        lv_obj_set_style_border_width(r, 1, 0);
        lv_obj_t * c0 = col(r, 0);
        lv_obj_set_width(c0, LV_PCT(wd[0] - 2));
        const char * mname = d->model[0] ? d->model : d->device;
        for(int k = 0; k < g.drive_count; k++)
            if(strcmp(g.drives[k].dev, d->device) == 0 && strlen(g.drives[k].model) > strlen(mname)) mname = g.drives[k].model;
        lv_obj_t * m = label(c0, mname, &f_sans_sb16, COL_INK);
        lv_label_set_long_mode(m, LV_LABEL_LONG_DOT);
        lv_obj_set_width(m, LV_PCT(100));
        char s2[160];
        snprintf(s2, sizeof(s2), "S/N %s", d->serial[0] ? d->serial : "–");
        label(c0, s2, &f_mono_m14, COL_MUTED);
        char du[48];
        format_duration(d->end > d->start ? (long long)(d->end - d->start) : -1, du, sizeof(du));
        lv_obj_set_width(label(r, du, &f_sans_r16, COL_INK), LV_PCT(wd[1] - 2));
        int dok = d->status == 0 && d->result == 0 && d->pass_errors + d->verify_errors + d->fsync_errors == 0;
        pill(r, dok ? "Gelöscht" : (d->status != 0 ? "Abgebrochen" : "Fehler"), 0xFFFFFF, dok ? COL_GREEN : COL_RED);
    }

    lv_obj_t * rc = card(body, 20);
    lv_obj_set_width(rc, s_compact ? 290 : 360);
    label(rc, "Löschbericht", &f_sans_sb18, COL_INK);
    if(g.report_path[0] && g.report_error[0] == '\0') {
        const char * fn = strrchr(g.report_path, '/');
        lv_obj_t * fl = label(rc, fn ? fn + 1 : g.report_path, &f_mono_m14, COL_MUTED);
        lv_label_set_long_mode(fl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(fl, LV_PCT(100));
        lv_obj_t * ok_l = label(rc, storage_on_usb() ? LV_SYMBOL_OK "  Gespeichert auf dem WipeOS-Stick im Ordner „berichte“"
                                : LV_SYMBOL_WARNING "  Kein WipeOS-Stick gefunden – Bericht liegt nur im Arbeitsspeicher (/tmp/berichte)",
                                &f_sans_sb14, storage_on_usb() ? COL_GREEN : COL_ACCENT);
        lv_label_set_long_mode(ok_l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(ok_l, LV_PCT(100));
    }
    else {
        lv_obj_t * el = label(rc, g.report_error[0] ? g.report_error : "Bericht konnte nicht erstellt werden.", &f_sans_sb14, COL_RED);
        lv_label_set_long_mode(el, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(el, LV_PCT(100));
    }
    char ord[96];
    snprintf(ord, sizeof(ord), "Auftrag %s · Kunde %s", g.order_no, g.customer.no);
    label(rc, ord, &f_sans_r14, COL_MUTED);

    lv_obj_t * f = row(s_main, 12);
    lv_obj_set_flex_align(f, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    button(f, "Neustart", BTN_GHOST, reboot_cb, NULL);
    button(f, "Herunterfahren", BTN_GHOST, poweroff_cb, NULL);
    button(f, "Nächster Auftrag", BTN_ACCENT, new_order_cb, NULL);
}

/* ======================================================================
 * Start
 * ====================================================================== */

void ui_init(void)
{
    lv_display_t * disp = lv_display_get_default();
    lv_theme_t * th = lv_theme_default_init(disp, C(COL_ACCENT), C(COL_INK), false, &f_sans_r16);
    lv_display_set_theme(disp, th);
    sysinfo_read_dmi(&g.dmi);
    g.method = METHOD_AUTO;

    s_compact = lv_display_get_horizontal_resolution(disp) < 1200;
    lv_obj_t * scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, C(COL_BG), 0);
    lv_obj_set_style_text_font(scr, &f_sans_r16, 0);
    s_root = box(scr);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_ROW);
    build_step(1);
}

void ui_tick(void)
{
}

/* ---- nur für automatische Tests (Bildschirmfotos ohne Hardware) ---- */
void ui_test_action(const char * cmd)
{
    if(strcmp(cmd, "demo") == 0) {
        tech_t t1 = {"T-04", "Markus Keller"}, t2 = {"T-07", "Selin Yılmaz"};
        if(storage_load_techs(s_techs, MAX_TECHS) == 0) {
            storage_save_tech(&t1);
            storage_save_tech(&t2);
        }
        customer_t c;
        memset(&c, 0, sizeof(c));
        if(storage_load_customers(s_customers, MAX_CUSTOMERS) == 0) {
            const char * names[3][3] = {{"Autohaus Lindner", "Augsburg", "M. Lindner"}, {"Praxis Dr. Sommer", "München", "Dr. K. Sommer"},
                {"Stadtwerke Ostend", "Freising", "IT-Abteilung"}};
            for(int i = 0; i < 3; i++) {
                storage_next_customer_no(c.no, sizeof(c.no));
                str_copy(c.name, names[i][0], sizeof(c.name));
                str_copy(c.city, names[i][1], sizeof(c.city));
                str_copy(c.contact, names[i][2], sizeof(c.contact));
                storage_save_customer(&c);
            }
        }
        str_copy(g.org_name, "Muster IT-Service GmbH", sizeof(g.org_name));
        str_copy(g.org_address, "Werkstraße 3, 80999 München", sizeof(g.org_address));
        str_copy(s_new_cust.name, "Berger Steuerberatung GmbH", sizeof(s_new_cust.name));
        str_copy(s_new_cust.contact, "Anna Berger", sizeof(s_new_cust.contact));
        str_copy(s_new_cust.street, "Hauptstraße 12", sizeof(s_new_cust.street));
        str_copy(s_new_cust.zip, "80331", sizeof(s_new_cust.zip));
        str_copy(s_new_cust.city, "München", sizeof(s_new_cust.city));
        str_copy(g.sysname, "Buchhaltung-NB-07", sizeof(g.sysname));
        s_tech_sel = 0;
        s_cust_mode_new = 1;
        build_step(1);
    }
    else if(strcmp(cmd, "list") == 0) {
        s_cust_mode_new = 0;
        build_step(1);
    }
    else if(strcmp(cmd, "next") == 0) {
        if(s_step == 1) step1_next_cb(NULL);
        else if(s_step == 2) build_step(3);
    }
    else if(strncmp(cmd, "select:", 7) == 0) {
        int i = atoi(cmd + 7);
        if(i < g.drive_count && !g.drives[i].is_boot) g.drives[i].selected = 1;
        build_step(s_step);
    }
    else if(strcmp(cmd, "confirm") == 0) start_cb(NULL);
    else if(strcmp(cmd, "close") == 0) modal_close();
    else if(strcmp(cmd, "start") == 0) confirm_start_cb(NULL);
    else if(strcmp(cmd, "tech") == 0) tech_add_cb(NULL);
    else if(strncmp(cmd, "step:", 5) == 0) build_step(atoi(cmd + 5));
}
