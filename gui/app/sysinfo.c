/*
 * System information: device serial number etc. from BIOS/DMI and
 * the list of drives from /sys/block.
 */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <linux/hdreg.h>

static void read_line(const char * path, char * out, size_t len)
{
    out[0] = '\0';
    FILE * fp = fopen(path, "r");
    if(!fp) return;
    if(fgets(out, (int)len, fp) == NULL) out[0] = '\0';
    fclose(fp);
    str_trim(out);
}

static int is_placeholder(const char * s)
{
    static const char * bad[] = {"To be filled by O.E.M.", "To Be Filled By O.E.M.", "Default string", "System Serial Number",
                                 "Not Specified", "Not Applicable", "None", "0", "0123456789", "Chassis Serial Number", NULL
                                };
    if(s[0] == '\0') return 1;
    for(int i = 0; bad[i]; i++) if(strcasecmp(s, bad[i]) == 0) return 1;
    return 0;
}

static const char * chassis_name(int t)
{
    switch(t) {
        case 3: case 4: case 5: case 6: case 7: case 15: case 16: case 35: case 36: return "Desktop";
        case 8: case 9: case 10: case 14: case 31: case 32: return "Laptop";
        case 11: return "Handheld";
        case 13: return "All-in-one";
        case 17: case 23: case 28: case 29: return "Server";
        case 30: return "Tablet";
        case 34: return "Embedded PC";
        default: return "Computer";
    }
}

void sysinfo_read_dmi(dmi_t * d)
{
    memset(d, 0, sizeof(*d));
    const char * base = getenv("SHREDOS_DMI_DIR");
    if(!base) base = "/sys/class/dmi/id";
    char p[256], ct[32];
#define RD(field, file) do { snprintf(p, sizeof(p), "%s/%s", base, file); read_line(p, d->field, sizeof(d->field)); } while(0)
    RD(vendor, "sys_vendor");
    RD(product, "product_name");
    RD(version, "product_version");
    RD(serial, "product_serial");
    RD(uuid, "product_uuid");
    RD(bios, "bios_version");
    RD(bios_date, "bios_date");
#undef RD
    /* BIOS date MM/DD/YYYY -> YYYY-MM-DD */
    {
        int mm, dd, yy;
        if(sscanf(d->bios_date, "%d/%d/%d", &mm, &dd, &yy) == 3) snprintf(d->bios_date, sizeof(d->bios_date), "%04d-%02d-%02d", yy, mm, dd);
    }
    snprintf(p, sizeof(p), "%s/chassis_type", base);
    read_line(p, ct, sizeof(ct));
    str_copy(d->chassis, chassis_name(atoi(ct)), sizeof(d->chassis));

    /* Lenovo puts the model name in product_version and the type number in product_name */
    if(strncasecmp(d->vendor, "LENOVO", 6) == 0 && d->version[0] && !is_placeholder(d->version)) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp), "%s (%s)", d->version, d->product);
        str_copy(d->product, tmp, sizeof(d->product));
    }
    if(is_placeholder(d->serial)) {
        /* fallback: mainboard or chassis serial number */
        char alt[96];
        snprintf(p, sizeof(p), "%s/board_serial", base);
        read_line(p, alt, sizeof(alt));
        if(is_placeholder(alt)) {
            snprintf(p, sizeof(p), "%s/chassis_serial", base);
            read_line(p, alt, sizeof(alt));
        }
        if(!is_placeholder(alt)) str_copy(d->serial, alt, sizeof(d->serial));
        else str_copy(d->serial, "none", sizeof(d->serial));
    }
    if(d->vendor[0] == '\0') str_copy(d->vendor, "unknown", sizeof(d->vendor));
    if(d->product[0] == '\0') str_copy(d->product, "unknown", sizeof(d->product));
}

static int skip_block(const char * n)
{
    static const char * pre[] = {"loop", "ram", "zram", "sr", "fd", "dm-", "md", "nbd", "mtdblock", NULL};
    for(int i = 0; pre[i]; i++) if(strncmp(n, pre[i], strlen(pre[i])) == 0) return 1;
    /* MMC boot areas */
    if(strstr(n, "boot") || strstr(n, "rpmb")) return 1;
    return 0;
}

static void serial_from_vpd(const char * name, char * out, size_t len)
{
    char p[256];
    snprintf(p, sizeof(p), "/sys/block/%s/device/vpd_pg80", name);
    FILE * fp = fopen(p, "rb");
    if(!fp) return;
    unsigned char buf[256];
    size_t n = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);
    if(n <= 4) return;
    size_t l = buf[3];
    if(l > n - 4) l = n - 4;
    char s[128];
    if(l >= sizeof(s)) l = sizeof(s) - 1;
    memcpy(s, buf + 4, l);
    s[l] = '\0';
    str_trim(s);
    if(s[0]) str_copy(out, s, len);
}

static void serial_from_ata(const char * dev, char * out, size_t len, char * fw, size_t fwlen, char * model, size_t mlen)
{
    int fd = open(dev, O_RDONLY | O_NONBLOCK);
    if(fd < 0) return;
    struct hd_driveid id;
    if(ioctl(fd, HDIO_GET_IDENTITY, &id) == 0) {
        char s[24];
        memcpy(s, id.serial_no, 20);
        s[20] = '\0';
        str_trim(s);
        if(s[0]) str_copy(out, s, len);
        char f[12];
        memcpy(f, id.fw_rev, 8);
        f[8] = '\0';
        str_trim(f);
        if(f[0] && fw[0] == '\0') str_copy(fw, f, fwlen);
        /* full model name (sysfs only has 16 characters for SATA) */
        char m[48];
        memcpy(m, id.model, 40);
        m[40] = '\0';
        str_trim(m);
        if(m[0] && model && strlen(m) > strlen(model)) str_copy(model, m, mlen);
    }
    close(fd);
}

static void boot_device(char * out, size_t len)
{
    out[0] = '\0';
    /* 1. partition the data folder (WipeOS stick) is mounted from */
    const char * data = getenv("SHREDOS_DATA");
    if(data) {
        FILE * fp = fopen("/proc/mounts", "r");
        if(fp) {
            char dev[128], mnt[256];
            while(fscanf(fp, "%127s %255s %*s %*s %*d %*d", dev, mnt) == 2) {
                if(strcmp(mnt, data) == 0) {
                    str_copy(out, dev, len);
                    break;
                }
            }
            fclose(fp);
        }
    }
    if(out[0]) return;
    /* 2. device found by find_shredos_boot_disc.sh */
    read_line("/boot_device.txt", out, len);
}

int sysinfo_scan_drives(drive_t * list, int max)
{
    char boot[128];
    boot_device(boot, sizeof(boot));

    DIR * dir = opendir("/sys/block");
    if(!dir) return 0;
    struct dirent * e;
    int n = 0;
    while((e = readdir(dir)) != NULL && n < max) {
        if(e->d_name[0] == '.') continue;
        const char * only = getenv("SHREDOS_ONLY_DEVS"); /* tests only */
        if(only) {
            char pat[64];
            snprintf(pat, sizeof(pat), ",%s,", e->d_name);
            char lst[512];
            snprintf(lst, sizeof(lst), ",%s,", only);
            if(!strstr(lst, pat)) continue;
        }
        else if(skip_block(e->d_name)) continue;
        char p[512], buf[256];
        drive_t * d = &list[n];
        memset(d, 0, sizeof(*d));
        str_copy(d->name, e->d_name, sizeof(d->name));
        snprintf(d->dev, sizeof(d->dev), "/dev/%s", e->d_name);

        snprintf(p, sizeof(p), "/sys/block/%s/size", e->d_name);
        read_line(p, buf, sizeof(buf));
        d->size = strtoull(buf, NULL, 10) * 512ULL;
        if(d->size == 0) continue;

        snprintf(p, sizeof(p), "/sys/block/%s/device/model", e->d_name);
        read_line(p, d->model, sizeof(d->model));
        snprintf(p, sizeof(p), "/sys/block/%s/device/vendor", e->d_name);
        read_line(p, buf, sizeof(buf));
        if(buf[0] && strcmp(buf, "ATA") != 0 && strncmp(d->model, buf, strlen(buf)) != 0) {
            char m[160];
            snprintf(m, sizeof(m), "%s %s", buf, d->model);
            str_copy(d->model, m, sizeof(d->model));
        }
        if(d->model[0] == '\0') str_copy(d->model, "Unknown drive", sizeof(d->model));

        snprintf(p, sizeof(p), "/sys/block/%s/device/serial", e->d_name);
        read_line(p, d->serial, sizeof(d->serial));
        snprintf(p, sizeof(p), "/sys/block/%s/device/firmware_rev", e->d_name);
        read_line(p, d->fw, sizeof(d->fw));
        if(d->fw[0] == '\0') {
            snprintf(p, sizeof(p), "/sys/block/%s/device/rev", e->d_name);
            read_line(p, d->fw, sizeof(d->fw));
        }
        if(d->serial[0] == '\0') serial_from_vpd(e->d_name, d->serial, sizeof(d->serial));
        {
            char ser[96] = "";
            serial_from_ata(d->dev, ser, sizeof(ser), d->fw, sizeof(d->fw), d->model, sizeof(d->model));
            if(d->serial[0] == '\0') str_copy(d->serial, ser, sizeof(d->serial));
        }

        snprintf(p, sizeof(p), "/sys/block/%s/queue/rotational", e->d_name);
        read_line(p, buf, sizeof(buf));
        d->rotational = atoi(buf);
        snprintf(p, sizeof(p), "/sys/block/%s/removable", e->d_name);
        read_line(p, buf, sizeof(buf));
        d->removable = atoi(buf);

        char real[PATH_MAX];
        snprintf(p, sizeof(p), "/sys/block/%s", e->d_name);
        if(realpath(p, real) == NULL) real[0] = '\0';
        if(strncmp(e->d_name, "nvme", 4) == 0) str_copy(d->bus, "NVMe", sizeof(d->bus));
        else if(strstr(real, "/usb")) str_copy(d->bus, "USB", sizeof(d->bus));
        else if(strncmp(e->d_name, "mmcblk", 6) == 0) str_copy(d->bus, "MMC", sizeof(d->bus));
        else if(strstr(real, "/ata")) str_copy(d->bus, "SATA", sizeof(d->bus));
        else if(strstr(real, "virtio") || strncmp(e->d_name, "vd", 2) == 0) str_copy(d->bus, "VIRT", sizeof(d->bus));
        else str_copy(d->bus, "SAS/SCSI", sizeof(d->bus));

        if(boot[0] && strncmp(boot, d->dev, strlen(d->dev)) == 0) {
            /* /dev/sdb or partition /dev/sdb1, /dev/nvme0n1p1 */
            const char * rest = boot + strlen(d->dev);
            if(*rest == 'p') rest++;
            while(*rest >= '0' && *rest <= '9') rest++;
            if(*rest == '\0') d->is_boot = 1;
        }
        n++;
    }
    closedir(dir);

    /* sort: internal before USB, then by name */
    for(int i = 0; i < n; i++) {
        for(int j = i + 1; j < n; j++) {
            int ki = (strcmp(list[i].bus, "USB") == 0) * 2 + list[i].is_boot * 4;
            int kj = (strcmp(list[j].bus, "USB") == 0) * 2 + list[j].is_boot * 4;
            if(kj < ki || (kj == ki && strcmp(list[j].name, list[i].name) < 0)) {
                drive_t t = list[i];
                list[i] = list[j];
                list[j] = t;
            }
        }
    }
    return n;
}

void format_size(uint64_t bytes, char * out, size_t len)
{
    double v = (double)bytes;
    const char * unit = "B";
    if(v >= 1e12) {
        v /= 1e12;
        unit = "TB";
    }
    else if(v >= 1e9) {
        v /= 1e9;
        unit = "GB";
    }
    else if(v >= 1e6) {
        v /= 1e6;
        unit = "MB";
    }
    char tmp[32];
    if(v >= 100 || strcmp(unit, "B") == 0) snprintf(tmp, sizeof(tmp), "%.0f %s", v, unit);
    else snprintf(tmp, sizeof(tmp), "%.1f %s", v, unit);
    str_copy(out, tmp, len);
}

void format_duration(long long s, char * out, size_t len)
{
    if(s < 0) {
        str_copy(out, "–", len);
        return;
    }
    long long d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
    if(d > 0) snprintf(out, len, "%lld d %lld h", d, h);
    else if(h > 0) snprintf(out, len, "%lld h %lld min", h, m);
    else if(m > 0) snprintf(out, len, "%lld min", m);
    else snprintf(out, len, "%lld s", s);
}
