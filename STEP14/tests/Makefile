CC     = gcc
CFLAGS = -Wall -Wextra -Wno-unused-parameter -std=c99 -I. -I..

# Kompilacja na hoscie (Linux/GCC) z podmieniona dos.h z tests/
# Flaga -Wtautological-compare pomijana bo moze byc uzywana w roznych wersjach

.PHONY: all test clean

all: test

test: test_pm_helpers
	./test_pm_helpers

test_pm_helpers: test_pm_helpers.c ../pm_helpers.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f test_pm_helpers
