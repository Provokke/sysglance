/* libFuzzer harness for the config parser and all /proc, /etc text parsers. Build and run with `make fuzz` (requires clang).
 * SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sg.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    sg_config c;
    char err[128];
    sg_config_defaults(&c);
    if (sg_config_parse((const char *)data, size, &c, err, sizeof err) == 0) {
        /* invariants on success */
        if (strlen(c.separator) >= sizeof c.separator || sg_color_code(c.key_color) < 0 ||
            c.color < -1 || c.color > 1)
            abort();
    } else if (!err[0]) {
        abort(); /* failures must explain themselves */
    }

    /* The /proc and /etc text parsers take NUL-terminated input. */
    char *text = malloc(size + 1);
    if (!text)
        return 0;
    memcpy(text, data, size);
    text[size] = '\0';
    sg_osrel o;
    sg_meminfo m;
    sg_cpuinfo cpu;
    sg_mount mnt;
    double d, l[3];
    int a, b;
    char v[64], dev[64], comm[32];
    sg_parse_os_release(text, &o);
    if (sg_parse_meminfo(text, &m) == 0 && sg_mem_used_kb(&m) > m.total)
        abort();
    sg_parse_cpuinfo(text, &cpu);
    sg_parse_uptime(text, &d);
    sg_parse_loadavg(text, l, &a, &b);
    sg_parse_mount_line(text, &mnt);
    sg_pci_ids_lookup(text, 0x8086, 0x9a49, v, sizeof v, dev, sizeof dev);
    sg_parse_stat_ppid(text, comm, sizeof comm, &a);
    char esc[96];
    sg_json_escape(text, esc, sizeof esc);
    free(text);
    return 0;
}
