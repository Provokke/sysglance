/* SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "test.h"

#include <stdlib.h>

#include "sg.h"

int t_checks, t_failures;
const char *t_fixture_dir = "tests/fixtures";
static int t_tests, t_failed_tests;

void t_run(const char *name, test_fn fn)
{
    int before = t_failures;
    fn();
    t_tests++;
    if (t_failures != before) {
        t_failed_tests++;
        printf("not ok - %s\n", name);
    } else {
        printf("ok     - %s\n", name);
    }
}

const char *t_fixture(const char *rel)
{
    static char buf[4096];
    snprintf(buf, sizeof buf, "%s/%s", t_fixture_dir, rel);
    return buf;
}

char *t_load(const char *rel)
{
    return sg_slurp_raw(t_fixture(rel), NULL);
}

int main(int argc, char **argv)
{
    if (argc > 1)
        t_fixture_dir = argv[1];
    suite_parse();
    suite_config();
    suite_modules();
    sg_set_root(NULL);
    printf("\nunit tests: %d tests, %d checks, %d failed checks, %d failed tests\n", t_tests,
           t_checks, t_failures, t_failed_tests);
    return t_failures ? 1 : 0;
}
