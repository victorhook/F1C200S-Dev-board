#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/random.h>
#include <linux/uaccess.h>

#define DEVNAME "hello"
#define BUFSZ   256

static ssize_t hello_read(struct file *f, char __user *ubuf,
			  size_t len, loff_t *ppos)
{
	u8 kbuf[BUFSZ];
	size_t n = min_t(size_t, len, BUFSZ);

	get_random_bytes(kbuf, n);
	if (copy_to_user(ubuf, kbuf, n))
		return -EFAULT;
	return n;
}

static ssize_t hello_write(struct file *f, const char __user *ubuf,
			   size_t len, loff_t *ppos)
{
	u8 kbuf[BUFSZ];
	size_t n = min_t(size_t, len, BUFSZ);

	if (copy_from_user(kbuf, ubuf, n))
		return -EFAULT;

	pr_info("hello: write(%zu bytes)\n", len);
	print_hex_dump_bytes("hello: ", DUMP_PREFIX_OFFSET, kbuf, n);
	return len;
}

static const struct file_operations hello_fops = {
	.owner = THIS_MODULE,
	.read  = hello_read,
	.write = hello_write,
};

static struct miscdevice hello_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = DEVNAME,
	.fops  = &hello_fops,
	.mode  = 0666,
};

static int __init hello_init(void)
{
	int ret = misc_register(&hello_misc);
	if (ret) {
		pr_err("hello: misc_register failed: %d\n", ret);
		return ret;
	}
	pr_info("hello: registered /dev/%s\n", DEVNAME);
	return 0;
}

static void __exit hello_exit(void)
{
	misc_deregister(&hello_misc);
	pr_info("hello: deregistered\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Victor");
MODULE_DESCRIPTION("Char device: read returns random bytes, write logs to dmesg");
