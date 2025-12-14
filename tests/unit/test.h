/* Minimal unit test harness. SPDX-License-Identifier: MIT */
#ifndef SG_TEST_H
#define SG_TEST_H

#include <stdio.h>
#include <string.h>

extern int t_checks, t_failures;
extern const char *t_fixture_dir;

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        t_checks++;                                                                        \
        if (!(cond)) {                                                                     \
            t_failures++;                                                                  \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
        }                                                                                  \
    } while (0)

#define CHECK_STR(a, b)                                                                    \
    do {                                                                                   \
        const char *a_ = (a), *b_ = (b);                                                   \
        t_checks++;                                                                        \
        if (!a_ || !b_ || strcmp(a_, b_)) {                                                \
            t_failures++;                                                                  \
            fprintf(stderr, "  FAIL %s:%d: %s == \"%s\", expected \"%s\"\n", __FILE__,     \
                    __LINE__, #a, a_ ? a_ : "(null)", b_ ? b_ : "(null)");                 \
        }                                                                                  \
    } while (0)

#define CHECK_INT(a, b)                                                                    \
    do {                                                                                   \
        long long a_ = (long long)(a), b_ = (long long)(b);                                \
        t_checks++;                                                                        \
        if (a_ != b_) {                                                                    \
            t_failures++;                                                                  \
            fprintf(stderr, "  FAIL %s:%d: %s == %lld, expected %lld\n", __FILE__,         \
                    __LINE__, #a, a_, b_);                                                 \
        }                                                                                  \
    } while (0)

#define CHECK_CONTAINS(hay, needle)                                                        \
    do {                                                                                   \
        const char *h_ = (hay);                                                            \
        t_checks++;                                                                        \
        if (!h_ || !strstr(h_, (needle))) {                                                \
            t_failures++;                                                                  \
            fprintf(stderr, "  FAIL %s:%d: \"%s\" does not contain \"%s\"\n", __FILE__,    \
                    __LINE__, h_ ? h_ : "(null)", (needle));                               \
        }                                                                                  \
    } while (0)

typedef void (*test_fn)(void);
void t_run(const char *name, test_fn fn);
#define RUN(fn) t_run(#fn, fn)

/* Returns malloc'd contents of <fixture_dir>/<rel>, or NULL. */
char *t_load(const char *rel);
/* Writes "<fixture_dir>/<rel>" to a static buffer. */
const char *t_fixture(const char *rel);

void suite_parse(void);
void suite_config(void);
void suite_modules(void);

#endif
