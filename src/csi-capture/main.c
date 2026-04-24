/*
 * csi-capture  —  bare-metal single-frame CSI capture via /dev/mem
 *
 * The mainline sun4i-csi driver configures the suniv CSI block through its
 * multi-plane YUV420 pipeline and produces a 1:4 byte pattern per row that
 * we cannot explain from register readback.  This tool bypasses the driver's
 * state machine: it leaves sun4i-csi bound (so clocks, pinmux and OV2640
 * init are already done), then takes over the CSI via /dev/mem, reprograms
 * it for RAW passthrough (no hardware format conversion at all), and
 * redirects the DMA output to a buffer allocated by the csi-buf kernel
 * module.
 *
 * Answers the question "is the suniv CSI block itself capable of capturing
 * a clean VGA frame?" — which the current driver cannot.
 *
 *   ─────────────────────────────────────────────────────────────────────
 *   Board-side setup (once per power cycle)
 *   ─────────────────────────────────────────────────────────────────────
 *     1. insmod csi-buf.ko   (allocates DMA-coherent buffer, creates
 *                             /dev/csi-buf; dmesg shows the phys addr)
 *     2. Start yavta in the background so OV2640 is running and CSI's
 *        clocks/pinmux are live:
 *            yavta --format=YUV420M --size=640x480 --capture=999 \
 *                  /dev/video0 >/dev/null 2>&1 &
 *            sleep 2
 *     3. Run this tool.  yavta will stall (its frames stop completing)
 *        while we have CSI — that's fine, kill it afterwards.
 *
 *   ─────────────────────────────────────────────────────────────────────
 *   Memory footprint
 *   ─────────────────────────────────────────────────────────────────────
 *     VGA YUYV frame   = 640 × 480 × 2   = 614,400 bytes  (0x96000)
 *     csi-buf default  = 4 MiB
 *     Headroom         = 4,194,304 − 614,400  ≈  3.4 MiB   (plenty)
 *
 *   ─────────────────────────────────────────────────────────────────────
 *   Register map (F1C200s UM rev 1.2, §6.1.7, base 0x01CB0000)
 *   ─────────────────────────────────────────────────────────────────────
 *     0x00  CSI_EN         [0]       CSI enable
 *     0x04  CSI_CFG        [22:20]   INPUT_FMT   (0=RAW, 3=YUV422, 2=BT656)
 *                          [19:16]   OUTPUT_FMT  (0=passthrough for RAW in)
 *                          [ 9: 8]   INPUT_SEQ   (YUV byte order, unused in RAW)
 *                          [    2]   VREF_POL    (1=positive/active-high)
 *                          [    1]   HREF_POL    (1=positive/active-high)
 *                          [    0]   CLK_POL     (1=rising-edge sampling)
 *     0x08  CSI_CAP        [1]=VIDEO_START, [0]=IMAGE_START (single frame)
 *     0x0C  CSI_SCALE      [27:24]=VER_MASK (0xF), [15:0]=HOR_MASK (0xFFFF)
 *     0x10  FIFO0_BUFA     DMA target A for plane 0 (Y or RAW stream)
 *     0x14  FIFO0_BUFB     DMA target B for plane 0 (double-buffer)
 *     0x18/0x1C  FIFO1 A/B (Cb)      unused in RAW passthrough
 *     0x20/0x24  FIFO2 A/B (Cr)      unused in RAW passthrough
 *     0x28  CSI_BUFCTL     [0]=DBE (double-buffer enable)
 *     0x2C  CSI_STA        [1]=VCAP_STA, [0]=SCAP_STA
 *     0x30  CSI_INT_EN     [1]=FRM_DONE, [0]=CAP_DONE, [2-4]=FIFO OF, [6]=HB OF
 *     0x34  CSI_INT_STA    write-1-to-clear; [0]=CD_PD = "still capture done"
 *     0x40  CSI_HSIZE      [28:16]=HOR_LEN (pclk cycles of valid line),
 *                          [12: 0]=HOR_START
 *     0x44  CSI_VSIZE      [28:16]=VER_LEN (lines/frame), [12:0]=VER_START
 *     0x48  CSI_BUF_LEN    [12:0]=bytes per output line (max of the 3 FIFOs)
 *
 *   ─────────────────────────────────────────────────────────────────────
 *   CSI interrupts & DMA
 *   ─────────────────────────────────────────────────────────────────────
 *   CSI has its own internal DMA engine — no external DMA controller /
 *   channel is involved.  Frame bytes go CSI FIFO → AHB master → DDR
 *   directly, controlled by FIFO{0,1,2}_BUF{A,B}_REG + BUF_LEN + H/VSIZE.
 *
 *   CSI IRQ on suniv F1C100S is GIC line 32 (confirmed from the platform
 *   DTS).  We don't use it: we poll CSI_INT_STA for CD_PD (bit 0) after
 *   issuing IMAGE_START.  We also set CSI_INT_EN=0 up-front so the
 *   sun4i-csi kernel handler doesn't run while we're in control.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#include "../../modules/csi-buf/csi-buf.h"

/* ── Hardware addresses ─────────────────────────────────────────────── */
#define CSI_BASE        0x01CB0000u
#define CSI_MAP_SIZE    0x1000u              /* one page, covers all regs */

/* ── Capture parameters ─────────────────────────────────────────────── */
#define WIDTH           640u
#define HEIGHT          480u
#define BPP             2u                   /* YUYV: 2 bytes/pixel */
#define BYTES_PER_LINE  (WIDTH * BPP)        /* 1280 */
#define PCLK_PER_LINE   (WIDTH * BPP)        /* 1280 — same, 1 byte/PCLK */
#define FRAME_BYTES     (BYTES_PER_LINE * HEIGHT)   /* 614400 */

/* ── CSI register word offsets ──────────────────────────────────────── */
#define R_EN            (0x00 / 4)
#define R_CFG           (0x04 / 4)
#define R_CAP           (0x08 / 4)
#define R_SCALE         (0x0C / 4)
#define R_FIFO0A        (0x10 / 4)
#define R_FIFO0B        (0x14 / 4)
#define R_FIFO1A        (0x18 / 4)
#define R_FIFO1B        (0x1C / 4)
#define R_FIFO2A        (0x20 / 4)
#define R_FIFO2B        (0x24 / 4)
#define R_BUFCTL        (0x28 / 4)
#define R_STA           (0x2C / 4)
#define R_INT_EN        (0x30 / 4)
#define R_INT_STA       (0x34 / 4)
#define R_HSIZE         (0x40 / 4)
#define R_VSIZE         (0x44 / 4)
#define R_BUFLEN        (0x48 / 4)

/* ── CSI field values ───────────────────────────────────────────────── */
#define CFG_INPUT_RAW           (0u << 20)
#define CFG_OUTPUT_PASSTHROUGH  (0u << 16)
#define CFG_VREF_POS            (1u << 2)
#define CFG_HREF_POS            (1u << 1)
#define CFG_CLK_POS_EDGE        (1u << 0)

#define CAP_IMAGE_START         (1u << 0)
#define CAP_VIDEO_START         (1u << 1)

#define BUFCTL_DBE              (1u << 0)

#define INT_CAP_DONE            (1u << 0)
#define INT_FRM_DONE            (1u << 1)

/* Datasheet-default "keep every byte / every line" — reset value */
#define SCALE_PASSTHROUGH       ((0xFu << 24) | 0xFFFFu)    /* 0x0F00FFFF */

static void *map_phys(int fd, off_t phys, size_t len, int prot)
{
	void *p = mmap(NULL, len, prot, MAP_SHARED, fd, phys);
	if (p == MAP_FAILED) {
		fprintf(stderr, "mmap(0x%08lx, %zu): %s\n",
			(unsigned long)phys, len, strerror(errno));
		return NULL;
	}
	return p;
}

static void dump_state(const char *tag, volatile uint32_t *csi)
{
	printf("%s\n", tag);
	printf("  EN      0x00 = 0x%08x\n", csi[R_EN]);
	printf("  CFG     0x04 = 0x%08x\n", csi[R_CFG]);
	printf("  CAP     0x08 = 0x%08x\n", csi[R_CAP]);
	printf("  SCALE   0x0C = 0x%08x\n", csi[R_SCALE]);
	printf("  FIFO0A  0x10 = 0x%08x\n", csi[R_FIFO0A]);
	printf("  FIFO0B  0x14 = 0x%08x\n", csi[R_FIFO0B]);
	printf("  BUFCTL  0x28 = 0x%08x\n", csi[R_BUFCTL]);
	printf("  STA     0x2C = 0x%08x\n", csi[R_STA]);
	printf("  INT_EN  0x30 = 0x%08x\n", csi[R_INT_EN]);
	printf("  INT_STA 0x34 = 0x%08x\n", csi[R_INT_STA]);
	printf("  HSIZE   0x40 = 0x%08x  (HOR_LEN=%u)\n",
	       csi[R_HSIZE], (csi[R_HSIZE] >> 16) & 0x1FFF);
	printf("  VSIZE   0x44 = 0x%08x  (VER_LEN=%u)\n",
	       csi[R_VSIZE], (csi[R_VSIZE] >> 16) & 0x1FFF);
	printf("  BUFLEN  0x48 = 0x%08x\n", csi[R_BUFLEN]);
}

static double ms_since(struct timespec t0)
{
	struct timespec t1;
	clock_gettime(CLOCK_MONOTONIC, &t1);
	return (t1.tv_sec - t0.tv_sec) * 1000.0
	     + (t1.tv_nsec - t0.tv_nsec) / 1e6;
}

int main(int argc, char **argv)
{
	const char *out_path = (argc >= 2) ? argv[1] : "capture.yuyv";

	/* ── Get our DMA buffer from the csi-buf kernel module ───────── */
	int bfd = open("/dev/csi-buf", O_RDWR);
	if (bfd < 0) {
		fprintf(stderr, "/dev/csi-buf: %s\n"
			"  → did you `insmod csi-buf.ko` first?\n",
			strerror(errno));
		return 1;
	}

	struct csi_buf_info info;
	if (ioctl(bfd, CSIBUF_IOC_GETINFO, &info) < 0) {
		perror("ioctl CSIBUF_IOC_GETINFO");
		return 1;
	}
	if (info.size < FRAME_BYTES) {
		fprintf(stderr,
			"csi-buf too small: have %llu, need %u. "
			"Re-insmod with size_mib=4 (or more).\n",
			(unsigned long long)info.size, FRAME_BYTES);
		return 2;
	}

	printf("csi-capture:  VGA YUYV → %u bytes into csi-buf @ phys 0x%08llx\n"
	       "              (buffer size %llu bytes, via /dev/csi-buf)\n\n",
	       FRAME_BYTES,
	       (unsigned long long)info.phys_addr,
	       (unsigned long long)info.size);

	uint8_t *buf = mmap(NULL, info.size, PROT_READ | PROT_WRITE,
			    MAP_SHARED, bfd, 0);
	if (buf == MAP_FAILED) { perror("mmap csi-buf"); return 1; }

	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) { perror("/dev/mem"); return 1; }

	volatile uint32_t *csi =
		map_phys(fd, CSI_BASE, CSI_MAP_SIZE, PROT_READ | PROT_WRITE);
	if (!csi) return 1;

	const uint32_t DMA_PHYS = (uint32_t)info.phys_addr;

	/* Zero buffer so anything non-zero afterwards is definitely
	 * something the CSI DMA wrote — no stale-data confusion. */
	memset(buf, 0, FRAME_BYTES);

	dump_state("=== before reconfig (kernel driver's state) ===", csi);
	printf("\n");

	/* ── Take control ────────────────────────────────────────────── */

	/* Mask all CSI interrupts so the sun4i-csi kernel handler
	 * doesn't run while we're reprogramming.  Its buffers-done logic
	 * expects invariants that we're about to break.  */
	csi[R_INT_EN] = 0;

	/* Stop any running capture, wait out the current frame. */
	csi[R_CAP] = 0;
	usleep(100 * 1000);

	/* Disable peripheral before writing window/size registers (some
	 * Allwinner CSI blocks latch these only on EN=0→1 transition). */
	csi[R_EN] = 0;
	usleep(1 * 1000);

	/* ── Reprogram for RAW-passthrough YUYV VGA ──────────────────── */

	/* Polarities chosen to match what the LA capture proved:
	 *   HREF high during valid data  →  HREF_POS
	 *   VSYNC high during sync frame →  VREF_POS
	 *   data settled around rising PCLK edge → CLK sample on rising */
	csi[R_CFG] = CFG_INPUT_RAW
		   | CFG_OUTPUT_PASSTHROUGH
		   | CFG_VREF_POS
		   | CFG_HREF_POS
		   | CFG_CLK_POS_EDGE;   /* = 0x00000007 */

	/* Try to restore the SCALE register's datasheet default, in case
	 * the silicon's actual state is non-passthrough.  Our earlier
	 * csi-poke tests suggest the write may not latch in this block;
	 * the post-write dump_state() will tell us definitively. */
	csi[R_SCALE] = SCALE_PASSTHROUGH;

	/* Full VGA YUYV: 1280 PCLK cycles × 480 lines, 1280 bytes/line */
	csi[R_HSIZE]  = PCLK_PER_LINE << 16;      /* HOR_LEN = 1280, start=0 */
	csi[R_VSIZE]  = HEIGHT        << 16;      /* VER_LEN = 480,  start=0 */
	csi[R_BUFLEN] = BYTES_PER_LINE;           /* 1280 bytes/line */

	/* Single output FIFO in passthrough mode.  Point both A and B at
	 * our reserved buffer and disable double-buffering so CSI never
	 * tries to alternate into memory we don't own. */
	csi[R_FIFO0A] = DMA_PHYS;
	csi[R_FIFO0B] = DMA_PHYS;
	csi[R_FIFO1A] = DMA_PHYS;     /* unused, but don't leave dangling */
	csi[R_FIFO1B] = DMA_PHYS;
	csi[R_FIFO2A] = DMA_PHYS;
	csi[R_FIFO2B] = DMA_PHYS;
	csi[R_BUFCTL] = 0;            /* DBE = 0 → single buffer */

	/* Clear any pending interrupt bits (write-1-to-clear). */
	csi[R_INT_STA] = 0xFFu;

	/* Bring CSI back up. */
	csi[R_EN] = 1;

	dump_state("=== after reconfig (our state, pre-capture) ===", csi);
	printf("\n");

	/* Issue still-image capture — self-clears after one frame. */
	csi[R_CAP] = CAP_IMAGE_START;

	/* ── Poll CSI_INT_STA for CD_PD (still-capture done) ─────────── */
	struct timespec t0;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	const double TIMEOUT_MS = 500.0;      /* ~7 frames at 14 fps */
	uint32_t sta = 0;
	while (ms_since(t0) < TIMEOUT_MS) {
		sta = csi[R_INT_STA];
		if (sta & INT_CAP_DONE)
			break;
		usleep(500);
	}
	double elapsed_ms = ms_since(t0);

	printf("capture: %s after %.2f ms, INT_STA=0x%08x, STA=0x%08x\n",
	       (sta & INT_CAP_DONE) ? "DONE ✓" : "TIMEOUT ✗",
	       elapsed_ms, sta, csi[R_STA]);

	/* Stop capture & clear the interrupt. */
	csi[R_CAP] = 0;
	csi[R_INT_STA] = 0xFFu;

	/* ── Inspect result ──────────────────────────────────────────── */

	/* Count non-zero bytes, min/max, first unique bytes of row 0. */
	unsigned nz = 0, uniq_bits = 0;
	uint8_t min = 0xFF, max = 0;
	unsigned seen[256] = { 0 };
	for (unsigned i = 0; i < FRAME_BYTES; i++) {
		uint8_t b = buf[i];
		if (b) nz++;
		if (b < min) min = b;
		if (b > max) max = b;
		seen[b]++;
	}
	for (int i = 0; i < 256; i++) if (seen[i]) uniq_bits++;

	printf("\nframe stats:\n");
	printf("  non-zero bytes: %u / %u  (%.1f%%)\n",
	       nz, FRAME_BYTES, 100.0 * nz / FRAME_BYTES);
	printf("  unique values:  %u\n", uniq_bits);
	printf("  min / max:      0x%02x / 0x%02x\n", min, max);

	/* Row-0 fingerprint: if we're getting real pixels, first few
	 * bytes of YUYV should vary.  Dump a handful. */
	printf("  row 0 [0..31]:  ");
	for (int i = 0; i < 32; i++) printf("%02x", buf[i]);
	printf("\n  row 240 [0..31]:");
	for (int i = 0; i < 32; i++) printf("%02x", buf[240 * BYTES_PER_LINE + i]);
	printf("\n");

	/* ── Save file ───────────────────────────────────────────────── */
	FILE *f = fopen(out_path, "wb");
	if (!f) {
		perror(out_path);
	} else {
		if (fwrite(buf, 1, FRAME_BYTES, f) != FRAME_BYTES)
			perror("fwrite");
		fclose(f);
		printf("\nwrote %u bytes → %s\n", FRAME_BYTES, out_path);
	}

	munmap((void *)csi, CSI_MAP_SIZE);
	munmap(buf, info.size);
	close(fd);
	close(bfd);
	return (sta & INT_CAP_DONE) ? 0 : 3;
}
