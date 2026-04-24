/*
 * Write a 32-bit value to a suniv CSI register via /dev/mem.
 * Useful for testing hypothesized CSI register fixes without kernel rebuilds.
 *
 * Usage:  ./csi-poke OFFSET VALUE
 *         OFFSET  in hex, e.g. 0x0c for CSI_SCALE
 *         VALUE   in hex, e.g. 0x0F00FFFF
 *
 * Example — set SCALE register to datasheet default (all mask bits enabled):
 *         ./csi-poke 0x0c 0x0F00FFFF
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define CSI_BASE 0x01CB0000
#define CSI_SIZE 0x1000

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s OFFSET VALUE (both in hex)\n", argv[0]);
		return 2;
	}

	uint32_t off = strtoul(argv[1], NULL, 16);
	uint32_t val = strtoul(argv[2], NULL, 16);

	if (off >= CSI_SIZE || off & 3) {
		fprintf(stderr, "invalid offset 0x%x\n", off);
		return 2;
	}

	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) { perror("/dev/mem"); return 1; }

	volatile uint32_t *csi = mmap(NULL, CSI_SIZE, PROT_READ | PROT_WRITE,
				       MAP_SHARED, fd, CSI_BASE);
	if (csi == MAP_FAILED) { perror("mmap"); return 1; }

	uint32_t before = csi[off / 4];
	csi[off / 4] = val;
	uint32_t after = csi[off / 4];

	printf("CSI 0x%02x: 0x%08x → 0x%08x (readback 0x%08x%s)\n",
	       off, before, val, after,
	       after == val ? " ✓" : " ✗ MISMATCH (read-only or partial mask)");

	munmap((void *)csi, CSI_SIZE);
	close(fd);
	return 0;
}
