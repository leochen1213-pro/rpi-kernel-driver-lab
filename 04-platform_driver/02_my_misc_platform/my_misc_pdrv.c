// SPDX-License-Identifier: GPL-2.0
/*
 * Lesson 07-06: 04's misc device, bound through module_platform_driver()
 *
 * 05 still needed its own module_init()/module_exit(), because two things
 * were SHARED by all devices and had to exist before the first probe():
 *   - the dev_t range   (alloc_chrdev_region)
 *   - the class         (class_create)
 *
 * With misc, both belong to the misc core (major 10, /sys/class/misc), so
 * nothing is shared at module level any more. Every device is set up and
 * torn down entirely in probe()/remove(), and module_platform_driver()
 * writes module_init/exit for us.
 *
 *   my_misc_pdev.ko registers "my_misc_pdev.0"  --probe-->  /dev/my_misc0
 *                             "my_misc_pdev.1"  --probe-->  /dev/my_misc1
 *
 * open/read/write/release and struct my_client are unchanged from 04.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Leo");
MODULE_DESCRIPTION("04 misc device bound through module_platform_driver()");

#define PDEV_NAME	"my_misc_pdev"	/* must match my_misc_pdev.c */
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

/*
 * No module_init() any more, so there is no "run once at load" place to
 * mutex_init() the locks. They can't go in probe() either: after an
 * unbind/bind with a file still open, probe() would re-initialise a mutex
 * that may be in use. So the locks are initialised statically, at compile
 * time. (Next lesson: allocate my_dev in probe() and this goes away.)
 */
static_assert(NR_DEVS == 2, "add a devs[] initializer for each device");
static struct my_dev devs[NR_DEVS] = {
	[0] = { .lock = __MUTEX_INITIALIZER(devs[0].lock) },
	[1] = { .lock = __MUTEX_INITIALIZER(devs[1].lock) },
};

static atomic_t next_client_id = ATOMIC_INIT(0);

/* ---------------- file_operations: unchanged from 04 ---------------- */

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

/* ---------------- platform driver ---------------- */

/* probe() = the body of 04's my_init() loop, for ONE device */
static int my_probe(struct platform_device *pdev)
{
	int i = pdev->id;
	struct my_dev *d;
	int status;

	if (i < 0 || i >= NR_DEVS) {
		dev_err(&pdev->dev, "id %d out of range 0..%d\n", i, NR_DEVS - 1);
		return -EINVAL;
	}
	d = &devs[i];

	mutex_lock(&d->lock);
	d->len = 0;
	mutex_unlock(&d->lock);
	snprintf(d->name, sizeof(d->name), "my_misc%d", i);

	/*
	 * Set every field again on each probe: misc_deregister() does not
	 * reset misc.minor, so after unbind/bind a stale minor would be reused.
	 */
	d->misc.minor  = MISC_DYNAMIC_MINOR;
	d->misc.name   = d->name;		/* becomes /dev/my_miscN */
	d->misc.fops   = &fops;
	d->misc.mode   = 0666;			/* lab use only */
	d->misc.parent = &pdev->dev;		/* NEW: hang under the pdev in sysfs */

	/* After this succeeds the device may be opened at once */
	status = misc_register(&d->misc);
	if (status)
		return status;

	platform_set_drvdata(pdev, d);
	dev_info(&pdev->dev, "probe -> /dev/%s (major 10 minor %d)\n",
		 d->name, d->misc.minor);
	return 0;
}

/* remove() = 04's my_exit() loop body, for ONE device */
static void my_remove(struct platform_device *pdev)
{
	struct my_dev *d = platform_get_drvdata(pdev);

	misc_deregister(&d->misc);
	dev_info(&pdev->dev, "remove -> /dev/%s gone\n", d->name);
}

static const struct platform_device_id my_id_table[] = {
	{ PDEV_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(platform, my_id_table);

static struct platform_driver my_pdrv = {
	.probe    = my_probe,
	.remove   = my_remove,
	.id_table = my_id_table,
	.driver   = {
		.name = "my_misc_pdrv",
	},
};

/*
 * Replaces module_init()/module_exit(). It expands to:
 *   static int __init my_pdrv_init(void)
 *   { return platform_driver_register(&my_pdrv); }
 *   static void __exit my_pdrv_exit(void)
 *   { platform_driver_unregister(&my_pdrv); }
 *   module_init(my_pdrv_init);
 *   module_exit(my_pdrv_exit);
 */
module_platform_driver(my_pdrv);
