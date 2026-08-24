/* Wall-clock benchmark: fork/exec a command N times with stdout and stderr
 * sent to /dev/null and report min/median/mean/p95/max in milliseconds.
 * Usage: bench RUNS COMMAND [ARGS...]
 * SPDX-License-Identifier: MIT */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static int cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static int run_once(char **argv)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) {
            dup2(fd, 1);
            dup2(fd, 2);
        }
        execv(argv[0], argv);
        _exit(127);
    }
    int st;
    if (waitpid(pid, &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0)
        return -1;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: bench RUNS COMMAND [ARGS...]\n");
        return 2;
    }
    int runs = atoi(argv[1]);
    if (runs < 1)
        runs = 1;
    double *t = malloc(sizeof *t * (size_t)runs);
    if (!t)
        return 1;
    for (int i = 0; i < 20; i++) /* warm up page cache */
        if (run_once(argv + 2) < 0) {
            fprintf(stderr, "bench: command failed\n");
            free(t);
            return 1;
        }
    double sum = 0;
    for (int i = 0; i < runs; i++) {
        double s = now_ms();
        if (run_once(argv + 2) < 0) {
            fprintf(stderr, "bench: command failed\n");
            free(t);
            return 1;
        }
        t[i] = now_ms() - s;
        sum += t[i];
    }
    qsort(t, (size_t)runs, sizeof *t, cmp);
    printf("runs=%d min=%.3fms median=%.3fms mean=%.3fms p95=%.3fms max=%.3fms\n", runs, t[0],
           t[runs / 2], sum / runs, t[(int)(runs * 0.95)], t[runs - 1]);
    free(t);
    return 0;
}
