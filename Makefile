BUILDROOT_VERSION := 2026.02
BUILDROOT_DIR := buildroot
BR2_EXTERNAL := $(CURDIR)/br2-external
DEFCONFIG := f1c200s_devboard_defconfig
IMAGES := $(BUILDROOT_DIR)/output/images
TARGET_BIN := $(BUILDROOT_DIR)/output/target/usr/bin
TARGET_SBIN := $(BUILDROOT_DIR)/output/target/usr/sbin
BOARD ?= root@192.168.100.1

.PHONY: all build kernel uboot menuconfig linux-menuconfig uboot-menuconfig clean \
        distclean help deploy deploy-kernel deploy-apps reboot wait

all: build

$(BUILDROOT_DIR):
	git clone --depth 1 --branch $(BUILDROOT_VERSION) \
		https://gitlab.com/buildroot.org/buildroot.git $(BUILDROOT_DIR)

# Only apply defconfig once. Delete .stamp_defconfig to re-apply.
$(BUILDROOT_DIR)/.stamp_defconfig: $(BR2_EXTERNAL)/configs/$(DEFCONFIG) | $(BUILDROOT_DIR)
	$(MAKE) -C $(BUILDROOT_DIR) BR2_EXTERNAL=$(BR2_EXTERNAL) $(DEFCONFIG)
	touch $@

build: $(BUILDROOT_DIR)/.stamp_defconfig
	$(MAKE) -C $(BUILDROOT_DIR)
	@echo ""
	@echo "Build complete!"
	@echo "  Image: $(BUILDROOT_DIR)/output/images/sdcard.img"
	@echo ""
	@echo "  Flash: sudo dd if=$(BUILDROOT_DIR)/output/images/sdcard.img of=/dev/sdX bs=4M status=progress"
	@echo "  Console: 115200 8N1 on UART0 (PE0/PE1, camera connector J4)"

# Safe kernel rebuild path after changing kernel patches, DTS, or kernel config.
# We remove only the extracted linux tree so Buildroot reapplies patches from
# scratch, then run a normal Buildroot build.
kernel: $(BUILDROOT_DIR)/.stamp_defconfig
	rm -rf $(BUILDROOT_DIR)/output/build/linux-*
	$(MAKE) -C $(BUILDROOT_DIR)

# Safe U-Boot rebuild path after changing U-Boot patches, DTS, or config.
# Note: there is no network deploy target for U-Boot; write the new image to
# boot media with your normal flashing path after this completes.
uboot: $(BUILDROOT_DIR)/.stamp_defconfig
	rm -rf $(BUILDROOT_DIR)/output/build/uboot-*
	$(MAKE) -C $(BUILDROOT_DIR)

# Force re-apply defconfig (e.g. after editing the defconfig file)
defconfig: | $(BUILDROOT_DIR)
	$(MAKE) -C $(BUILDROOT_DIR) BR2_EXTERNAL=$(BR2_EXTERNAL) $(DEFCONFIG)
	touch $(BUILDROOT_DIR)/.stamp_defconfig

menuconfig: | $(BUILDROOT_DIR)
	$(MAKE) -C $(BUILDROOT_DIR) BR2_EXTERNAL=$(BR2_EXTERNAL) menuconfig

linux-menuconfig: $(BUILDROOT_DIR)/.stamp_defconfig
	$(MAKE) -C $(BUILDROOT_DIR) linux-menuconfig

uboot-menuconfig: $(BUILDROOT_DIR)/.stamp_defconfig
	$(MAKE) -C $(BUILDROOT_DIR) uboot-menuconfig

clean:
	$(MAKE) -C $(BUILDROOT_DIR) clean
	rm -f $(BUILDROOT_DIR)/.stamp_defconfig

distclean:
	rm -rf $(BUILDROOT_DIR)

# Deploy a freshly-built kernel + DTB to the running board via its FAT boot
# partition (no SD-card removal). Safe: old zImage and DTB are backed up with
# a .bak extension before being overwritten, so you can revert via serial if
# the new build refuses to boot.
deploy-kernel:
	@echo "==> Uploading zImage + DTB to $(BOARD):/tmp/"
	scp -O $(IMAGES)/zImage $(IMAGES)/suniv-f1c200s-devboard.dtb $(BOARD):/tmp/
	@echo "==> Installing into /dev/mmcblk0p1 (FAT boot partition)"
	ssh $(BOARD) 'set -e; \
		mkdir -p /mnt/boot && mount -t vfat /dev/mmcblk0p1 /mnt/boot && \
		cp -f /mnt/boot/zImage                        /mnt/boot/zImage.bak && \
		cp -f /mnt/boot/suniv-f1c200s-devboard.dtb    /mnt/boot/suniv-f1c200s-devboard.dtb.bak && \
		cp /tmp/zImage                        /mnt/boot/zImage && \
		cp /tmp/suniv-f1c200s-devboard.dtb    /mnt/boot/suniv-f1c200s-devboard.dtb && \
		sync && umount /mnt/boot && \
		echo "Installed. .bak copies retained. Run: make reboot"'

# Push freshly-built userspace app binaries (src/**) + yavta to /root/ on the
# board. Non-destructive; the running rootfs is untouched except for /root.
deploy-apps:
	$(MAKE) -C src deploy BOARD=$(BOARD)
	@for t in $(TARGET_BIN)/yavta $(TARGET_SBIN)/i2cdetect; do \
		if [ -x "$$t" ]; then \
			echo "==> Uploading $$(basename $$t) to $(BOARD):/root/"; \
			scp -O "$$t" $(BOARD):/root/; \
		fi; \
	done

deploy: deploy-kernel deploy-apps
	@echo ""
	@echo "Deploy complete. Verify then:  make reboot"

reboot:
	@echo "==> Rebooting $(BOARD) (SSH will drop; USB gadget reenumerates ~10s later)"
	-ssh $(BOARD) 'sync && reboot' 2>/dev/null || true

wait:
	@echo "==> Waiting for board to come back online..."
	@until ping -c 1 -W 1 192.168.100.1 >/dev/null 2>&1; do sleep 1; done
	@echo "Board is pingable. Give SSH another few seconds to spin up."

help:
	@echo "F1C200S Dev Board - Build targets:"
	@echo ""
	@echo "  make              - Full build (applies defconfig on first run only)"
	@echo "  make kernel       - Safe kernel rebuild (re-extract linux tree, reapply patches)"
	@echo "  make uboot        - Safe U-Boot rebuild (re-extract u-boot tree)"
	@echo "  make defconfig    - Force re-apply defconfig (overwrites menuconfig changes)"
	@echo "  make menuconfig   - Buildroot configuration (preserved across builds)"
	@echo "  make linux-menuconfig  - Kernel configuration"
	@echo "  make uboot-menuconfig  - U-Boot configuration"
	@echo "  make clean        - Clean build artifacts"
	@echo "  make distclean    - Remove Buildroot entirely"
	@echo ""
	@echo "Deploy targets (network, no SD removal):"
	@echo "  make deploy-kernel - scp zImage+DTB, install into /dev/mmcblk0p1"
	@echo "  make deploy-apps   - scp src/** binaries + yavta to /root/"
	@echo "  make deploy        - both of the above"
	@echo "  make reboot        - ssh 'reboot' on the board"
	@echo "  make wait          - block until board pings back"
	@echo ""
	@echo "Common workflows:"
	@echo "  src/** changed                          -> make deploy-apps"
	@echo "  linux-patches/, DTS, linux.fragment     -> make kernel && make deploy-kernel"
	@echo "  U-Boot patches/config/DTS               -> make uboot   (then reflash boot media)"
	@echo "  Buildroot defconfig / package config    -> make defconfig && make"
	@echo ""
	@echo "  BOARD=$(BOARD) (override with BOARD=...)"
