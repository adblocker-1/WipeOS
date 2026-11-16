/*
 * Storage for customers, technicians, job numbers and company details.
 * Everything is kept as plain text files on the WipeOS USB stick
 * (folder wipeos/) so it survives reboots and can be edited on a PC.
 */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

static char s_dir[400] = "/tmp/wipeos";
static int s_on_usb = 0;

void str_copy(char * dst, const char * src, size_t len)
{
    if(len == 0) return;
    if(!src) src = "";
    strncpy(dst, src, len - 1);
    dst[len - 1] = '\0';
}

void str_trim(char * s)
{
    size_t n = strlen(s);
    while(n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
    size_t i = 0;
    while(s[i] == ' ' || s[i] == '\t') i++;
    if(i) memmove(s, s + i, strlen(s + i) + 1);
}

static void mkdir_p(const char * path)
{
    char tmp[512];
    str_copy(tmp, path, sizeof(tmp));
    for(char * p = tmp + 1; *p; p++) {
        if(*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

void storage_init(void)
{
    const char * base = getenv("SHREDOS_DATA");
    if(base && base[0]) {
        snprintf(s_dir, sizeof(s_dir), "%s/wipeos", base);
        mkdir_p(s_dir);
        if(access(s_dir, W_OK) == 0) {
            s_on_usb = 1;
        }
        else {
            str_copy(s_dir, "/tmp/wipeos", sizeof(s_dir));
        }
    }
    mkdir_p(s_dir);
    storage_load_org();
}

const char * storage_dir(void)
{
    return s_dir;
}

int storage_on_usb(void)
{
    return s_on_usb;
}

void storage_sync(void)
{
    sync();
}

static void path_of(char * out, size_t len, const char * file)
{
    snprintf(out, len, "%s/%s", s_dir, file);
}

/* ---- CSV: fields separated by ';', ';' and line breaks inside values are replaced ---- */

static void csv_clean(char * dst, const char * src, size_t len)
{
    size_t j = 0;
    for(size_t i = 0; src[i] && j + 1 < len; i++) {
        char ch = src[i];
        if(ch == ';' || ch == '\n' || ch == '\r') ch = ',';
        dst[j++] = ch;
    }
    dst[j] = '\0';
}

static int csv_split(char * line, char ** fields, int max)
{
    int n = 0;
    char * p = line;
    while(n < max) {
        fields[n++] = p;
        char * sep = strchr(p, ';');
        if(!sep) break;
        *sep = '\0';
        p = sep + 1;
    }
    for(int i = 0; i < n; i++) str_trim(fields[i]);
    return n;
}

int storage_load_customers(customer_t * list, int max)
{
    char path[512];
    path_of(path, sizeof(path), "customers.csv");
    FILE * fp = fopen(path, "r");
    if(!fp) return 0;
    char line[1024];
    int n = 0;
    while(n < max && fgets(line, sizeof(line), fp)) {
        str_trim(line);
        if(line[0] == '\0' || line[0] == '#') continue;
        if((strncmp(line, "Kunden-Nr", 9) == 0 || strncmp(line, "CustomerNo", 10) == 0)) continue;
        char * f[8] = {0};
        int k = csv_split(line, f, 8);
        if(k < 2) continue;
        customer_t * c = &list[n];
        memset(c, 0, sizeof(*c));
        str_copy(c->no, f[0], sizeof(c->no));
        str_copy(c->name, f[1], sizeof(c->name));
        if(k > 2) str_copy(c->contact, f[2], sizeof(c->contact));
        if(k > 3) str_copy(c->street, f[3], sizeof(c->street));
        if(k > 4) str_copy(c->zip, f[4], sizeof(c->zip));
        if(k > 5) str_copy(c->city, f[5], sizeof(c->city));
        if(k > 6) str_copy(c->phone, f[6], sizeof(c->phone));
        if(k > 7) str_copy(c->ref, f[7], sizeof(c->ref));
        n++;
    }
    fclose(fp);
    return n;
}

int storage_save_customer(const customer_t * c)
{
    char path[512];
    path_of(path, sizeof(path), "customers.csv");
    int exists = access(path, F_OK) == 0;
    FILE * fp = fopen(path, "a");
    if(!fp) return -1;
    if(!exists) fprintf(fp, "CustomerNo;Name;Contact;Street;Postcode;City;Phone/Email;Reference\r\n");
    char a[8][160];
    csv_clean(a[0], c->no, 160);
    csv_clean(a[1], c->name, 160);
    csv_clean(a[2], c->contact, 160);
    csv_clean(a[3], c->street, 160);
    csv_clean(a[4], c->zip, 160);
    csv_clean(a[5], c->city, 160);
    csv_clean(a[6], c->phone, 160);
    csv_clean(a[7], c->ref, 160);
    fprintf(fp, "%s;%s;%s;%s;%s;%s;%s;%s\r\n", a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    fclose(fp);
    storage_sync();
    return 0;
}

void storage_next_customer_no(char * out, size_t len)
{
    static customer_t list[MAX_CUSTOMERS];
    int n = storage_load_customers(list, MAX_CUSTOMERS);
    int max = 1000;
    for(int i = 0; i < n; i++) {
        const char * p = list[i].no;
        while(*p && (*p < '0' || *p > '9')) p++;
        int v = atoi(p);
        if(v > max) max = v;
    }
    snprintf(out, len, "C-%d", max + 1);
}

int storage_load_techs(tech_t * list, int max)
{
    char path[512];
    path_of(path, sizeof(path), "technicians.csv");
    FILE * fp = fopen(path, "r");
    if(!fp) return 0;
    char line[512];
    int n = 0;
    while(n < max && fgets(line, sizeof(line), fp)) {
        str_trim(line);
        if(line[0] == '\0' || line[0] == '#') continue;
        if(strncmp(line, "ID;", 3) == 0) continue;
        char * f[2] = {0};
        int k = csv_split(line, f, 2);
        if(k < 2) continue;
        str_copy(list[n].id, f[0], sizeof(list[n].id));
        str_copy(list[n].name, f[1], sizeof(list[n].name));
        n++;
    }
    fclose(fp);
    return n;
}

int storage_save_tech(const tech_t * t)
{
    char path[512];
    path_of(path, sizeof(path), "technicians.csv");
    int exists = access(path, F_OK) == 0;
    FILE * fp = fopen(path, "a");
    if(!fp) return -1;
    if(!exists) fprintf(fp, "ID;Name\r\n");
    char a[2][128];
    csv_clean(a[0], t->id, 128);
    csv_clean(a[1], t->name, 128);
    fprintf(fp, "%s;%s\r\n", a[0], a[1]);
    fclose(fp);
    storage_sync();
    return 0;
}

void storage_next_order_no(char * out, size_t len)
{
    char path[512];
    path_of(path, sizeof(path), "job_counter.txt");
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    int year = tm.tm_year + 1900;
    int last_year = 0, last_no = 0;
    FILE * fp = fopen(path, "r");
    if(fp) {
        if(fscanf(fp, "%d %d", &last_year, &last_no) != 2) {
            last_year = 0;
            last_no = 0;
        }
        fclose(fp);
    }
    int no = (last_year == year) ? last_no + 1 : 1;
    fp = fopen(path, "w");
    if(fp) {
        fprintf(fp, "%d %d\n", year, no);
        fclose(fp);
        storage_sync();
    }
    snprintf(out, len, "WO-%d-%04d", year, no);
}

void storage_load_org(void)
{
    char path[512];
    path_of(path, sizeof(path), "company.txt");
    g.org_name[0] = '\0';
    g.org_address[0] = '\0';
    FILE * fp = fopen(path, "r");
    if(!fp) return;
    char line[256];
    int n = 0;
    while(fgets(line, sizeof(line), fp)) {
        str_trim(line);
        if(line[0] == '#' || line[0] == '\0') continue;
        if(n == 0) str_copy(g.org_name, line, sizeof(g.org_name));
        else if(n == 1) str_copy(g.org_address, line, sizeof(g.org_address));
        n++;
    }
    fclose(fp);
}

void storage_save_org(void)
{
    char path[512];
    path_of(path, sizeof(path), "company.txt");
    FILE * fp = fopen(path, "w");
    if(!fp) return;
    fprintf(fp, "# Line 1: company name, line 2: address (shown on the erasure report)\n");
    fprintf(fp, "%s\n%s\n", g.org_name, g.org_address);
    fclose(fp);
    storage_sync();
}
