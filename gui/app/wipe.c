/*
 * Runs nwipe without its own UI and reads progress from the
 * status file (nwipe option --status-file, WipeOS patch).
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

typedef struct {
    const char * label;
    const char * cli;
    int passes;        /* write passes per round, 0 = read only */
    const char * desc;
} method_def_t;

static const method_def_t s_methods[METHOD_COUNT] = {
    {"PRNG Stream", "random", 1, "Fills the device with a stream from the random number generator."},
    {"Fill With Zeros", "zero", 1, "Fills the device with zeros (0x00)."},
    {"Fill With Ones", "one", 1, "Fills the device with ones (0xFF)."},
    {"RCMP TSSIT OPS-II", "ops2", 8, "Royal Canadian Mounted Police Technical Security Standard, OPS-II."},
    {"DoD 5220.22-M (Short)", "dodshort", 3, "U.S. Department of Defense 5220.22-M short 3-pass wipe (passes 1, 2 & 7)."},
    {"DoD 5220.22-M (Full)", "dod522022m", 7, "U.S. Department of Defense 5220.22-M full 7-pass wipe."},
    {"Gutmann Wipe", "gutmann", 35, "Peter Gutmann's 35-pass method. Very slow, not needed for modern drives."},
    {"HMG IS5 Enhanced", "is5enh", 3, "UK HMG IS5 (Enhanced) sanitisation method for protectively marked information."},
    {"Schneier Wipe", "bruce7", 7, "Bruce Schneier's 7-pass mixed-pattern algorithm."},
    {"BMB21-2019", "bmb", 6, "Chinese State Secrets Bureau BMB21-2019 technical requirement for data sanitisation."},
    {"Verify Zeros", "verify_zero", 0, "Only reads the device and checks that it is filled with zeros (0x00)."},
    {"Verify Ones", "verify_one", 0, "Only reads the device and checks that it is filled with ones (0xFF)."},
};

typedef struct {
    const char * label;
    const char * cli;
} prng_def_t;

static const prng_def_t s_prngs[PRNG_COUNT] = {
    {"ChaCha20 (CSPRNG)", "chacha20"},
    {"AES-256-CTR (CSPRNG, needs AES-NI)", "aes_ctr_prng"},
    {"ISAAC-64 (CSPRNG)", "isaac64"},
    {"ISAAC (CSPRNG)", "isaac"},
    {"XORoshiro-256", "xoroshiro256_prng"},
    {"SplitMix64", "splitmix64"},
    {"Lagged Fibonacci", "add_lagg_fibonacci_prng"},
    {"Mersenne Twister", "twister"},
};

const char * method_label(method_id_t m)
{
    return (m >= 0 && m < METHOD_COUNT) ? s_methods[m].label : "?";
}

const char * method_desc(method_id_t m)
{
    return (m >= 0 && m < METHOD_COUNT) ? s_methods[m].desc : "";
}

int method_pass_count(method_id_t m)
{
    return (m >= 0 && m < METHOD_COUNT) ? s_methods[m].passes : 1;
}

int method_is_verify_only(method_id_t m)
{
    return method_pass_count(m) == 0;
}

const char * method_passes(method_id_t m)
{
    static char buf[32];
    int p = method_pass_count(m);
    if(p == 0) return "read only";
    snprintf(buf, sizeof(buf), "%d pass%s", p, p == 1 ? "" : "es");
    return buf;
}

const char * prng_label(int i)
{
    return (i >= 0 && i < PRNG_COUNT) ? s_prngs[i].label : "?";
}

/* nwipe forces "no blanking" for OPS-II and the verify methods */
static int effective_blank(void)
{
    return g.blank && g.method != METHOD_OPS2 && !method_is_verify_only(g.method);
}

void method_summary(char * out, size_t len)
{
    if(method_is_verify_only(g.method)) {
        snprintf(out, len, "%s (read only)", method_label(g.method));
        return;
    }
    const char * v = g.verify == 2 ? "verify all passes" : (g.verify == 1 ? "verify last pass" : "no verification");
    snprintf(out, len, "%s · %d round%s%s · %s", method_label(g.method), g.rounds, g.rounds == 1 ? "" : "s",
             effective_blank() ? " + zero blanking" : "", v);
}

const char * method_short(method_id_t m)
{
    (void)m;
    static char buf[160];
    method_summary(buf, sizeof(buf));
    return buf;
}

double method_factor(method_id_t m)
{
    int p = method_pass_count(m);
    if(p == 0) return 1.0;
    double writes = (double)p * (g.rounds > 0 ? g.rounds : 1) + (effective_blank() ? 1 : 0);
    double reads = g.verify == 2 ? writes : (g.verify == 1 ? 1 : 0);
    return writes + reads;
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
    static char m_arg[64], p_arg[64], r_arg[32];
    snprintf(m_arg, sizeof(m_arg), "--method=%s", s_methods[g.method].cli);
    argv[a++] = m_arg;
    if(!method_is_verify_only(g.method)) {
        snprintf(p_arg, sizeof(p_arg), "--prng=%s", s_prngs[g.prng].cli);
        argv[a++] = p_arg;
        argv[a++] = g.verify == 2 ? "--verify=all" : (g.verify == 1 ? "--verify=last" : "--verify=off");
        snprintf(r_arg, sizeof(r_arg), "--rounds=%d", g.rounds > 0 ? g.rounds : 1);
        argv[a++] = r_arg;
        if(!g.blank) argv[a++] = "--noblank";
    }
    /* keep nwipe's own per-drive PDFs in /tmp; "-P noPDF" would change nwipe.conf permanently */
    if(system("mkdir -p /tmp/nwipe_pdf") != 0) { /* ignore */ }
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
            /* copy the log next to the reports */
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "mkdir -p '%s/../reports' && cp " LOG_FILE " '%s/../reports/%s_nwipe.log' 2>/dev/null",
                     storage_dir(), storage_dir(), g.order_no);
            if(system(cmd) != 0) { /* not critical */ }
            return 0;
        }
        return 1;
    }
    return 0;
}
