// SPDX-License-Identifier: GPL-2.0
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
MODULE_DESCRIPTION("Character device with a per-open buffer in private_data");

#define DEV_BUFFER_SIZE 64

static dev_t dev_nr;
static struct cdev my_cdev;
static struct class *my_class;

/* State that belongs to ONE open file, not to the device */
struct my_file {
	struct mutex lock;	/* the same fd may be shared by threads or fork() */
	char buf[DEV_BUFFER_SIZE];
	size_t len;		/* valid bytes in buf */
};

static int my_open(struct inode *inode, struct file *file)
{
	struct my_file *f;
 
	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;
	mutex_init(&f->lock);
 
	/* Hang the per-open state on struct file; read/write get it back */
	file->private_data = f;
 
	pr_info("open: minor %d, allocated %zu bytes at %p\n", iminor(inode), sizeof(*f), f);
	return 0;
}
 
static int my_release(struct inode *inode, struct file *file)
{
	struct my_file *f = file->private_data;
 
	/* release() runs once, after the LAST close of this struct file */
	pr_info("release: freeing %p (held %zu bytes of data)\n", f, f->len);
    mutex_destroy(&f->lock);
	kfree(f);
	return 0;
}

static ssize_t my_read(struct file *file, char __user *ubuf,
		       size_t count, loff_t *ppos)
{
	struct my_file *f = file->private_data;
	ssize_t ret;
 
	if (mutex_lock_interruptible(&f->lock))
		return -ERESTARTSYS;
	ret = simple_read_from_buffer(ubuf, count, ppos, f->buf, f->len);
	mutex_unlock(&f->lock);
 
	pr_info("read  %p: requested %zu, returned %zd, offset %lld\n", f, count, ret, *ppos);
	return ret;
}

static ssize_t my_write(struct file *file, const char __user *ubuf,
			size_t count, loff_t *ppos)
{
	struct my_file *f = file->private_data;
	ssize_t ret;
 
	if (mutex_lock_interruptible(&f->lock))
		return -ERESTARTSYS;
 
	if (*ppos >= DEV_BUFFER_SIZE) {
		ret = -ENOSPC;
		goto out;
	}
	ret = simple_write_to_buffer(f->buf, DEV_BUFFER_SIZE, ppos, ubuf, count);
	if (ret > 0 && *ppos > f->len)
		f->len = *ppos;
out:
	mutex_unlock(&f->lock);
	pr_info("write %p: requested %zu, returned %zd, length %zu\n",
		f, count, ret, f->len);
	return ret;
}

static const struct file_operations fops = {
	.owner   = THIS_MODULE,
	.open    = my_open,
	.release = my_release,
	.read    = my_read,
	.write   = my_write,
	.llseek  = default_llseek,	/* lets the test lseek() back to 0 */
};

/* Make /dev/my_cdev0 readable and writable without sudo (lab use only) */
static char *my_devnode(const struct device *dev, umode_t *mode)
{
	if (mode)
		*mode = 0666;
	return NULL;
}

static int __init my_init(void)
{
    //struct device *dev;

    int status = alloc_chrdev_region(&dev_nr, 0, 1, KBUILD_MODNAME);
    if (status)
        return status;

    cdev_init(&my_cdev, &fops);
    my_cdev.owner = THIS_MODULE;

    status = cdev_add(&my_cdev, dev_nr, 1);
    if (status)
        goto free_device_nr;

    my_class = class_create("my_class");
    if (IS_ERR(my_class)) {
        status = PTR_ERR(my_class);
        goto delete_cdev;
    }

    my_class->devnode = my_devnode;     /* 新增：讓節點權限變成 0666 */

    if (IS_ERR(device_create(my_class, NULL, dev_nr, NULL, "my_cdev%d", 0))) {
        status = -ENOMEM;
        goto delete_class;
    }
    
    pr_info("Character device registered, Major number: %d Minor number: %d\n", MAJOR(dev_nr), MINOR(dev_nr));
    //pr_info("Created /sys/class/my_class/my_cdev0\n");
    //pr_info("Created device node /dev/my_cdev0\n");
    return 0;

delete_class:
    class_destroy(my_class);

delete_cdev:
    cdev_del(&my_cdev);

free_device_nr:
    unregister_chrdev_region(dev_nr, 1);
    return status;
}

static void __exit my_exit(void)
{
    device_destroy(my_class, dev_nr);
    class_destroy(my_class);
    cdev_del(&my_cdev);
    unregister_chrdev_region(dev_nr, 1);
    pr_info("Goodbye, Kernel\n");
}

module_init(my_init);
module_exit(my_exit);