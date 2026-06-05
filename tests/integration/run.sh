#!/usr/bin/env bash
# Integration tests for the sysglance binary.
# Usage: tests/integration/run.sh path/to/sysglance
# SG_WRAP may hold a command prefix (e.g. valgrind ...) applied to every run.
set -u

BIN=${1:?usage: run.sh BINARY}
HERE=$(cd "$(dirname "$0")" && pwd)
FIX=$(cd "$HERE/../fixtures" && pwd)
WRAP=${SG_WRAP:-}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# isolate from the developer's environment
export XDG_CONFIG_HOME="$TMP/xdg"
unset NO_COLOR SYSGLANCE_ROOT
export USER=tester

pass=0 fail=0
ok()  { pass=$((pass + 1)); printf 'ok     - %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'not ok - %s\n' "$1"; [ -n "${2:-}" ] && printf '         %s\n' "$2"; }

# run ARGS... -> sets $rc, output in $TMP/out, $TMP/err
run() { $WRAP "$BIN" "$@" >"$TMP/out" 2>"$TMP/err"; rc=$?; }

expect_rc() { # name rc
    if [ "$rc" -eq "$2" ]; then ok "$1"; else bad "$1" "exit $rc, expected $2; stderr: $(head -c 300 "$TMP/err")"; fi
}
has_esc() { grep -q $'\033' "$TMP/out"; }
json_ok() { python3 -I "$HERE/check_json.py" "$TMP/out" "$@" 2>"$TMP/jerr"; }

# ------------------------------------------------------------- basics
run --version; expect_rc "--version exits 0" 0
grep -Eq '^sysglance [0-9]+\.[0-9]+\.[0-9]+$' "$TMP/out" && ok "--version format" || bad "--version format"
run -V; expect_rc "-V exits 0" 0
run --help; expect_rc "--help exits 0" 0
grep -q '^Usage: sysglance' "$TMP/out" && ok "--help shows usage" || bad "--help shows usage"
for opt in --modules --list-modules --json --no-color --no-logo --config --root --version; do
    grep -q -- "$opt" "$TMP/out" && ok "--help documents $opt" || bad "--help documents $opt"
done

run --list-modules; expect_rc "--list-modules exits 0" 0
MODULES=$(awk '{print $1}' "$TMP/out")
nmod=$(echo "$MODULES" | wc -l)
[ "$nmod" -ge 18 ] && ok "--list-modules lists $nmod modules" || bad "--list-modules count $nmod"
run --list-logos; expect_rc "--list-logos exits 0" 0
for l in sysglance ubuntu arch fedora alpine debian; do
    grep -q "^$l " "$TMP/out" && ok "logo '$l' listed" || bad "logo '$l' listed"
done

# ------------------------------------------------------------- default run
run; expect_rc "default run exits 0" 0
[ -s "$TMP/out" ] && ok "default run prints output" || bad "default run prints output"
has_esc && bad "non-TTY output has no escape codes" || ok "non-TTY output has no escape codes"
grep -q '^\s*$' "$TMP/err" || [ ! -s "$TMP/err" ] && ok "default run has empty stderr" || bad "default run has empty stderr" "$(head -c 300 "$TMP/err")"

# ------------------------------------------------------------- color handling
run --color; expect_rc "--color exits 0" 0
has_esc && ok "--color forces escape codes" || bad "--color forces escape codes"
run --color --no-color; has_esc && bad "--no-color after --color disables color" || ok "--no-color after --color disables color"
run --no-color; has_esc && bad "--no-color has no escape codes" || ok "--no-color has no escape codes"
printf '[display]\ncolor = true\n' > "$TMP/color.ini"
run --config "$TMP/color.ini"; has_esc && ok "config color=true enables color" || bad "config color=true enables color"
NO_COLOR=1 $WRAP "$BIN" --config "$TMP/color.ini" >"$TMP/out" 2>&1
has_esc && bad "NO_COLOR overrides config color=true" || ok "NO_COLOR overrides config color=true"
NO_COLOR=1 $WRAP "$BIN" --color >"$TMP/out" 2>&1
has_esc && ok "explicit --color overrides NO_COLOR" || bad "explicit --color overrides NO_COLOR"
NO_COLOR= $WRAP "$BIN" --config "$TMP/color.ini" >"$TMP/out" 2>&1
has_esc && ok "empty NO_COLOR is ignored" || bad "empty NO_COLOR is ignored"
if command -v script >/dev/null 2>&1; then
    script -qec "$WRAP $BIN --modules os" /dev/null >"$TMP/out" 2>&1 </dev/null
    has_esc && ok "TTY output is colored (via script)" || bad "TTY output is colored (via script)"
    NO_COLOR=1 script -qec "$WRAP $BIN --modules os" /dev/null >"$TMP/out" 2>&1 </dev/null
    has_esc && bad "NO_COLOR disables color on a TTY" || ok "NO_COLOR disables color on a TTY"
fi

# ------------------------------------------------------------- JSON
run --json; expect_rc "--json exits 0" 0
python3 -I -m json.tool "$TMP/out" >/dev/null 2>&1 && ok "--json parses with json.tool" || bad "--json parses with json.tool"
json_ok && ok "--json matches schema" || bad "--json matches schema" "$(cat "$TMP/jerr")"
has_esc && bad "--json has no escape codes" || ok "--json has no escape codes"
run --json --color; has_esc && bad "--json --color still has no escape codes" || ok "--json --color still has no escape codes"

# every module, alone, in both modes
for m in $MODULES; do
    run --json --modules "$m"
    if [ "$rc" -eq 0 ] && json_ok "$m"; then ok "module $m: valid JSON"; else bad "module $m: valid JSON" "rc=$rc $(cat "$TMP/jerr" "$TMP/err")"; fi
    run --no-color --modules "$m"
    if [ "$rc" -eq 0 ] && ! has_esc; then ok "module $m: text mode"; else bad "module $m: text mode" "rc=$rc"; fi
done

# all modules at once, ordering preserved
ALL=$(echo $MODULES | tr ' ' ',')
run --json --modules "$ALL"; json_ok "$ALL" && ok "all modules in requested order" || bad "all modules in requested order" "$(cat "$TMP/jerr")"
run --json --modules "kernel,os,kernel"; json_ok "kernel,os,kernel" && ok "custom order with duplicates" || bad "custom order with duplicates"
run --json --modules=" cpu , memory"; json_ok "cpu,memory" && ok "--modules= form with spaces" || bad "--modules= form with spaces"
run -m uptime -j; json_ok "uptime" && ok "short options -m/-j" || bad "short options -m/-j"

# text order
run --no-logo --modules kernel,os
head -1 "$TMP/out" | grep -q '^Kernel: ' && sed -n 2p "$TMP/out" | grep -q '^OS: ' && ok "text output honors module order" || bad "text output honors module order" "$(cat "$TMP/out")"

# ------------------------------------------------------------- logo
run --no-logo --modules title,kernel
head -1 "$TMP/out" | grep -q '^tester@' && ok "--no-logo starts with title" || bad "--no-logo starts with title" "$(head -1 "$TMP/out")"
sed -n 2p "$TMP/out" | grep -q '^-\+$' && ok "title is underlined" || bad "title is underlined"
run --logo arch --modules kernel; expect_rc "--logo arch exits 0" 0
head -1 "$TMP/out" | grep -q '^            \.' && ok "--logo arch draws arch art" || bad "--logo arch draws arch art" "$(head -2 "$TMP/out")"
run --logo none --modules kernel; head -1 "$TMP/out" | grep -q '^Kernel' && ok "--logo none disables logo" || bad "--logo none disables logo"
run --logo=fedora --modules kernel; expect_rc "--logo=NAME form" 0

# ------------------------------------------------------------- fixture roots
run --root "$FIX/ubuntu" --json --modules os,kernel,cpu,memory,swap,packages,battery,gpu,uptime,loadavg,processes,title
expect_rc "--root ubuntu exits 0" 0
json_ok && ok "--root ubuntu JSON schema" || bad "--root ubuntu JSON schema"
python3 -I - "$TMP/out" <<'PY' && ok "--root ubuntu values" || bad "--root ubuntu values"
import json, sys
d = {m["name"]: m for m in json.load(open(sys.argv[1]))["modules"]}
v = lambda n: d[n]["entries"][0]["value"]
assert v("os").startswith("Ubuntu 24.04.1 LTS"), v("os")
assert v("kernel") == "Linux 6.8.0-45-generic", v("kernel")
assert v("cpu") == "11th Gen Intel Core i7-1165G7 (4C/8T) @ 4.70 GHz", v("cpu")
assert v("memory") == "3.89 GiB / 15.55 GiB (25%)", v("memory")
assert d["memory"]["entries"][0]["data"]["total_bytes"] == 16303428 * 1024
assert v("swap") == "1.00 GiB / 4.00 GiB (25%)", v("swap")
assert v("packages") == "5 (dpkg)", v("packages")
assert v("battery") == "87% [Discharging]", v("battery")
assert v("gpu") == "Intel TigerLake-LP GT2 [Iris Xe Graphics]", v("gpu")
assert v("uptime") == "1 day, 2 hours, 3 mins", v("uptime")
assert v("loadavg") == "0.52, 0.58, 0.59"
assert v("processes") == "4"
assert v("title") == "tester@fixture-ubuntu", v("title")
PY
SYSGLANCE_ROOT="$FIX/arch" $WRAP "$BIN" --json --modules os,cpu,packages,swap >"$TMP/out" 2>"$TMP/err"
python3 -I - "$TMP/out" <<'PY' && ok "SYSGLANCE_ROOT env (arch)" || bad "SYSGLANCE_ROOT env (arch)"
import json, sys
d = {m["name"]: m["entries"][0]["value"] for m in json.load(open(sys.argv[1]))["modules"]}
assert d["os"].startswith("Arch Linux"), d
assert d["cpu"] == "AMD Ryzen 7 5800X (8C/16T) @ 3.80 GHz", d
assert d["packages"] == "3 (pacman), 1 (flatpak)", d
assert d["swap"] == "Disabled", d
PY
for distro in ubuntu arch fedora alpine; do
    run --root "$FIX/$distro" --color --modules os
    case $distro in
        ubuntu) want='33' ;; arch) want='36' ;; fedora) want='34' ;; alpine) want='34' ;;
    esac
    grep -q $'\033\\[1;'"$want"'m' "$TMP/out" && ok "auto logo color for $distro" || bad "auto logo color for $distro"
    run --root "$FIX/$distro" --json
    json_ok && ok "full JSON for $distro fixture" || bad "full JSON for $distro fixture" "$(cat "$TMP/jerr")"
done
run --root "$FIX/fedora" --no-color --modules os,cpu,host,gpu --no-logo
grep -q '^OS: Fedora Linux 40 (Workstation Edition) aarch64$' "$TMP/out" && grep -q '^CPU: ARM Cortex-A76 (4)$' "$TMP/out" && grep -q '^Host: Raspberry Pi 5' "$TMP/out" && ok "fedora aarch64 text output" || bad "fedora aarch64 text output" "$(cat "$TMP/out")"
run --root "$FIX/alpine" --no-color --no-logo --modules packages
grep -q '^Packages: 3 (apk)$' "$TMP/out" && ok "alpine apk count" || bad "alpine apk count"

# missing / malformed files must degrade gracefully
run --root "$TMP/does-not-exist" --json; expect_rc "nonexistent root exits 0" 0
json_ok && ok "nonexistent root still valid JSON" || bad "nonexistent root still valid JSON"
python3 -I -c 'import json,sys; d={m["name"]:m for m in json.load(open(sys.argv[1]))["modules"]}; assert not d["os"]["available"] and not d["memory"]["available"] and not d["cpu"]["available"]' "$TMP/out" \
  && ok "missing files reported as unavailable" || bad "missing files reported as unavailable"
run --root "$TMP/does-not-exist" --no-color; expect_rc "nonexistent root text mode exits 0" 0
run --root "$FIX/malformed" --json; expect_rc "malformed root exits 0" 0
json_ok && ok "malformed root still valid JSON" || bad "malformed root still valid JSON"
run --root "$FIX/malformed"; expect_rc "malformed root text mode exits 0" 0

# hostile os-release: control chars and quotes must not break JSON or inject escapes
mkdir -p "$TMP/evil/etc"
printf 'PRETTY_NAME="Evil \033[31mRed\033[0m \\"quoted\\" \\\\ tab\there"\nID=evil\n' > "$TMP/evil/etc/os-release"
run --root "$TMP/evil" --json --modules os; json_ok && ok "hostile os-release -> valid JSON" || bad "hostile os-release -> valid JSON" "$(cat "$TMP/out")"
run --root "$TMP/evil" --no-color --modules os; has_esc && bad "hostile os-release escapes sanitized" || ok "hostile os-release escapes sanitized"

# ------------------------------------------------------------- config
run --config "$FIX/configs/full.ini"; expect_rc "full config exits 0" 0
head -1 "$TMP/out" | grep -q '^OS -> ' && ok "config separator + module list + no logo" || bad "config separator + module list + no logo" "$(head -2 "$TMP/out")"
[ "$(wc -l < "$TMP/out")" -eq 4 ] && ok "config module list size" || bad "config module list size"
run --config "$FIX/configs/full.ini" --modules kernel; [ "$(wc -l < "$TMP/out")" -eq 1 ] && ok "--modules overrides config" || bad "--modules overrides config"
run --config "$FIX/configs/bad.ini"; expect_rc "bad config exits 1" 1
grep -q 'line 4' "$TMP/err" && ok "bad config error names line" || bad "bad config error names line"
run --config "$TMP/nope.ini"; expect_rc "missing --config file exits 1" 1
grep -q 'cannot read config' "$TMP/err" && ok "missing config error message" || bad "missing config error message"
mkdir -p "$XDG_CONFIG_HOME/sysglance"
printf '[display]\nlogo = false\nseparator = " | "\n[modules]\nlist = kernel\n' > "$XDG_CONFIG_HOME/sysglance/config.ini"
run; grep -q '^Kernel | ' "$TMP/out" && ok "default XDG config is loaded" || bad "default XDG config is loaded" "$(cat "$TMP/out")"
run --no-config --no-logo --modules kernel; grep -q '^Kernel: ' "$TMP/out" && ok "--no-config ignores default config" || bad "--no-config ignores default config"
printf '[display]\nbogus = 1\n' > "$XDG_CONFIG_HOME/sysglance/config.ini"
run; expect_rc "broken default config exits 1" 1
rm -f "$XDG_CONFIG_HOME/sysglance/config.ini"
printf '[display]\njson = true\n' > "$TMP/json.ini"
run --config "$TMP/json.ini" --modules os; json_ok "os" && ok "config json = true" || bad "config json = true"

# ------------------------------------------------------------- error handling
run --bogus; expect_rc "unknown flag exits 2" 2
grep -q "unknown option '--bogus'" "$TMP/err" && ok "unknown flag message" || bad "unknown flag message"
[ ! -s "$TMP/out" ] && ok "unknown flag prints nothing on stdout" || bad "unknown flag prints nothing on stdout"
run --modules; expect_rc "--modules without argument exits 2" 2
run --config; expect_rc "--config without argument exits 2" 2
run --root; expect_rc "--root without argument exits 2" 2
run --modules os,nosuchmodule; expect_rc "unknown module exits 2" 2
grep -q "unknown module 'nosuchmodule'" "$TMP/err" && ok "unknown module message" || bad "unknown module message"
run --modules ""; expect_rc "empty module list exits 2" 2
run --modules ",,,"; expect_rc "only-commas module list exits 2" 2
run --logo nosuchlogo; expect_rc "unknown logo exits 2" 2
long=$(printf 'os,%.0s' $(seq 1 64))os
run --modules "$long"; expect_rc "more than 64 modules rejected" 2
run --modules "$(printf 'kernel,%.0s' $(seq 1 63))kernel" --json; expect_rc "64 modules accepted" 0
huge=$(head -c 3000 /dev/zero | tr '\0' 'x')
run --modules "$huge"; expect_rc "huge module list rejected" 2
if [ -w /dev/full ]; then
    $WRAP "$BIN" >/dev/full 2>"$TMP/err"; rc=$?
    expect_rc "write error (/dev/full) exits 1" 1
fi
run positional; expect_rc "positional argument rejected" 2

echo
echo "integration tests: $((pass + fail)) checks, $pass passed, $fail failed"
[ "$fail" -eq 0 ]
