# Makefile for C910 predictor check kernel module

obj-m += check_c910_predictors.o

# Allow overriding kernel directory, default to running kernel
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	-$(MAKE) -C $(KDIR) M=$(PWD) clean 2>/dev/null || true

install:
	sudo insmod check_c910_predictors.ko

remove:
	sudo rmmod check_c910_predictors

check:
	dmesg | tail -50

test: install check

reload: remove install check

.PHONY: all clean install remove check test reload
