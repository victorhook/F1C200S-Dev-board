TOPDIR  := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
CC      := $(TOPDIR)/buildroot/output/host/bin/arm-buildroot-linux-musleabi-gcc
SYSROOT := $(shell $(CC) -print-sysroot 2>/dev/null)
BUILD   := $(TOPDIR)/build
BOARD   ?= root@192.168.100.1

CFLAGS  ?= -Wall -Wextra -O2
LDFLAGS ?=

export CC CFLAGS LDFLAGS SYSROOT BUILD

# Every subdirectory with a Makefile (except drivers/) is one app
APPS := $(sort $(patsubst %/,%, $(filter-out drivers/, $(dir $(wildcard */Makefile)))))
BINS := $(addprefix $(BUILD)/, $(APPS))

.PHONY: all clean list deploy run-% $(APPS)

all: $(APPS)

$(APPS): | $(BUILD)
	@$(MAKE) -C $@ --no-print-directory

$(BUILD):
	@mkdir -p $@

clean:
	rm -rf $(BUILD)

list:
	@echo "Apps:   $(APPS)"
	@echo "BUILD:  $(BUILD)"
	@echo "BOARD:  $(BOARD)"

# Copy every built binary to the board over USB ethernet
deploy: all
	scp $(BINS) $(BOARD):/root/

# Emulate on host for a quick smoke test, no board needed
run-%: $(BUILD)/%
	QEMU_LD_PREFIX=$(SYSROOT) qemu-arm-static $<
