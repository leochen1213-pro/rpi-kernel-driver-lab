// SPDX-License-Identifier: GPL-2.0
/*
 * Lesson 07-04: misc device + per-open client (Bootlin / evdev style)
 *
 * Two ideas combined:
 *
 * 1. Misc device framework (what the Bootlin course uses).
 *    misc_register() replaces alloc_chrdev_region + cdev + class +
 *    device_create: misc devices share major 10, get a dynamic minor, and
 *    udev creates /dev/<name>. Before calling our open(), the misc core
 *    sets file->private_data to our struct miscdevice, so container_of()
 *    gets back to the whole struct my_dev.
 *
 * 2. Two levels of state (the drivers/input/evdev.c pattern).
 *    struct my_dev    - one per device, shared by every open (the data)
 *    struct my_client - one per open (who is reading, how much)
 *    The client points at its device, and file->private_data points at
 *    the client.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Leo");
MODULE_DESCRIPTION("Misc device with per-device data and per-open clients");

#define NR_DEVS		2
#define DEV_BUFFER_SIZE	64

/* Per-device state: shared by every open of /dev/my_miscN */
struct my_dev {
	struct miscdevice misc;		/* embedded: container_of() starts here */
	char name[16];			/* "my_misc0", referenced by misc.name */
	struct mutex lock;		/* protects buf, len and nr_clients */
	char buf[DEV_BUFFER_SIZE];
	size_t len;
	int nr_clients;			/* currently open */
};

/* Per-open state: one for each open() */
struct my_client {
	struct my_dev *dev;		/* which device this client uses */
	int id;				/* client number, for the log */
	size_t bytes_read;
	size_t bytes_written;
};

static struct my_dev devs[NR_DEVS];
static atomic_t next_client_id = ATOMIC_INIT(0);

static int my_open(struct inode *inode, struct file *file)
{
	/* Set by the misc core to &devs[i].misc before calling us */
	struct my_dev *d = container_of(file->private_data,
					struct my_dev, misc);
	struct my_client *c;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return -ENOMEM;
	c->dev = d;
	c->id = atomic_inc_return(&next_client_id);

	if (mutex_lock_interruptible(&d->lock)) {
		kfree(c);
		return -ERESTARTSYS;
	}
	if ((file->f_mode & FMODE_WRITE) && (file->f_flags & O_TRUNC))
		d->len = 0;
	d->nr_clients++;
	mutex_unlock(&d->lock);

	/* From now on read/write/release see the client, not the device */
	file->private_data = c;
	pr_info("%s: client %d opened (%d open)\n",
		d->name, c->id, d->nr_clients);
	return 0;
}

static int my_release(struct inode *inode, struct file *file)
{
	struct my_client *c = file->private_data;
	struct my_dev *d = c->dev;

	mutex_lock(&d->lock);
	d->nr_clients--;
	mutex_unlock(&d->lock);

	pr_info("%s: client %d closed, read %zu, wrote %zu (%d still open)\n",
		d->name, c->id, c->bytes_read, c->bytes_written,
		d->nr_clients);
	kfree(c);			/* frees the client, never the device */
	return 0;
}

static ssize_t my_read(struct file *file, char __user *ubuf,
		       size_t count, loff_t *ppos)
{
	struct my_client *c = file->private_data;
	struct my_dev *d = c->dev;
	ssize_t ret;

	if (mutex_lock_interruptible(&d->lock))
		return -ERESTARTSYS;
	ret = simple_read_from_buffer(ubuf, count, ppos, d->buf, d->len);
	mutex_unlock(&d->lock);

	if (ret > 0)
		c->bytes_read += ret;	/* only this client touches c */
	return ret;
}

static ssize_t my_write(struct file *file, const char __user *ubuf,
			size_t count, loff_t *ppos)
{
	struct my_client *c = file->private_data;
	struct my_dev *d = c->dev;
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

	if (ret > 0)
		c->bytes_written += ret;
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

		mutex_init(&d->lock);
		snprintf(d->name, sizeof(d->name), "my_misc%d", i);

		d->misc.minor = MISC_DYNAMIC_MINOR;
		d->misc.name = d->name;		/* becomes /dev/my_miscN */
		d->misc.fops = &fops;
		d->misc.mode = 0666;		/* lab use only */

		/* After this succeeds the device may be opened at once */
		status = misc_register(&d->misc);
		if (status)
			goto undo;
		pr_info("/dev/%s registered, major 10 minor %d\n",
			d->name, d->misc.minor);
	}
	return 0;

undo:
	while (--i >= 0)
		misc_deregister(&devs[i].misc);
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
