// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define DEV_NAME	"chardrv1"
#define DEV_COUNT	1
#define KBUF_SIZE	256

static dev_t dev_num;
static struct cdev chardrv1_cdev;
static struct class *chardrv1_class;

/* 核心內部的小緩衝區，模擬這個裝置「存了什麼資料」 */
static char kbuf[KBUF_SIZE];
static size_t kbuf_len;	/* 目前緩衝區裡實際有效的資料長度 */

static ssize_t chardrv1_read(struct file *file, char __user *buf,
			      size_t size, loff_t *off)
{
	size_t remaining;
	size_t to_copy;

	/* *off 代表這個 fd 目前讀到哪裡了，超過資料長度就是 EOF */
	if (*off >= kbuf_len)
		return 0;		/* 真正的 EOF，不是隨便寫死的 0 */

	remaining = kbuf_len - *off;
	to_copy = min(size, remaining);	/* 不能給超過使用者要求的量 */

	if (copy_to_user(buf, kbuf + *off, to_copy))
		return -EFAULT;		/* 使用者緩衝區不可存取 */

	*off += to_copy;
	pr_info("read %zu bytes (offset now %lld)\n", to_copy, *off);

	return to_copy;		/* 回傳「實際」複製了幾個 bytes */
}

static ssize_t chardrv1_write(struct file *file, const char __user *buf,
			       size_t size, loff_t *off)
{
	size_t to_copy = min(size, (size_t)KBUF_SIZE);

	if (copy_from_user(kbuf, buf, to_copy))
		return -EFAULT;		/* 使用者傳的位址讀不到 */

	kbuf_len = to_copy;
	*off = 0;		/* 每次寫入後，讀取位置重置回開頭 */

	pr_info("write %zu bytes\n", to_copy);

	return to_copy;		/* 回傳「實際」寫入了幾個 bytes */
}

static int chardrv1_open(struct inode *inode, struct file *file)
{
	pr_info("open\n");
	return 0;
}

static int chardrv1_release(struct inode *inode, struct file *file)
{
	pr_info("release\n");
	return 0;
}

static const struct file_operations chardrv1_fops = {
	.owner		= THIS_MODULE,
	.read		= chardrv1_read,
	.write		= chardrv1_write,
	.open		= chardrv1_open,
	.release	= chardrv1_release,
};

static int __init chardrv1_init(void)
{
	int ret;

	/* 1. 跟核心要一個 major/minor number */
	ret = alloc_chrdev_region(&dev_num, 0, DEV_COUNT, DEV_NAME);
	if (ret < 0) {
		pr_err("failed to allocate char device region\n");
		return ret;
	}

	/* 2. 把 file_operations 掛進 cdev，並註冊給核心 */
	cdev_init(&chardrv1_cdev, &chardrv1_fops);

	ret = cdev_add(&chardrv1_cdev, dev_num, DEV_COUNT);
	if (ret < 0) {
		pr_err("failed to add cdev\n");
		goto err_unregister;
	}

	/*
	 * 3. 建立一個 class，這是 udev 用來判斷
	 *    "要在 /dev 底下幫哪個裝置建節點" 的依據。
	 *    核心 6.4 之後 class_create() 只吃一個參數
	 *    (舊版多一個 THIS_MODULE，6.4 開始拿掉了)。
	 */
	chardrv1_class = class_create(DEV_NAME);
	if (IS_ERR(chardrv1_class)) {
		ret = PTR_ERR(chardrv1_class);
		pr_err("failed to create class\n");
		goto err_cdev_del;
	}

	/*
	 * 4. 真正觸發 udev 在 /dev 底下建立節點的呼叫。
	 *    這行執行完，/dev/chardrv1 就會自動出現，
	 *    不需要手動 mknod。
	 */
	{
		struct device *dev;

		dev = device_create(chardrv1_class, NULL, dev_num, NULL,
				     DEV_NAME);
		if (IS_ERR(dev)) {
			ret = PTR_ERR(dev);
			pr_err("failed to create device\n");
			goto err_class_destroy;
		}
	}

	pr_info("registered, major = %d, minor = %d\n",
		 MAJOR(dev_num), MINOR(dev_num));

	return 0;

err_class_destroy:
	class_destroy(chardrv1_class);
err_cdev_del:
	cdev_del(&chardrv1_cdev);
err_unregister:
	unregister_chrdev_region(dev_num, DEV_COUNT);
	return ret;
}

static void __exit chardrv1_exit(void)
{
	/* 跟 init 完全相反的順序做清理 */
	device_destroy(chardrv1_class, dev_num);
	class_destroy(chardrv1_class);
	cdev_del(&chardrv1_cdev);
	unregister_chrdev_region(dev_num, DEV_COUNT);
	pr_info("unregistered\n");
}

module_init(chardrv1_init);
module_exit(chardrv1_exit);

MODULE_AUTHOR("Leo");
MODULE_DESCRIPTION("Minimal char driver teaching example (chardrv1)");
MODULE_LICENSE("Dual BSD/GPL");
