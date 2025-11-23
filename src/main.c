/* SPDX-License-Identifier: MIT
 * sysglance - a fast system information tool. */
#define _POSIX_C_SOURCE 200809L
#include "sg.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define POOL_SIZE 160

static void usage(FILE *f)
{
    fputs("Usage: sysglance [OPTIONS]\n"
          "\n"
          "Show system information next to a logo, quickly.\n"
          "\n"
          "Options:\n"
          "  -m, --modules LIST   comma separated modules to show, in order\n"
          "  -l, --list-modules   list available modules and exit\n"
          "      --list-logos     list available logos and exit\n"
          "  -j, --json           print machine readable JSON\n"
          "      --no-color       disable colors (also: NO_COLOR env, non-TTY)\n"
          "      --color          force colors even when not on a TTY\n"
          "      --no-logo        do not print the logo\n"
          "      --logo NAME      use a specific logo (see --list-logos)\n"
          "  -c, --config FILE    read configuration from FILE\n"
          "      --no-config      ignore the default configuration file\n"
          "      --root DIR       read /proc, /sys, /etc ... below DIR (testing)\n"
          "  -V, --version        print version and exit\n"
          "  -h, --help           print this help and exit\n"
          "\n"
          "Default config: $XDG_CONFIG_HOME/sysglance/config.ini\n"
          "                (or ~/.config/sysglance/config.ini)\n",
          f);
}

typedef struct {
    const sg_module *mod;
    sg_entry *entries;
    int n;
} result;

static int load_config_file(const char *path, sg_config *cfg, int required)
{
    size_t len = 0;
    char *text = sg_slurp_raw(path, &len);
    if (!text) {
        if (!required)
            return 0;
        fprintf(stderr, "sysglance: cannot read config '%s': %s\n", path, strerror(errno));
        return -1;
    }
    char err[160];
    int r = sg_config_parse(text, len, cfg, err, sizeof err);
    free(text);
    if (r < 0) {
        fprintf(stderr, "sysglance: %s: %s\n", path, err);
        return -1;
    }
    return 0;
}

static int default_config_path(char *out, size_t cap)
{
    const char *x = getenv("XDG_CONFIG_HOME");
    int n;
    if (x && *x)
        n = snprintf(out, cap, "%s/sysglance/config.ini", x);
    else if (getenv("HOME") && *getenv("HOME"))
        n = snprintf(out, cap, "%s/.config/sysglance/config.ini", getenv("HOME"));
    else
        return -1;
    return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

/* Parse a module list into mods[]. Returns count, or -1 on error. */
static int parse_module_list(const char *list, const sg_module **mods, int max)
{
    char buf[1024];
    if (strlen(list) >= sizeof buf) {
        fprintf(stderr, "sysglance: module list too long\n");
        return -1;
    }
    sg_strlcpy(buf, list, sizeof buf);
    int n = 0;
    char *save = NULL;
    for (char *tok = strtok_r(buf, ", \t", &save); tok; tok = strtok_r(NULL, ", \t", &save)) {
        const sg_module *m = sg_find_module(tok);
        if (!m) {
            fprintf(stderr, "sysglance: unknown module '%s' (see --list-modules)\n", tok);
            return -1;
        }
        if (n >= max) {
            fprintf(stderr, "sysglance: too many modules (max %d)\n", max);
            return -1;
        }
        mods[n++] = m;
    }
    if (!n) {
        fprintf(stderr, "sysglance: empty module list\n");
        return -1;
    }
    return n;
}

/* ------------------------------------------------------------ output */

static void print_json(const result *res, int nres)
{
    char buf[700];
    printf("{\n  \"sysglance\": \"%s\",\n  \"modules\": [", SG_VERSION);
    for (int i = 0; i < nres; i++) {
        printf("%s\n    {\n      \"name\": \"%s\",\n      \"available\": %s,\n"
               "      \"entries\": [",
               i ? "," : "", res[i].mod->name, res[i].n > 0 ? "true" : "false");
        for (int j = 0; j < res[i].n; j++) {
            const sg_entry *e = &res[i].entries[j];
            printf("%s\n        {\n", j ? "," : "");
            sg_json_escape(e->label, buf, sizeof buf);
            printf("          \"label\": \"%s\",\n", buf);
            sg_json_escape(e->value, buf, sizeof buf);
            printf("          \"value\": \"%s\",\n          \"data\": {", buf);
            for (int k = 0; k < e->nfields; k++) {
                sg_json_escape(e->fields[k].name, buf, sizeof buf);
                printf("%s\n            \"%s\": %s", k ? "," : "", buf, e->fields[k].json);
            }
            printf("%s}\n        }", e->nfields ? "\n          " : "");
        }
        printf("%s]\n    }", res[i].n ? "\n      " : "");
    }
    printf("\n  ]\n}\n");
}

typedef struct {
    int color;
    int c1, c2, key;
    const char *sep;
} style;

static void put_logo_line(const char *line, const style *st, int width)
{
    int w = sg_logo_line_width(line);
    for (const char *p = line; *p; p++) {
        if (p[0] == '$' && (p[1] == '1' || p[1] == '2')) {
            if (st->color)
                printf("\033[1;%dm", p[1] == '1' ? st->c1 : st->c2);
            p++;
            continue;
        }
        putchar(*p);
    }
    if (st->color)
        fputs("\033[0m", stdout);
    printf("%*s", width - w + 3, "");
}

/* Writes the i-th info line (0 based) if it exists. Returns 1 if printed. */
static int put_info_line(const result *res, int nres, int idx, const style *st)
{
    int line = 0;
    for (int i = 0; i < nres; i++) {
        for (int j = 0; j < res[i].n; j++) {
            const sg_entry *e = &res[i].entries[j];
            int is_title = e->label[0] == '\0';
            char val[sizeof e->value];
            sg_strlcpy(val, e->value, sizeof val);
            sg_sanitize(val);
            if (line == idx) {
                if (is_title) {
                    if (st->color)
                        printf("\033[1;%dm%s\033[0m", st->key, val);
                    else
                        fputs(val, stdout);
                } else if (st->color) {
                    printf("\033[1;%dm%s\033[0m%s%s", st->key, e->label, st->sep, val);
                } else {
                    printf("%s%s%s", e->label, st->sep, val);
                }
                return 1;
            }
            line++;
            if (is_title) {
                if (line == idx) {
                    size_t n = strlen(val);
                    for (size_t k = 0; k < n; k++)
                        putchar('-');
                    return 1;
                }
                line++;
            }
        }
    }
    return 0;
}

static int count_info_lines(const result *res, int nres)
{
    int n = 0;
    for (int i = 0; i < nres; i++)
        for (int j = 0; j < res[i].n; j++)
            n += res[i].entries[j].label[0] ? 1 : 2;
    return n;
}

static void print_text(const result *res, int nres, const sg_logo *logo, const style *st)
{
    int info = count_info_lines(res, nres);
    int nlogo = 0, width = 0;
    if (logo) {
        for (; logo->lines[nlogo]; nlogo++) {
            int w = sg_logo_line_width(logo->lines[nlogo]);
            if (w > width)
                width = w;
        }
    }
    int total = info > nlogo ? info : nlogo;
    for (int i = 0; i < total; i++) {
        if (logo) {
            if (i < nlogo)
                put_logo_line(logo->lines[i], st, width);
            else if (i < info)
                printf("%*s", width + 3, "");
        }
        put_info_line(res, nres, i, st);
        putchar('\n');
    }
}

/* -------------------------------------------------------------- main */

/* Fetch the argument of an option given as "--opt VALUE" or "--opt=VALUE". */
static const char *opt_arg(int argc, char **argv, int *i, const char *name)
{
    const char *a = argv[*i];
    size_t n = strlen(name);
    if (!strncmp(a, name, n) && a[n] == '=')
        return a + n + 1;
    if (*i + 1 >= argc) {
        fprintf(stderr, "sysglance: option '%s' requires an argument\n", name);
        return NULL;
    }
    return argv[++*i];
}

static int matches(const char *a, const char *s, const char *l)
{
    size_t n = strlen(l);
    return (s && !strcmp(a, s)) || !strcmp(a, l) || (!strncmp(a, l, n) && a[n] == '=');
}

int main(int argc, char **argv)
{
    sg_config cfg;
    sg_config_defaults(&cfg);

    const char *cfg_path = NULL, *modules = NULL, *logo_name = NULL, *root = NULL;
    int no_config = 0, list_modules = 0, list_logos = 0;
    int cli_color = -2, cli_logo = -1, cli_json = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (matches(a, "-h", "--help")) {
            usage(stdout);
            return 0;
        } else if (matches(a, "-V", "--version")) {
            printf("sysglance %s\n", SG_VERSION);
            return 0;
        } else if (matches(a, "-m", "--modules")) {
            if (!(modules = opt_arg(argc, argv, &i, "--modules")))
                return 2;
        } else if (matches(a, "-c", "--config")) {
            if (!(cfg_path = opt_arg(argc, argv, &i, "--config")))
                return 2;
        } else if (matches(a, NULL, "--root")) {
            if (!(root = opt_arg(argc, argv, &i, "--root")))
                return 2;
        } else if (matches(a, NULL, "--logo")) {
            if (!(logo_name = opt_arg(argc, argv, &i, "--logo")))
                return 2;
        } else if (!strcmp(a, "-l") || !strcmp(a, "--list-modules")) {
            list_modules = 1;
        } else if (!strcmp(a, "--list-logos")) {
            list_logos = 1;
        } else if (!strcmp(a, "-j") || !strcmp(a, "--json")) {
            cli_json = 1;
        } else if (!strcmp(a, "--no-color") || !strcmp(a, "--no-colour")) {
            cli_color = 0;
        } else if (!strcmp(a, "--color") || !strcmp(a, "--colour")) {
            cli_color = 1;
        } else if (!strcmp(a, "--no-logo")) {
            cli_logo = 0;
        } else if (!strcmp(a, "--no-config")) {
            no_config = 1;
        } else {
            fprintf(stderr, "sysglance: unknown option '%s'\nTry 'sysglance --help'.\n", a);
            return 2;
        }
    }

    if (list_modules) {
        for (int i = 0; i < sg_nmodules; i++)
            printf("%-10s %s\n", sg_modules[i].name, sg_modules[i].desc);
        return 0;
    }
    if (list_logos) {
        sg_logo_list();
        return 0;
    }

    if (cfg_path) {
        if (load_config_file(cfg_path, &cfg, 1) < 0)
            return 1;
    } else if (!no_config) {
        char p[PATH_MAX];
        if (default_config_path(p, sizeof p) == 0 && load_config_file(p, &cfg, 0) < 0)
            return 1;
    }

    if (!root)
        root = getenv("SYSGLANCE_ROOT");
    sg_set_root(root);

    if (cli_json)
        cfg.json = 1;
    if (cli_logo == 0)
        cfg.logo = 0;
    if (logo_name)
        sg_strlcpy(cfg.logo_name, logo_name, sizeof cfg.logo_name);

    const sg_module *mods[SG_MAX_MODULES];
    int nmods = parse_module_list(modules ? modules
                                          : (cfg.modules[0] ? cfg.modules : sg_default_modules),
                                  mods, SG_MAX_MODULES);
    if (nmods < 0)
        return 2;

    const sg_logo *logo = NULL;
    if (!strcmp(cfg.logo_name, "none"))
        cfg.logo = 0;
    if (cfg.logo && !cfg.json) {
        if (strcmp(cfg.logo_name, "auto")) {
            logo = sg_logo_find(cfg.logo_name);
            if (!logo) {
                fprintf(stderr, "sysglance: unknown logo '%s' (see --list-logos)\n",
                        cfg.logo_name);
                return 2;
            }
        } else {
            sg_osrel o;
            logo = sg_logo_for_os(sg_detect_os(&o) == 0 ? &o : NULL);
        }
    }

    style st;
    const char *nc = getenv("NO_COLOR");
    if (cli_color != -2)
        st.color = cli_color;
    else if (nc && *nc)
        st.color = 0;
    else if (cfg.color >= 0)
        st.color = cfg.color;
    else {
        const char *term = getenv("TERM");
        st.color = isatty(STDOUT_FILENO) && !(term && !strcmp(term, "dumb"));
    }
    if (logo) {
        st.c1 = logo->color1;
        st.c2 = logo->color2;
    } else {
        st.c1 = 36;
        st.c2 = 34;
    }
    int kc = sg_color_code(cfg.key_color);
    st.key = kc > 0 ? kc : st.c1;
    st.sep = cfg.separator;

    sg_entry *pool = calloc(POOL_SIZE, sizeof *pool);
    result *res = calloc((size_t)nmods, sizeof *res);
    if (!pool || !res) {
        fprintf(stderr, "sysglance: out of memory\n");
        free(pool);
        free(res);
        return 1;
    }
    int used = 0;
    for (int i = 0; i < nmods; i++) {
        res[i].mod = mods[i];
        res[i].entries = pool + used;
        int room = POOL_SIZE - used;
        if (room > SG_MAX_ENTRIES)
            room = SG_MAX_ENTRIES;
        res[i].n = room > 0 ? mods[i]->fn(res[i].entries, room) : 0;
        if (res[i].n < 0)
            res[i].n = 0;
        used += res[i].n;
    }

    if (cfg.json)
        print_json(res, nmods);
    else
        print_text(res, nmods, logo, &st);

    free(pool);
    free(res);
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "sysglance: write error\n");
        return 1;
    }
    return 0;
}
