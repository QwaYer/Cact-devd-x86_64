/*
 * devd — device daemon for CactOS (a simplified analog of udev/device daemon).
 *
 * Periodically enumerates the devfs root (/dev) and writes device node
 * appearance and disappearance events to a text log. Watches the whole node
 * tree registered by built-in drivers and loaded .cctk modules.
 *
 * Started by the cgoct supervisor as /sbin/devd (see Cgoct-x86_32).
 *
 * /etc/devd.conf (all keys optional; created on first start):
 *   file=/var/log/devd.log  — event log
 *   interval=5              — /dev poll period (sec)
 *   console=0               — duplicate events to the console
 *   verbose=1               — on start print the full list of nodes
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

#define DEV_PATH    "/dev"
#define CONFIG_PATH "/etc/devd.conf"
#define MAX_DEV     96
#define MAX_NAME    64
#define LOG_DEFAULT "/var/log/devd.log"

static char log_path[128] = LOG_DEFAULT;
static int  interval_sec  = 5;
static int  console_on    = 0;
static int  verbose       = 0;
static int  out_fd        = -1;

struct snapshot {
    int  n;
    char name[MAX_DEV][MAX_NAME];
};

static struct snapshot prev_snap;

/* Default config: written on first start if the file does not exist yet. */
static const char default_config[] =
    "# devd config - auto-generated on first start.\n"
    "#\n"
    "# file     - event log\n"
    "# interval - /dev poll period (sec)\n"
    "# console  - duplicate to /dev/console (0|1)\n"
    "# verbose  - print the node list on start (0|1)\n"
    "\n"
    "file=/var/log/devd.log\n"
    "interval=5\n"
    "console=0\n"
    "verbose=0\n";

static void ensure_dir(const char *path) {
    (void)mkdir(path, 0755);
}

static void config_write_default(void) {
    int fd = open(CONFIG_PATH, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) return;
    write(fd, default_config, sizeof(default_config) - 1);
    close(fd);
}

static void config_load(void) {
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) {
        config_write_default();
        f = fopen(CONFIG_PATH, "r");
        if (!f) return;
    }
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;
        char *eq = p;
        while (*eq && *eq != '=' && *eq != '\n') eq++;
        if (*eq != '=') continue;
        *eq = '\0';
        char *key = p;
        char *val = eq + 1;
        int vlen = (int)strlen(val);
        while (vlen > 0 && (val[vlen - 1] == '\n' || val[vlen - 1] == '\r' ||
                            val[vlen - 1] == ' ' || val[vlen - 1] == '\t'))
            val[--vlen] = '\0';
        if (strcmp(key, "file") == 0) {
            strncpy(log_path, val, sizeof(log_path) - 1);
            log_path[sizeof(log_path) - 1] = '\0';
        } else if (strcmp(key, "interval") == 0) {
            int v = atoi(val);
            if (v >= 1 && v <= 3600) interval_sec = v;
        } else if (strcmp(key, "console") == 0) {
            console_on = (val[0] == '1' || val[0] == 'y' || val[0] == 'Y');
        } else if (strcmp(key, "verbose") == 0) {
            verbose = (val[0] == '1' || val[0] == 'y' || val[0] == 'Y');
        }
    }
    fclose(f);
}

static void log_event(const char *msg) {
    if (out_fd >= 0) {
        write(out_fd, msg, strlen(msg));
    }
    if (console_on) {
        int cfd = open("/dev/console", O_WRONLY);
        if (cfd >= 0) {
            write(cfd, msg, strlen(msg));
            close(cfd);
        }
    }
}

static int name_in(const struct snapshot *s, const char *name) {
    int i;
    for (i = 0; i < s->n; i++) {
        if (strcmp(s->name[i], name) == 0) return 1;
    }
    return 0;
}

/* Enumerate the devfs root into snapshot (at most MAX_DEV names). */
static void scan_devfs(struct snapshot *s) {
    s->n = 0;
    int fd = open(DEV_PATH, O_RDONLY);
    if (fd < 0) return;
    struct dirent buf[24];
    int n;
    while ((n = getdents(fd, buf, sizeof(buf))) > 0) {
        int count = n / (int)sizeof(struct dirent);
        int i;
        for (i = 0; i < count && s->n < MAX_DEV; i++) {
            const char *nm = buf[i].d_name;
            if (nm[0] == '\0') continue;
            if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) continue;
            strncpy(s->name[s->n], nm, MAX_NAME - 1);
            s->name[s->n][MAX_NAME - 1] = '\0';
            s->n++;
        }
    }
    close(fd);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("devd: starting\n");
    config_load();
    ensure_dir("/var/log");

    out_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (out_fd < 0) {
        printf("devd: cannot open %s\n", log_path);
    }

    scan_devfs(&prev_snap);
    printf("devd: %d device node(s) present\n", prev_snap.n);
    log_event("devd: starting\n");

    if (verbose) {
        char line[MAX_NAME + 16];
        int i;
        for (i = 0; i < prev_snap.n; i++) {
            snprintf(line, sizeof(line), "  %s\n", prev_snap.name[i]);
            log_event(line);
        }
    }

    for (;;) {
        struct snapshot cur;
        char line[MAX_NAME + 32];
        int i;

        scan_devfs(&cur);

        /* Removed nodes. */
        for (i = 0; i < prev_snap.n; i++) {
            if (!name_in(&cur, prev_snap.name[i])) {
                snprintf(line, sizeof(line), "devd: removed %s\n", prev_snap.name[i]);
                log_event(line);
                printf("devd: removed %s\n", prev_snap.name[i]);
            }
        }
        /* New nodes. */
        for (i = 0; i < cur.n; i++) {
            if (!name_in(&prev_snap, cur.name[i])) {
                snprintf(line, sizeof(line), "devd: added %s\n", cur.name[i]);
                log_event(line);
                printf("devd: added %s\n", cur.name[i]);
            }
        }

        prev_snap = cur;
        sleep((unsigned int)interval_sec);
    }
    return 0;
}
