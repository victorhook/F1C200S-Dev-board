BUILDROOT_VERSION := 2026.02
BUILDROOT_DIR := buildroot
BR2_EXTERNAL := $(CURDIR)/br2-external
DEFCONFIG := f1c200s_devboard_defconfig

.PHONY: all build menuconfig linux-menuconfig uboot-menuconfig image clean distclean help

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

# Rebuild just the image (after changing overlay/DTS/boot.cmd)
image:
	$(MAKE) -C $(BUILDROOT_DIR) linux-rebuild uboot-rebuild
	$(MAKE) -C $(BUILDROOT_DIR)

clean:
	$(MAKE) -C $(BUILDROOT_DIR) clean
	rm -f $(BUILDROOT_DIR)/.stamp_defconfig

distclean:
	rm -rf $(BUILDROOT_DIR)

help:
	@echo "F1C200S Dev Board - Build targets:"
	@echo ""
	@echo "  make              - Full build (applies defconfig on first run only)"
	@echo "  make defconfig    - Force re-apply defconfig (overwrites menuconfig changes)"
	@echo "  make menuconfig   - Buildroot configuration (preserved across builds)"
	@echo "  make linux-menuconfig  - Kernel configuration"
	@echo "  make uboot-menuconfig  - U-Boot configuration"
	@echo "  make image        - Rebuild kernel/uboot/image only"
	@echo "  make clean        - Clean build artifacts"
	@echo "  make distclean    - Remove Buildroot entirely"
