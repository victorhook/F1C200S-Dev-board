/*
 * Shared ABI between csi-buf.ko and userspace (csi-capture).
 *
 * The module allocates one DMA-coherent buffer at insmod time; userspace
 * queries its physical address via ioctl and gets a virtual pointer via
 * mmap() on the same fd.  No reboot, no mem= bootarg.
 */
#ifndef _CSI_BUF_H_
#define _CSI_BUF_H_

#include <linux/types.h>
#include <linux/ioctl.h>

struct csi_buf_info {
	__u64 phys_addr;
	__u64 size;
};

#define CSIBUF_IOC_MAGIC	'C'
#define CSIBUF_IOC_GETINFO	_IOR(CSIBUF_IOC_MAGIC, 1, struct csi_buf_info)

#endif
