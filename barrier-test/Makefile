# Barrier Test - Tests which barriers stop speculation after faults

CFLAGS += -Os -g -Wall -Werror -Wno-unused-function -Wno-unused-variable
TARGETS = barrier_test barrier_test_rsb

# Auto-detect CPU
CPUINFO := $(shell cat /proc/cpuinfo 2>/dev/null)
ifneq (,$(findstring p550,$(CPUINFO)))
    CFLAGS += -DP550
else ifneq (,$(findstring thead,c910,$(CPUINFO)))
    CFLAGS += -DC910
else ifneq (,$(findstring cpu-vector,$(CPUINFO)))
    CFLAGS += -DC910
endif

all: $(TARGETS)

%: %.c
	$(CC) $(CFLAGS) -Os $^ -o $@

kmod:
	$(MAKE) -f Makefile.kmod

clean:
	rm -f $(TARGETS)
	$(MAKE) -f Makefile.kmod clean 2>/dev/null || true

.PHONY: all clean kmod
