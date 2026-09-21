# SPDX-License-Identifier: Apache-2.0 OR MIT

CC ?= cc
PKG_CONFIG ?= pkg-config
PROJECT_CPPFLAGS := -Iinclude -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_FILE_OFFSET_BITS=64
CFLAGS ?= -O2 -g
WARNINGS := -Wall -Wextra -Wpedantic -Werror

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

PROGRAM := build/oms
GUI := build/oms-gui
GUI_PACKAGES := gtk+-3.0 json-glib-1.0
GUI_CFLAGS = $(shell $(PKG_CONFIG) --cflags $(GUI_PACKAGES))
GUI_LIBS = $(shell $(PKG_CONFIG) --libs $(GUI_PACKAGES))
SOURCES := src/main.c src/device.c src/erase.c
OBJECTS := $(SOURCES:src/%.c=build/%.o)

.PHONY: all clean install test test-gui run demo

all: $(PROGRAM) $(GUI)

cli: $(PROGRAM)

$(GUI): src/gui.c src/workspace.c include/workspace.h include/oms.h Makefile | build
	$(CC) $(PROJECT_CPPFLAGS) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(GUI_CFLAGS) -std=c11 src/gui.c src/workspace.c $(LDFLAGS) $(GUI_LIBS) $(LDLIBS) -o $@

$(PROGRAM): $(OBJECTS)
	$(CC) $(CFLAGS) $(WARNINGS) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

build/%.o: src/%.c include/oms.h Makefile | build
	$(CC) $(PROJECT_CPPFLAGS) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -c $< -o $@

build:
	mkdir -p $@

install: all
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 0755 $(PROGRAM) "$(DESTDIR)$(BINDIR)/oms"
	install -m 0755 $(GUI) "$(DESTDIR)$(BINDIR)/oms-gui"

run: all
	./$(GUI)

demo: all
	./$(GUI) --demo

build/device-probe: tests/device_probe.c src/device.c include/oms.h Makefile | build
	$(CC) $(PROJECT_CPPFLAGS) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 $< $(LDFLAGS) -o $@

build/oms-faults: $(OBJECTS) tests/io_faults.c Makefile
	$(CC) $(PROJECT_CPPFLAGS) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 $(OBJECTS) tests/io_faults.c $(LDFLAGS) \
		-Wl,--wrap=pwrite64,--wrap=pread64,--wrap=__pread64_chk,--wrap=fsync,--wrap=close,--wrap=fstat64 -o $@

build/test-native: tests/test_native.c src/workspace.c include/workspace.h Makefile | build
	$(CC) $(PROJECT_CPPFLAGS) $(CPPFLAGS) -DOMS_QUERY_TIMEOUT_MS=2000 $(CFLAGS) $(WARNINGS) $(GUI_CFLAGS) -std=c11 tests/test_native.c src/workspace.c $(LDFLAGS) $(GUI_LIBS) -o $@

build/test-gui: tests/test_gui.c src/gui.c src/workspace.c include/workspace.h Makefile | build
	$(CC) $(PROJECT_CPPFLAGS) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(GUI_CFLAGS) -std=c11 tests/test_gui.c src/workspace.c $(LDFLAGS) $(GUI_LIBS) -o $@

test: all build/device-probe build/oms-faults build/test-native
	sh tests/test_cli.sh ./$(PROGRAM)
	./build/test-native
	sh tests/test_devices.sh

test-gui: all build/test-gui
	NO_AT_BRIDGE=1 xvfb-run -a -s '-screen 0 1280x900x24' ./build/test-gui

clean:
	rm -rf build
