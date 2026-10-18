CC = gcc
CPPFLAGS = -Iinclude
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror
LDLIBS = -pthread -lm
SOURCES = src/main.c src/simulation.c

.PHONY: all test clean
all: build/box

build/box: $(SOURCES) include/simulation.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $@ $(LDLIBS)

test: build/box
	bash pruebas/probar_ayudantes.sh

clean:
	rm -f build/box
