CC = gcc
CFLAGS = -g -Os -Wall -I..

# Detect processor type
UNAME := $(shell uname -m)
ifeq ($(UNAME), riscv64)
    PROCESSOR := $(shell cat /proc/cpuinfo | grep -m1 "uarch" | awk '{print $$3}')
    ifneq (,$(findstring sifive,$(PROCESSOR)))
        CFLAGS += -DP550
    else
        CFLAGS += -DC910
    endif
endif

all: main

main: main.c ../../libjit.h
	$(CC) $(CFLAGS) main.c -o main

clean:
	rm -f main *.csv

.PHONY: all clean
