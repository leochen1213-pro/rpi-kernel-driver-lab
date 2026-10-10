#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Leo");
MODULE_DESCRIPTION("Our first character device");

static dev_t dev_nr;

static struct cdev my_cdev;

//模組名稱改由 pr_fmt 的 KBUILD_MODNAME 提供
//static const char *my_device = "my_cdev";

static struct class *my_class;
static const struct file_operations fops = { .owner = THIS_MODULE };

static int __init my_init(void)
{
    int status = alloc_chrdev_region(&dev_nr, 0, 1, "my_cdev");
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

    if (IS_ERR(device_create(my_class, NULL, dev_nr, NULL, "my_cdev%d", 0))) {
        status = -ENOMEM;
        goto delete_class;
    }
    
    pr_info("Character device registered, Major number: %d Minor number: %d\n", MAJOR(dev_nr), MINOR(dev_nr));
    pr_info("Created /sys/class/my_class/my_cdev0\n");
    pr_info("Created device node /dev/my_cdev0\n");
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