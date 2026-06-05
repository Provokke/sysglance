/* Collector tests: run real modules against fixture filesystem trees.
 * SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "test.h"

#include <stdlib.h>

#include "sg.h"

static sg_entry E[SG_MAX_ENTRIES];

static int run_mod(const char *root, const char *name)
{
    const sg_module *m = sg_find_module(name);
    CHECK(m != NULL);
    if (!m)
        return -1;
    sg_set_root(t_fixture(root));
    memset(E, 0, sizeof E);
    return m->fn(E, SG_MAX_ENTRIES);
}

static const char *field(const sg_entry *e, const char *name)
{
    for (int i = 0; i < e->nfields; i++)
        if (!strcmp(e->fields[i].name, name))
            return e->fields[i].json;
    return NULL;
}

static void test_module_registry(void)
{
    CHECK(sg_nmodules >= 18);
    CHECK(sg_find_module("cpu") != NULL);
    CHECK(sg_find_module("nope") == NULL);
    for (int i = 0; i < sg_nmodules; i++) {
        CHECK(sg_modules[i].fn != NULL);
        CHECK(sg_modules[i].desc && sg_modules[i].desc[0]);
        for (int j = i + 1; j < sg_nmodules; j++)
            CHECK(strcmp(sg_modules[i].name, sg_modules[j].name) != 0);
    }
    /* every default module exists */
    char buf[512];
    sg_strlcpy(buf, sg_default_modules, sizeof buf);
    for (char *tok = strtok(buf, ","); tok; tok = strtok(NULL, ","))
        CHECK(sg_find_module(tok) != NULL);
}

static void test_root_path(void)
{
    char p[64];
    sg_set_root("/x/y///");
    CHECK_STR(sg_get_root(), "/x/y");
    CHECK_INT(sg_path(p, sizeof p, "/proc/meminfo"), 0);
    CHECK_STR(p, "/x/y/proc/meminfo");
    CHECK_INT(sg_path(p, 8, "/proc/meminfo"), -1);
    sg_set_root("/");
    CHECK_STR(sg_get_root(), "");
    sg_set_root(NULL);
    CHECK_STR(sg_get_root(), "");
}

static void test_mod_os(void)
{
    CHECK_INT(run_mod("ubuntu", "os"), 1);
    CHECK_CONTAINS(E[0].value, "Ubuntu 24.04.1 LTS");
    CHECK_STR(field(&E[0], "id"), "\"ubuntu\"");
    CHECK_INT(run_mod("fedora", "os"), 1); /* /usr/lib/os-release fallback */
    CHECK_STR(E[0].value, "Fedora Linux 40 (Workstation Edition) aarch64");
    CHECK_STR(field(&E[0], "arch"), "\"aarch64\"");
    CHECK_INT(run_mod("alpine", "os"), 1);
    CHECK_CONTAINS(E[0].value, "Alpine Linux v3.20");
    CHECK_INT(run_mod("empty", "os"), 0);
    CHECK_INT(run_mod("malformed", "os"), 0);
}

static void test_mod_kernel_hostname(void)
{
    CHECK_INT(run_mod("ubuntu", "kernel"), 1);
    CHECK_STR(E[0].value, "Linux 6.8.0-45-generic");
    CHECK_INT(run_mod("arch", "hostname"), 1);
    CHECK_STR(E[0].value, "archbox");
    setenv("USER", "fixtureuser", 1);
    CHECK_INT(run_mod("alpine", "title"), 1);
    CHECK_STR(E[0].value, "fixtureuser@alpine-pi");
    CHECK_STR(E[0].label, "");
    CHECK_INT(run_mod("alpine", "user"), 1);
    CHECK_STR(E[0].value, "fixtureuser");
    /* empty root falls back to uname() */
    CHECK_INT(run_mod("empty", "kernel"), 1);
    CHECK(E[0].value[0] != '\0');
}

static void test_mod_uptime(void)
{
    CHECK_INT(run_mod("ubuntu", "uptime"), 1);
    CHECK_STR(E[0].value, "1 day, 2 hours, 3 mins");
    CHECK_STR(field(&E[0], "seconds"), "93784");
    CHECK_INT(run_mod("arch", "uptime"), 1);
    CHECK_STR(E[0].value, "59 secs");
    CHECK_INT(run_mod("malformed", "uptime"), 0);
    CHECK_INT(run_mod("empty", "uptime"), 0);
}

static void test_mod_packages(void)
{
    CHECK_INT(run_mod("ubuntu", "packages"), 1);
    CHECK_STR(E[0].value, "5 (dpkg)");
    CHECK_INT(run_mod("arch", "packages"), 1);
    CHECK_STR(E[0].value, "3 (pacman), 1 (flatpak)");
    CHECK_STR(field(&E[0], "total"), "4");
    CHECK_INT(run_mod("alpine", "packages"), 1);
    CHECK_STR(E[0].value, "3 (apk)");
    CHECK_INT(run_mod("empty", "packages"), 0);
}

static void test_mod_cpu(void)
{
    CHECK_INT(run_mod("ubuntu", "cpu"), 1);
    /* cpufreq max (4.7 GHz) wins over cpuinfo MHz */
    CHECK_STR(E[0].value, "11th Gen Intel Core i7-1165G7 (4C/8T) @ 4.70 GHz");
    CHECK_STR(field(&E[0], "cores_logical"), "8");
    CHECK_INT(run_mod("arch", "cpu"), 1);
    CHECK_STR(E[0].value, "AMD Ryzen 7 5800X (8C/16T) @ 3.80 GHz");
    CHECK_INT(run_mod("fedora", "cpu"), 1);
    CHECK_STR(E[0].value, "ARM Cortex-A76 (4)");
    CHECK_INT(run_mod("malformed", "cpu"), 0);
}

static void test_mod_gpu(void)
{
    CHECK_INT(run_mod("ubuntu", "gpu"), 1); /* host bridge (class 0x06) ignored */
    CHECK_STR(E[0].value, "Intel TigerLake-LP GT2 [Iris Xe Graphics]");
    CHECK_STR(field(&E[0], "pci_id"), "\"8086:9a49\"");
    CHECK_INT(run_mod("arch", "gpu"), 1); /* no pci.ids available */
    CHECK_STR(E[0].value, "NVIDIA [10de:2484]");
    CHECK_INT(run_mod("fedora", "gpu"), 1); /* DRM-only SoC GPU */
    CHECK_STR(E[0].value, "vc4-drm (card0)");
    CHECK_INT(run_mod("empty", "gpu"), 0);
}

static void test_mod_memory_swap(void)
{
    CHECK_INT(run_mod("ubuntu", "memory"), 1);
    CHECK_STR(E[0].value, "3.89 GiB / 15.55 GiB (25%)");
    CHECK_INT(run_mod("ubuntu", "swap"), 1);
    CHECK_STR(E[0].value, "1.00 GiB / 4.00 GiB (25%)");
    CHECK_INT(run_mod("arch", "swap"), 1);
    CHECK_STR(E[0].value, "Disabled");
    CHECK_INT(run_mod("alpine", "swap"), 1);
    CHECK_STR(field(&E[0], "percent"), "50");
    CHECK_INT(run_mod("malformed", "memory"), 0);
    CHECK_INT(run_mod("empty", "swap"), 0);
}

static void test_mod_disk(void)
{
    /* "/" maps to the fixture directory itself, which statvfs can see;
     * /boot/efi does not exist in the fixture and is skipped. */
    CHECK_INT(run_mod("ubuntu", "disk"), 1);
    CHECK_STR(E[0].label, "Disk (/)");
    CHECK_CONTAINS(E[0].value, "- ext4");
    CHECK_STR(field(&E[0], "device"), "\"/dev/nvme0n1p2\"");
    CHECK_INT(run_mod("arch", "disk"), 1);
    CHECK_CONTAINS(E[0].value, "btrfs");
    CHECK_INT(run_mod("malformed", "disk"), 0);
    CHECK_INT(run_mod("empty", "disk"), 0);
}

static void test_mod_load_procs(void)
{
    CHECK_INT(run_mod("arch", "loadavg"), 1);
    CHECK_STR(E[0].value, "1.00, 2.50, 3.75");
    CHECK_STR(field(&E[0], "15m"), "3.75");
    CHECK_INT(run_mod("ubuntu", "processes"), 1);
    CHECK_STR(E[0].value, "4");
    CHECK_INT(run_mod("malformed", "loadavg"), 0);
    CHECK_INT(run_mod("empty", "processes"), 0);
}

static void test_mod_battery(void)
{
    CHECK_INT(run_mod("ubuntu", "battery"), 1); /* AC adapter ignored */
    CHECK_STR(E[0].label, "Battery (BAT0)");
    CHECK_STR(E[0].value, "87% [Discharging]");
    CHECK_INT(run_mod("arch", "battery"), 0);
    CHECK_INT(run_mod("malformed", "battery"), 0); /* no capacity file */
}

static void test_mod_host(void)
{
    CHECK_INT(run_mod("ubuntu", "host"), 1);
    CHECK_STR(E[0].value, "XPS 13 9310"); /* "None" version dropped */
    CHECK_INT(run_mod("fedora", "host"), 1);
    CHECK_STR(E[0].value, "Raspberry Pi 5 Model B Rev 1.0");
    CHECK_INT(run_mod("empty", "host"), 0);
}

static void test_mod_env_based(void)
{
    setenv("SHELL", "/usr/bin/zsh", 1);
    CHECK_INT(run_mod("empty", "shell"), 1);
    CHECK_STR(E[0].value, "zsh");
    setenv("TERM_PROGRAM", "FixtureTerm", 1);
    CHECK_INT(run_mod("empty", "terminal"), 1);
    CHECK_STR(E[0].value, "FixtureTerm");
    unsetenv("TERM_PROGRAM");
    setenv("TERM", "xterm-256color", 1);
    CHECK_INT(run_mod("empty", "terminal"), 1); /* no /proc in fixture -> $TERM */
    CHECK_STR(E[0].value, "xterm-256color");
    setenv("LC_ALL", "de_DE.UTF-8", 1);
    CHECK_INT(run_mod("empty", "locale"), 1);
    CHECK_STR(E[0].value, "de_DE.UTF-8");
    CHECK_STR(field(&E[0], "source"), "\"LC_ALL\"");
    unsetenv("LC_ALL");
    unsetenv("LC_MESSAGES");
    unsetenv("LANG");
    CHECK_INT(run_mod("empty", "locale"), 1);
    CHECK_STR(E[0].value, "C");
}

static void test_logo_selection(void)
{
    sg_osrel o;
    memset(&o, 0, sizeof o);
    strcpy(o.id, "manjaro");
    CHECK_STR(sg_logo_for_os(&o)->name, "arch");
    strcpy(o.id, "rocky");
    CHECK_STR(sg_logo_for_os(&o)->name, "fedora");
    strcpy(o.id, "unknownix");
    strcpy(o.id_like, "foo debian");
    CHECK_STR(sg_logo_for_os(&o)->name, "debian");
    o.id_like[0] = '\0';
    CHECK_STR(sg_logo_for_os(&o)->name, "sysglance");
    CHECK_STR(sg_logo_for_os(NULL)->name, "sysglance");
    CHECK(sg_logo_find("alpine") != NULL);
    CHECK(sg_logo_find("nope") == NULL);
    CHECK_INT(sg_logo_line_width("$1ab$2cd"), 4);
    CHECK_INT(sg_logo_line_width("caf\xc3\xa9"), 4);
    /* every logo has lines and fits a sane width */
    const char *names[] = {"sysglance", "ubuntu", "arch", "fedora", "alpine", "debian", "tux"};
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        const sg_logo *l = sg_logo_find(names[i]);
        CHECK(l != NULL);
        if (!l)
            continue;
        int n = 0;
        for (; l->lines[n]; n++)
            CHECK(sg_logo_line_width(l->lines[n]) <= 40);
        CHECK(n >= 5 && n <= 20);
    }
}

void suite_modules(void)
{
    RUN(test_module_registry);
    RUN(test_root_path);
    RUN(test_mod_os);
    RUN(test_mod_kernel_hostname);
    RUN(test_mod_uptime);
    RUN(test_mod_packages);
    RUN(test_mod_cpu);
    RUN(test_mod_gpu);
    RUN(test_mod_memory_swap);
    RUN(test_mod_disk);
    RUN(test_mod_load_procs);
    RUN(test_mod_battery);
    RUN(test_mod_host);
    RUN(test_mod_env_based);
    RUN(test_logo_selection);
    sg_set_root(NULL);
}
