// SPDX-License-Identifier: GPL-2.0
/*
 * Lesson 08: ioctl, built on the 07-03 per-device driver
 *
 * Same as 07-03 (two nodes, per-device state found with container_of),
 * plus .unlocked_ioctl / .compat_ioctl. The command numbers live in
 * my_cdev_ioctl.h, shared with the test program.
 *
 * read/write carry DATA; ioctl carries CONTROL (clear, get/set a setting,
 * query status).
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "my_cdev_ioctl.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Leo");
MODULE_DESCRIPTION("Per-device char driver with ioctl");

#define NR_DEVS			2
#define DEV_BUFFER_SIZE		64
#define MYCDEV_DEFAULT_VALUE	0xc0ffee

struct my_dev {
	struct cdev cdev;		/* embedded: container_of() starts here */
	struct mutex lock;		/* protects buf, len and value */
	char buf[DEV_BUFFER_SIZE];
	size_t len;
	s32 value;			/* a "setting", changed through ioctl */
	int index;
};

static dev_t dev_base;
static struct class *my_class;
static struct my_dev devs[NR_DEVS];

static dev_t my_devt(int i)
{
	return MKDEV(MAJOR(dev_base), MINOR(dev_base) + i);
}

static int my_open(struct inode *inode, struct file *file)
{
	struct my_dev *d = container_of(inode->i_cdev, struct my_dev, cdev);

	file->private_data = d;

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

	if (file->f_flags & O_APPEND)
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

static long my_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct my_dev *d = file->private_data;
	void __user *uarg = (void __user *)arg;	/* arg is a user pointer */
	struct mycdev_info info;
	s32 value;
	long ret = 0;

	/* Cheap first filter: not our magic -> not our command */
	if (_IOC_TYPE(cmd) != MYCDEV_MAGIC)
		return -ENOTTY;

	switch (cmd) {
	case MYCDEV_SAY_HELLO:
		/* Touches no shared state, so no lock needed */
		pr_info("hello from my_cdev%d via ioctl\n", d->index);
		return 0;

	case MYCDEV_SET_VALUE:
		/*
		 * Copy from user space BEFORE taking the lock: get_user() may
		 * sleep on a page fault, and the lock should only cover the
		 * shared data.
		 */
		if (get_user(value, (s32 __user *)uarg))
			return -EFAULT;
		if (mutex_lock_interruptible(&d->lock))
			return -ERESTARTSYS;
		d->value = value;
		mutex_unlock(&d->lock);
		return 0;

	case MYCDEV_CLEAR:
	case MYCDEV_GET_VALUE:
	case MYCDEV_GET_INFO:
		break;			/* handled below, under the lock */

	default:
		/* Unknown command: -ENOTTY ("Inappropriate ioctl"), not -EINVAL */
		return -ENOTTY;
	}

	if (mutex_lock_interruptible(&d->lock))
		return -ERESTARTSYS;

	switch (cmd) {
	case MYCDEV_CLEAR:
		memset(d->buf, 0, sizeof(d->buf));
		d->len = 0;
		break;
	case MYCDEV_GET_VALUE:
		value = d->value;
		break;
	case MYCDEV_GET_INFO:
		/* memset first: never leak uninitialized stack bytes (padding) */
		memset(&info, 0, sizeof(info));
		info.index = d->index;
		info.len = d->len;
		info.capacity = DEV_BUFFER_SIZE;
		info.value = d->value;
		break;
	}

	mutex_unlock(&d->lock);

	/* Copy to user space AFTER dropping the lock, from the local copy */
	if (cmd == MYCDEV_GET_VALUE)
		ret = put_user(value, (s32 __user *)uarg);	/* scalar */
	else if (cmd == MYCDEV_GET_INFO &&
		 copy_to_user(uarg, &info, sizeof(info)))	/* struct */
		ret = -EFAULT;

	return ret;
}

static const struct file_operations fops = {
	.owner		= THIS_MODULE,
	.open		= my_open,
	.release	= my_release,
	.read		= my_read,
	.write		= my_write,
	.llseek		= default_llseek,
	.unlocked_ioctl	= my_ioctl,
	/*
	 * 32-bit programs on a 64-bit kernel (e.g. 32-bit Raspberry Pi OS
	 * userland on a 64-bit kernel): our arguments are pointers to
	 * fixed-size data, so the generic pointer conversion is enough.
	 */
	.compat_ioctl	= compat_ptr_ioctl,
};

static char *my_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;		/* lab use only */
	return NULL;
}

static int __init my_init(void)
{
	struct device *dev;
	int status, i;

	status = alloc_chrdev_region(&dev_base, 0, NR_DEVS, KBUILD_MODNAME);
	if (status)
		return status;

	my_class = class_create("my_class");
	if (IS_ERR(my_class)) {
		status = PTR_ERR(my_class);
		goto free_region;
	}
	my_class->devnode = my_devnode;

	for (i = 0; i < NR_DEVS; i++) {
		struct my_dev *d = &devs[i];

		mutex_init(&d->lock);
		d->len = 0;
		d->value = MYCDEV_DEFAULT_VALUE;
		d->index = i;

		cdev_init(&d->cdev, &fops);
		d->cdev.owner = THIS_MODULE;
		status = cdev_add(&d->cdev, my_devt(i), 1);
		if (status)
			goto undo_devs;

		dev = device_create(my_class, NULL, my_devt(i), NULL,
				    "my_cdev%d", i);
		if (IS_ERR(dev)) {
			status = PTR_ERR(dev);
			cdev_del(&d->cdev);
			goto undo_devs;
		}
	}

	pr_info("registered %d devices, major %d\n", NR_DEVS, MAJOR(dev_base));
	return 0;

undo_devs:
	while (--i >= 0) {
		device_destroy(my_class, my_devt(i));
		cdev_del(&devs[i].cdev);
	}
	class_destroy(my_class);
free_region:
	unregister_chrdev_region(dev_base, NR_DEVS);
	return status;
}

static void __exit my_exit(void)
{
	int i;

	for (i = NR_DEVS - 1; i >= 0; i--) {
		device_destroy(my_class, my_devt(i));
		cdev_del(&devs[i].cdev);
		mutex_destroy(&devs[i].lock);
	}
	class_destroy(my_class);
	unregister_chrdev_region(dev_base, NR_DEVS);
	pr_info("Goodbye, Kernel\n");
}

module_init(my_init);
module_exit(my_exit);
