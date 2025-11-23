/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "sg.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char g_root[PATH_MAX];

void sg_set_root(const char *root)
{
    if (!root || !*root || strcmp(root, "/") == 0) {
        g_root[0] = '\0';
        return;
    }
    sg_strlcpy(g_root, root, sizeof g_root);
    size_t n = strlen(g_root);
    while (n > 1 && g_root[n - 1] == '/')
        g_root[--n] = '\0';
}

const char *sg_get_root(void) { return g_root; }

int sg_path(char *out, size_t cap, const char *abs_path)
{
    int n = snprintf(out, cap, "%s%s", g_root, abs_path);
    return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

size_t sg_strlcpy(char *dst, const char *src, size_t cap)
{
    size_t n = strlen(src);
    if (cap) {
        size_t c = n < cap - 1 ? n : cap - 1;
        memcpy(dst, src, c);
        dst[c] = '\0';
    }
    return n;
}

char *sg_slurp_raw(const char *real_path, size_t *len_out)
{
    int fd = open(real_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return NULL;
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        close(fd);
        return NULL;
    }
    for (;;) {
        if (len + 1 >= cap) {
            if (cap > (64u << 20)) /* refuse absurd files */
                break;
            char *nb = realloc(buf, cap * 2);
            if (!nb) {
                free(buf);
                close(fd);
                return NULL;
            }
            buf = nb;
            cap *= 2;
        }
        ssize_t r = read(fd, buf + len, cap - 1 - len);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            free(buf);
            close(fd);
            return NULL;
        }
        if (r == 0)
            break;
        len += (size_t)r;
    }
    close(fd);
    buf[len] = '\0';
    if (len_out)
        *len_out = len;
    return buf;
}

char *sg_slurp(const char *abs_path, size_t *len_out)
{
    char p[PATH_MAX];
    if (sg_path(p, sizeof p, abs_path) < 0)
        return NULL;
    return sg_slurp_raw(p, len_out);
}

int sg_read_line(const char *abs_path, char *buf, size_t cap)
{
    char p[PATH_MAX];
    if (cap == 0 || sg_path(p, sizeof p, abs_path) < 0)
        return -1;
    int fd = open(p, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    ssize_t r;
    do {
        r = read(fd, buf, cap - 1);
    } while (r < 0 && errno == EINTR);
    close(fd);
    if (r < 0)
        return -1;
    buf[r] = '\0';
    char *nl = strchr(buf, '\n');
    if (nl)
        *nl = '\0';
    char *t = sg_trim(buf);
    if (t != buf)
        memmove(buf, t, strlen(t) + 1);
    return (int)strlen(buf);
}

char *sg_trim(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
    return s;
}

void sg_json_escape(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    if (!cap)
        return;
    for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
        char tmp[8];
        const char *rep = NULL;
        switch (*p) {
        case '"': rep = "\\\""; break;
        case '\\': rep = "\\\\"; break;
        case '\n': rep = "\\n"; break;
        case '\r': rep = "\\r"; break;
        case '\t': rep = "\\t"; break;
        default:
            if (*p < 0x20 || *p == 0x7f) {
                snprintf(tmp, sizeof tmp, "\\u%04x", *p);
                rep = tmp;
            }
        }
        if (rep) {
            size_t rl = strlen(rep);
            if (o + rl >= cap)
                break;
            memcpy(out + o, rep, rl);
            o += rl;
        } else {
            if (o + 1 >= cap)
                break;
            out[o++] = (char)*p;
        }
    }
    /* never leave a truncated UTF-8 sequence at the end */
    while (o > 0 && ((unsigned char)out[o - 1] & 0x80)) {
        size_t start = o - 1;
        while (start > 0 && ((unsigned char)out[start] & 0xC0) == 0x80)
            start--;
        unsigned char lead = (unsigned char)out[start];
        size_t need = (lead >= 0xF0) ? 4 : (lead >= 0xE0) ? 3 : (lead >= 0xC0) ? 2 : 1;
        if (o - start >= need)
            break;
        o = start;
    }
    out[o] = '\0';
}

void sg_sanitize(char *s)
{
    for (; *s; s++)
        if ((unsigned char)*s < 0x20 || *s == 0x7f)
            *s = '?';
}

void sg_entry_init(sg_entry *e, const char *label)
{
    memset(e, 0, sizeof *e);
    sg_strlcpy(e->label, label, sizeof e->label);
}

static sg_field *field_add(sg_entry *e, const char *name)
{
    if (e->nfields >= SG_MAX_FIELDS)
        return NULL;
    sg_field *f = &e->fields[e->nfields++];
    sg_strlcpy(f->name, name, sizeof f->name);
    return f;
}

void sg_field_str(sg_entry *e, const char *name, const char *v)
{
    sg_field *f = field_add(e, name);
    if (!f)
        return;
    f->json[0] = '"';
    sg_json_escape(v, f->json + 1, sizeof f->json - 2);
    size_t n = strlen(f->json);
    f->json[n] = '"';
    f->json[n + 1] = '\0';
}

void sg_field_int(sg_entry *e, const char *name, long long v)
{
    sg_field *f = field_add(e, name);
    if (f)
        snprintf(f->json, sizeof f->json, "%lld", v);
}

void sg_field_dbl(sg_entry *e, const char *name, double v)
{
    sg_field *f = field_add(e, name);
    if (!f)
        return;
    if (isfinite(v))
        snprintf(f->json, sizeof f->json, "%.2f", v);
    else
        sg_strlcpy(f->json, "null", sizeof f->json);
}
