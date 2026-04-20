TOPDIR  := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
CC      := $(TOPDIR)/buildroot/output/host/bin/arm-buildroot-linux-musleabi-gcc
SYSROOT := $(shell $(CC) -print-sysroot 2>/dev/null)
BUILD   := $(TOPDIR)/build

SRCS := $(wildcard *.c)
BINS := $(SRCS:%.c=$(BUILD)/%)

CFLAGS  ?= -Wall -Wextra -O2
LDFLAGS ?=

.PHONY: all clean run-% static-%

all: $(BINS)

$(BUILD)/%: %.c | $(BUILD)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD):
	mkdir -p $@

run-%: $(BUILD)/%
	QEMU_LD_PREFIX=$(SYSROOT) qemu-arm-static $<

static-%: %.c | $(BUILD)
	$(CC) $(CFLAGS) -static $< -o $(BUILD)/$*
	qemu-arm-static $(BUILD)/$*

clean:
	rm -rf $(BUILD)
