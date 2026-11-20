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
static int s_shift = 0, s_altgr = 0, s_ctrl = 0, s_caps = 0, s_alt = 0, s_toggled = 0;
static uint32_t s_keyq[64];
static int s_kq_r = 0, s_kq_w = 0;
static uint32_t s_last_scan = 0;

/*
 * Keyboard layouts. Each layout lists the 48 character keys in this order:
 *   row 1: ` 1 2 3 4 5 6 7 8 9 0 - =        (13 keys)
 *   row 2: Q W E R T Y U I O P [ ]          (12)
 *   row 3: A S D F G H J K L ; ' \          (12)
 *   row 4: <102nd> Z X C V B N M , . /      (11)
 * as space-separated tokens for: normal, Shift, AltGr. "_" = nothing.
 * Dead keys: D' acute, D` grave, D^ circumflex, D" diaeresis, D~ tilde, Dv caron.
 * AltGr + Shift gives the upper-case form of an AltGr letter (e.g. Polish Ą).
 */
#define NKEYS 48
static const unsigned short s_keycodes[NKEYS] = {
    KEY_GRAVE, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0, KEY_MINUS, KEY_EQUAL,
    KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P, KEY_LEFTBRACE, KEY_RIGHTBRACE,
    KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_BACKSLASH,
    KEY_102ND, KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M, KEY_COMMA, KEY_DOT, KEY_SLASH,
};

typedef struct {
    const char * id;
    const char * name;
    const char * n, * s, * a;
    int cyr; /* Cyrillic: Alt+Shift switches to US and back */
} layout_t;

#define US_N "` 1 2 3 4 5 6 7 8 9 0 - = q w e r t y u i o p [ ] a s d f g h j k l ; ' \\ \\ z x c v b n m , . /"
#define US_S "~ ! @ # $ % ^ & * ( ) _ + Q W E R T Y U I O P { } A S D F G H J K L : \" | | Z X C V B N M < > ?"
#define NONE "_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _"

static const layout_t s_layouts[] = {
    {"us", "English (US)", US_N, US_S, NONE, 0},
    {"gb", "English (UK)",
     "` 1 2 3 4 5 6 7 8 9 0 - = q w e r t y u i o p [ ] a s d f g h j k l ; ' # \\ z x c v b n m , . /",
     "¬ ! \" £ $ % ^ & * ( ) _ + Q W E R T Y U I O P { } A S D F G H J K L : @ ~ | Z X C V B N M < > ?",
     "¦ _ _ _ € _ _ _ _ _ _ _ _ _ _ é _ _ _ ú í ó _ _ _ á _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _", 0},
    {"de", "Deutsch",
     "D^ 1 2 3 4 5 6 7 8 9 0 ß D' q w e r t z u i o p ü + a s d f g h j k l ö ä # < y x c v b n m , . -",
     "° ! \" § $ % & / ( ) = ? D` Q W E R T Z U I O P Ü * A S D F G H J K L Ö Ä ' > Y X C V B N M ; : _",
     "_ _ ² ³ _ _ _ { [ ] } \\ _ @ _ € _ _ _ _ _ _ _ _ ~ _ _ _ _ _ _ _ _ _ _ _ _ | _ _ _ _ _ _ µ _ _ _", 0},
    {"ch", "Schweiz (Deutsch)",
     "§ 1 2 3 4 5 6 7 8 9 0 ' D^ q w e r t z u i o p ü D\" a s d f g h j k l ö ä $ < y x c v b n m , . -",
     "° + \" * ç % & / ( ) = ? D` Q W E R T Z U I O P è ! A S D F G H J K L é à £ > Y X C V B N M ; : _",
     "_ ¦ @ # ° § ¬ | ¢ _ _ D' D~ _ _ € _ _ _ _ _ _ _ [ ] _ _ _ _ _ _ _ _ _ _ { } \\ _ _ _ _ _ _ _ _ _ _", 0},
    {"ch-fr", "Suisse (Français)",
     "§ 1 2 3 4 5 6 7 8 9 0 ' D^ q w e r t z u i o p è D\" a s d f g h j k l é à $ < y x c v b n m , . -",
     "° + \" * ç % & / ( ) = ? D` Q W E R T Z U I O P ü ! A S D F G H J K L ö ä £ > Y X C V B N M ; : _",
     "_ ¦ @ # ° § ¬ | ¢ _ _ D' D~ _ _ € _ _ _ _ _ _ _ [ ] _ _ _ _ _ _ _ _ _ _ { } \\ _ _ _ _ _ _ _ _ _ _", 0},
    {"fr", "Français (AZERTY)",
     "² & é \" ' ( - è _U ç à ) = a z e r t y u i o p D^ $ q s d f g h j k l m ù * < w x c v b n , ; : !",
     "_ 1 2 3 4 5 6 7 8 9 0 ° + A Z E R T Y U I O P D\" £ Q S D F G H J K L M % µ > W X C V B N ? . / §",
     "_ _ ~ # { [ | ` \\ ^ @ ] } _ _ € _ _ _ _ _ _ _ _ ¤ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _", 0},
    {"be", "Belgique / België",
     "² & é \" ' ( § è ! ç à ) - a z e r t y u i o p D^ $ q s d f g h j k l m ù µ < w x c v b n , ; : =",
     "³ 1 2 3 4 5 6 7 8 9 0 ° _U A Z E R T Y U I O P D\" * Q S D F G H J K L M % £ > W X C V B N ? . / +",
     "_ | @ # _ _ ^ _ _ { } _ _ _ _ € _ _ _ _ _ _ _ [ ] _ _ _ _ _ _ _ _ _ _ D' D` \\ _ _ _ _ _ _ _ _ _ ~", 0},
    {"es", "Español",
     "º 1 2 3 4 5 6 7 8 9 0 ' ¡ q w e r t y u i o p D` + a s d f g h j k l ñ D' ç < z x c v b n m , . -",
     "ª ! \" · $ % & / ( ) = ? ¿ Q W E R T Y U I O P D^ * A S D F G H J K L Ñ D\" Ç > Z X C V B N M ; : _",
     "\\ | @ # ~ € ¬ _ _ _ _ _ _ _ _ € _ _ _ _ _ _ _ [ ] _ _ _ _ _ _ _ _ _ _ { } _ _ _ _ _ _ _ _ _ _ _", 0},
    {"latam", "Español (Latinoamérica)",
     "| 1 2 3 4 5 6 7 8 9 0 ' ¿ q w e r t y u i o p D' + a s d f g h j k l ñ { } < z x c v b n m , . -",
     "° ! \" # $ % & / ( ) = ? ¡ Q W E R T Y U I O P D\" * A S D F G H J K L Ñ [ ] > Z X C V B N M ; : _",
     "¬ _ _ _ _ _ _ _ _ _ _ \\ _ @ _ € _ _ _ _ _ _ _ _ ~ _ _ _ _ _ _ _ _ _ _ ^ ` _ _ _ _ _ _ _ _ _ _ _", 0},
    {"it", "Italiano",
     "\\ 1 2 3 4 5 6 7 8 9 0 ' ì q w e r t y u i o p è + a s d f g h j k l ò à ù < z x c v b n m , . -",
     "| ! \" £ $ % & / ( ) = ? ^ Q W E R T Y U I O P é * A S D F G H J K L ç ° § > Z X C V B N M ; : _",
     "_ _ _ _ _ € _ { [ ] } ` ~ _ _ € _ _ _ _ _ _ _ [ ] _ _ _ _ _ _ _ _ _ @ # _ _ _ _ _ _ _ _ _ _ _ _", 0},
    {"pt", "Português",
     "\\ 1 2 3 4 5 6 7 8 9 0 ' « q w e r t y u i o p + D' a s d f g h j k l ç º D~ < z x c v b n m , . -",
     "| ! \" # $ % & / ( ) = ? » Q W E R T Y U I O P * D` A S D F G H J K L Ç ª D^ > Z X C V B N M ; : _",
     "_ _ @ £ § € _ { [ ] } _ _ _ _ € _ _ _ _ _ _ _ D\" _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _", 0},
    {"br", "Português (Brasil)",
     "' 1 2 3 4 5 6 7 8 9 0 - = q w e r t y u i o p D' [ a s d f g h j k l ç D~ ] \\ z x c v b n m , . ;",
     "\" ! @ # $ % D\" & * ( ) _ + Q W E R T Y U I O P D` { A S D F G H J K L Ç D^ } | Z X C V B N M < > :",
     "_ ¹ ² ³ £ ¢ ¬ _ _ _ _ _ § / ? € _ _ _ _ _ _ _ _ ª _ _ _ _ _ _ _ _ _ _ _ º _ _ _ _ _ _ _ _ _ _ _", 0},
    {"pl", "Polski", US_N, US_S,
     "_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ ę _ _ _ € _ ó _ _ _ ą ś _ _ _ _ _ _ ł _ _ _ _ ż ź ć _ _ ń _ _ _ _", 0},
    {"tr", "Türkçe (Q)",
     "\" 1 2 3 4 5 6 7 8 9 0 * - q w e r t y u ı o p ğ ü a s d f g h j k l ş i , < z x c v b n m ö ç .",
     "é ! ' ^ + % & / ( ) = ? _U Q W E R T Y U I O P Ğ Ü A S D F G H J K L Ş İ ; > Z X C V B N M Ö Ç :",
     "< > £ # $ ½ _ { [ ] } \\ | @ _ € _ _ _ _ _ _ _ D\" ~ _ _ _ _ _ _ _ _ _ D' _ ` | _ _ _ _ _ _ _ _ _ _", 0},
    {"se", "Svenska / Suomi",
     "§ 1 2 3 4 5 6 7 8 9 0 + D' q w e r t y u i o p å D\" a s d f g h j k l ö ä ' < z x c v b n m , . -",
     "½ ! \" # ¤ % & / ( ) = ? D` Q W E R T Y U I O P Å D^ A S D F G H J K L Ö Ä * > Z X C V B N M ; : _",
     "_ _ @ £ $ € _ { [ ] } \\ _ _ _ € _ _ _ _ _ _ _ _ D~ _ _ _ _ _ _ _ _ _ _ _ _ | _ _ _ _ _ _ µ _ _ _", 0},
    {"no", "Norsk",
     "| 1 2 3 4 5 6 7 8 9 0 + \\ q w e r t y u i o p å D\" a s d f g h j k l ø æ ' < z x c v b n m , . -",
     "§ ! \" # ¤ % & / ( ) = ? D` Q W E R T Y U I O P Å D^ A S D F G H J K L Ø Æ * > Z X C V B N M ; : _",
     "_ _ @ £ $ € _ { [ ] } _ D' _ _ € _ _ _ _ _ _ _ _ D~ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ µ _ _ _", 0},
    {"dk", "Dansk",
     "½ 1 2 3 4 5 6 7 8 9 0 + D' q w e r t y u i o p å D\" a s d f g h j k l æ ø ' < z x c v b n m , . -",
     "§ ! \" # ¤ % & / ( ) = ? D` Q W E R T Y U I O P Å D^ A S D F G H J K L Æ Ø * > Z X C V B N M ; : _",
     "_ _ @ £ $ € _ { [ ] } _ | _ _ € _ _ _ _ _ _ _ _ D~ _ _ _ _ _ _ _ _ _ _ _ _ \\ _ _ _ _ _ _ µ _ _ _", 0},
    {"cz", "Čeština",
     "; + ě š č ř ž ý á í é = D' q w e r t z u i o p ú ) a s d f g h j k l ů § D\" \\ y x c v b n m , . -",
     "° 1 2 3 4 5 6 7 8 9 0 % Dv Q W E R T Z U I O P / ( A S D F G H J K L \" ! ' | Y X C V B N M ? : _",
     "_ ~ _ ^ _ _ _ ` _ _ _ _ _ \\ | € _ _ _ _ _ _ _ _ _ _ _ _ [ ] _ _ _ _ $ _ _ _ _ # & @ { } _ < > _", 0},
    {"ru", "Русский",
     "ё 1 2 3 4 5 6 7 8 9 0 - = й ц у к е н г ш щ з х ъ ф ы в а п р о л д ж э \\ \\ я ч с м и т ь б ю .",
     "Ё ! \" № ; % : ? * ( ) _ + Й Ц У К Е Н Г Ш Щ З Х Ъ Ф Ы В А П Р О Л Д Ж Э / / Я Ч С М И Т Ь Б Ю ,",
     NONE, 1},
    {"ua", "Українська",
     "' 1 2 3 4 5 6 7 8 9 0 - = й ц у к е н г ш щ з х ї ф і в а п р о л д ж є \\ \\ я ч с м и т ь б ю .",
     "~ ! \" № ; % : ? * ( ) _ + Й Ц У К Е Н Г Ш Щ З Х Ї Ф І В А П Р О Л Д Ж Є / / Я Ч С М И Т Ь Б Ю ,",
     "_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ ґ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _", 1},
};
#define NLAYOUTS ((int)(sizeof(s_layouts) / sizeof(s_layouts[0])))

static char s_tok[2][3][NKEYS][8]; /* [0] = selected layout, [1] = US (for Cyrillic toggle) */
static int s_layout = 0;
static int s_latin = 0;            /* Cyrillic layout temporarily switched to US */
static char s_dead = 0;            /* pending dead key: ' ` ^ " ~ v */

static void parse_row(const char * src, char out[NKEYS][8])
{
    int k = 0;
    while(*src && k < NKEYS) {
        while(*src == ' ') src++;
        int n = 0;
        while(*src && *src != ' ' && n < 7) out[k][n++] = *src++;
        out[k][n] = '\0';
        while(*src && *src != ' ') src++;
        if(strcmp(out[k], "_") == 0) out[k][0] = '\0';
        else if(strcmp(out[k], "_U") == 0) strcpy(out[k], "_");
        k++;
    }
    for(; k < NKEYS; k++) out[k][0] = '\0';
}

static void load_layout(int idx)
{
    if(idx < 0 || idx >= NLAYOUTS) idx = 0;
    s_layout = idx;
    s_latin = 0;
    s_dead = 0;
    parse_row(s_layouts[idx].n, s_tok[0][0]);
    parse_row(s_layouts[idx].s, s_tok[0][1]);
    parse_row(s_layouts[idx].a, s_tok[0][2]);
    parse_row(US_N, s_tok[1][0]);
    parse_row(US_S, s_tok[1][1]);
    parse_row(NONE, s_tok[1][2]);
}

int input_layout_count(void)
{
    return NLAYOUTS;
}

const char * input_layout_id(int i)
{
    return s_layouts[(i >= 0 && i < NLAYOUTS) ? i : 0].id;
}

const char * input_layout_name(int i)
{
    return s_layouts[(i >= 0 && i < NLAYOUTS) ? i : 0].name;
}

int input_layout_index(const char * id)
{
    if(!id) return -1;
    for(int i = 0; i < NLAYOUTS; i++)
        if(strcmp(s_layouts[i].id, id) == 0) return i;
    return -1;
}

int input_layout(void)
{
    return s_layout;
}

void input_set_layout(int idx)
{
    load_layout(idx);
}

int input_layout_is_cyrillic(void)
{
    return s_layouts[s_layout].cyr;
}

int input_layout_latin_active(void)
{
    return s_latin;
}

/* upper-case form of a single (lower-case) letter token, for AltGr + Shift */
static void upper_token(const char * in, char * out)
{
    uint32_t c = 0;
    const unsigned char * u = (const unsigned char *)in;
    int len = 0;
    if(u[0] < 0x80) { c = u[0]; len = 1; }
    else if((u[0] & 0xE0) == 0xC0) { c = ((u[0] & 0x1F) << 6) | (u[1] & 0x3F); len = 2; }
    if(len == 0 || in[len] != '\0') { strcpy(out, in); return; }
    if(c >= 'a' && c <= 'z') c -= 32;
    else if((c >= 0xE0 && c <= 0xFE && c != 0xF7)) c -= 32;
    else if(c >= 0x100 && c <= 0x17F && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F) {
        if(c >= 0x139 && c <= 0x148) { if(!(c & 1)) c -= 1; }       /* Ĺĺ … Ňň: upper is odd */
        else if(c >= 0x179 && c <= 0x17E) { if(!(c & 1)) c -= 1; }  /* Źź Żż Žž */
        else if(c & 1) c -= 1;                                      /* Āā … : upper is even */
    }
    else if(c == 0x491) c = 0x490;
    if(c < 0x80) { out[0] = (char)c; out[1] = '\0'; }
    else { out[0] = (char)(0xC0 | (c >> 6)); out[1] = (char)(0x80 | (c & 0x3F)); out[2] = '\0'; }
}

/* dead key + letter */
static const char * compose(char dead, const char * base)
{
    static const struct { char d; const char * from; const char * to; } tab[] = {
        {'\'', "aeiouyAEIOUYcnszCNSZ", "á é í ó ú ý Á É Í Ó Ú Ý ć ń ś ź Ć Ń Ś Ź"},
        {'`', "aeiouAEIOU", "à è ì ò ù À È Ì Ò Ù"},
        {'^', "aeiouAEIOU", "â ê î ô û Â Ê Î Ô Û"},
        {'"', "aeiouyAEIOU", "ä ë ï ö ü ÿ Ä Ë Ï Ö Ü"},
        {'~', "anoANO", "ã ñ õ Ã Ñ Õ"},
        {'v', "cdenrstzCDENRSTZ", "č ď ě ň ř š ť ž Č Ď Ě Ň Ř Š Ť Ž"},
    };
    static char out[8];
    if(base[0] == '\0' || base[1] != '\0') return NULL;
    for(size_t i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        if(tab[i].d != dead) continue;
        const char * p = strchr(tab[i].from, base[0]);
        if(!p) return NULL;
        int idx = (int)(p - tab[i].from);
        const char * t = tab[i].to;
        while(idx-- > 0) {
            t = strchr(t, ' ');
            if(!t) return NULL;
            t++;
        }
        int n = 0;
        while(t[n] && t[n] != ' ' && n < 7) { out[n] = t[n]; n++; }
        out[n] = '\0';
        return out;
    }
    return NULL;
}

static const char * dead_char(char d)
{
    switch(d) {
        case '\'': return "´";
        case '`': return "`";
        case '^': return "^";
        case '"': return "¨";
        case '~': return "~";
        default: return "ˇ";
    }
}

static int key_index(unsigned short code)
{
    for(int i = 0; i < NKEYS; i++)
        if(s_keycodes[i] == code) return i;
    return -1;
}

static void kq_push(uint32_t k)
{
    int nw = (s_kq_w + 1) % 64;
    if(nw == s_kq_r) return;
    s_keyq[s_kq_w] = k;
    s_kq_w = nw;
}

static int is_letter_token(const char * t)
{
    unsigned char c = (unsigned char)t[0];
    if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return t[1] == '\0';
    return c >= 0xC3 && c <= 0xD3; /* accented Latin and Cyrillic letters */
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
        case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT:
            if(value == 1 && s_alt && s_layouts[s_layout].cyr) s_latin = !s_latin, s_toggled = 1;
            s_shift = down;
            return;
        case KEY_LEFTALT:
            if(value == 1 && s_shift && s_layouts[s_layout].cyr) s_latin = !s_latin, s_toggled = 1;
            s_alt = down;
            return;
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
    const char * fixed = NULL;
    switch(code) {
        case KEY_SPACE: fixed = " "; break;
        case KEY_KP0: fixed = "0"; break;
        case KEY_KP1: fixed = "1"; break;
        case KEY_KP2: fixed = "2"; break;
        case KEY_KP3: fixed = "3"; break;
        case KEY_KP4: fixed = "4"; break;
        case KEY_KP5: fixed = "5"; break;
        case KEY_KP6: fixed = "6"; break;
        case KEY_KP7: fixed = "7"; break;
        case KEY_KP8: fixed = "8"; break;
        case KEY_KP9: fixed = "9"; break;
        case KEY_KPDOT: fixed = "."; break;
        case KEY_KPMINUS: fixed = "-"; break;
        case KEY_KPPLUS: fixed = "+"; break;
        case KEY_KPASTERISK: fixed = "*"; break;
        case KEY_KPSLASH: fixed = "/"; break;
        case KEY_RO: fixed = s_shift ? "?" : "/"; break; /* extra key on Brazilian keyboards */
        default: break;
    }
    char tok[8];
    if(fixed) str_copy(tok, fixed, sizeof(tok));
    else {
        int ki = key_index(code);
        if(ki < 0) return;
        int set = (s_layouts[s_layout].cyr && s_latin) ? 1 : 0;
        const char * n = s_tok[set][0][ki];
        if(s_altgr) {
            str_copy(tok, s_tok[set][2][ki], sizeof(tok));
            if(s_shift && tok[0] && tok[0] != 'D') {
                char up[8];
                upper_token(tok, up);
                str_copy(tok, up, sizeof(tok));
            }
        }
        else {
            int shift = s_shift;
            if(s_caps && is_letter_token(n)) shift = !shift;
            str_copy(tok, s_tok[set][shift ? 1 : 0][ki], sizeof(tok));
        }
    }
    if(!tok[0]) return;
    /* dead key pressed? ("D" followed by exactly one mark; a plain "D" is the letter) */
    if(tok[0] == 'D' && tok[1] != '\0' && tok[2] == '\0' && strchr("'`^\"~v", tok[1])) {
        if(s_dead == tok[1]) { /* twice = the accent itself */
            insert_text(dead_char(s_dead));
            s_dead = 0;
        }
        else s_dead = tok[1];
        return;
    }
    if(s_dead) {
        char d = s_dead;
        s_dead = 0;
        const char * c = compose(d, tok);
        if(c) {
            insert_text(c);
            return;
        }
        if(strcmp(tok, " ") == 0) {
            insert_text(dead_char(d));
            return;
        }
        insert_text(dead_char(d));
    }
    insert_text(tok);
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

/* for tests: press one key with modifiers */
void input_test_key(int code, int shift, int altgr)
{
    s_shift = shift;
    s_altgr = altgr;
    handle_key((unsigned short)code, 1);
    handle_key((unsigned short)code, 0);
    s_shift = s_altgr = 0;
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
    if(s_tok[0][0][1][0] == '\0') load_layout(s_layout);
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

/* 1 once after Alt+Shift switched between Cyrillic and Latin (the UI updates its hint) */
int input_take_layout_toggle(void)
{
    int t = s_toggled;
    s_toggled = 0;
    return t;
}

int input_take_wheel(void)
{
    int w = s_wheel;
    s_wheel = 0;
    return w;
}
