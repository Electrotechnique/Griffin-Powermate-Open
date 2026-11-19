# Build with MinGW-w64 (Linux cross-compile or MSYS2):  make
CROSS   ?= x86_64-w64-mingw32-
CC      := $(CROSS)gcc
WINDRES := $(CROSS)windres
CFLAGS  ?= -O2 -Wall -Wextra -Wno-cast-function-type -Wno-missing-field-initializers
CFLAGS  += -municode -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -std=c11
LDFLAGS += -municode -mwindows -static -s
WINE    ?= wine
LIBS    := -lhid -lsetupapi -lole32 -luuid -lshell32 -luser32 -ladvapi32 -lm -lcomctl32 -lcomdlg32

SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=build/%.o) build/app.res.o

build/PowerMate.exe: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

build/%.o: src/%.c src/*.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/app.res.o: src/app.rc src/app.manifest src/resource.h res/*.ico | build
	$(WINDRES) -I src -O coff -o $@ $<

build:
	mkdir -p build

clean:
	rm -rf build

.PHONY: clean

build/test_config.exe: tests/test_config.c src/config.c src/actions.c src/*.h | build
	$(CC) $(CFLAGS) -o $@ tests/test_config.c src/config.c src/actions.c -static $(LIBS)

test: build/test_config.exe
	$(if $(filter Windows_NT,$(OS)),,$(WINE)) build/test_config.exe

.PHONY: test
