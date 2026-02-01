# Common Makefile rules for all Spectre exploit variants
# Include this file in variant Makefiles and set BINARY variable

CC = gcc

# Default optimization level (can be overridden)
OPT_LEVEL ?= s
CFLAGS = -O$(OPT_LEVEL) -g -pthread -Wall -Werror -Wno-unused-function -Wno-unused-variable

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
    CFLAGS += -DP550
else ifeq ($(CPU_TYPE),C910)
    CFLAGS += -DC910
endif

# Timer backend selection: rdcycle (default) or counter
TIMER_BACKEND ?= rdcycle
ifeq ($(TIMER_BACKEND),counter)
    CFLAGS += -DCACHEUTILS_TIMER_COUNTER
endif

# Optional: Force eviction sets usage (overrides default flush on C910)
ifdef USE_EVICTION
    CFLAGS += -DCACHEUTILS_USE_EVICTION
endif

# Optional: Speculation fence for verification mode
# Usage: make SPEC_FENCE='rdtime x0' to insert speculation barrier before encoding
# This verifies: (1) attack is speculative, (2) fence stops speculation
ifdef SPEC_FENCE
    CFLAGS += -DSPEC_FENCE_INSN='"$(SPEC_FENCE)"'
endif

# Ensure BINARY is defined
ifndef BINARY
    $(error BINARY variable must be defined before including common.mk)
endif

# Default target
all: $(BINARY)

# Normal build
$(BINARY): main.c $(EXTRA_SOURCES)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) main.c $(EXTRA_SOURCES) -o $(BINARY)

# Clean
clean:
	rm -f $(BINARY)

.PHONY: all clean
