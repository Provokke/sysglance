/* SPDX-License-Identifier: MIT
 * A tiny INI-style config parser.
 *
 *   # comment            ; also a comment
 *   [display]
 *   color = auto         # auto | true | false
 *   logo = true
 *   logo_name = auto
 *   separator = ": "
 *   key_color = auto     # auto | red | green | ... | bright-blue ...
 *   [modules]
 *   list = os, kernel, cpu
 */
#define _POSIX_C_SOURCE 200809L
#include "sg.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

void sg_config_defaults(sg_config *c)
{
    memset(c, 0, sizeof *c);
    c->color = -1;
    c->logo = 1;
    c->json = 0;
    sg_strlcpy(c->logo_name, "auto", sizeof c->logo_name);
    sg_strlcpy(c->separator, ": ", sizeof c->separator);
    sg_strlcpy(c->key_color, "auto", sizeof c->key_color);
}

static const struct {
    const char *name;
    int code;
} colors[] = {
    {"auto", 0},          {"black", 30},          {"red", 31},
    {"green", 32},        {"yellow", 33},         {"blue", 34},
    {"magenta", 35},      {"cyan", 36},           {"white", 37},
    {"bright-black", 90}, {"bright-red", 91},     {"bright-green", 92},
    {"bright-yellow", 93}, {"bright-blue", 94},   {"bright-magenta", 95},
    {"bright-cyan", 96},  {"bright-white", 97},
};

int sg_color_code(const char *name)
{
    for (size_t i = 0; i < sizeof colors / sizeof *colors; i++)
        if (!strcasecmp(name, colors[i].name))
            return colors[i].code;
    return -1;
}

static int parse_bool(const char *v, int *out)
{
    if (!strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on") ||
        !strcmp(v, "1")) {
        *out = 1;
        return 0;
    }
    if (!strcasecmp(v, "false") || !strcasecmp(v, "no") || !strcasecmp(v, "off") ||
        !strcmp(v, "0")) {
        *out = 0;
        return 0;
    }
    return -1;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
        s[--n] = '\0';
    return s;
}

/* Strip an inline comment that starts with " #" or " ;" outside quotes, then
 * remove surrounding quotes. Returns -1 on an unterminated quote. */
static int clean_value(char *v)
{
    if (*v == '"' || *v == '\'') {
        char q = *v;
        char *end = strchr(v + 1, q);
        if (!end)
            return -1;
        char *rest = trim(end + 1);
        if (*rest && *rest != '#' && *rest != ';')
            return -1;
        *end = '\0';
        memmove(v, v + 1, (size_t)(end - v));
        return 0;
    }
    for (char *p = v; *p; p++) {
        if ((*p == '#' || *p == ';') && (p == v || p[-1] == ' ' || p[-1] == '\t')) {
            *p = '\0';
            break;
        }
    }
    char *t = trim(v);
    memmove(v, t, strlen(t) + 1);
    return 0;
}

#define FAIL(...)                                                                          \
    do {                                                                                   \
        if (err && errcap)                                                                 \
            snprintf(err, errcap, __VA_ARGS__);                                            \
        return -1;                                                                         \
    } while (0)

static int set_value(sg_config *c, const char *section, const char *k, const char *v,
                     int lineno, char *err, size_t errcap)
{
    if (!strcmp(section, "display")) {
        if (!strcmp(k, "color")) {
            int b;
            if (!strcasecmp(v, "auto"))
                c->color = -1;
            else if (parse_bool(v, &b) == 0)
                c->color = b;
            else
                FAIL("line %d: invalid value for color (want auto/true/false)", lineno);
        } else if (!strcmp(k, "logo")) {
            int b;
            if (parse_bool(v, &b) < 0)
                FAIL("line %d: invalid boolean for logo", lineno);
            c->logo = b;
        } else if (!strcmp(k, "json")) {
            int b;
            if (parse_bool(v, &b) < 0)
                FAIL("line %d: invalid boolean for json", lineno);
            c->json = b;
        } else if (!strcmp(k, "logo_name")) {
            if (!*v || strlen(v) >= sizeof c->logo_name)
                FAIL("line %d: invalid logo_name", lineno);
            sg_strlcpy(c->logo_name, v, sizeof c->logo_name);
        } else if (!strcmp(k, "separator")) {
            if (strlen(v) >= sizeof c->separator)
                FAIL("line %d: separator too long (max %zu bytes)", lineno,
                     sizeof c->separator - 1);
            for (const char *p = v; *p; p++)
                if ((unsigned char)*p < 0x20)
                    FAIL("line %d: control character in separator", lineno);
            sg_strlcpy(c->separator, v, sizeof c->separator);
        } else if (!strcmp(k, "key_color")) {
            if (sg_color_code(v) < 0)
                FAIL("line %d: unknown color '%.32s'", lineno, v);
            sg_strlcpy(c->key_color, v, sizeof c->key_color);
        } else {
            FAIL("line %d: unknown key '%.32s' in [display]", lineno, k);
        }
    } else if (!strcmp(section, "modules")) {
        if (!strcmp(k, "list")) {
            if (strlen(v) >= sizeof c->modules)
                FAIL("line %d: module list too long", lineno);
            sg_strlcpy(c->modules, v, sizeof c->modules);
        } else {
            FAIL("line %d: unknown key '%.32s' in [modules]", lineno, k);
        }
    } else if (!section[0]) {
        FAIL("line %d: key '%.32s' outside of a [section]", lineno, k);
    } else {
        FAIL("line %d: unknown section [%.32s]", lineno, section);
    }
    return 0;
}

int sg_config_parse(const char *text, size_t len, sg_config *c, char *err, size_t errcap)
{
    char section[32] = "";
    char line[1024];
    size_t pos = 0;
    int lineno = 0;
    if (err && errcap)
        err[0] = '\0';
    while (pos < len) {
        size_t start = pos;
        while (pos < len && text[pos] != '\n')
            pos++;
        size_t n = pos - start;
        if (pos < len)
            pos++; /* skip newline */
        lineno++;
        if (n >= sizeof line)
            FAIL("line %d: line too long", lineno);
        memcpy(line, text + start, n);
        line[n] = '\0';
        if (memchr(line, '\0', n))
            FAIL("line %d: embedded NUL byte", lineno);
        char *t = trim(line);
        if (!*t || *t == '#' || *t == ';')
            continue;
        if (*t == '[') {
            char *end = strchr(t, ']');
            if (!end)
                FAIL("line %d: unterminated section header", lineno);
            *end = '\0';
            char *rest = trim(end + 1);
            if (*rest && *rest != '#' && *rest != ';')
                FAIL("line %d: garbage after section header", lineno);
            char *name = trim(t + 1);
            if (!*name || strlen(name) >= sizeof section)
                FAIL("line %d: invalid section name", lineno);
            for (char *q = name; *q; q++)
                *q = (char)tolower((unsigned char)*q);
            sg_strlcpy(section, name, sizeof section);
            continue;
        }
        char *eq = strchr(t, '=');
        if (!eq)
            FAIL("line %d: expected 'key = value'", lineno);
        *eq = '\0';
        char *k = trim(t);
        char *v = trim(eq + 1);
        if (!*k)
            FAIL("line %d: empty key", lineno);
        for (char *q = k; *q; q++)
            *q = (char)tolower((unsigned char)*q);
        if (clean_value(v) < 0)
            FAIL("line %d: bad quoting", lineno);
        if (set_value(c, section, k, v, lineno, err, errcap) < 0)
            return -1;
    }
    return 0;
}
