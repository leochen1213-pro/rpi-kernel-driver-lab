// SPDX-License-Identifier: GPL-2.0
/*
 * Lesson 07-05: the 03 char device, driven by a platform driver
 *
 * Same struct my_dev, same file_operations as 03. What changed is WHO
 * decides when a /dev node appears:
 *
 *   03: module_init() loops i = 0..NR_DEVS-1 and creates every node itself
 *   05: module_init() only prepares shared resources; the platform buscd
 *       calls probe() once per matching platform_device, and probe()
 *       creates that one node. remove() is the reverse of probe().
 *
 *   my_cdev_pdev.ko registers "my_cdev_pdev.0"  --probe-->  /dev/my_cdev0
 *                              "my_cdev_pdev.1"  --probe-->  /dev/my_cdev1
 *
 * Kept simple on purpose: the per-device structs are still a static array,
 * indexed by pdev->id. Dynamic allocation comes in the next lesson.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Leo");
MODULE_DESCRIPTION("03 char device bound through a platform driver");

#define PDEV_NAME	"my_cdev_pdev"	/* must match my_cdev_pdev.c */
#define NR_DEVS		2		/* how many minors we reserve */
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

/* ---------------- file_operations: unchanged from 03 ---------------- */

static int my_open(struct inode *inode, struct file *file)
{
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

/* ---------------- platform driver: NEW in 05 ---------------- */

/*
 * probe() = the body of 03's for-loop, for ONE device.
 * The platform bus tells us which one through pdev->id.
 */
static int my_probe(struct platform_device *pdev)
{
	int i = pdev->id;
	struct my_dev *d;
	struct device *dev;
	int status;

	if (i < 0 || i >= NR_DEVS) {
		dev_err(&pdev->dev, "id %d out of range 0..%d\n", i, NR_DEVS - 1);
		return -EINVAL;
	}
	d = &devs[i];

	mutex_lock(&d->lock);
	d->len = 0;
	d->index = i;
	mutex_unlock(&d->lock);

	cdev_init(&d->cdev, &fops);
	d->cdev.owner = THIS_MODULE;
	status = cdev_add(&d->cdev, my_devt(i), 1);
	if (status)
		return status;

	/*
	 * parent = &pdev->dev (03 passed NULL): the node now hangs under the
	 * platform device in sysfs, so you can see which device it came from:
	 *   /sys/devices/platform/my_cdev_pdev.0/my_class/my_cdev0
	 */
	dev = device_create(my_class, &pdev->dev, my_devt(i), NULL,
			    "my_cdev%d", i);
	if (IS_ERR(dev)) {
		cdev_del(&d->cdev);
		return PTR_ERR(dev);
	}

	/* remember which my_dev belongs to this pdev, for remove() */
	platform_set_drvdata(pdev, d);
	dev_info(&pdev->dev, "probe -> /dev/my_cdev%d\n", i);
	return 0;
}

/* remove() = the undo path of 03's loop, for ONE device. */
static void my_remove(struct platform_device *pdev)
{
	struct my_dev *d = platform_get_drvdata(pdev);

	device_destroy(my_class, my_devt(d->index));
	cdev_del(&d->cdev);
	dev_info(&pdev->dev, "remove -> /dev/my_cdev%d gone\n", d->index);
}

/* match by name: "my_cdev_pdev" from my_cdev_pdev.c */
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
		.name = "my_cdev_pdrv",
	},
};

/* ---------------- module init/exit: only shared resources ---------------- */

static int __init my_init(void)
{
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

	/*
	 * Init the locks once, here, not in probe(): if a device is unbound
	 * and bound again while someone still holds the file open, probe()
	 * must not re-initialise a mutex that may be in use.
	 */
	for (i = 0; i < NR_DEVS; i++)
		mutex_init(&devs[i].lock);

	pr_info("major %d, registering platform driver\n", MAJOR(dev_base));

	/* must be last: probe() can run inside this call */
	status = platform_driver_register(&my_pdrv);
	if (status)
		goto destroy_class;

	return 0;

destroy_class:
	class_destroy(my_class);
free_region:
	unregister_chrdev_region(dev_base, NR_DEVS);
	return status;
}

static void __exit my_exit(void)
{
	int i;

	/* calls remove() for every bound device first */
	platform_driver_unregister(&my_pdrv);

	for (i = 0; i < NR_DEVS; i++)
		mutex_destroy(&devs[i].lock);
	class_destroy(my_class);
	unregister_chrdev_region(dev_base, NR_DEVS);
	pr_info("Goodbye, Kernel\n");
}

module_init(my_init);
module_exit(my_exit);
