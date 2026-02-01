# Detect CPU type
CPU_INFO := $(shell cat /proc/cpuinfo 2>/dev/null)
ifneq (,$(findstring sifive,$(CPU_INFO)))
    CFLAGS += -DP550
else ifneq (,$(findstring thead,c910,$(CPU_INFO)))
    CFLAGS += -DC910
else ifneq (,$(findstring T-HEAD,$(CPU_INFO)))
    CFLAGS += -DC910
else
    # Default to C910 if unknown
    CFLAGS += -DC910
endif

CFLAGS += -Os -g -Wall

.PHONY: all disasm clean

all: dispatch

dispatch: dispatch.c
	$(CC) $(CFLAGS) -o $@ $< -lm

disasm: dispatch
	objdump -d dispatch | less

clean:
	rm -f dispatch
