/* SPDX-License-Identifier: MIT
 * Pure text parsers. Nothing in here touches the filesystem, which keeps
 * every function unit-testable against fixture strings. */
#define _POSIX_C_SOURCE 200809L
#include "sg.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Copy the next line of text into buf (truncated to cap-1). Returns pointer
 * to the start of the following line, or NULL at end of text. */
static const char *next_line(const char *p, char *buf, size_t cap)
{
    if (!p || !*p)
        return NULL;
    const char *nl = strchr(p, '\n');
    size_t n = nl ? (size_t)(nl - p) : strlen(p);
    size_t c = n < cap - 1 ? n : cap - 1;
    memcpy(buf, p, c);
    buf[c] = '\0';
    return nl ? nl + 1 : p + n;
}

/* Split "key<sep>value" in place; both trimmed. Returns 0 if sep found. */
static int split_kv(char *line, char sep, char **k, char **v)
{
    char *s = strchr(line, sep);
    if (!s)
        return -1;
    *s = '\0';
    *k = sg_trim(line);
    *v = sg_trim(s + 1);
    return 0;
}

/* ---------------- os-release ---------------- */

static void unquote(char *v)
{
    size_t n = strlen(v);
    if (n >= 1 && (v[0] == '"' || v[0] == '\'')) {
        char q = v[0];
        char *w = v;
        const char *r = v + 1;
        while (*r && *r != q) {
            if (q == '"' && *r == '\\' && r[1])
                r++;
            *w++ = *r++;
        }
        *w = '\0';
    }
}

int sg_parse_os_release(const char *text, sg_osrel *o)
{
    memset(o, 0, sizeof *o);
    char line[512];
    const char *p = text;
    while ((p = next_line(p, line, sizeof line))) {
        char *t = sg_trim(line), *k, *v;
        if (!*t || *t == '#' || split_kv(t, '=', &k, &v) < 0)
            continue;
        unquote(v);
        if (!strcmp(k, "NAME"))
            sg_strlcpy(o->name, v, sizeof o->name);
        else if (!strcmp(k, "PRETTY_NAME"))
            sg_strlcpy(o->pretty_name, v, sizeof o->pretty_name);
        else if (!strcmp(k, "ID"))
            sg_strlcpy(o->id, v, sizeof o->id);
        else if (!strcmp(k, "ID_LIKE"))
            sg_strlcpy(o->id_like, v, sizeof o->id_like);
        else if (!strcmp(k, "VERSION_ID"))
            sg_strlcpy(o->version_id, v, sizeof o->version_id);
        else if (!strcmp(k, "VERSION_CODENAME"))
            sg_strlcpy(o->version_codename, v, sizeof o->version_codename);
    }
    if (!o->pretty_name[0] && o->name[0]) {
        if (o->version_id[0])
            snprintf(o->pretty_name, sizeof o->pretty_name, "%.60s %.30s", o->name, o->version_id);
        else
            sg_strlcpy(o->pretty_name, o->name, sizeof o->pretty_name);
    }
    if (!o->id[0] && o->name[0]) {
        /* derive an id from NAME: lowercase first word */
        size_t i = 0;
        for (; o->name[i] && !isspace((unsigned char)o->name[i]) && i < sizeof o->id - 1; i++)
            o->id[i] = (char)tolower((unsigned char)o->name[i]);
        o->id[i] = '\0';
    }
    return (o->pretty_name[0] || o->id[0]) ? 0 : -1;
}

/* ---------------- meminfo ---------------- */

int sg_parse_meminfo(const char *text, sg_meminfo *m)
{
    memset(m, 0, sizeof *m);
    int have_total = 0;
    char line[256];
    const char *p = text;
    while ((p = next_line(p, line, sizeof line))) {
        char *k, *v;
        if (split_kv(line, ':', &k, &v) < 0)
            continue;
        char *end;
        unsigned long long val = strtoull(v, &end, 10);
        if (end == v)
            continue;
        if (!strcmp(k, "MemTotal")) {
            m->total = val;
            have_total = 1;
        } else if (!strcmp(k, "MemFree"))
            m->free = val;
        else if (!strcmp(k, "MemAvailable")) {
            m->available = val;
            m->has_available = 1;
        } else if (!strcmp(k, "Buffers"))
            m->buffers = val;
        else if (!strcmp(k, "Cached"))
            m->cached = val;
        else if (!strcmp(k, "SReclaimable"))
            m->sreclaimable = val;
        else if (!strcmp(k, "Shmem"))
            m->shmem = val;
        else if (!strcmp(k, "SwapTotal"))
            m->swap_total = val;
        else if (!strcmp(k, "SwapFree"))
            m->swap_free = val;
    }
    return have_total ? 0 : -1;
}

unsigned long long sg_mem_used_kb(const sg_meminfo *m)
{
    if (m->has_available)
        return m->available >= m->total ? 0 : m->total - m->available;
    unsigned long long freeish = m->free + m->buffers + m->cached + m->sreclaimable;
    freeish = freeish > m->shmem ? freeish - m->shmem : 0;
    return freeish >= m->total ? 0 : m->total - freeish;
}

/* ---------------- cpuinfo ---------------- */

static void remove_all(char *s, const char *needle)
{
    size_t nl = strlen(needle);
    char *f;
    while ((f = strstr(s, needle)))
        memmove(f, f + nl, strlen(f + nl) + 1);
}

void sg_clean_cpu_name(char *s)
{
    remove_all(s, "(R)");
    remove_all(s, "(r)");
    remove_all(s, "(TM)");
    remove_all(s, "(tm)");
    char *at = strstr(s, " @ ");
    if (at)
        *at = '\0';
    /* "AMD Ryzen 7 5800X 8-Core Processor" -> drop "8-Core Processor" */
    char *core = strstr(s, "-Core Processor");
    if (core) {
        char *sp = core;
        while (sp > s && sp[-1] != ' ')
            sp--;
        *sp = '\0';
    }
    /* collapse whitespace */
    char *w = s;
    int prev_space = 1;
    for (char *r = s; *r; r++) {
        int sp = isspace((unsigned char)*r);
        if (sp && prev_space)
            continue;
        *w++ = sp ? ' ' : *r;
        prev_space = sp;
    }
    *w = '\0';
    size_t n = strlen(s);
    while (n && s[n - 1] == ' ')
        s[--n] = '\0';
    if (n > 4 && !strcmp(s + n - 4, " CPU"))
        s[n - 4] = '\0';
}

#define MAX_CORE_IDS 1024

/* ARM Ltd. (implementer 0x41) part numbers -> marketing names, for kernels
 * that don't print a "model name" on arm64. */
static const char *arm_part_name(unsigned part)
{
    switch (part) {
    case 0xd03: return "Cortex-A53";
    case 0xd04: return "Cortex-A35";
    case 0xd05: return "Cortex-A55";
    case 0xd07: return "Cortex-A57";
    case 0xd08: return "Cortex-A72";
    case 0xd09: return "Cortex-A73";
    case 0xd0a: return "Cortex-A75";
    case 0xd0b: return "Cortex-A76";
    case 0xd0c: return "Neoverse-N1";
    case 0xd0d: return "Cortex-A77";
    case 0xd40: return "Neoverse-V1";
    case 0xd41: return "Cortex-A78";
    case 0xd44: return "Cortex-X1";
    case 0xd46: return "Cortex-A510";
    case 0xd47: return "Cortex-A710";
    case 0xd48: return "Cortex-X2";
    case 0xd49: return "Neoverse-N2";
    case 0xd4f: return "Neoverse-V2";
    default: return NULL;
    }
}

int sg_parse_cpuinfo(const char *text, sg_cpuinfo *c)
{
    memset(c, 0, sizeof *c);
    /* priority: lower index wins */
    static const char *const model_keys[] = {"model name", "Hardware", "Processor",
                                             "cpu model", "uarch", "cpu"};
    int model_prio = 99;
    unsigned implementer = 0, part = 0;
    int phys = -1, core = -1;
    static const int max_ids = MAX_CORE_IDS;
    long *ids = malloc(sizeof(long) * MAX_CORE_IDS);
    int nids = 0, have_topology = 0;
    char line[512];
    const char *p = text;

    for (;;) {
        p = next_line(p, line, sizeof line);
        char *k = NULL, *v = NULL;
        int end_block = !p || split_kv(line, ':', &k, &v) < 0;
        if (!end_block && !*k)
            end_block = 1;
        if (end_block) {
            /* blank line: end of a processor block */
            if (phys >= 0 || core >= 0) {
                have_topology = 1;
                long key = (long)(phys < 0 ? 0 : phys) * 65536L + (core < 0 ? 0 : core);
                int seen = 0;
                for (int i = 0; i < nids; i++)
                    if (ids[i] == key) {
                        seen = 1;
                        break;
                    }
                if (!seen && ids && nids < max_ids)
                    ids[nids++] = key;
            }
            phys = core = -1;
            if (!p)
                break;
            continue;
        }
        if (!strcmp(k, "processor")) {
            /* "processor : 0" on x86/arm64; on old ARM "Processor : ARMv7.." */
            if (isdigit((unsigned char)*v))
                c->logical++;
            continue;
        }
        if (!strcmp(k, "physical id")) {
            phys = atoi(v);
            continue;
        }
        if (!strcmp(k, "core id")) {
            core = atoi(v);
            continue;
        }
        if (!strcmp(k, "CPU implementer")) {
            implementer = (unsigned)strtoul(v, NULL, 0);
            continue;
        }
        if (!strcmp(k, "CPU part")) {
            part = (unsigned)strtoul(v, NULL, 0);
            continue;
        }
        if (!strcmp(k, "cpu MHz") || !strcmp(k, "clock")) {
            double mhz = strtod(v, NULL);
            if (mhz > c->mhz_max)
                c->mhz_max = mhz;
            continue;
        }
        for (int i = 0; i < (int)(sizeof model_keys / sizeof *model_keys); i++) {
            if (i < model_prio && !strcmp(k, model_keys[i]) && *v) {
                sg_strlcpy(c->model, v, sizeof c->model);
                model_prio = i;
            }
        }
    }
    c->physical_cores = (have_topology && nids > 0) ? nids : c->logical;
    free(ids);
    if (!c->model[0] && implementer == 0x41 && arm_part_name(part))
        snprintf(c->model, sizeof c->model, "ARM %s", arm_part_name(part));
    if (c->model[0])
        sg_clean_cpu_name(c->model);
    return (c->model[0] || c->logical) ? 0 : -1;
}

/* ---------------- uptime / loadavg ---------------- */

int sg_parse_uptime(const char *text, double *seconds)
{
    char *end;
    double v = strtod(text, &end);
    if (end == text || v < 0)
        return -1;
    *seconds = v;
    return 0;
}

int sg_parse_loadavg(const char *text, double load[3], int *running, int *total)
{
    int r = 0, t = 0;
    if (sscanf(text, "%lf %lf %lf %d/%d", &load[0], &load[1], &load[2], &r, &t) < 3)
        return -1;
    if (running)
        *running = r;
    if (total)
        *total = t;
    return 0;
}

/* ---------------- mounts ---------------- */

/* /proc/mounts escapes spaces etc. as \040 octal */
static void unescape_octal(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++) {
        if (r[0] == '\\' && r[1] >= '0' && r[1] <= '3' && r[2] >= '0' && r[2] <= '7' &&
            r[3] >= '0' && r[3] <= '7') {
            *w++ = (char)((r[1] - '0') * 64 + (r[2] - '0') * 8 + (r[3] - '0'));
            r += 3;
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
}

int sg_parse_mount_line(const char *line, sg_mount *m)
{
    char dev[256], mnt[256], fs[64];
    memset(m, 0, sizeof *m);
    if (sscanf(line, "%255s %255s %63s", dev, mnt, fs) != 3)
        return -1;
    unescape_octal(dev);
    unescape_octal(mnt);
    sg_strlcpy(m->device, dev, sizeof m->device);
    sg_strlcpy(m->mountpoint, mnt, sizeof m->mountpoint);
    sg_strlcpy(m->fstype, fs, sizeof m->fstype);
    return 0;
}

int sg_is_disk_fs(const char *fs)
{
    static const char *const ok[] = {"ext2", "ext3", "ext4", "xfs", "btrfs", "zfs",
                                     "f2fs", "jfs", "reiserfs", "vfat", "exfat",
                                     "ntfs", "ntfs3", "fuseblk", "bcachefs", "nilfs2"};
    for (size_t i = 0; i < sizeof ok / sizeof *ok; i++)
        if (!strcmp(fs, ok[i]))
            return 1;
    return 0;
}

/* ---------------- formatting ---------------- */

void sg_format_bytes(unsigned long long bytes, char *out, size_t cap)
{
    static const char *const units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
    double v = (double)bytes;
    int u = 0;
    while (v >= 1023.995 && u < 6) { /* avoid "1024.00 MiB" after rounding */
        v /= 1024.0;
        u++;
    }
    if (u == 0)
        snprintf(out, cap, "%llu B", bytes);
    else
        snprintf(out, cap, "%.2f %s", v, units[u]);
}

void sg_format_duration(unsigned long long secs, char *out, size_t cap)
{
    unsigned long long d = secs / 86400, h = (secs / 3600) % 24, m = (secs / 60) % 60;
    size_t o = 0;
    out[0] = '\0';
    if (secs < 60) {
        snprintf(out, cap, "%llu sec%s", secs, secs == 1 ? "" : "s");
        return;
    }
#define APPEND(val, unit)                                                                   \
    do {                                                                                    \
        if ((val) && o < cap) {                                                             \
            int w = snprintf(out + o, cap - o, "%s%llu " unit "%s", o ? ", " : "", (val),   \
                             (val) == 1 ? "" : "s");                                        \
            if (w > 0)                                                                      \
                o += (size_t)w;                                                             \
        }                                                                                   \
    } while (0)
    APPEND(d, "day");
    APPEND(h, "hour");
    APPEND(m, "min");
#undef APPEND
}

/* ---------------- PCI ---------------- */

const char *sg_pci_vendor_name(unsigned vendor)
{
    switch (vendor) {
    case 0x1002: return "AMD";
    case 0x10de: return "NVIDIA";
    case 0x8086: return "Intel";
    case 0x1af4: return "Red Hat (virtio)";
    case 0x1234: return "QEMU";
    case 0x15ad: return "VMware";
    case 0x80ee: return "VirtualBox";
    case 0x1414: return "Microsoft";
    case 0x1a03: return "ASPEED";
    case 0x5143: return "Qualcomm";
    case 0x13b5: return "ARM";
    case 0x1013: return "Cirrus Logic";
    default: return NULL;
    }
}

int sg_pci_ids_lookup(const char *text, unsigned vendor, unsigned device, char *vendor_out,
                      size_t vcap, char *dev_out, size_t dcap)
{
    char vhex[8], dhex[8];
    snprintf(vhex, sizeof vhex, "%04x", vendor & 0xffffu);
    snprintf(dhex, sizeof dhex, "%04x", device & 0xffffu);
    const char *p = text;
    int in_vendor = 0;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (!in_vendor) {
            if (len > 6 && !strncmp(p, vhex, 4) && p[4] == ' ') {
                in_vendor = 1;
                size_t n = len - 4;
                const char *name = p + 4;
                while (n && *name == ' ') {
                    name++;
                    n--;
                }
                if (n >= vcap)
                    n = vcap - 1;
                memcpy(vendor_out, name, n);
                vendor_out[n] = '\0';
            }
        } else {
            if (len && p[0] != '\t' && p[0] != '#')
                return 1; /* next vendor: device not listed */
            if (len > 6 && p[0] == '\t' && p[1] != '\t' && !strncmp(p + 1, dhex, 4)) {
                const char *name = p + 5;
                size_t n = len - 5;
                while (n && *name == ' ') {
                    name++;
                    n--;
                }
                if (n >= dcap)
                    n = dcap - 1;
                memcpy(dev_out, name, n);
                dev_out[n] = '\0';
                return 0;
            }
        }
        p = nl ? nl + 1 : NULL;
    }
    return in_vendor ? 1 : -1;
}

int sg_parse_stat_ppid(const char *text, char *comm, size_t ccap, int *ppid)
{
    const char *open = strchr(text, '(');
    const char *close = strrchr(text, ')');
    if (!open || !close || close < open)
        return -1;
    size_t n = (size_t)(close - open - 1);
    if (n >= ccap)
        n = ccap - 1;
    memcpy(comm, open + 1, n);
    comm[n] = '\0';
    char state;
    int pp;
    if (sscanf(close + 1, " %c %d", &state, &pp) != 2)
        return -1;
    *ppid = pp;
    return 0;
}
