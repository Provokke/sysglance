/* Config parser unit tests. SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "test.h"

#include <stdlib.h>

#include "sg.h"

static int parse(const char *text, sg_config *c, char *err, size_t errcap)
{
    sg_config_defaults(c);
    return sg_config_parse(text, strlen(text), c, err, errcap);
}

static void test_config_defaults(void)
{
    sg_config c;
    sg_config_defaults(&c);
    CHECK_INT(c.color, -1);
    CHECK_INT(c.logo, 1);
    CHECK_INT(c.json, 0);
    CHECK_STR(c.logo_name, "auto");
    CHECK_STR(c.separator, ": ");
    CHECK_STR(c.key_color, "auto");
    CHECK_STR(c.modules, "");
}

static void test_config_full(void)
{
    sg_config c;
    char err[128];
    char *t = t_load("configs/full.ini");
    CHECK(t != NULL);
    sg_config_defaults(&c);
    CHECK_INT(sg_config_parse(t, strlen(t), &c, err, sizeof err), 0);
    CHECK_STR(err, "");
    CHECK_INT(c.color, 0);
    CHECK_INT(c.logo, 0);
    CHECK_STR(c.logo_name, "arch");
    CHECK_STR(c.separator, " -> ");
    CHECK_STR(c.key_color, "bright-magenta");
    CHECK_STR(c.modules, "os, kernel, cpu, memory");
    free(t);
}

static void test_config_syntax(void)
{
    sg_config c;
    char err[128];
    /* comments, blank lines, CRLF, case-insensitive sections/keys, inline comments */
    CHECK_INT(parse("; hi\r\n\r\n[DISPLAY]\r\nColor = YES # inline\r\n", &c, err, sizeof err),
              0);
    CHECK_INT(c.color, 1);
    CHECK_INT(parse("[display]\ncolor=auto\n", &c, err, sizeof err), 0);
    CHECK_INT(c.color, -1);
    CHECK_INT(parse("[display]\nseparator = ' = '\n", &c, err, sizeof err), 0);
    CHECK_STR(c.separator, " = ");
    CHECK_INT(parse("[display]\nseparator = \"#;\" ; trailing\n", &c, err, sizeof err), 0);
    CHECK_STR(c.separator, "#;");
    CHECK_INT(parse("[display]\nseparator =\n", &c, err, sizeof err), 0);
    CHECK_STR(c.separator, "");
    CHECK_INT(parse("[modules]\nlist = os,cpu\n[display]\njson = on\n", &c, err, sizeof err),
              0);
    CHECK_STR(c.modules, "os,cpu");
    CHECK_INT(c.json, 1);
    CHECK_INT(parse("", &c, err, sizeof err), 0);
    /* no trailing newline */
    CHECK_INT(parse("[display]\nlogo=false", &c, err, sizeof err), 0);
    CHECK_INT(c.logo, 0);
}

static void test_config_errors(void)
{
    sg_config c;
    char err[128];
    CHECK_INT(parse("color = true\n", &c, err, sizeof err), -1);
    CHECK_CONTAINS(err, "outside of a [section]");
    CHECK_INT(parse("[display]\ncolor = maybe\n", &c, err, sizeof err), -1);
    CHECK_CONTAINS(err, "line 2");
    CHECK_INT(parse("[display\n", &c, err, sizeof err), -1);
    CHECK_CONTAINS(err, "unterminated section");
    CHECK_INT(parse("[nope]\nx=1\n", &c, err, sizeof err), -1);
    CHECK_CONTAINS(err, "unknown section");
    CHECK_INT(parse("[display]\nwat = 1\n", &c, err, sizeof err), -1);
    CHECK_CONTAINS(err, "unknown key");
    CHECK_INT(parse("[display]\njust text\n", &c, err, sizeof err), -1);
    CHECK_CONTAINS(err, "expected 'key = value'");
    CHECK_INT(parse("[display]\n = 3\n", &c, err, sizeof err), -1);
    CHECK_CONTAINS(err, "empty key");
    CHECK_INT(parse("[display]\nseparator = \"abc\n", &c, err, sizeof err), -1);
    CHECK_CONTAINS(err, "bad quoting");
    CHECK_INT(parse("[display]\nseparator = \"a\" junk\n", &c, err, sizeof err), -1);
    CHECK_INT(parse("[display]\nseparator = 0123456789abcdefXYZ\n", &c, err, sizeof err), -1);
    CHECK_CONTAINS(err, "too long");
    CHECK_INT(parse("[display]\nkey_color = mauve\n", &c, err, sizeof err), -1);
    CHECK_CONTAINS(err, "unknown color");
    CHECK_INT(parse("[display]\nlogo = 2\n", &c, err, sizeof err), -1);
    CHECK_INT(parse("[] \n", &c, err, sizeof err), -1);
    CHECK_INT(parse("[display] x\n", &c, err, sizeof err), -1);
    char *t = t_load("configs/bad.ini");
    CHECK(t != NULL);
    sg_config_defaults(&c);
    CHECK_INT(sg_config_parse(t, strlen(t), &c, err, sizeof err), -1);
    CHECK_CONTAINS(err, "line 4");
    free(t);
    /* very long line */
    char *big = malloc(5000);
    memset(big, 'x', 4999);
    big[4999] = '\0';
    CHECK_INT(parse(big, &c, err, sizeof err), -1);
    CHECK_CONTAINS(err, "too long");
    free(big);
    /* embedded NUL */
    sg_config_defaults(&c);
    CHECK_INT(sg_config_parse("[display]\nlogo=t\0rue\n", 20, &c, err, sizeof err), -1);
    /* NULL / zero-size error buffer is allowed */
    CHECK_INT(parse("bad", &c, NULL, 0), -1);
}

static void test_color_codes(void)
{
    CHECK_INT(sg_color_code("auto"), 0);
    CHECK_INT(sg_color_code("red"), 31);
    CHECK_INT(sg_color_code("CYAN"), 36);
    CHECK_INT(sg_color_code("bright-blue"), 94);
    CHECK_INT(sg_color_code("chartreuse"), -1);
}

void suite_config(void)
{
    RUN(test_config_defaults);
    RUN(test_config_full);
    RUN(test_config_syntax);
    RUN(test_config_errors);
    RUN(test_color_codes);
}
