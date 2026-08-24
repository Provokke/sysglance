# sysglance - Makefile
# SPDX-License-Identifier: MIT

PREFIX  ?= /usr/local
DESTDIR ?=
BINDIR  ?= $(PREFIX)/bin
MANDIR  ?= $(PREFIX)/share/man/man1
BUILD   ?= build

OPT      ?= -O2
WARN     := -std=c11 -Wall -Wextra -Werror -Wshadow -Wpointer-arith \
            -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wundef
CFLAGS   ?=
LDFLAGS  ?=
LDLIBS   ?= -lm
ALL_CFLAGS := $(WARN) $(OPT) $(CFLAGS) -Isrc

LIB_SRC := src/util.c src/parse.c src/config.c src/modules.c src/logo.c
LIB_OBJ := $(LIB_SRC:src/%.c=$(BUILD)/%.o)
TEST_SRC := tests/unit/test_main.c tests/unit/test_parse.c tests/unit/test_config.c \
            tests/unit/test_modules.c
TEST_OBJ := $(TEST_SRC:tests/unit/%.c=$(BUILD)/test/%.o)

BIN      := $(BUILD)/sysglance
TEST_BIN := $(BUILD)/test_unit
BENCH_BIN := $(BUILD)/bench

SAN_FLAGS := -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
             -fno-sanitize-recover=all
VALGRIND  ?= valgrind
VG_FLAGS  := --quiet --error-exitcode=99 --leak-check=full --show-leak-kinds=all \
             --errors-for-leak-kinds=all --track-origins=yes
FUZZ_TIME ?= 60

.PHONY: all test unit integration sanitize-test valgrind-test fuzz bench \
        check-compilers install uninstall clean

all: $(BIN)

$(BUILD)/%.o: src/%.c src/sg.h src/logo_data.inc
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BUILD)/test/%.o: tests/unit/%.c tests/unit/test.h src/sg.h
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -Itests/unit -c $< -o $@

$(BUILD)/libsg.a: $(LIB_OBJ)
	$(AR) rcs $@ $^

$(BIN): $(BUILD)/main.o $(BUILD)/libsg.a
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(TEST_BIN): $(TEST_OBJ) $(BUILD)/libsg.a
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BENCH_BIN): tests/bench.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -D_POSIX_C_SOURCE=200809L $(LDFLAGS) -o $@ $<

# ---------------------------------------------------------------- tests

unit: $(TEST_BIN)
	$(TEST_WRAP) ./$(TEST_BIN) tests/fixtures

integration: $(BIN)
	SG_WRAP="$(TEST_WRAP)" tests/integration/run.sh ./$(BIN)

test: unit integration

sanitize-test:
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
	UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
	$(MAKE) BUILD=build-san OPT="$(SAN_FLAGS)" LDFLAGS="-fsanitize=address,undefined" test

valgrind-test:
	$(MAKE) BUILD=build-vg OPT="-O0 -g" TEST_WRAP="$(VALGRIND) $(VG_FLAGS)" test

check-compilers:
	$(MAKE) BUILD=build-gcc CC=gcc test
	$(MAKE) BUILD=build-clang CC=clang test

fuzz:
	@mkdir -p build-fuzz fuzz/corpus-work
	clang $(WARN) -O1 -g -Isrc -fsanitize=fuzzer,address,undefined \
	    fuzz/fuzz_config.c src/config.c src/util.c src/parse.c -o build-fuzz/fuzz_config
	cp -n fuzz/corpus/* fuzz/corpus-work/ 2>/dev/null || true
	./build-fuzz/fuzz_config -max_total_time=$(FUZZ_TIME) -max_len=4096 \
	    fuzz/corpus-work fuzz/corpus

bench: $(BIN) $(BENCH_BIN)
	@scripts/bench.sh ./$(BIN) ./$(BENCH_BIN)

# -------------------------------------------------------------- install

install: $(BIN)
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(MANDIR)
	install -m 755 $(BIN) $(DESTDIR)$(BINDIR)/sysglance
	install -m 644 man/sysglance.1 $(DESTDIR)$(MANDIR)/sysglance.1

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/sysglance $(DESTDIR)$(MANDIR)/sysglance.1

clean:
	rm -rf build build-san build-vg build-gcc build-clang build-fuzz build-cmake fuzz/corpus-work
