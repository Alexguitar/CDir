SHELL := /bin/sh

CC := gcc
CFLAGS := -g -Wall
LDFLAGS :=
LDLIBS :=

all: dir.o dir

dir: main.c dir.o
	@$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) $(LDLIBS)

dir.o: core.o interface.o
	@ld -o $@ -r $<

core.o: core.c interface.o
	@$(CC) -o $@ -c $< $(CFLAGS) $(LDFLAGS) $(LDLIBS)

interface.o: interface.c
	@$(CC) -o $@ -c $< $(CFLAGS) $(LDFLAGS) $(LDLIBS)
