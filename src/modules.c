/* SPDX-License-Identifier: MIT
 * Information collectors. Every file read goes through sg_slurp()/sg_read_line()
 * so the whole module set can be pointed at a fixture tree via --root. */
#define _DEFAULT_SOURCE
#include "sg.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <limits.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const char *basename_of(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static DIR *open_dir(const char *abs)
{
    char p[PATH_MAX];
    if (sg_path(p, sizeof p, abs) < 0)
        return NULL;
    return opendir(p);
}

static int is_number(const char *s)
{
    if (!*s)
        return 0;
    for (; *s; s++)
        if (!isdigit((unsigned char)*s))
            return 0;
    return 1;
}

/* ------------------------------------------------------------------ OS */

int sg_detect_os(sg_osrel *o)
{
    static const char *const paths[] = {"/etc/os-release", "/usr/lib/os-release"};
    for (size_t i = 0; i < 2; i++) {
        char *t = sg_slurp(paths[i], NULL);
        if (!t)
            continue;
        int r = sg_parse_os_release(t, o);
        free(t);
        if (r == 0)
            return 0;
    }
    memset(o, 0, sizeof *o);
    return -1;
}

static void get_user(char *out, size_t cap)
{
    const char *u = getenv("USER");
    if (!u || !*u)
        u = getenv("LOGNAME");
    if (u && *u) {
        sg_strlcpy(out, u, cap);
        return;
    }
    struct passwd *pw = getpwuid(geteuid());
    sg_strlcpy(out, pw && pw->pw_name ? pw->pw_name : "unknown", cap);
}

static void get_hostname(char *out, size_t cap)
{
    if (sg_read_line("/proc/sys/kernel/hostname", out, cap) > 0)
        return;
    struct utsname u;
    if (uname(&u) == 0)
        sg_strlcpy(out, u.nodename, cap);
    else
        sg_strlcpy(out, "unknown", cap);
}

static int m_title(sg_entry *e, int max)
{
    (void)max;
    char user[64], host[128];
    get_user(user, sizeof user);
    get_hostname(host, sizeof host);
    sg_entry_init(e, "");
    snprintf(e->value, sizeof e->value, "%s@%s", user, host);
    sg_field_str(e, "user", user);
    sg_field_str(e, "hostname", host);
    return 1;
}

static int m_user(sg_entry *e, int max)
{
    (void)max;
    char user[64];
    get_user(user, sizeof user);
    sg_entry_init(e, "User");
    sg_strlcpy(e->value, user, sizeof e->value);
    sg_field_str(e, "name", user);
    sg_field_int(e, "uid", (long long)geteuid());
    return 1;
}

static int m_hostname(sg_entry *e, int max)
{
    (void)max;
    char host[128];
    get_hostname(host, sizeof host);
    sg_entry_init(e, "Hostname");
    sg_strlcpy(e->value, host, sizeof e->value);
    sg_field_str(e, "name", host);
    return 1;
}

static int m_os(sg_entry *e, int max)
{
    (void)max;
    sg_osrel o;
    if (sg_detect_os(&o) < 0)
        return 0;
    struct utsname u;
    const char *arch = uname(&u) == 0 ? u.machine : "";
    sg_entry_init(e, "OS");
    snprintf(e->value, sizeof e->value, "%s%s%s", o.pretty_name, *arch ? " " : "", arch);
    sg_field_str(e, "name", o.name);
    sg_field_str(e, "pretty_name", o.pretty_name);
    sg_field_str(e, "id", o.id);
    sg_field_str(e, "id_like", o.id_like);
    sg_field_str(e, "version_id", o.version_id);
    sg_field_str(e, "arch", arch);
    return 1;
}

static int m_host(sg_entry *e, int max)
{
    (void)max;
    char name[128] = "", ver[64] = "";
    if (sg_read_line("/sys/devices/virtual/dmi/id/product_name", name, sizeof name) > 0) {
        sg_read_line("/sys/devices/virtual/dmi/id/product_version", ver, sizeof ver);
        /* vendors love placeholder strings */
        if (!strcasecmp(ver, "None") || !strcasecmp(ver, "To be filled by O.E.M.") ||
            !strcasecmp(ver, "System Version") || !strcasecmp(ver, "Not Applicable"))
            ver[0] = '\0';
    } else if (sg_read_line("/proc/device-tree/model", name, sizeof name) <= 0) {
        return 0;
    }
    if (!strcasecmp(name, "To be filled by O.E.M.") || !strcasecmp(name, "System Product Name"))
        return 0;
    sg_entry_init(e, "Host");
    snprintf(e->value, sizeof e->value, "%s%s%s", name, ver[0] ? " " : "", ver);
    sg_field_str(e, "product", name);
    sg_field_str(e, "version", ver);
    return 1;
}

static int m_kernel(sg_entry *e, int max)
{
    (void)max;
    char type[32] = "", rel[128] = "";
    struct utsname u;
    int have_u = uname(&u) == 0;
    if (sg_read_line("/proc/sys/kernel/osrelease", rel, sizeof rel) <= 0) {
        if (!have_u)
            return 0;
        sg_strlcpy(rel, u.release, sizeof rel);
    }
    if (sg_read_line("/proc/sys/kernel/ostype", type, sizeof type) <= 0)
        sg_strlcpy(type, have_u ? u.sysname : "Linux", sizeof type);
    sg_entry_init(e, "Kernel");
    snprintf(e->value, sizeof e->value, "%s %s", type, rel);
    sg_field_str(e, "name", type);
    sg_field_str(e, "release", rel);
    return 1;
}

static int m_uptime(sg_entry *e, int max)
{
    (void)max;
    char *t = sg_slurp("/proc/uptime", NULL);
    double s;
    int r = t ? sg_parse_uptime(t, &s) : -1;
    free(t);
    if (r < 0)
        return 0;
    sg_entry_init(e, "Uptime");
    sg_format_duration((unsigned long long)s, e->value, sizeof e->value);
    sg_field_int(e, "seconds", (long long)s);
    return 1;
}

/* ------------------------------------------------------------ packages */

static long count_dir(const char *abs, const char *suffix, int dirs_only)
{
    DIR *d = open_dir(abs);
    if (!d)
        return -1;
    long n = 0;
    struct dirent *de;
    size_t sl = suffix ? strlen(suffix) : 0;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue;
        if (dirs_only && de->d_type != DT_DIR && de->d_type != DT_UNKNOWN)
            continue;
        if (suffix) {
            size_t l = strlen(de->d_name);
            if (l < sl || strcmp(de->d_name + l - sl, suffix))
                continue;
        }
        n++;
    }
    closedir(d);
    return n;
}

static long count_line_prefix(const char *abs, const char *prefix)
{
    char *t = sg_slurp(abs, NULL);
    if (!t)
        return -1;
    long n = 0;
    size_t pl = strlen(prefix);
    for (const char *p = t; p && *p;) {
        if (!strncmp(p, prefix, pl))
            n++;
        p = strchr(p, '\n');
        if (p)
            p++;
    }
    free(t);
    return n;
}

static int m_packages(sg_entry *e, int max)
{
    (void)max;
    struct {
        const char *name;
        long n;
    } pm[5];
    int npm = 0;
    long c;
    if ((c = count_dir("/var/lib/dpkg/info", ".list", 0)) > 0)
        pm[npm].name = "dpkg", pm[npm++].n = c;
    if ((c = count_dir("/var/lib/pacman/local", NULL, 1)) > 0)
        pm[npm].name = "pacman", pm[npm++].n = c;
    if ((c = count_line_prefix("/lib/apk/db/installed", "P:")) > 0)
        pm[npm].name = "apk", pm[npm++].n = c;
    if ((c = count_dir("/var/lib/flatpak/app", NULL, 1)) > 0)
        pm[npm].name = "flatpak", pm[npm++].n = c;
    if ((c = count_dir("/var/lib/snapd/snaps", ".snap", 0)) > 0)
        pm[npm].name = "snap", pm[npm++].n = c;
    if (!npm)
        return 0;
    sg_entry_init(e, "Packages");
    size_t o = 0;
    long total = 0;
    for (int i = 0; i < npm; i++) {
        int w = snprintf(e->value + o, sizeof e->value - o, "%s%ld (%s)", i ? ", " : "",
                         pm[i].n, pm[i].name);
        if (w > 0 && (size_t)w < sizeof e->value - o)
            o += (size_t)w;
        sg_field_int(e, pm[i].name, pm[i].n);
        total += pm[i].n;
    }
    sg_field_int(e, "total", total);
    return 1;
}

/* ------------------------------------------------------- shell/terminal */

static int m_shell(sg_entry *e, int max)
{
    (void)max;
    const char *sh = getenv("SHELL");
    if (!sh || !*sh) {
        struct passwd *pw = getpwuid(geteuid());
        sh = pw ? pw->pw_shell : NULL;
    }
    if (!sh || !*sh)
        return 0;
    sg_entry_init(e, "Shell");
    sg_strlcpy(e->value, basename_of(sh), sizeof e->value);
    sg_field_str(e, "name", basename_of(sh));
    sg_field_str(e, "path", sh);
    return 1;
}

static int is_shell_like(const char *comm)
{
    static const char *const names[] = {"bash", "zsh", "fish", "sh", "dash", "ksh",
                                        "mksh", "tcsh", "csh", "nu", "elvish", "xonsh",
                                        "sudo", "su", "doas", "sysglance", "sysglance-test",
                                        "make", "timeout", "env", "strace", "valgrind",
                                        "tmux: client", "screen", "login"};
    for (size_t i = 0; i < sizeof names / sizeof *names; i++)
        if (!strcmp(comm, names[i]))
            return 1;
    if (!strncmp(comm, "memcheck", 8) || !strncmp(comm, "python", 6))
        return 1;
    return 0;
}

static int find_terminal_process(char *out, size_t cap)
{
    int pid = (int)getppid();
    for (int depth = 0; depth < 16 && pid > 1; depth++) {
        char path[64], comm[64];
        snprintf(path, sizeof path, "/proc/%d/stat", pid);
        char *t = sg_slurp(path, NULL);
        int ppid;
        int r = t ? sg_parse_stat_ppid(t, comm, sizeof comm, &ppid) : -1;
        free(t);
        if (r < 0)
            return -1;
        if (!is_shell_like(comm)) {
            sg_strlcpy(out, comm, cap);
            return 0;
        }
        pid = ppid;
    }
    return -1;
}

static int m_terminal(sg_entry *e, int max)
{
    (void)max;
    char term[64] = "";
    const char *src = "env";
    const char *tp = getenv("TERM_PROGRAM");
    if (tp && *tp)
        sg_strlcpy(term, tp, sizeof term);
    else if (find_terminal_process(term, sizeof term) == 0)
        src = "process";
    else if (getenv("TERM") && *getenv("TERM"))
        sg_strlcpy(term, getenv("TERM"), sizeof term);
    else
        return 0;
    sg_entry_init(e, "Terminal");
    sg_strlcpy(e->value, term, sizeof e->value);
    sg_field_str(e, "name", term);
    sg_field_str(e, "source", src);
    const char *tv = getenv("TERM");
    sg_field_str(e, "term", tv ? tv : "");
    return 1;
}

/* ----------------------------------------------------------------- CPU */

static int m_cpu(sg_entry *e, int max)
{
    (void)max;
    char *t = sg_slurp("/proc/cpuinfo", NULL);
    sg_cpuinfo c;
    int r = t ? sg_parse_cpuinfo(t, &c) : -1;
    free(t);
    if (r < 0)
        return 0;
    char buf[32];
    double ghz = 0;
    if (sg_read_line("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", buf,
                     sizeof buf) > 0)
        ghz = strtod(buf, NULL) / 1e6; /* kHz -> GHz */
    if (ghz <= 0)
        ghz = c.mhz_max / 1000.0;
    if (!c.model[0])
        sg_strlcpy(c.model, "Unknown CPU", sizeof c.model);
    sg_entry_init(e, "CPU");
    int n = c.logical ? c.logical : 1;
    if (c.physical_cores && c.physical_cores != n)
        snprintf(buf, sizeof buf, "%dC/%dT", c.physical_cores, n);
    else
        snprintf(buf, sizeof buf, "%d", n);
    if (ghz > 0)
        snprintf(e->value, sizeof e->value, "%s (%s) @ %.2f GHz", c.model, buf, ghz);
    else
        snprintf(e->value, sizeof e->value, "%s (%s)", c.model, buf);
    sg_field_str(e, "model", c.model);
    sg_field_int(e, "cores_logical", c.logical);
    sg_field_int(e, "cores_physical", c.physical_cores);
    sg_field_dbl(e, "freq_ghz", ghz);
    return 1;
}

/* ----------------------------------------------------------------- GPU */

static const char *const pci_ids_paths[] = {"/usr/share/hwdata/pci.ids",
                                            "/usr/share/misc/pci.ids", "/usr/share/pci.ids"};

static void gpu_name(unsigned v, unsigned d, char *out, size_t cap)
{
    char vend[96] = "", dev[128] = "";
    for (size_t i = 0; i < sizeof pci_ids_paths / sizeof *pci_ids_paths; i++) {
        char *t = sg_slurp(pci_ids_paths[i], NULL);
        if (!t)
            continue;
        int r = sg_pci_ids_lookup(t, v, d, vend, sizeof vend, dev, sizeof dev);
        free(t);
        if (r >= 0)
            break;
    }
    const char *short_vendor = sg_pci_vendor_name(v);
    if (short_vendor)
        sg_strlcpy(vend, short_vendor, sizeof vend);
    if (!vend[0])
        snprintf(vend, sizeof vend, "Vendor %04x", v);
    if (dev[0])
        snprintf(out, cap, "%s %s", vend, dev);
    else
        snprintf(out, cap, "%s [%04x:%04x]", vend, v, d);
}

static int read_hex(const char *abs, unsigned *out)
{
    char buf[32];
    if (sg_read_line(abs, buf, sizeof buf) <= 0)
        return -1;
    char *end;
    unsigned long v = strtoul(buf, &end, 16);
    if (end == buf)
        return -1;
    *out = (unsigned)v;
    return 0;
}

static int m_gpu(sg_entry *out, int max)
{
    int n = 0;
    /* Scan PCI display-class devices (class 0x03xxxx). This also covers
     * every DRM card that sits on PCI. */
    DIR *d = open_dir("/sys/bus/pci/devices");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) && n < max) {
            if (de->d_name[0] == '.')
                continue;
            char p[PATH_MAX];
            unsigned cls, v, dv;
            snprintf(p, sizeof p, "/sys/bus/pci/devices/%.200s/class", de->d_name);
            if (read_hex(p, &cls) < 0 || (cls >> 16) != 0x03)
                continue;
            snprintf(p, sizeof p, "/sys/bus/pci/devices/%.200s/vendor", de->d_name);
            if (read_hex(p, &v) < 0)
                continue;
            snprintf(p, sizeof p, "/sys/bus/pci/devices/%.200s/device", de->d_name);
            if (read_hex(p, &dv) < 0)
                continue;
            sg_entry *e = &out[n];
            sg_entry_init(e, "GPU");
            gpu_name(v, dv, e->value, sizeof e->value);
            char ids[16];
            snprintf(ids, sizeof ids, "%04x:%04x", v, dv);
            sg_field_str(e, "name", e->value);
            sg_field_str(e, "pci_id", ids);
            sg_field_str(e, "slot", de->d_name);
            n++;
        }
        closedir(d);
    }
    if (n)
        return n;
    /* Non-PCI (SoC) GPUs: fall back to the DRM driver name. */
    d = open_dir("/sys/class/drm");
    if (!d)
        return 0;
    struct dirent *de;
    while ((de = readdir(d)) && n < max) {
        if (strncmp(de->d_name, "card", 4) || strchr(de->d_name, '-'))
            continue;
        char p[PATH_MAX];
        snprintf(p, sizeof p, "/sys/class/drm/%.200s/device/uevent", de->d_name);
        char *t = sg_slurp(p, NULL);
        if (!t)
            continue;
        char *drv = strstr(t, "DRIVER=");
        if (drv) {
            drv += 7;
            drv[strcspn(drv, "\n")] = '\0';
            sg_entry_init(&out[n], "GPU");
            snprintf(out[n].value, sizeof out[n].value, "%.120s (%.64s)", drv, de->d_name);
            sg_field_str(&out[n], "driver", drv);
            sg_field_str(&out[n], "card", de->d_name);
            n++;
        }
        free(t);
    }
    closedir(d);
    return n;
}

/* -------------------------------------------------------------- memory */

static int pct(unsigned long long used, unsigned long long total)
{
    return total ? (int)((used * 100 + total / 2) / total) : 0;
}

static int m_memory(sg_entry *e, int max)
{
    (void)max;
    char *t = sg_slurp("/proc/meminfo", NULL);
    sg_meminfo m;
    int r = t ? sg_parse_meminfo(t, &m) : -1;
    free(t);
    if (r < 0)
        return 0;
    unsigned long long used = sg_mem_used_kb(&m) * 1024ULL, total = m.total * 1024ULL;
    char a[32], b[32];
    sg_format_bytes(used, a, sizeof a);
    sg_format_bytes(total, b, sizeof b);
    sg_entry_init(e, "Memory");
    snprintf(e->value, sizeof e->value, "%s / %s (%d%%)", a, b, pct(used, total));
    sg_field_int(e, "used_bytes", (long long)used);
    sg_field_int(e, "total_bytes", (long long)total);
    sg_field_int(e, "percent", pct(used, total));
    return 1;
}

static int m_swap(sg_entry *e, int max)
{
    (void)max;
    char *t = sg_slurp("/proc/meminfo", NULL);
    sg_meminfo m;
    int r = t ? sg_parse_meminfo(t, &m) : -1;
    free(t);
    if (r < 0)
        return 0;
    sg_entry_init(e, "Swap");
    unsigned long long total = m.swap_total * 1024ULL;
    unsigned long long used = m.swap_free <= m.swap_total ? (m.swap_total - m.swap_free) * 1024ULL : 0;
    if (!total) {
        sg_strlcpy(e->value, "Disabled", sizeof e->value);
    } else {
        char a[32], b[32];
        sg_format_bytes(used, a, sizeof a);
        sg_format_bytes(total, b, sizeof b);
        snprintf(e->value, sizeof e->value, "%s / %s (%d%%)", a, b, pct(used, total));
    }
    sg_field_int(e, "used_bytes", (long long)used);
    sg_field_int(e, "total_bytes", (long long)total);
    sg_field_int(e, "percent", pct(used, total));
    return 1;
}

/* ---------------------------------------------------------------- disk */

static int m_disk(sg_entry *out, int max)
{
    char *t = sg_slurp("/proc/mounts", NULL);
    if (!t)
        return 0;
    int n = 0;
    char devs[SG_MAX_ENTRIES][128];
    char line[1024];
    for (const char *p = t; p && *p && n < max && n < SG_MAX_ENTRIES;) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len >= sizeof line)
            len = sizeof line - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        p = nl ? nl + 1 : NULL;
        sg_mount m;
        if (sg_parse_mount_line(line, &m) < 0)
            continue;
        int is_root = !strcmp(m.mountpoint, "/");
        if (!sg_is_disk_fs(m.fstype) && !is_root)
            continue;
        if (!strncmp(m.mountpoint, "/snap/", 6) || !strncmp(m.mountpoint, "/var/lib/docker", 15) ||
            !strncmp(m.mountpoint, "/proc", 5) || !strncmp(m.mountpoint, "/sys", 4))
            continue;
        int dup = 0;
        for (int i = 0; i < n; i++)
            if (!strcmp(devs[i], m.device))
                dup = 1;
        if (dup && !is_root)
            continue;
        char real[PATH_MAX];
        struct statvfs sv;
        if (sg_path(real, sizeof real, m.mountpoint) < 0 || statvfs(real, &sv) < 0 ||
            sv.f_blocks == 0)
            continue;
        unsigned long long total = (unsigned long long)sv.f_blocks * sv.f_frsize;
        unsigned long long avail = (unsigned long long)sv.f_bavail * sv.f_frsize;
        unsigned long long freeb = (unsigned long long)sv.f_bfree * sv.f_frsize;
        unsigned long long used = total - freeb;
        /* percentage like df: used / (used + avail) */
        unsigned long long denom = used + avail;
        sg_entry *e = &out[n];
        char label[40];
        snprintf(label, sizeof label, "Disk (%.30s)", m.mountpoint);
        sg_entry_init(e, label);
        char a[32], b[32];
        sg_format_bytes(used, a, sizeof a);
        sg_format_bytes(total, b, sizeof b);
        snprintf(e->value, sizeof e->value, "%s / %s (%d%%) - %s", a, b, pct(used, denom),
                 m.fstype);
        sg_field_str(e, "mountpoint", m.mountpoint);
        sg_field_str(e, "device", m.device);
        sg_field_str(e, "fstype", m.fstype);
        sg_field_int(e, "used_bytes", (long long)used);
        sg_field_int(e, "total_bytes", (long long)total);
        sg_field_int(e, "percent", pct(used, denom));
        sg_strlcpy(devs[n], m.device, sizeof devs[n]);
        n++;
    }
    free(t);
    return n;
}

/* ----------------------------------------------------- load/processes */

static int m_loadavg(sg_entry *e, int max)
{
    (void)max;
    char *t = sg_slurp("/proc/loadavg", NULL);
    double l[3];
    int r = t ? sg_parse_loadavg(t, l, NULL, NULL) : -1;
    free(t);
    if (r < 0)
        return 0;
    sg_entry_init(e, "Load");
    snprintf(e->value, sizeof e->value, "%.2f, %.2f, %.2f", l[0], l[1], l[2]);
    sg_field_dbl(e, "1m", l[0]);
    sg_field_dbl(e, "5m", l[1]);
    sg_field_dbl(e, "15m", l[2]);
    return 1;
}

static int m_processes(sg_entry *e, int max)
{
    (void)max;
    DIR *d = open_dir("/proc");
    if (!d)
        return 0;
    long n = 0;
    struct dirent *de;
    while ((de = readdir(d)))
        if (is_number(de->d_name))
            n++;
    closedir(d);
    if (!n)
        return 0;
    sg_entry_init(e, "Processes");
    snprintf(e->value, sizeof e->value, "%ld", n);
    sg_field_int(e, "count", n);
    return 1;
}

/* ------------------------------------------------------------- network */

static int m_localip(sg_entry *out, int max)
{
    struct ifaddrs *ifa = NULL;
    if (getifaddrs(&ifa) < 0)
        return 0;
    int n = 0;
    for (struct ifaddrs *i = ifa; i && n < max; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET)
            continue;
        if ((i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP))
            continue;
        char addr[INET_ADDRSTRLEN] = "";
        struct sockaddr_in *sin = (struct sockaddr_in *)(void *)i->ifa_addr;
        inet_ntop(AF_INET, &sin->sin_addr, addr, sizeof addr);
        int prefix = 0;
        if (i->ifa_netmask) {
            struct sockaddr_in *nm = (struct sockaddr_in *)(void *)i->ifa_netmask;
            uint32_t m = ntohl(nm->sin_addr.s_addr);
            while (m & 0x80000000u) {
                prefix++;
                m <<= 1;
            }
        }
        char label[40];
        snprintf(label, sizeof label, "Local IP (%.24s)", i->ifa_name);
        sg_entry_init(&out[n], label);
        snprintf(out[n].value, sizeof out[n].value, "%s/%d", addr, prefix);
        sg_field_str(&out[n], "interface", i->ifa_name);
        sg_field_str(&out[n], "address", addr);
        sg_field_int(&out[n], "prefix", prefix);
        n++;
    }
    freeifaddrs(ifa);
    return n;
}

/* ------------------------------------------------------------- battery */

static int m_battery(sg_entry *out, int max)
{
    DIR *d = open_dir("/sys/class/power_supply");
    if (!d)
        return 0;
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) && n < max) {
        if (de->d_name[0] == '.')
            continue;
        char p[PATH_MAX], type[32], cap[16], status[32] = "";
        snprintf(p, sizeof p, "/sys/class/power_supply/%.200s/type", de->d_name);
        if (sg_read_line(p, type, sizeof type) <= 0 || strcmp(type, "Battery"))
            continue;
        snprintf(p, sizeof p, "/sys/class/power_supply/%.200s/capacity", de->d_name);
        if (sg_read_line(p, cap, sizeof cap) <= 0)
            continue;
        snprintf(p, sizeof p, "/sys/class/power_supply/%.200s/status", de->d_name);
        sg_read_line(p, status, sizeof status);
        char label[40];
        snprintf(label, sizeof label, "Battery (%.24s)", de->d_name);
        sg_entry_init(&out[n], label);
        int c = atoi(cap);
        if (status[0])
            snprintf(out[n].value, sizeof out[n].value, "%d%% [%s]", c, status);
        else
            snprintf(out[n].value, sizeof out[n].value, "%d%%", c);
        sg_field_str(&out[n], "name", de->d_name);
        sg_field_int(&out[n], "capacity", c);
        sg_field_str(&out[n], "status", status);
        n++;
    }
    closedir(d);
    return n;
}

/* -------------------------------------------------------------- locale */

static int m_locale(sg_entry *e, int max)
{
    (void)max;
    static const char *const vars[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
    const char *v = NULL, *src = "default";
    for (size_t i = 0; i < 3; i++) {
        const char *x = getenv(vars[i]);
        if (x && *x) {
            v = x;
            src = vars[i];
            break;
        }
    }
    sg_entry_init(e, "Locale");
    sg_strlcpy(e->value, v ? v : "C", sizeof e->value);
    sg_field_str(e, "locale", e->value);
    sg_field_str(e, "source", src);
    return 1;
}

/* ------------------------------------------------------------ registry */

const sg_module sg_modules[] = {
    {"title", "user@hostname header line", m_title},
    {"os", "Distribution name, version and architecture", m_os},
    {"host", "Machine model (DMI / device-tree)", m_host},
    {"kernel", "Kernel name and release", m_kernel},
    {"uptime", "Time since boot", m_uptime},
    {"packages", "Installed package counts (dpkg, pacman, apk, flatpak, snap)", m_packages},
    {"shell", "Login shell", m_shell},
    {"terminal", "Terminal emulator", m_terminal},
    {"cpu", "CPU model, cores and max frequency", m_cpu},
    {"gpu", "Graphics adapters (PCI / DRM)", m_gpu},
    {"memory", "RAM usage", m_memory},
    {"swap", "Swap usage", m_swap},
    {"disk", "Mounted disk usage", m_disk},
    {"localip", "IPv4 addresses of active interfaces", m_localip},
    {"battery", "Battery charge and status", m_battery},
    {"loadavg", "Load averages (1, 5, 15 min)", m_loadavg},
    {"processes", "Number of running processes", m_processes},
    {"locale", "Active locale", m_locale},
    {"user", "Current user", m_user},
    {"hostname", "Host name", m_hostname},
};
const int sg_nmodules = (int)(sizeof sg_modules / sizeof *sg_modules);

const char *sg_default_modules = "title,os,host,kernel,uptime,packages,shell,terminal,cpu,"
                                 "gpu,memory,swap,disk,localip,battery,loadavg,processes,"
                                 "locale";

const sg_module *sg_find_module(const char *name)
{
    for (int i = 0; i < sg_nmodules; i++)
        if (!strcmp(sg_modules[i].name, name))
            return &sg_modules[i];
    return NULL;
}
