# SPDX-License-Identifier: Apache-2.0 OR MIT

CC ?= cc
CPPFLAGS ?= -Iinclude -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_FILE_OFFSET_BITS=64
CFLAGS ?= -O2 -g
WARNINGS := -Wall -Wextra -Wpedantic -Werror

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

PROGRAM := build/oms
SOURCES := src/main.c src/device.c src/erase.c
OBJECTS := $(SOURCES:src/%.c=build/%.o)

.PHONY: all clean install test

all: $(PROGRAM)

$(PROGRAM): $(OBJECTS)
	$(CC) $(CFLAGS) $(WARNINGS) $(OBJECTS) -o $@

build/%.o: src/%.c include/oms.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -c $< -o $@

build:
	mkdir -p $@

install: $(PROGRAM)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(PROGRAM) $(DESTDIR)$(BINDIR)/oms

test: $(PROGRAM)
	sh tests/test_cli.sh ./$(PROGRAM)

clean:
	rm -rf build
