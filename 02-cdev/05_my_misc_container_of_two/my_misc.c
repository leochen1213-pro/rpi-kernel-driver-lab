// SPDX-License-Identifier: GPL-2.0
/*
 * Lesson 07-04: 03 rewritten with misc devices (no per-open client)
 *
 * Same behaviour as 03_my_cdev_private_data_container_of:
 *
 *   /dev/my_misc0 -> devs[0]   every open of my_misc0 shares devs[0]
 *   /dev/my_misc1 -> devs[1]   independent from my_misc0
 *
 * Only two things differ from 03:
 *
 * 1. Registration
 *    alloc_chrdev_region + cdev_init/cdev_add + class_create +
 *    device_create  --->  one misc_register() per device.
 *    Both devices get major 10 and a dynamic minor.
 *
 * 2. Where container_of() starts
 *    03:   container_of(inode->i_cdev,      struct my_dev, cdev)
 *    here: container_of(file->private_data, struct my_dev, misc)
 *    The cdev belongs to the misc core now, so inode->i_cdev does not
 *    point into our struct. Instead misc_open() sets
 *    file->private_data = &devs[i].misc before calling our open().
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Leo");
MODULE_DESCRIPTION("Per-device state with misc + container_of");

#define NR_DEVS		2
#define DEV_BUFFER_SIZE	64

struct my_dev {
	struct miscdevice misc;		/* embedded: container_of() starts here */
	char name[16];			/* "my_misc0", referenced by misc.name */
	struct mutex lock;		/* protects buf and len */
	char buf[DEV_BUFFER_SIZE];
	size_t len;			/* valid bytes in buf */
	int index;			/* 0 for my_misc0, 1 for my_misc1 */
};

static struct my_dev devs[NR_DEVS];

static int my_open(struct inode *inode, struct file *file)
{
	/*
	 * misc_open() set file->private_data = &devs[i].misc.
	 * Step back from that member to the whole struct my_dev.
	 */
	struct my_dev *d = container_of(file->private_data,
					struct my_dev, misc);

	/* Replace it with the whole struct for read/write/release */
	file->private_data = d;
	pr_info("open my_misc%d (minor %d)\n", d->index, iminor(inode));

	/* "echo text > /dev/my_miscN" opens with O_TRUNC: start empty */
	if ((file->f_mode & FMODE_WRITE) && (file->f_flags & O_TRUNC)) {
		if (mutex_lock_interruptible(&d->lock))
			return -ERESTARTSYS;
		d->len = 0;
		mutex_unlock(&d->lock);
	}
	return 0;
}

static int my_release(struct inode *inode, struct file *file)
{
	struct my_dev *d = file->private_data;

	/* Nothing to free: the state belongs to the device, not to this open */
	pr_info("release my_misc%d\n", d->index);
	return 0;
}

static ssize_t my_read(struct file *file, char __user *ubuf,
		       size_t count, loff_t *ppos)
{
	struct my_dev *d = file->private_data;
	ssize_t ret;

	if (mutex_lock_interruptible(&d->lock))
		return -ERESTARTSYS;
	ret = simple_read_from_buffer(ubuf, count, ppos, d->buf, d->len);
	mutex_unlock(&d->lock);
	return ret;
}

static ssize_t my_write(struct file *file, const char __user *ubuf,
			size_t count, loff_t *ppos)
{
	struct my_dev *d = file->private_data;
	ssize_t ret;

	if (mutex_lock_interruptible(&d->lock))
		return -ERESTARTSYS;

	if (file->f_flags & O_APPEND)		/* "echo text >> ..." */
		*ppos = d->len;

	if (*ppos >= DEV_BUFFER_SIZE) {
		ret = -ENOSPC;
		goto out;
	}
	ret = simple_write_to_buffer(d->buf, DEV_BUFFER_SIZE, ppos, ubuf, count);
	if (ret > 0 && *ppos > d->len)
		d->len = *ppos;
out:
	mutex_unlock(&d->lock);
	return ret;
}

static const struct file_operations fops = {
	.owner   = THIS_MODULE,
	.open    = my_open,
	.release = my_release,
	.read    = my_read,
	.write   = my_write,
	.llseek  = default_llseek,
};

static int __init my_init(void)
{
	int status, i;

	for (i = 0; i < NR_DEVS; i++) {
		struct my_dev *d = &devs[i];

		/* Everything open() may touch is ready before misc_register() */
		mutex_init(&d->lock);
		d->len = 0;
		d->index = i;
		snprintf(d->name, sizeof(d->name), "my_misc%d", i);

		d->misc.minor = MISC_DYNAMIC_MINOR;
		d->misc.name  = d->name;	/* becomes /dev/my_miscN */
		d->misc.fops  = &fops;		/* both devices share the same fops */
		d->misc.mode  = 0666;		/* lab use only */

		status = misc_register(&d->misc);
		if (status) {
			mutex_destroy(&d->lock);
			goto undo_devs;
		}
		pr_info("/dev/%s registered, major 10 minor %d\n",
			d->name, d->misc.minor);
	}
	return 0;

undo_devs:
	/* Undo only the devices that were fully set up, newest first */
	while (--i >= 0) {
		misc_deregister(&devs[i].misc);
		mutex_destroy(&devs[i].lock);
	}
	return status;
}

static void __exit my_exit(void)
{
	int i;

	for (i = NR_DEVS - 1; i >= 0; i--) {
		misc_deregister(&devs[i].misc);
		mutex_destroy(&devs[i].lock);
	}
	pr_info("Goodbye, Kernel\n");
}

module_init(my_init);
module_exit(my_exit);
