// SPDX-License-Identifier: GPL-2.0
/*
 * Lesson 07-04 (simplest): one misc device, no per-open client
 *
 * Same behaviour as 03_my_cdev_private_data_container_of, but registered
 * with the misc framework instead of cdev:
 *
 *   03 (cdev)                         this file (misc)
 *   alloc_chrdev_region  \
 *   cdev_init/cdev_add    |  ---->    misc_register()
 *   class_create          |
 *   device_create        /
 *
 *   container_of(inode->i_cdev, ...)  container_of(file->private_data, ...)
 *
 * The misc core owns the cdev (major 10). Before calling our open(),
 * misc_open() sets file->private_data = &mydev.misc, so we step back
 * from that member to the whole struct my_dev.
 *
 * Only one level of state: every open shares mydev.
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
MODULE_DESCRIPTION("Single misc device, shared state");

#define DEV_BUFFER_SIZE	64

struct my_dev {
	struct miscdevice misc;		/* embedded: container_of() starts here */
	struct mutex lock;		/* protects buf and len */
	char buf[DEV_BUFFER_SIZE];
	size_t len;			/* valid bytes in buf */
};

static struct my_dev mydev;		/* the one and only device */

static int my_open(struct inode *inode, struct file *file)
{
	/* misc core set file->private_data = &mydev.misc before calling us */
	struct my_dev *d = container_of(file->private_data,
					struct my_dev, misc);

	/* Replace it with the whole struct for read/write/release */
	file->private_data = d;
	pr_info("open (minor %d)\n", iminor(inode));

	/* "echo text > /dev/my_misc" opens with O_TRUNC: start empty */
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
	/* Nothing to free: the state belongs to the device, not to this open */
	pr_info("release\n");
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
	int status;

	/* Everything open() may touch is ready before misc_register() */
	mutex_init(&mydev.lock);
	mydev.len = 0;

	mydev.misc.minor = MISC_DYNAMIC_MINOR;	/* let the kernel pick */
	mydev.misc.name  = "my_misc";		/* becomes /dev/my_misc */
	mydev.misc.fops  = &fops;
	mydev.misc.mode  = 0666;		/* lab use only */

	/* After this succeeds the device may be opened at once */
	status = misc_register(&mydev.misc);
	if (status) {
		mutex_destroy(&mydev.lock);
		return status;
	}

	pr_info("/dev/my_misc registered, major 10 minor %d\n", mydev.misc.minor);
	return 0;
}

static void __exit my_exit(void)
{
	misc_deregister(&mydev.misc);
	mutex_destroy(&mydev.lock);
	pr_info("Goodbye, Kernel\n");
}

module_init(my_init);
module_exit(my_exit);
