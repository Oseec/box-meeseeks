CC = gcc
CPPFLAGS = -Iinclude
CFLAGS = -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror
LDLIBS = -pthread -lm
SOURCES = src/main.c src/simulation.c src/calculator.c

.PHONY: all test clean
all: build/box

build/box: $(SOURCES) include/simulation.h include/calculator.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $@ $(LDLIBS)

build/test_calculator: pruebas/test_calculator.c src/calculator.c include/calculator.h
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) pruebas/test_calculator.c src/calculator.c -o $@ $(LDLIBS)

test: build/box build/test_calculator
	./build/test_calculator
	bash pruebas/probar_ayudantes.sh
	bash pruebas/probar_calculos.sh

clean:
	rm -f build/box build/test_calculator
