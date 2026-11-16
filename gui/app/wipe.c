/*
 * Startet nwipe ohne eigene Oberfläche und liest den Fortschritt
 * aus der Statusdatei (nwipe-Option --status-file, WipeOS-Patch).
 */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define STATUS_FILE "/tmp/shredos_gui_status"
#define LOG_FILE "/tmp/shredos_gui_nwipe.log"
#define OUT_FILE "/tmp/shredos_gui_nwipe.out"

static pid_t s_pid = 0;
static int s_exit_code = -1;

const char * method_label(method_id_t m)
{
    switch(m) {
        case METHOD_AUTO: return "Automatisch";
        case METHOD_ZERO: return "Schnell";
        case METHOD_DODSHORT: return "DoD 5220.22-M (kurz)";
        case METHOD_GUTMANN: return "Gutmann";
        default: return "?";
    }
}

const char * method_short(method_id_t m)
{
    switch(m) {
        case METHOD_AUTO: return "Zufallsdaten 1× + Prüfung";
        case METHOD_ZERO: return "Nullen 1× + Prüfung";
        case METHOD_DODSHORT: return "DoD kurz 3× + Prüfung";
        case METHOD_GUTMANN: return "Gutmann 35× + Prüfung";
        default: return "?";
    }
}

const char * method_desc(method_id_t m)
{
    switch(m) {
        case METHOD_AUTO: return "Einmal mit Zufallsdaten überschreiben, danach jeden Block prüfen.";
        case METHOD_ZERO: return "Einmal mit Nullen überschreiben und prüfen.";
        case METHOD_DODSHORT: return "Muster, Komplement, Zufallsdaten – danach prüfen.";
        case METHOD_GUTMANN: return "Sehr gründlich, aber extrem langsam. Für moderne Laufwerke nicht nötig.";
        default: return "";
    }
}

const char * method_passes(method_id_t m)
{
    switch(m) {
        case METHOD_AUTO: return "1 Durchgang";
        case METHOD_ZERO: return "1 Durchgang";
        case METHOD_DODSHORT: return "3 Durchgänge";
        case METHOD_GUTMANN: return "35 Durchgänge";
        default: return "";
    }
}

double method_factor(method_id_t m)
{
    /* Schreibdurchgänge + ein Prüfdurchgang (Lesen) */
    switch(m) {
        case METHOD_AUTO: return 2.0;
        case METHOD_ZERO: return 2.0;
        case METHOD_DODSHORT: return 4.0;
        case METHOD_GUTMANN: return 36.0;
        default: return 2.0;
    }
}

long long estimate_seconds(const drive_t * d, method_id_t m)
{
    double mbps;
    if(strcmp(d->bus, "NVMe") == 0) mbps = 900;
    else if(strcmp(d->bus, "USB") == 0) mbps = 25;
    else if(strcmp(d->bus, "MMC") == 0) mbps = 20;
    else if(d->rotational) mbps = 140;
    else mbps = 350;
    return (long long)((double)d->size * method_factor(m) / (mbps * 1e6));
}

int wipe_running(void)
{
    return s_pid > 0;
}

int wipe_start(void)
{
    unlink(STATUS_FILE);
    unlink(LOG_FILE);
    memset(&g.status, 0, sizeof(g.status));

    const char * nwipe = getenv("SHREDOS_NWIPE");
    if(!nwipe) nwipe = "/usr/bin/nwipe";

    char * argv[64];
    int a = 0;
    argv[a++] = (char *)nwipe;
    argv[a++] = "--nogui";
    argv[a++] = "--autonuke";
    switch(g.method) {
        case METHOD_ZERO: argv[a++] = "--method=zero"; break;
        case METHOD_DODSHORT: argv[a++] = "--method=dodshort"; break;
        case METHOD_GUTMANN: argv[a++] = "--method=gutmann"; break;
        default: argv[a++] = "--method=random"; break;
    }
    argv[a++] = "--prng=chacha20";  /* kryptografisch sicher, ohne Hardware-Abhängigkeit */
    argv[a++] = "--verify=last";
    argv[a++] = "--noblank";
    /* nwipes eigene Einzel-PDFs nur in /tmp ablegen; "-P noPDF" würde nwipe.conf dauerhaft ändern */
    if(system("mkdir -p /tmp/nwipe_pdf") != 0) { /* egal */ }
    argv[a++] = "-P";
    argv[a++] = "/tmp/nwipe_pdf";
    argv[a++] = "--logfile=" LOG_FILE;
    argv[a++] = "--status-file=" STATUS_FILE;
    for(int i = 0; i < g.drive_count && a < 62; i++) {
        if(g.drives[i].selected && !g.drives[i].is_boot) argv[a++] = g.drives[i].dev;
    }
    argv[a] = NULL;

    g.wipe_start = time(NULL);
    g.wipe_end = 0;
    s_exit_code = -1;

    pid_t pid = fork();
    if(pid < 0) return -1;
    if(pid == 0) {
        int fd = open(OUT_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if(fd >= 0) {
            dup2(fd, 1);
            dup2(fd, 2);
            close(fd);
        }
        int nul = open("/dev/null", O_RDONLY);
        if(nul >= 0) {
            dup2(nul, 0);
            close(nul);
        }
        setsid();
        execv(nwipe, argv);
        _exit(127);
    }
    s_pid = pid;
    return 0;
}

void wipe_abort(void)
{
    if(s_pid > 0) kill(s_pid, SIGINT);
}

static void parse_status(void)
{
    FILE * fp = fopen(STATUS_FILE, "r");
    if(!fp) return;
    wipe_status_t st;
    memset(&st, 0, sizeof(st));
    char line[512];
    while(fgets(line, sizeof(line), fp)) {
        str_trim(line);
        char * eq = strchr(line, '=');
        if(!eq) continue;
        *eq = '\0';
        const char * k = line;
        const char * v = eq + 1;
        if(strcmp(k, "final") == 0) st.final = atoi(v);
        else if(strcmp(k, "count") == 0) st.count = atoi(v);
        else if(strcmp(k, "version") == 0) str_copy(st.version, v, sizeof(st.version));
        else if(strcmp(k, "method") == 0) str_copy(st.method, v, sizeof(st.method));
        else if(strcmp(k, "prng") == 0) str_copy(st.prng, v, sizeof(st.prng));
        else if(strcmp(k, "verify") == 0) st.verify = atoi(v);
        else if(strcmp(k, "noblank") == 0) st.noblank = atoi(v);
        else if(k[0] == 'd' && k[1] >= '0' && k[1] <= '9') {
            int idx = atoi(k + 1);
            const char * dot = strchr(k, '.');
            if(!dot || idx < 0 || idx >= MAX_DRIVES) continue;
            const char * f = dot + 1;
            wipe_drive_t * d = &st.d[idx];
            if(strcmp(f, "device") == 0) str_copy(d->device, v, sizeof(d->device));
            else if(strcmp(f, "model") == 0) str_copy(d->model, v, sizeof(d->model));
            else if(strcmp(f, "serial") == 0) str_copy(d->serial, v, sizeof(d->serial));
            else if(strcmp(f, "bus") == 0) {
                str_copy(d->bus, v, sizeof(d->bus));
                str_trim(d->bus);
            }
            else if(strcmp(f, "size") == 0) d->size = strtoull(v, NULL, 10);
            else if(strcmp(f, "status") == 0) d->status = atoi(v);
            else if(strcmp(f, "result") == 0) d->result = atoi(v);
            else if(strcmp(f, "percent") == 0) d->percent = atof(v);
            else if(strcmp(f, "pass") == 0) d->pass = atoi(v);
            else if(strcmp(f, "passes") == 0) d->passes = atoi(v);
            else if(strcmp(f, "passtype") == 0) d->passtype = atoi(v);
            else if(strcmp(f, "round") == 0) d->round = atoi(v);
            else if(strcmp(f, "rounds") == 0) d->rounds = atoi(v);
            else if(strcmp(f, "throughput") == 0) d->throughput = strtoull(v, NULL, 10);
            else if(strcmp(f, "eta") == 0) d->eta = atoll(v);
            else if(strcmp(f, "start") == 0) d->start = (time_t)atoll(v);
            else if(strcmp(f, "end") == 0) d->end = (time_t)atoll(v);
            else if(strcmp(f, "bytes_erased") == 0) d->bytes_erased = strtoull(v, NULL, 10);
            else if(strcmp(f, "pass_errors") == 0) d->pass_errors = strtoull(v, NULL, 10);
            else if(strcmp(f, "verify_errors") == 0) d->verify_errors = strtoull(v, NULL, 10);
            else if(strcmp(f, "fsync_errors") == 0) d->fsync_errors = strtoull(v, NULL, 10);
            else if(strcmp(f, "hpa_status") == 0) d->hpa_status = atoi(v);
        }
    }
    fclose(fp);
    st.valid = 1;
    g.status = st;
}

int wipe_poll(void)
{
    parse_status();
    if(s_pid > 0) {
        int stat;
        pid_t r = waitpid(s_pid, &stat, WNOHANG);
        if(r == s_pid) {
            s_exit_code = WIFEXITED(stat) ? WEXITSTATUS(stat) : -1;
            s_pid = 0;
            g.wipe_end = time(NULL);
            parse_status();
            /* Protokoll neben die Berichte kopieren */
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "mkdir -p '%s/../berichte' && cp " LOG_FILE " '%s/../berichte/%s_nwipe.log' 2>/dev/null",
                     storage_dir(), storage_dir(), g.order_no);
            if(system(cmd) != 0) { /* nicht kritisch */ }
            return 0;
        }
        return 1;
    }
    return 0;
}
