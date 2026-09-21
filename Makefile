# SPDX-License-Identifier: Apache-2.0 OR MIT

CC ?= cc
PROJECT_CPPFLAGS := -Iinclude -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_FILE_OFFSET_BITS=64
CFLAGS ?= -O2 -g
WARNINGS := -Wall -Wextra -Wpedantic -Werror

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

PROGRAM := build/oms
SOURCES := src/main.c src/device.c src/erase.c
OBJECTS := $(SOURCES:src/%.c=build/%.o)

.PHONY: all clean install test test-gui run demo

all: $(PROGRAM)

$(PROGRAM): $(OBJECTS)
	$(CC) $(CFLAGS) $(WARNINGS) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

build/%.o: src/%.c include/oms.h Makefile | build
	$(CC) $(PROJECT_CPPFLAGS) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -c $< -o $@

build:
	mkdir -p $@

install: $(PROGRAM)
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 0755 $(PROGRAM) "$(DESTDIR)$(BINDIR)/oms"

run: all
	python3 app/main.py

demo: all
	python3 app/main.py --demo

build/device-probe: tests/device_probe.c src/device.c include/oms.h Makefile | build
	$(CC) $(PROJECT_CPPFLAGS) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 $< $(LDFLAGS) -o $@

build/oms-faults: $(OBJECTS) tests/io_faults.c Makefile
	$(CC) $(PROJECT_CPPFLAGS) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 $(OBJECTS) tests/io_faults.c $(LDFLAGS) \
		-Wl,--wrap=pwrite64,--wrap=pread64,--wrap=__pread64_chk,--wrap=fsync,--wrap=close,--wrap=fstat64 -o $@

test: $(PROGRAM) build/device-probe build/oms-faults
	sh tests/test_cli.sh ./$(PROGRAM)
	python3 -m unittest discover -s tests -p 'test_*.py' -v

test-gui: $(PROGRAM)
	xvfb-run -a -s '-screen 0 1280x900x24' python3 -m unittest discover -s tests -p 'test_gui.py' -v

clean:
	rm -rf build
