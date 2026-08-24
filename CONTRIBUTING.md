# Contributing to sysglance

Thanks for helping! sysglance aims to stay small, fast and dependency-free.

## Ground rules

* C11, no external libraries, no `fork`/`exec`/`popen` in any module.
  If information is only available by running a program, it doesn't belong
  in the default path.
* Every file read goes through `sg_slurp()` / `sg_read_line()` with an
  absolute path such as `/proc/meminfo`. That keeps `--root` working, which
  is what the tests rely on.
* Keep parsing in `src/parse.c` as pure functions over strings, so they can
  be unit tested and fuzzed. Collectors in `src/modules.c` should mostly glue
  file reads to parsers.
* Treat all file contents as untrusted: bounded copies, no assumptions about
  field order, never trust lengths. Text output is passed through
  `sg_sanitize()` and JSON through `sg_json_escape()`.
* Warnings are errors (`-Wall -Wextra -Werror` and more). Code must build with
  both gcc and clang.
* Logos must be original artwork. Edit `scripts/genlogo.py` and run
  `python3 scripts/genlogo.py` to regenerate `src/logo_data.inc`.

## Adding a module

1. Write any parsing as a pure function in `src/parse.c`, declared in
   `src/sg.h`.
2. Add a collector `static int m_<name>(sg_entry *out, int max)` in
   `src/modules.c`. Return the number of entries filled (0 = unavailable).
   Fill `label`, a human readable `value`, and structured fields via
   `sg_field_str/int/dbl` for JSON.
3. Register it in the `sg_modules[]` table (and in `sg_default_modules` if
   it should show by default).
4. Add fixture files under `tests/fixtures/<distro>/...`, unit tests in
   `tests/unit/`, and document it in `README.md` and `man/sysglance.1`.
   The integration suite automatically exercises every listed module.

## Before sending a change

```sh
make check-compilers   # gcc + clang, unit + integration
make sanitize-test     # ASan + UBSan + LSan
make valgrind-test     # valgrind memcheck, zero errors/leaks
make fuzz FUZZ_TIME=60 # if you touched a parser (needs clang + compiler-rt)
make bench             # make sure it is still fast
```

Please keep commits focused and describe *why* in the message.
