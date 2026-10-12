<div align="center">

# sysglance

A lightning-fast system information tool for Linux, written in C11.

![License](https://img.shields.io/badge/LICENSE-MIT-blue?style=for-the-badge)  ![Language](https://img.shields.io/badge/C11-555555?style=for-the-badge)

Refer to [LICENSE](LICENSE) for the legal bits.

</div>

## Sample output

Real output from the (containerised) machine this README was written on,
`sysglance --no-config` (colors are stripped here; on a terminal the logo
and keys are colored in the distribution's colors):

```
           ___         root@vm
      .--(   )--.      -------
    .'    '-'    '.    OS: Ubuntu 24.04.5 LTS x86_64
   /    .-----.    \   Kernel: Linux 6.18.44-fc-v236
 (   |  /       \  |   Uptime: 21 mins
(   )  |       |   |   Packages: 914 (dpkg)
 '-' \  \       /  /   Shell: bash
     '. '-----' .___   Terminal: xterm-256color
       '-.....-(   )   CPU: Intel Xeon Processor (4) @ 2.30 GHz
                '-'    Memory: 1.03 GiB / 15.71 GiB (7%)
                       Swap: Disabled
                       Disk (/): 13.96 GiB / 251.97 GiB (36%) - ext4
                       Disk (/opt/app): 320.08 MiB / 353.23 MiB (92%) - ext4
                       Disk (/opt/runner): 31.84 MiB / 48.48 MiB (67%) - ext4
                       Local IP (eth0): 192.0.2.2/24
                       Load: 2.54, 1.69, 0.95
                       Processes: 90
                       Locale: C
```

Pointed at the bundled Ubuntu laptop fixture tree
(`sysglance --root tests/fixtures/ubuntu`):

```
           ___         root@fixture-ubuntu
      .--(   )--.      -------------------
    .'    '-'    '.    OS: Ubuntu 24.04.1 LTS x86_64
   /    .-----.    \   Host: XPS 13 9310
 (   |  /       \  |   Kernel: Linux 6.8.0-45-generic
(   )  |       |   |   Uptime: 1 day, 2 hours, 3 mins
 '-' \  \       /  /   Packages: 5 (dpkg)
     '. '-----' .___   CPU: 11th Gen Intel Core i7-1165G7 (4C/8T) @ 4.70 GHz
       '-.....-(   )   GPU: Intel TigerLake-LP GT2 [Iris Xe Graphics]
                '-'    Memory: 3.89 GiB / 15.55 GiB (25%)
                       Swap: 1.00 GiB / 4.00 GiB (25%)
                       Battery (BAT0): 87% [Discharging]
                       Load: 0.52, 0.58, 0.59
```

Alpine on a Raspberry Pi fixture:

```
              .                   root@alpine-pi
             / \        .         --------------
            /   \      / \        OS: Alpine Linux v3.20 armv7l
           /  /\ \    /   \       Kernel: Linux 6.6.31-0-rpi
          /  /  \ \  /     \      CPU: ARMv7 Processor rev 4 (v7l) (2)
         /  /    \ \/  /\   \     Memory: 242.48 MiB / 926.08 MiB (26%)
        /  /      \   /  \   \    Swap: 50.00 MiB / 100.00 MiB (50%)
       /__/________\_/____\___\   Packages: 3 (apk)
```

`--json` output (`sysglance --json --modules os,memory`):

```json
{
  "sysglance": "0.1.0",
  "modules": [
    {
      "name": "os",
      "available": true,
      "entries": [
        {
          "label": "OS",
          "value": "Ubuntu 24.04.5 LTS x86_64",
          "data": {
            "name": "Ubuntu",
            "pretty_name": "Ubuntu 24.04.5 LTS",
            "id": "ubuntu",
            "id_like": "debian",
            "version_id": "24.04",
            "arch": "x86_64"
          }
        }
      ]
    },
    {
      "name": "memory",
      "available": true,
      "entries": [
        {
          "label": "Memory",
          "value": "1.03 GiB / 15.71 GiB (7%)",
          "data": {
            "used_bytes": 1109676032,
            "total_bytes": 16867418112,
            "percent": 7
          }
        }
      ]
    }
  ]
}
```

## 🚀 Quick start

Requirements: a C11 compiler (gcc or clang), GNU make, Linux.

```sh
make -j2                     # builds ./build/sysglance with -Wall -Wextra -Werror -O2
./build/sysglance
make install PREFIX=/usr/local       # binary + man page (honours DESTDIR)
make uninstall PREFIX=/usr/local
```

An optional CMake build is also provided:

```sh
cmake -S . -B build-cmake && cmake --build build-cmake && ctest --test-dir build-cmake
```

### CLI

```
Usage: sysglance [OPTIONS]

  -m, --modules LIST   comma separated modules to show, in order
  -l, --list-modules   list available modules and exit
      --list-logos     list available logos and exit
  -j, --json           print machine readable JSON
      --no-color       disable colors (also: NO_COLOR env, non-TTY)
      --color          force colors even when not on a TTY
      --no-logo        do not print the logo
      --logo NAME      use a specific logo (see --list-logos)
  -c, --config FILE    read configuration from FILE
      --no-config      ignore the default configuration file
      --root DIR       read /proc, /sys, /etc ... below DIR (testing)
  -V, --version        print version and exit
  -h, --help           print this help and exit
```

Examples:

```sh
sysglance --modules os,kernel,cpu,memory --no-logo
sysglance --json | python3 -m json.tool
sysglance --logo alpine
NO_COLOR=1 sysglance
```

Colors are enabled only when stdout is a terminal, `TERM` is not `dumb` and
`NO_COLOR` is unset/empty. `--color` overrides all of that; `--no-color` wins
over everything.

Exit status: `0` success, `1` configuration or write error, `2` usage error
(unknown option, module or logo).

### Modules

| module      | source |
|-------------|--------|
| `title`     | `$USER` (fallback `getpwuid`) + `/proc/sys/kernel/hostname` |
| `os`        | `/etc/os-release` (fallback `/usr/lib/os-release`), `kernel.arch` sysctl / `uname` |
| `host`      | DMI `product_name`/`product_version`, or `/proc/device-tree/model` |
| `kernel`    | `/proc/sys/kernel/{ostype,osrelease}` (fallback `uname`) |
| `uptime`    | `/proc/uptime` |
| `packages`  | dpkg (`/var/lib/dpkg/info/*.list`), pacman (`/var/lib/pacman/local`), apk (`/lib/apk/db/installed`), flatpak, snap |
| `shell`     | `$SHELL` (fallback passwd entry) |
| `terminal`  | `$TERM_PROGRAM`, else walks parent processes via `/proc/<pid>/stat`, else `$TERM` |
| `cpu`       | `/proc/cpuinfo` (x86, ARM, POWER, RISC-V), `cpufreq/cpuinfo_max_freq` |
| `gpu`       | PCI display-class devices in `/sys/bus/pci/devices`, names from `pci.ids` if installed; DRM driver for SoC GPUs |
| `memory`    | `/proc/meminfo` (`MemTotal - MemAvailable`, with a fallback formula for old kernels) |
| `swap`      | `/proc/meminfo` |
| `disk`      | `/proc/mounts` + `statvfs` for real filesystems (ext4, btrfs, xfs, ...) and `/` |
| `localip`   | `getifaddrs`, IPv4, non-loopback, up |
| `battery`   | `/sys/class/power_supply/*` with `type == Battery` |
| `loadavg`   | `/proc/loadavg` |
| `processes` | number of numeric entries in `/proc` |
| `locale`    | `LC_ALL`, `LC_MESSAGES`, `LANG` |
| `user`, `hostname` | stand-alone versions of the title parts |

Modules without data (e.g. `battery` on a desktop) are omitted from text
output and reported as `"available": false` in JSON.

### JSON schema

```
{ "sysglance": "<version>",
  "modules": [ { "name": str, "available": bool,
                 "entries": [ { "label": str, "value": str,
                                "data": { <key>: str | number | null } } ] } ] }
```

`value` is the human-readable string; `data` holds structured values such as
`total_bytes`, `percent`, `cores_logical`. The schema is enforced by
`tests/integration/check_json.py`.

## ⚙️ Configuration

Optional file at `$XDG_CONFIG_HOME/sysglance/config.ini` (or
`~/.config/sysglance/config.ini`), or any file given with `--config`.
Command-line options take precedence.

```ini
# Lines starting with # or ; are comments; inline comments need a space before them.
[display]
color = auto            # auto | true | false   (also yes/no/on/off/1/0)
logo = true             # show the logo
logo_name = auto        # auto | none | sysglance | ubuntu | arch | fedora | alpine | debian | tux
separator = ": "        # quote to keep spaces; max 15 bytes
key_color = auto        # auto | black | red | green | yellow | blue | magenta | cyan | white
                        #   or bright-<color>
json = false            # default to JSON output

[modules]
list = title, os, kernel, uptime, cpu, memory, disk
```

Sections and keys are case-insensitive. Any unknown section, key or invalid
value is a hard error that names the line (`config.ini: line 4: invalid
boolean for logo`) so typos never go unnoticed.

## ⏱️ Benchmarks

`make bench` runs [hyperfine](https://github.com/sharkdp/hyperfine) when it is
installed and otherwise falls back to the bundled fork/exec +
`clock_gettime` harness (`tests/bench.c`). Results on the 4-vCPU Xeon VM used
for development (shared with other builds at the time, `-O2`, gcc 13):

| command | mean | notes |
|---------|------|-------|
| `sysglance` (all 18 default modules) | **1.8 ms ± 0.3** | hyperfine, 500 runs |
| `sysglance --json` | 1.8 ms ± 0.3 | hyperfine, 500 runs |
| `sysglance --no-logo --no-color` | 1.7 ms ± 0.2 | hyperfine, 500 runs |
| `sysglance --version` (process floor) | 1.0 ms ± 0.3 | hyperfine, 500 runs |
| `/bin/true` (reference) | 0.92 ms | bundled harness |
| `sysglance` | median 1.68 ms, p95 2.24 ms | bundled harness, 1000 runs |

The target was < 5 ms; the actual information gathering costs well under
1 ms. Things that keep it fast: no `fork`/`exec`, no `getpwuid` (NSS) unless
`$USER` is unset, package counting by directory listing instead of parsing
package databases, and a single pass over each `/proc` file.

## 🧪 Testing

```sh
make test             # unit tests + integration tests
make sanitize-test    # the same suite built with -fsanitize=address,undefined (LSan on)
make valgrind-test    # the same suite under valgrind --leak-check=full, errors fail the run
make check-compilers  # full suite built with gcc and with clang
make fuzz             # libFuzzer (clang) on the config + /proc parsers, 60 s by default
make bench
```

* **Unit tests** (`tests/unit`, 43 tests / ~700 checks) cover every parser
  with fixture files and edge cases (missing fields, overflow-length values,
  underflow in memory math, odd `/proc/mounts` escapes, truncation of JSON
  escapes and UTF-8), the config parser (syntax and every error path), and run
  each collector module against fixture trees.
* **Fixture trees** (`tests/fixtures/{ubuntu,arch,fedora,alpine,malformed,empty}`)
  are fake root filesystems with `/proc`, `/sys` and `/etc` content for an
  x86 Ubuntu laptop (battery, Intel iGPU, dpkg), an AMD + NVIDIA Arch desktop
  (pacman, flatpak, no `MemAvailable`), a Fedora aarch64 Raspberry Pi (no
  `model name`, DRM-only GPU, `/usr/lib/os-release`) and Alpine on ARMv7 (apk).
  Any run can be pointed at one with `--root DIR` or `SYSGLANCE_ROOT=DIR`.
* **Integration tests** (`tests/integration/run.sh`, 143 checks) run the real
  binary: every flag, JSON validity (`python3 -m json.tool` and a schema
  checker) for every module, `--no-color`/`NO_COLOR`/TTY behaviour (via
  `script(1)`), module ordering, configs, fixture roots, nonexistent and
  malformed roots, hostile `os-release` content containing escape sequences,
  bad flags, unknown modules/logos, missing and broken config files, and
  `/dev/full` write errors. Any sanitizer or valgrind report fails the run.

## Logos

All logos are original ASCII drawings made for this project
(`scripts/genlogo.py` holds the source art and generates
`src/logo_data.inc`). They are matched against `ID`, then `ID_LIKE`, from
os-release. Unknown distributions get the sysglance "eye" logo.

## ⚠️ Honest limits

* Linux only. It has not been built or tested on other kernels, and would
  report very little there (everything comes from `/proc` and `/sys`).
* RPM-based package counts are not supported: the rpm database is SQLite
  (or Berkeley DB) and reading it without librpm/sqlite is out of scope, so
  Fedora/RHEL show only flatpak/snap counts (if any).
* GPU names need a `pci.ids` file (`hwdata` / `pciutils`); without it the
  vendor name and PCI id are shown, e.g. `NVIDIA [10de:2484]`.
* `--root` redirects file reads only. `uname`, `statvfs` sizes (the fixture's
  own filesystem), `getifaddrs`, environment variables and the parent-process
  walk still describe the real machine.
* Shell versions are not shown (that would require running the shell).
* Terminal detection is heuristic (parent process name).

## Licence

MIT — see [LICENSE](LICENSE).
