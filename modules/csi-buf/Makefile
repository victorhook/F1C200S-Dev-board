TOPDIR        := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
KDIR          ?= $(TOPDIR)/buildroot/output/build/linux-6.19.11
ARCH          ?= arm
CROSS_COMPILE ?= $(TOPDIR)/buildroot/output/host/bin/arm-buildroot-linux-musleabi-
BOARD         ?= board       # alias in tools/ssh-config
SSH_CONF      := $(TOPDIR)/tools/ssh-config

obj-m += csi-buf.o

.PHONY: all clean deploy
all:
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) \
	       M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

deploy: all
	scp -F $(SSH_CONF) -O csi-buf.ko $(BOARD):/root/
