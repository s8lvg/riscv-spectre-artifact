CC = gcc

# Read cpuinfo once
CPUINFO := $(shell cat /proc/cpuinfo 2>/dev/null)

# Function to detect CPU type from /proc/cpuinfo
# Returns: C910, P550, or empty string if unknown
# P550: Has "p550" in uarch field (uarch : sifive,p550)
# C910: Has "cpu-vector" field (old kernel) or "thead,c910" in uarch (mainline kernel)
define detect_cpu
$(strip \
  $(if $(findstring p550,$(1)),P550,\
  $(if $(findstring thead,c910,$(1)),C910,\
  $(if $(findstring cpu-vector,$(1)),C910))))
endef

# Detect CPU platform using the function
CPU_TYPE := $(call detect_cpu,$(CPUINFO))

# Add CPU-specific flags
ifeq ($(CPU_TYPE),P550)
    CFLAGS = -O2 -DP550
else ifeq ($(CPU_TYPE),C910)
    CFLAGS = -O2 -DC910
else
    $(error Unknown CPU type. Cannot auto-detect P550 or C910 from /proc/cpuinfo)
endif

all: test

test: main.c ../../libjit.h ../../cacheutils.h
	$(CC) $(CFLAGS) main.c -o test

clean:
	rm -f test log.txt

.PHONY: all clean
