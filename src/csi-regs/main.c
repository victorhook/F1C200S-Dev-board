/*
 * Dump live suniv CSI registers via /dev/mem.
 *
 * Use while yavta is actively streaming so registers reflect the
 * configured-for-capture state, not power-on defaults.
 *
 *     yavta --format=YUV420M --capture=999 --size=640x480 /dev/video0 &
 *     sleep 3
 *     ./csi-regs
 *     kill %1
 *
 * Requires CONFIG_DEVMEM=y (on by default on most buildroot configs).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define CSI_BASE   0x01CB0000
#define CSI_SIZE   0x1000
#define PAGE_SIZE  4096

int main(void)
{
	int fd = open("/dev/mem", O_RDONLY | O_SYNC);
	if (fd < 0) { perror("/dev/mem (need CONFIG_DEVMEM=y)"); return 1; }

	volatile uint32_t *csi = mmap(NULL, CSI_SIZE, PROT_READ, MAP_SHARED,
				       fd, CSI_BASE);
	if (csi == MAP_FAILED) { perror("mmap"); return 1; }

	uint32_t en      = csi[0x00 / 4];
	uint32_t cfg     = csi[0x04 / 4];
	uint32_t cap     = csi[0x08 / 4];
	uint32_t scale   = csi[0x0c / 4];
	uint32_t f0_a    = csi[0x10 / 4];
	uint32_t f0_b    = csi[0x14 / 4];
	uint32_t f1_a    = csi[0x18 / 4];
	uint32_t f1_b    = csi[0x1c / 4];
	uint32_t f2_a    = csi[0x20 / 4];
	uint32_t f2_b    = csi[0x24 / 4];
	uint32_t buf_ctl = csi[0x28 / 4];
	uint32_t buf_sta = csi[0x2c / 4];
	uint32_t int_en  = csi[0x30 / 4];
	uint32_t int_sta = csi[0x34 / 4];
	uint32_t hsize   = csi[0x40 / 4];
	uint32_t vsize   = csi[0x44 / 4];
	uint32_t buf_len = csi[0x48 / 4];

	printf("=== suniv CSI (base 0x%08x) ===\n\n", CSI_BASE);

	printf("EN          0x00 = 0x%08x  (CSI_EN=%s)\n",
	       en, (en & 1) ? "ENABLED" : "disabled");

	printf("CFG         0x04 = 0x%08x\n", cfg);
	printf("  [22:20] INPUT_FMT  = %u  %s\n",
	       (cfg >> 20) & 7,
	       ((cfg >> 20) & 7) == 3 ? "(YUV422 ✓)"
	       : ((cfg >> 20) & 7) == 2 ? "(BT656)"
	       : ((cfg >> 20) & 7) == 0 ? "(RAW)" : "(invalid)");
	printf("  [19:16] OUTPUT_FMT = 0x%x  %s\n",
	       (cfg >> 16) & 0xf,
	       ((cfg >> 16) & 0xf) == 1 ? "(planar YUV 420 ✓)"
	       : ((cfg >> 16) & 0xf) == 0 ? "(planar YUV 422)"
	       : "(other)");
	printf("  [ 9: 8] INPUT_SEQ  = %u  (0=YUYV, 1=YVYU, 2=UYVY, 3=VYUY)\n",
	       (cfg >> 8) & 3);
	printf("  [    2] VREF_POL   = %u  (0=neg, 1=pos)\n", (cfg >> 2) & 1);
	printf("  [    1] HREF_POL   = %u  (0=neg, 1=pos)\n", (cfg >> 1) & 1);
	printf("  [    0] CLK_POL    = %u  (0=falling, 1=rising)\n", cfg & 1);

	printf("CAP         0x08 = 0x%08x  (VCAP=%u SCAP=%u)\n",
	       cap, (cap >> 1) & 1, cap & 1);
	printf("SCALE       0x0c = 0x%08x\n", scale);
	printf("  [27:24] VER_MASK   = 0x%x  (expect 0xf)\n", (scale >> 24) & 0xf);
	printf("  [15: 0] HOR_MASK   = 0x%04x  (expect 0xffff)\n", scale & 0xffff);

	printf("FIFO0 buf A 0x10 = 0x%08x   (Y plane A — DMA target)\n", f0_a);
	printf("FIFO0 buf B 0x14 = 0x%08x   (Y plane B)\n", f0_b);
	printf("FIFO1 buf A 0x18 = 0x%08x   (Cb plane A)\n", f1_a);
	printf("FIFO1 buf B 0x1c = 0x%08x   (Cb plane B)\n", f1_b);
	printf("FIFO2 buf A 0x20 = 0x%08x   (Cr plane A)\n", f2_a);
	printf("FIFO2 buf B 0x24 = 0x%08x   (Cr plane B)\n", f2_b);

	printf("BUF_CTL     0x28 = 0x%08x\n", buf_ctl);
	printf("BUF_STA     0x2c = 0x%08x\n", buf_sta);
	printf("INT_EN      0x30 = 0x%08x\n", int_en);
	printf("INT_STA     0x34 = 0x%08x\n", int_sta);

	printf("HSIZE       0x40 = 0x%08x\n", hsize);
	printf("  [28:16] HOR_LEN   = %u PCLK cycles  %s\n",
	       (hsize >> 16) & 0x1fff,
	       ((hsize >> 16) & 0x1fff) == 1280 ? "(✓ VGA YUYV = 640 px × 2)"
	       : ((hsize >> 16) & 0x1fff) == 320 ? "(✗ only 160 pixels — BUG)"
	       : "(??)");
	printf("  [12: 0] HOR_START = %u\n", hsize & 0x1fff);

	printf("VSIZE       0x44 = 0x%08x\n", vsize);
	printf("  [28:16] VER_LEN   = %u lines  %s\n",
	       (vsize >> 16) & 0x1fff,
	       ((vsize >> 16) & 0x1fff) == 480 ? "(✓ VGA)" : "(??)");
	printf("  [12: 0] VER_START = %u\n", vsize & 0x1fff);

	printf("BUF_LEN     0x48 = 0x%08x\n", buf_len);
	printf("  [12: 0] BUF_LEN   = %u bytes per line  %s\n",
	       buf_len & 0x1fff,
	       (buf_len & 0x1fff) == 640 ? "(✓ VGA Y plane)"
	       : (buf_len & 0x1fff) == 160 ? "(✗ only 160 — matches 160/640 capture pattern!)"
	       : "(??)");

	munmap((void *)csi, CSI_SIZE);
	close(fd);
	return 0;
}
