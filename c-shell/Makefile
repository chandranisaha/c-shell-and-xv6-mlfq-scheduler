CC := gcc
CSTD := -std=c23
DEFINES := -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700
WARNINGS := -Wall -Wextra -Werror -Wno-unused-parameter -fno-asm
CFLAGS := $(CSTD) $(DEFINES) $(WARNINGS) -Iinclude

SOURCES := $(wildcard src/*.c)
OBJECTS := $(SOURCES:src/%.c=build/%.o)

.PHONY: all clean

all: shell.out

shell.out: $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build shell.out
