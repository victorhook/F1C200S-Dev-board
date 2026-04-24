/*
 * csi-buf — DMA-coherent buffer for userspace CSI experimentation
 *
 * Allocates one contiguous DMA-coherent region at insmod time and exposes
 * it as /dev/csi-buf.  Userspace:
 *   fd = open("/dev/csi-buf", O_RDWR);
 *   ioctl(fd, CSIBUF_IOC_GETINFO, &info);   // returns phys_addr + size
 *   buf = mmap(NULL, info.size, R|W, SHARED, fd, 0);
 * Then hand info.phys_addr to the CSI FIFO{0,1,2}_BUF{A,B} registers and
 * read pixel data through buf.
 *
 * Avoids the `mem=60M` bootarg trick — uses the kernel's regular
 * dma_alloc_coherent() (backed by CMA if configured, else a normal
 * contiguous page block).  Default size 4 MiB, tunable via `size_mib`
 * module parameter.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>
#include <linux/mm.h>

#include "csi-buf.h"

static unsigned int size_mib = 4;
module_param(size_mib, uint, 0444);
MODULE_PARM_DESC(size_mib, "DMA buffer size in MiB (default 4)");

static void *cb_vaddr;
static dma_addr_t cb_paddr;
static size_t cb_size;

static int cb_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int cb_release(struct inode *inode, struct file *file)
{
	return 0;
}

static struct miscdevice cb_miscdev;

static int cb_mmap(struct file *file, struct vm_area_struct *vma)
{
	size_t size = vma->vm_end - vma->vm_start;

	if (vma->vm_pgoff != 0)
		return -EINVAL;
	if (size > cb_size)
		return -EINVAL;

	return dma_mmap_coherent(cb_miscdev.this_device, vma,
				 cb_vaddr, cb_paddr, size);
}

static long cb_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct csi_buf_info info;

	switch (cmd) {
	case CSIBUF_IOC_GETINFO:
		info.phys_addr = cb_paddr;
		info.size = cb_size;
		if (copy_to_user((void __user *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	return -ENOTTY;
}

static const struct file_operations cb_fops = {
	.owner		= THIS_MODULE,
	.open		= cb_open,
	.release	= cb_release,
	.mmap		= cb_mmap,
	.unlocked_ioctl	= cb_ioctl,
};

static struct miscdevice cb_miscdev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "csi-buf",
	.fops	= &cb_fops,
	.mode	= 0666,
};

static int __init cb_init(void)
{
	int ret;

	ret = misc_register(&cb_miscdev);
	if (ret) {
		pr_err("csi-buf: misc_register failed: %d\n", ret);
		return ret;
	}

	/* Misc device's synthetic struct device has no bus-configured
	 * dma_ops.  Force a 32-bit coherent mask so dma-direct kicks in
	 * on this no-IOMMU SoC. */
	ret = dma_coerce_mask_and_coherent(cb_miscdev.this_device,
					   DMA_BIT_MASK(32));
	if (ret) {
		pr_err("csi-buf: dma_coerce_mask_and_coherent failed: %d\n",
		       ret);
		goto fail;
	}

	cb_size = (size_t)size_mib * 1024 * 1024;
	cb_vaddr = dma_alloc_coherent(cb_miscdev.this_device, cb_size,
				      &cb_paddr, GFP_KERNEL);
	if (!cb_vaddr) {
		pr_err("csi-buf: dma_alloc_coherent(%zu bytes) failed\n",
		       cb_size);
		ret = -ENOMEM;
		goto fail;
	}

	pr_info("csi-buf: %zu bytes at phys 0x%08llx, virt %p\n",
		cb_size, (unsigned long long)cb_paddr, cb_vaddr);
	return 0;

fail:
	misc_deregister(&cb_miscdev);
	return ret;
}

static void __exit cb_exit(void)
{
	if (cb_vaddr)
		dma_free_coherent(cb_miscdev.this_device, cb_size,
				  cb_vaddr, cb_paddr);
	misc_deregister(&cb_miscdev);
	pr_info("csi-buf: removed\n");
}

module_init(cb_init);
module_exit(cb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor");
MODULE_DESCRIPTION("DMA-coherent buffer for userspace CSI experiments");
