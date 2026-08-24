#!/usr/bin/env bash
# Benchmark sysglance. Uses hyperfine when available, otherwise the bundled
# fork/exec + clock_gettime harness (tests/bench.c).
# Usage: scripts/bench.sh path/to/sysglance path/to/bench-harness
set -eu
BIN=$1
HARNESS=$2
RUNS=${RUNS:-500}
export XDG_CONFIG_HOME=/nonexistent

run_case() { # label args...
    local label=$1; shift
    if command -v hyperfine >/dev/null 2>&1; then
        hyperfine -N --warmup 20 --runs "$RUNS" --export-markdown /dev/null \
            "$BIN $*" 2>/dev/null | sed -n '1,3p' | sed "s/^/[$label] /"
    else
        printf '%-28s ' "[$label]"
        "$HARNESS" "$RUNS" "$BIN" "$@"
    fi
}

echo "sysglance benchmark ($(nproc) CPUs, $RUNS runs each)"
command -v hyperfine >/dev/null 2>&1 || echo "(hyperfine not found; using bundled clock_gettime harness)"
run_case "default (all modules)" --no-config
run_case "--json" --no-config --json
run_case "--no-logo --no-color" --no-config --no-logo --no-color
run_case "--version (process floor)" --version
if [ -x /bin/true ]; then
    printf '%-28s ' "[reference: /bin/true]"
    "$HARNESS" "$RUNS" /bin/true
fi
