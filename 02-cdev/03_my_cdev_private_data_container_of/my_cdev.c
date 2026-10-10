// SPDX-License-Identifier: GPL-2.0
/*
 * Lesson 07-03: per-device state found with container_of()
 *
 * Same registration flow as the MP Coding lessons (alloc_chrdev_region,
 * cdev, class, device_create), but the state lives in one struct per
 * device instead of global variables:
 *
 *   /dev/my_cdev0 -> devs[0]   every open of my_cdev0 shares devs[0]
 *   /dev/my_cdev1 -> devs[1]   independent from my_cdev0
 *
 * open() recovers the right struct from inode->i_cdev with container_of()
 * and stores it in file->private_data for read/write.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Leo");
MODULE_DESCRIPTION("Per-device state with container_of");

#define NR_DEVS		2
#define DEV_BUFFER_SIZE	64

struct my_dev {
	struct cdev cdev;		/* embedded: container_of() starts here */
	struct mutex lock;		/* protects buf and len */
	char buf[DEV_BUFFER_SIZE];
	size_t len;			/* valid bytes in buf */
	int index;			/* 0 for my_cdev0, 1 for my_cdev1 */
};

static dev_t dev_base;			/* first major:minor of our range */
static struct class *my_class;
static struct my_dev devs[NR_DEVS];

static dev_t my_devt(int i)
{
	return MKDEV(MAJOR(dev_base), MINOR(dev_base) + i);
}

static int my_open(struct inode *inode, struct file *file)
{
	/*
	 * inode->i_cdev points at the cdev we registered for this minor,
	 * which sits inside a struct my_dev. Step back to the whole struct.
	 */
	struct my_dev *d = container_of(inode->i_cdev, struct my_dev, cdev);

	file->private_data = d;
	pr_info("open my_cdev%d (minor %d)\n", d->index, iminor(inode));

	/* "echo text > /dev/my_cdevN" opens with O_TRUNC: start empty */
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
	pr_info("release my_cdev%d\n", d->index);
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

/* Make the nodes readable and writable without sudo (lab use only) */
static char *my_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;
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

		/* Everything open() may touch is ready before cdev_add() */
		mutex_init(&d->lock);
		d->len = 0;
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
	/* Undo only the devices that were fully set up, newest first */
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