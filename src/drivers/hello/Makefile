obj-m := hello.o

TOPDIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../..)
KDIR   := $(TOPDIR)/buildroot/output/build/linux-6.19.11
CROSS  := $(TOPDIR)/buildroot/output/host/bin/arm-buildroot-linux-musleabi-

.PHONY: all clean

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm CROSS_COMPILE=$(CROSS) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm CROSS_COMPILE=$(CROSS) clean
