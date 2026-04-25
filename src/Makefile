TOPDIR  := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
CC      := $(TOPDIR)/buildroot/output/host/bin/arm-buildroot-linux-musleabi-gcc
SYSROOT := $(shell $(CC) -print-sysroot 2>/dev/null)
BUILD   := $(TOPDIR)/build
BOARD     ?= board                                          # alias in tools/ssh-config
SSH_CONF  := $(TOPDIR)/tools/ssh-config

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

# Copy every built binary to the board over USB ethernet.
# -O forces legacy SCP protocol (dropbear on target doesn't speak SFTP,
# which OpenSSH 9.0+ clients default to).
# -F tools/ssh-config picks up our speed-tuned options (chacha20 cipher,
# curve25519 KEX, ControlMaster) — first scp pays the handshake, the
# rest reuse the socket.
deploy: all
	scp -F $(SSH_CONF) -O $(BINS) $(BOARD):/root/

# Emulate on host for a quick smoke test, no board needed
run-%: $(BUILD)/%
	QEMU_LD_PREFIX=$(SYSROOT) qemu-arm-static $<
