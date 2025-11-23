/* sysglance - fast system information tool
 * SPDX-License-Identifier: MIT
 */
#ifndef SG_H
#define SG_H

#include <stddef.h>
#include <stdint.h>

#define SG_VERSION "0.1.0"

#define SG_MAX_FIELDS 8
#define SG_MAX_ENTRIES 12
#define SG_MAX_MODULES 64

/* ---------- util.c ---------- */

/* The root prefix is prepended to every absolute path read by sysglance.
 * Empty (the default) means the real filesystem. Used by tests to point the
 * collectors at fixture trees. */
void sg_set_root(const char *root);
const char *sg_get_root(void);
int sg_path(char *out, size_t cap, const char *abs_path);

/* Read a whole (root-relative) file into a malloc'd NUL-terminated buffer.
 * Returns NULL on failure. */
char *sg_slurp(const char *abs_path, size_t *len_out);
/* Same, but for a real path that is NOT prefixed with the root. */
char *sg_slurp_raw(const char *real_path, size_t *len_out);
/* Read the first line of a (root-relative) file, trimmed. Returns length or -1. */
int sg_read_line(const char *abs_path, char *buf, size_t cap);

char *sg_trim(char *s);
size_t sg_strlcpy(char *dst, const char *src, size_t cap);
/* Escape a string for JSON output. Always NUL terminates. */
void sg_json_escape(const char *in, char *out, size_t cap);
/* Replace control characters so untrusted strings can't inject escapes. */
void sg_sanitize(char *s);

/* ---------- entries ---------- */

typedef struct {
    char name[32];
    char json[200]; /* already-encoded JSON value */
} sg_field;

typedef struct {
    char label[40];
    char value[256];
    int nfields;
    sg_field fields[SG_MAX_FIELDS];
} sg_entry;

void sg_entry_init(sg_entry *e, const char *label);
void sg_field_str(sg_entry *e, const char *name, const char *v);
void sg_field_int(sg_entry *e, const char *name, long long v);
void sg_field_dbl(sg_entry *e, const char *name, double v);

/* ---------- parse.c (pure parsers, unit tested) ---------- */

typedef struct {
    char name[64];
    char pretty_name[128];
    char id[32];
    char id_like[64];
    char version_id[32];
    char version_codename[32];
} sg_osrel;

int sg_parse_os_release(const char *text, sg_osrel *o);

typedef struct {
    unsigned long long total, free, available, buffers, cached;
    unsigned long long sreclaimable, shmem, swap_total, swap_free;
    int has_available;
} sg_meminfo; /* all values in kB */

int sg_parse_meminfo(const char *text, sg_meminfo *m);
unsigned long long sg_mem_used_kb(const sg_meminfo *m);

typedef struct {
    char model[128];
    int logical;
    int physical_cores;
    double mhz_max;
} sg_cpuinfo;

int sg_parse_cpuinfo(const char *text, sg_cpuinfo *c);
void sg_clean_cpu_name(char *s);

int sg_parse_uptime(const char *text, double *seconds);
int sg_parse_loadavg(const char *text, double load[3], int *running, int *total);

typedef struct {
    char device[128];
    char mountpoint[128];
    char fstype[32];
} sg_mount;

/* Parse one line of /proc/mounts. Returns 0 on success. */
int sg_parse_mount_line(const char *line, sg_mount *m);
/* Is this filesystem type a "real" disk worth showing? */
int sg_is_disk_fs(const char *fstype);

void sg_format_bytes(unsigned long long bytes, char *out, size_t cap);
void sg_format_duration(unsigned long long secs, char *out, size_t cap);

const char *sg_pci_vendor_name(unsigned vendor);
/* Look up vendor/device names in pci.ids formatted text. Returns 0 if the
 * device was found, 1 if only the vendor was found, -1 otherwise. */
int sg_pci_ids_lookup(const char *text, unsigned vendor, unsigned device,
                      char *vendor_out, size_t vcap, char *dev_out, size_t dcap);

/* Parse "PID (comm) S PPID ..." from /proc/<pid>/stat. */
int sg_parse_stat_ppid(const char *text, char *comm, size_t ccap, int *ppid);

/* ---------- config.c ---------- */

typedef struct {
    int color;          /* -1 auto, 0 off, 1 on */
    int logo;           /* 0 off, 1 on */
    int json;           /* 0/1 */
    char logo_name[32]; /* "auto" or a logo name */
    char separator[16];
    char key_color[16]; /* "auto" or a color name */
    char modules[512];  /* comma separated; empty = defaults */
} sg_config;

void sg_config_defaults(sg_config *c);
/* Parse INI-ish text. Returns 0 on success, -1 with a message in err. */
int sg_config_parse(const char *text, size_t len, sg_config *c, char *err, size_t errcap);
/* Returns ANSI SGR color number for a name, or -1 if unknown. 0 for "auto". */
int sg_color_code(const char *name);

/* ---------- modules.c ---------- */

typedef int (*sg_collect_fn)(sg_entry *out, int max);

typedef struct {
    const char *name;
    const char *desc;
    sg_collect_fn fn;
} sg_module;

extern const sg_module sg_modules[];
extern const int sg_nmodules;
extern const char *sg_default_modules;
const sg_module *sg_find_module(const char *name);
int sg_detect_os(sg_osrel *o);

/* ---------- logo.c ---------- */

typedef struct {
    const char *name;
    const char *aliases; /* space separated os-release ids */
    int color1, color2;  /* ANSI color codes */
    const char *const *lines;
} sg_logo;

const sg_logo *sg_logo_find(const char *name);
const sg_logo *sg_logo_for_os(const sg_osrel *o);
void sg_logo_list(void);
/* Visible width of a logo line (color markers stripped). */
int sg_logo_line_width(const char *line);

#endif
