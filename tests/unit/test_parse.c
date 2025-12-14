/* Parser unit tests. SPDX-License-Identifier: MIT */
#define _POSIX_C_SOURCE 200809L
#include "test.h"

#include <math.h>
#include <stdlib.h>

#include "sg.h"

/* ------------------------------------------------------------ os-release */

static void test_os_release_ubuntu(void)
{
    char *t = t_load("ubuntu/etc/os-release");
    sg_osrel o;
    CHECK(t != NULL);
    CHECK_INT(sg_parse_os_release(t, &o), 0);
    CHECK_STR(o.pretty_name, "Ubuntu 24.04.1 LTS");
    CHECK_STR(o.name, "Ubuntu");
    CHECK_STR(o.id, "ubuntu");
    CHECK_STR(o.id_like, "debian");
    CHECK_STR(o.version_id, "24.04");
    CHECK_STR(o.version_codename, "noble");
    free(t);
}

static void test_os_release_arch(void)
{
    char *t = t_load("arch/etc/os-release");
    sg_osrel o;
    CHECK_INT(sg_parse_os_release(t, &o), 0);
    CHECK_STR(o.pretty_name, "Arch Linux");
    CHECK_STR(o.id, "arch");
    CHECK_STR(o.version_id, "");
    free(t);
}

static void test_os_release_fedora(void)
{
    char *t = t_load("fedora/usr/lib/os-release");
    sg_osrel o;
    CHECK_INT(sg_parse_os_release(t, &o), 0);
    CHECK_STR(o.pretty_name, "Fedora Linux 40 (Workstation Edition)");
    CHECK_STR(o.id, "fedora");
    CHECK_STR(o.version_id, "40");
    free(t);
}

static void test_os_release_alpine(void)
{
    char *t = t_load("alpine/etc/os-release");
    sg_osrel o;
    CHECK_INT(sg_parse_os_release(t, &o), 0);
    CHECK_STR(o.pretty_name, "Alpine Linux v3.20");
    CHECK_STR(o.id, "alpine");
    CHECK_STR(o.version_id, "3.20.3");
    free(t);
}

static void test_os_release_edge_cases(void)
{
    sg_osrel o;
    /* no PRETTY_NAME: synthesize from NAME + VERSION_ID, derive id */
    CHECK_INT(sg_parse_os_release("NAME=Gentoo\nVERSION_ID=2.15\n", &o), 0);
    CHECK_STR(o.pretty_name, "Gentoo 2.15");
    CHECK_STR(o.id, "gentoo");
    /* single quotes, escapes, comments, whitespace, CRLF-free trailing space */
    CHECK_INT(sg_parse_os_release("# c\n  ID='void'  \nPRETTY_NAME=\"Say \\\"hi\\\"\"\n", &o),
              0);
    CHECK_STR(o.id, "void");
    CHECK_STR(o.pretty_name, "Say \"hi\"");
    /* unterminated quote keeps the rest */
    CHECK_INT(sg_parse_os_release("PRETTY_NAME=\"Broken\n", &o), 0);
    CHECK_STR(o.pretty_name, "Broken");
    /* nothing useful */
    CHECK_INT(sg_parse_os_release("", &o), -1);
    CHECK_INT(sg_parse_os_release("garbage\n=\n", &o), -1);
    char *t = t_load("malformed/etc/os-release");
    CHECK_INT(sg_parse_os_release(t, &o), -1);
    free(t);
    /* very long value is truncated, not overflowed */
    char big[2000];
    memset(big, 'A', sizeof big);
    memcpy(big, "PRETTY_NAME=", 12);
    big[sizeof big - 1] = '\0';
    CHECK_INT(sg_parse_os_release(big, &o), 0);
    CHECK_INT(strlen(o.pretty_name), sizeof o.pretty_name - 1);
}

/* --------------------------------------------------------------- meminfo */

static void test_meminfo_with_available(void)
{
    char *t = t_load("ubuntu/proc/meminfo");
    sg_meminfo m;
    CHECK_INT(sg_parse_meminfo(t, &m), 0);
    CHECK_INT(m.total, 16303428);
    CHECK_INT(m.available, 12227571);
    CHECK(m.has_available);
    CHECK_INT(m.swap_total, 4194300);
    CHECK_INT(m.swap_free, 3145725);
    CHECK_INT(sg_mem_used_kb(&m), 16303428 - 12227571);
    free(t);
}

static void test_meminfo_without_available(void)
{
    char *t = t_load("arch/proc/meminfo");
    sg_meminfo m;
    CHECK_INT(sg_parse_meminfo(t, &m), 0);
    CHECK(!m.has_available);
    /* total - (free + buffers + cached + sreclaimable - shmem) */
    CHECK_INT(sg_mem_used_kb(&m), 32768000 - (20000000 + 500000 + 4000000 + 268000 - 500000));
    CHECK_INT(m.swap_total, 0);
    free(t);
}

static void test_meminfo_edge_cases(void)
{
    sg_meminfo m;
    CHECK_INT(sg_parse_meminfo("", &m), -1);
    char *t = t_load("malformed/proc/meminfo");
    CHECK_INT(sg_parse_meminfo(t, &m), -1);
    free(t);
    /* available > total must not underflow */
    CHECK_INT(sg_parse_meminfo("MemTotal: 100 kB\nMemAvailable: 200 kB\n", &m), 0);
    CHECK_INT(sg_mem_used_kb(&m), 0);
    /* free > total without MemAvailable must not underflow */
    CHECK_INT(sg_parse_meminfo("MemTotal: 100 kB\nMemFree: 500 kB\n", &m), 0);
    CHECK_INT(sg_mem_used_kb(&m), 0);
}

/* --------------------------------------------------------------- cpuinfo */

static void test_cpuinfo_intel(void)
{
    char *t = t_load("ubuntu/proc/cpuinfo");
    sg_cpuinfo c;
    CHECK_INT(sg_parse_cpuinfo(t, &c), 0);
    CHECK_STR(c.model, "11th Gen Intel Core i7-1165G7");
    CHECK_INT(c.logical, 8);
    CHECK_INT(c.physical_cores, 4);
    CHECK(fabs(c.mhz_max - 1900.0) < 0.01);
    free(t);
}

static void test_cpuinfo_amd(void)
{
    char *t = t_load("arch/proc/cpuinfo");
    sg_cpuinfo c;
    CHECK_INT(sg_parse_cpuinfo(t, &c), 0);
    CHECK_STR(c.model, "AMD Ryzen 7 5800X");
    CHECK_INT(c.logical, 16);
    CHECK_INT(c.physical_cores, 8);
    free(t);
}

static void test_cpuinfo_arm64_no_model(void)
{
    char *t = t_load("fedora/proc/cpuinfo");
    sg_cpuinfo c;
    CHECK_INT(sg_parse_cpuinfo(t, &c), 0);
    CHECK_STR(c.model, "ARM Cortex-A76");
    CHECK_INT(c.logical, 4);
    CHECK_INT(c.physical_cores, 4);
    free(t);
}

static void test_cpuinfo_armv7(void)
{
    char *t = t_load("alpine/proc/cpuinfo");
    sg_cpuinfo c;
    CHECK_INT(sg_parse_cpuinfo(t, &c), 0);
    CHECK_STR(c.model, "ARMv7 Processor rev 4 (v7l)");
    CHECK_INT(c.logical, 2);
    free(t);
}

static void test_cpuinfo_edge_cases(void)
{
    sg_cpuinfo c;
    CHECK_INT(sg_parse_cpuinfo("", &c), -1);
    char *t = t_load("malformed/proc/cpuinfo");
    CHECK_INT(sg_parse_cpuinfo(t, &c), -1);
    free(t);
    /* powerpc style */
    CHECK_INT(sg_parse_cpuinfo("processor : 0\ncpu : POWER9 (raw), altivec supported\n"
                               "clock : 3800.000000MHz\n",
                               &c),
              0);
    CHECK_STR(c.model, "POWER9 (raw), altivec supported");
    CHECK(fabs(c.mhz_max - 3800.0) < 0.01);
    /* riscv style */
    CHECK_INT(sg_parse_cpuinfo("processor : 0\nisa : rv64imafdc\nuarch : sifive,u74-mc\n", &c),
              0);
    CHECK_STR(c.model, "sifive,u74-mc");
}

static void test_clean_cpu_name(void)
{
    char s[128];
    strcpy(s, "Intel(R) Xeon(R) CPU E5-2680 v4 @ 2.40GHz");
    sg_clean_cpu_name(s);
    CHECK_STR(s, "Intel Xeon CPU E5-2680 v4");
    strcpy(s, "  Intel(R)   Core(TM)  i5 CPU  ");
    sg_clean_cpu_name(s);
    CHECK_STR(s, "Intel Core i5");
    strcpy(s, "AMD EPYC 7763 64-Core Processor");
    sg_clean_cpu_name(s);
    CHECK_STR(s, "AMD EPYC 7763");
    strcpy(s, "");
    sg_clean_cpu_name(s);
    CHECK_STR(s, "");
}

/* --------------------------------------------------------- uptime / load */

static void test_uptime(void)
{
    double s = -1;
    char *t = t_load("ubuntu/proc/uptime");
    CHECK_INT(sg_parse_uptime(t, &s), 0);
    CHECK(fabs(s - 93784.52) < 0.001);
    free(t);
    CHECK_INT(sg_parse_uptime("abc", &s), -1);
    CHECK_INT(sg_parse_uptime("", &s), -1);
    CHECK_INT(sg_parse_uptime("-5 3", &s), -1);
}

static void test_loadavg(void)
{
    double l[3];
    int run = 0, tot = 0;
    char *t = t_load("ubuntu/proc/loadavg");
    CHECK_INT(sg_parse_loadavg(t, l, &run, &tot), 0);
    CHECK(fabs(l[0] - 0.52) < 1e-9 && fabs(l[1] - 0.58) < 1e-9 && fabs(l[2] - 0.59) < 1e-9);
    CHECK_INT(run, 2);
    CHECK_INT(tot, 1234);
    free(t);
    CHECK_INT(sg_parse_loadavg("x y z", l, NULL, NULL), -1);
    CHECK_INT(sg_parse_loadavg("1.0 2.0", l, NULL, NULL), -1);
}

/* ---------------------------------------------------------------- mounts */

static void test_mount_line(void)
{
    sg_mount m;
    CHECK_INT(sg_parse_mount_line("/dev/sda1 / ext4 rw,relatime 0 0", &m), 0);
    CHECK_STR(m.device, "/dev/sda1");
    CHECK_STR(m.mountpoint, "/");
    CHECK_STR(m.fstype, "ext4");
    CHECK_INT(sg_parse_mount_line("/dev/sdb1 /mnt/My\\040Disk vfat rw 0 0", &m), 0);
    CHECK_STR(m.mountpoint, "/mnt/My Disk");
    CHECK_INT(sg_parse_mount_line("onlyonefield", &m), -1);
    CHECK_INT(sg_parse_mount_line("", &m), -1);
    CHECK(sg_is_disk_fs("ext4"));
    CHECK(sg_is_disk_fs("btrfs"));
    CHECK(!sg_is_disk_fs("tmpfs"));
    CHECK(!sg_is_disk_fs("proc"));
    CHECK(!sg_is_disk_fs("squashfs"));
}

/* ------------------------------------------------------------ formatting */

static void test_format_bytes(void)
{
    char b[32];
    sg_format_bytes(0, b, sizeof b);
    CHECK_STR(b, "0 B");
    sg_format_bytes(1023, b, sizeof b);
    CHECK_STR(b, "1023 B");
    sg_format_bytes(1024, b, sizeof b);
    CHECK_STR(b, "1.00 KiB");
    sg_format_bytes(1536ULL * 1024 * 1024, b, sizeof b);
    CHECK_STR(b, "1.50 GiB");
    sg_format_bytes(1048575ULL * 1024, b, sizeof b); /* rounds up a unit */
    CHECK_STR(b, "1.00 GiB");
    sg_format_bytes(~0ULL, b, sizeof b);
    CHECK_STR(b, "16.00 EiB");
    sg_format_bytes(5ULL << 40, b, 4); /* truncation must stay NUL terminated */
    CHECK_INT(strlen(b), 3);
}

static void test_format_duration(void)
{
    char b[64];
    sg_format_duration(0, b, sizeof b);
    CHECK_STR(b, "0 secs");
    sg_format_duration(1, b, sizeof b);
    CHECK_STR(b, "1 sec");
    sg_format_duration(60, b, sizeof b);
    CHECK_STR(b, "1 min");
    sg_format_duration(3661, b, sizeof b);
    CHECK_STR(b, "1 hour, 1 min");
    sg_format_duration(93784, b, sizeof b);
    CHECK_STR(b, "1 day, 2 hours, 3 mins");
    sg_format_duration(172800, b, sizeof b);
    CHECK_STR(b, "2 days");
    sg_format_duration(93784, b, 8);
    CHECK(strlen(b) < 8);
}

/* ------------------------------------------------------------------- PCI */

static void test_pci_ids(void)
{
    char *t = t_load("ubuntu/usr/share/hwdata/pci.ids");
    char v[64], d[64];
    CHECK(t != NULL);
    CHECK_INT(sg_pci_ids_lookup(t, 0x8086, 0x9a49, v, sizeof v, d, sizeof d), 0);
    CHECK_STR(v, "Intel Corporation");
    CHECK_STR(d, "TigerLake-LP GT2 [Iris Xe Graphics]");
    CHECK_INT(sg_pci_ids_lookup(t, 0x10de, 0x2484, v, sizeof v, d, sizeof d), 0);
    CHECK_STR(d, "GA104 [GeForce RTX 3070]");
    CHECK_INT(sg_pci_ids_lookup(t, 0x8086, 0xffff, v, sizeof v, d, sizeof d), 1);
    CHECK_INT(sg_pci_ids_lookup(t, 0x1234, 0x1111, v, sizeof v, d, sizeof d), -1);
    /* small output buffers are truncated safely */
    CHECK_INT(sg_pci_ids_lookup(t, 0x8086, 0x9a49, v, 6, d, 6), 0);
    CHECK_STR(v, "Intel");
    CHECK_STR(d, "Tiger");
    free(t);
    CHECK_STR(sg_pci_vendor_name(0x10de), "NVIDIA");
    CHECK(sg_pci_vendor_name(0xdead) == NULL);
}

static void test_stat_ppid(void)
{
    char comm[32];
    int pp = -1;
    CHECK_INT(sg_parse_stat_ppid("123 (bash) S 99 123 123 0", comm, sizeof comm, &pp), 0);
    CHECK_STR(comm, "bash");
    CHECK_INT(pp, 99);
    /* comm containing spaces and parentheses */
    CHECK_INT(sg_parse_stat_ppid("7 (tmux: (srv) x) S 1 7", comm, sizeof comm, &pp), 0);
    CHECK_STR(comm, "tmux: (srv) x");
    CHECK_INT(pp, 1);
    CHECK_INT(sg_parse_stat_ppid("garbage", comm, sizeof comm, &pp), -1);
    CHECK_INT(sg_parse_stat_ppid("1 (x)", comm, sizeof comm, &pp), -1);
}

/* ------------------------------------------------------------------ util */

static void test_json_escape(void)
{
    char b[64];
    sg_json_escape("a\"b\\c\n\t\x01", b, sizeof b);
    CHECK_STR(b, "a\\\"b\\\\c\\n\\t\\u0001");
    sg_json_escape("caf\xc3\xa9", b, sizeof b);
    CHECK_STR(b, "caf\xc3\xa9");
    /* truncation never splits an escape or a UTF-8 sequence */
    sg_json_escape("ab\xc3\xa9", b, 4);
    CHECK_STR(b, "ab");
    sg_json_escape("a\"", b, 3);
    CHECK_STR(b, "a");
}

static void test_entry_fields(void)
{
    sg_entry e;
    sg_entry_init(&e, "Label");
    sg_field_str(&e, "s", "quote\"d");
    sg_field_int(&e, "i", -42);
    sg_field_dbl(&e, "d", 1.5);
    sg_field_dbl(&e, "nan", NAN);
    CHECK_INT(e.nfields, 4);
    CHECK_STR(e.fields[0].json, "\"quote\\\"d\"");
    CHECK_STR(e.fields[1].json, "-42");
    CHECK_STR(e.fields[2].json, "1.50");
    CHECK_STR(e.fields[3].json, "null");
    for (int i = 0; i < 20; i++)
        sg_field_int(&e, "x", i);
    CHECK_INT(e.nfields, SG_MAX_FIELDS);
    char s[16] = "a\x1b[31mb";
    sg_sanitize(s);
    CHECK_STR(s, "a?[31mb");
}

void suite_parse(void)
{
    RUN(test_os_release_ubuntu);
    RUN(test_os_release_arch);
    RUN(test_os_release_fedora);
    RUN(test_os_release_alpine);
    RUN(test_os_release_edge_cases);
    RUN(test_meminfo_with_available);
    RUN(test_meminfo_without_available);
    RUN(test_meminfo_edge_cases);
    RUN(test_cpuinfo_intel);
    RUN(test_cpuinfo_amd);
    RUN(test_cpuinfo_arm64_no_model);
    RUN(test_cpuinfo_armv7);
    RUN(test_cpuinfo_edge_cases);
    RUN(test_clean_cpu_name);
    RUN(test_uptime);
    RUN(test_loadavg);
    RUN(test_mount_line);
    RUN(test_format_bytes);
    RUN(test_format_duration);
    RUN(test_pci_ids);
    RUN(test_stat_ppid);
    RUN(test_json_escape);
    RUN(test_entry_fields);
}
