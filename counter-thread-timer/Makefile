# Counter-thread timer: implementation + resolution benchmark

CFLAGS += -O2 -g -Wall -Wno-unused-function -pthread -D_GNU_SOURCE
TARGETS = bench

ifeq ($(NO_RDCYCLE),1)
    CFLAGS += -DNO_RDCYCLE
endif

CFLAGS += $(EXTRA_CFLAGS)

# Auto-detect CPU
CPUINFO := $(shell cat /proc/cpuinfo 2>/dev/null)
ifneq (,$(findstring p550,$(CPUINFO)))
    CFLAGS += -DP550
else ifneq (,$(findstring thead,$(CPUINFO)))
    CFLAGS += -DC910
else ifneq (,$(findstring cpu-vector,$(CPUINFO)))
    CFLAGS += -DC910
endif

all: $(TARGETS)

bench: bench.c counter_timer.h
	$(CC) $(CFLAGS) bench.c -o bench

clean:
	rm -f $(TARGETS)

.PHONY: all clean
