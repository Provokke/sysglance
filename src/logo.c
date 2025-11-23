/* SPDX-License-Identifier: MIT
 * Original ASCII artwork. See scripts/genlogo.py for the source art. */
#include "sg.h"

#include <stdio.h>
#include <string.h>

#include "logo_data.inc"

static const size_t n_logos = sizeof sg_logo_table / sizeof *sg_logo_table;

const sg_logo *sg_logo_find(const char *name)
{
    for (size_t i = 0; i < n_logos; i++)
        if (!strcmp(sg_logo_table[i].name, name))
            return &sg_logo_table[i];
    return NULL;
}

static int word_in(const char *word, const char *list)
{
    size_t wl = strlen(word);
    if (!wl)
        return 0;
    for (const char *p = list; *p;) {
        while (*p == ' ')
            p++;
        const char *e = p;
        while (*e && *e != ' ')
            e++;
        if ((size_t)(e - p) == wl && !strncmp(p, word, wl))
            return 1;
        p = e;
    }
    return 0;
}

const sg_logo *sg_logo_for_os(const sg_osrel *o)
{
    if (o) {
        for (size_t i = 0; i < n_logos; i++)
            if (word_in(o->id, sg_logo_table[i].aliases))
                return &sg_logo_table[i];
        /* try ID_LIKE words, e.g. "rhel fedora" */
        char like[sizeof o->id_like];
        sg_strlcpy(like, o->id_like, sizeof like);
        for (char *tok = like; *tok;) {
            char *sp = strchr(tok, ' ');
            if (sp)
                *sp = '\0';
            for (size_t i = 0; i < n_logos; i++)
                if (word_in(tok, sg_logo_table[i].aliases))
                    return &sg_logo_table[i];
            if (!sp)
                break;
            tok = sp + 1;
        }
    }
    return &sg_logo_table[0];
}

void sg_logo_list(void)
{
    for (size_t i = 0; i < n_logos; i++)
        printf("%-10s %s\n", sg_logo_table[i].name,
               sg_logo_table[i].aliases[0] ? sg_logo_table[i].aliases : "(default)");
}

int sg_logo_line_width(const char *line)
{
    int w = 0;
    for (const char *p = line; *p; p++) {
        if (p[0] == '$' && (p[1] == '1' || p[1] == '2')) {
            p++;
            continue;
        }
        if (((unsigned char)*p & 0xC0) != 0x80) /* count UTF-8 code points */
            w++;
    }
    return w;
}
