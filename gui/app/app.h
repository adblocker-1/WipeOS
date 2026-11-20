/*
 * WipeOS GUI - graphical front end for nwipe
 * Shared types and functions.
 */
#ifndef SHREDOS_GUI_APP_H
#define SHREDOS_GUI_APP_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "lvgl.h"

#define MAX_DRIVES 32
#define MAX_CUSTOMERS 2000
#define MAX_TECHS 64

typedef struct {
    char no[16];
    char name[128];
    char contact[96];
    char street[128];
    char zip[16];
    char city[96];
    char phone[96];
    char ref[96];
} customer_t;

typedef struct {
    char id[16];
    char name[96];
} tech_t;

typedef struct {
    char vendor[96];
    char product[128];
    char version[96];
    char serial[96];
    char uuid[64];
    char bios[96];
    char bios_date[32];
    char chassis[48];
} dmi_t;

typedef struct {
    char dev[48];      /* /dev/sda */
    char name[32];     /* sda */
    char model[128];
    char serial[96];
    char fw[48];
    char bus[16];      /* NVMe, SATA, USB, MMC, SAS, VIRT */
    uint64_t size;
    int rotational;
    int removable;
    int is_boot;       /* WipeOS boot drive: never erase */
    int selected;
} drive_t;

/* result/progress of one drive from the nwipe status file */
typedef struct {
    char device[48];
    char model[128];
    char serial[96];
    char bus[16];
    uint64_t size;
    int status;        /* 1 = running, 0 = done, -1 = waiting */
    int result;        /* 0 = ok */
    double percent;
    int pass, passes, passtype, round, rounds;
    uint64_t throughput;
    long long eta;
    time_t start, end;
    uint64_t bytes_erased;
    uint64_t pass_errors, verify_errors, fsync_errors;
    int hpa_status;
} wipe_drive_t;

typedef struct {
    int valid;
    int final;
    int count;
    char version[32];
    char method[64];
    char prng[64];
    int verify;
    int noblank;
    wipe_drive_t d[MAX_DRIVES];
} wipe_status_t;

typedef enum {
    METHOD_PRNG = 0,   /* PRNG Stream (recommended) */
    METHOD_ZERO,       /* Fill With Zeros */
    METHOD_ONE,        /* Fill With Ones */
    METHOD_OPS2,       /* RCMP TSSIT OPS-II */
    METHOD_DODSHORT,   /* DoD 5220.22-M (Short) */
    METHOD_DODFULL,    /* DoD 5220.22-M (Full) */
    METHOD_GUTMANN,    /* Gutmann Wipe */
    METHOD_IS5ENH,     /* HMG IS5 Enhanced */
    METHOD_SCHNEIER,   /* Bruce Schneier 7-pass */
    METHOD_BMB,        /* BMB21-2019 */
    METHOD_VERIFY_ZERO, /* read only: check for zeros */
    METHOD_VERIFY_ONE, /* read only: check for ones */
    METHOD_COUNT
} method_id_t;

#define PRNG_COUNT 8

/* languages (i18n.c) */
typedef enum { LANG_EN, LANG_DE, LANG_FR, LANG_ES, LANG_IT, LANG_PT, LANG_NL, LANG_PL, LANG_TR, LANG_RU, LANG_UK, LANG_COUNT } lang_t;
typedef struct {
    const char * en;
    const char * tr;
} tr_pair_t;
extern lang_t g_lang;   /* interface language */
extern lang_t g_rlang;  /* report language */
const char * tr_in(lang_t l, const char * en);
#define _(s) tr_in(g_lang, s)   /* interface text */
#define R_(s) tr_in(g_rlang, s) /* report text */
#define N_(s) s                 /* marked for translation, translated where it is used */
const char * lang_code(lang_t l);
const char * lang_name(lang_t l);
const char * lang_default_kbd(lang_t l);
int lang_from_code(const char * code);
const tr_pair_t * lang_table(lang_t l);
const char * symt(const char * sym, const char * text);
const char * tsym(const char * text, const char * sym);

/* company logo limits */
#define LOGO_NAME_LEN 64
#define LOGO_MAX_BYTES (5 * 1024 * 1024)
#define LOGO_MAX_PX 4000
#define LOGO_MAX_FILES 12

typedef struct {
    /* company performing the erasure */
    char org_name[128];
    char org_address[160];
    char org_contact[128];  /* phone / email */
    char org_logo[LOGO_NAME_LEN]; /* file in the wipeos/ folder, "" = auto, "-" = none */

    /* job */
    char order_no[32];
    customer_t customer;
    int customer_is_new;
    tech_t tech;
    char sysname[128];
    char asset[64];
    char note[160];

    dmi_t dmi;

    drive_t drives[MAX_DRIVES];
    int drive_count;

    method_id_t method;
    int prng;          /* index into prng_label() */
    int verify;        /* 0 = off, 1 = last pass, 2 = all passes */
    int rounds;        /* 1..5 */
    int blank;         /* final blanking pass with zeros */

    /* result */
    wipe_status_t status;
    time_t wipe_start, wipe_end;
    char report_path[512];
    char report_error[256];
} app_state_t;

extern app_state_t g;

/* storage.c */
void storage_init(void);
const char * storage_dir(void);        /* config folder (wipeos/config on the USB stick, otherwise /tmp) */
const char * storage_logo_dir(void);   /* wipeos/logos */
const char * storage_report_dir(void); /* wipeos/reports */
int storage_on_usb(void);
int storage_load_customers(customer_t * list, int max);
int storage_save_customer(const customer_t * c);    /* append */
void storage_next_customer_no(char * out, size_t len);
int storage_load_techs(tech_t * list, int max);
int storage_save_tech(const tech_t * t);
void storage_next_order_no(char * out, size_t len); /* reserves the number */
void storage_load_org(void);
void storage_save_org(void);
void storage_sync(void);
int storage_get_scale(void);        /* saved zoom, 0 = automatic */
void storage_set_scale(int sc);
const char * storage_get_setting(const char * key); /* "language", "keyboard", "report_language" */
void storage_set_locale(const char * lang, const char * kbd, const char * rlang);
void locale_init(void);       /* applies saved settings / boot parameters */
extern int g_locale_chosen;   /* 0 = first start: ask for the language */
extern int g_rlang_same;      /* report language follows the interface language */
int storage_list_images(char names[][LOGO_NAME_LEN], int max); /* PNG/JPG files in the data folder */
int storage_logo_path(char * out, size_t len);                /* 0 = company logo file exists */

/* image.c – company logo (PNG/JPG) */
typedef struct {
    uint8_t * rgba;
    int w, h;
} img_t;
int img_check_file(const char * path, int * w, int * h, char * err, size_t errlen);
int img_load(const char * path, int max_w, int max_h, img_t * out, char * err, size_t errlen);
int img_wipeos_logo(int large, img_t * out);
uint8_t * img_to_rgb_on_white(const img_t * im);
uint8_t * img_to_jpeg(const img_t * im, int quality, size_t * len);
void img_free(img_t * im);

/* sysinfo.c */
void sysinfo_read_dmi(dmi_t * d);
int sysinfo_scan_drives(drive_t * list, int max);
void format_size(uint64_t bytes, char * out, size_t len);
void format_duration(long long secs, char * out, size_t len);

/* wipe.c */
int wipe_start(void);
int wipe_poll(void);         /* reads the status file, 1 = nwipe still running */
void wipe_abort(void);
int wipe_running(void);
const char * method_label(method_id_t m);
const char * method_short(method_id_t m);
const char * method_desc(method_id_t m);
const char * method_passes(method_id_t m);
double method_factor(method_id_t m);  /* data volume relative to drive size */
int method_is_verify_only(method_id_t m);
int method_pass_count(method_id_t m);
const char * prng_label(int i);
void method_summary(char * out, size_t len);  /* e.g. "PRNG Stream · 1 round · verify last pass" */
long long estimate_seconds(const drive_t * d, method_id_t m);

/* report.c */
int report_create(char * out_path, size_t len, char * err, size_t errlen);

/* input.c */
void input_init(lv_display_t * disp);
void input_poll_hotplug(void);
int input_has_keyboard(void);
int input_layout_count(void);
const char * input_layout_id(int i);    /* "us", "de", ... */
const char * input_layout_name(int i);
int input_layout_index(const char * id); /* -1 = unknown */
int input_layout(void);
void input_set_layout(int idx);
int input_layout_is_cyrillic(void);
int input_layout_latin_active(void);
int input_take_layout_toggle(void);
int input_take_wheel(void);

/* main.c */
void app_quit(int code);  /* 10 = classic nwipe, 20 = power off, 21 = reboot, 30 = restart interface */
int display_scale(void);            /* 1 = 100 %, 2 = 200 %, ... */
int display_scale_ok(int sc);
int display_scale_selectable(void); /* 0 when fixed by boot parameter or the screen is too small */

/* ui.c */
void ui_init(void);
void ui_tick(void);

/* fonts */
LV_FONT_DECLARE(f_sans_r14)
LV_FONT_DECLARE(f_sans_r16)
LV_FONT_DECLARE(f_sans_sb14)
LV_FONT_DECLARE(f_sans_sb16)
LV_FONT_DECLARE(f_sans_sb18)
LV_FONT_DECLARE(f_sans_b24)
LV_FONT_DECLARE(f_sans_b30)
LV_FONT_DECLARE(f_mono_m14)
LV_FONT_DECLARE(f_mono_m24)

/* helpers */
void str_copy(char * dst, const char * src, size_t len);
void str_trim(char * s);

#endif
