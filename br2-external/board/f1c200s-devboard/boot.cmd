setenv fdt_high 0xffffffff
setenv bootargs console=ttyS0,115200n8 console=ttyS1,115200n8 earlycon=uart8250,mmio32,0x01c25000,115200 keep_bootcon ignore_loglevel loglevel=8 root=/dev/mmcblk0p2 rootwait panic=10
load mmc 0:1 0x81C00000 suniv-f1c200s-devboard.dtb
load mmc 0:1 0x81000000 zImage
bootz 0x81000000 - 0x81C00000
