/*
 * WipeOS GUI - grafische Oberfläche für nwipe
 * Gemeinsame Datentypen und Funktionen.
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
    int is_boot;       /* WipeOS-Startmedium: niemals löschen */
    int selected;
} drive_t;

/* Ergebnis/Fortschritt eines Laufwerks aus der nwipe-Statusdatei */
typedef struct {
    char device[48];
    char model[128];
    char serial[96];
    char bus[16];
    uint64_t size;
    int status;        /* 1 = läuft, 0 = fertig, -1 = wartet */
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
    METHOD_AUTO = 0,   /* Zufallsdaten 1x + Prüfung */
    METHOD_ZERO,       /* Nullen 1x + Prüfung */
    METHOD_DODSHORT,   /* DoD 5220.22-M kurz */
    METHOD_GUTMANN,    /* Gutmann 35x */
    METHOD_COUNT
} method_id_t;

typedef struct {
    /* Firma (Durchführende Organisation) */
    char org_name[128];
    char org_address[160];

    /* Auftrag */
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

    /* Ergebnis */
    wipe_status_t status;
    time_t wipe_start, wipe_end;
    char report_path[512];
    char report_error[256];
} app_state_t;

extern app_state_t g;

/* storage.c */
void storage_init(void);
const char * storage_dir(void);      /* Datenordner (auf dem USB-Stick, sonst /tmp) */
int storage_on_usb(void);
int storage_load_customers(customer_t * list, int max);
int storage_save_customer(const customer_t * c);    /* neu anhängen */
void storage_next_customer_no(char * out, size_t len);
int storage_load_techs(tech_t * list, int max);
int storage_save_tech(const tech_t * t);
void storage_next_order_no(char * out, size_t len); /* reserviert die Nummer */
void storage_load_org(void);
void storage_save_org(void);
void storage_sync(void);

/* sysinfo.c */
void sysinfo_read_dmi(dmi_t * d);
int sysinfo_scan_drives(drive_t * list, int max);
void format_size(uint64_t bytes, char * out, size_t len);
void format_duration(long long secs, char * out, size_t len);

/* wipe.c */
int wipe_start(void);
int wipe_poll(void);         /* liest Statusdatei, 1 = nwipe läuft noch */
void wipe_abort(void);
int wipe_running(void);
const char * method_label(method_id_t m);
const char * method_short(method_id_t m);
const char * method_desc(method_id_t m);
const char * method_passes(method_id_t m);
double method_factor(method_id_t m);  /* Datenmenge relativ zur Laufwerksgröße */
long long estimate_seconds(const drive_t * d, method_id_t m);

/* report.c */
int report_create(char * out_path, size_t len, char * err, size_t errlen);

/* input.c */
void input_init(lv_display_t * disp);
void input_poll_hotplug(void);
int input_has_keyboard(void);

/* main.c */
void app_quit(int code);  /* 10 = klassisches nwipe, 20 = ausschalten, 21 = Neustart */

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

/* Hilfen */
void str_copy(char * dst, const char * src, size_t len);
void str_trim(char * s);

#endif
