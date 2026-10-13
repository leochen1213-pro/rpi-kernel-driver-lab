// SPDX-License-Identifier: GPL-2.0
/*
 * Lesson 07-05: the "hardware" side
 *
 * Registers two platform devices named "my_cdev_pdev" with id 0 and 1.
 * On their own they do nothing; once my_cdev_pdrv.ko is also loaded, the
 * platform bus matches the names and calls the driver's probe() for each.
 *
 * Load order does not matter - try both:
 *   insmod my_cdev_pdev.ko ; insmod my_cdev_pdrv.ko   (probe at 2nd insmod)
 *   insmod my_cdev_pdrv.ko ; insmod my_cdev_pdev.ko   (probe at 2nd insmod)
 *
 * On a real board this module would be replaced by Device Tree nodes.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/err.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Leo");
MODULE_DESCRIPTION("Registers two platform devices for my_cdev_pdrv");

#define PDEV_NAME	"my_cdev_pdev"	/* must match the driver's id_table */
#define NR_PDEVS	2

static struct platform_device *pdevs[NR_PDEVS];

static int __init my_pdev_init(void)
{
	int i;

	for (i = 0; i < NR_PDEVS; i++) {
		/*
		 * _simple() allocates the platform_device and gives it a proper
		 * release(), so we don't need a static struct + empty release.
		 * id = i -> sysfs name "my_cdev_pdev.0", "my_cdev_pdev.1"
		 */
		pdevs[i] = platform_device_register_simple(PDEV_NAME, i, NULL, 0);
		if (IS_ERR(pdevs[i])) {
			int status = PTR_ERR(pdevs[i]);

			while (--i >= 0)
				platform_device_unregister(pdevs[i]);
			return status;
		}
	}

	pr_info("registered %d platform devices\n", NR_PDEVS);
	return 0;
}

static void __exit my_pdev_exit(void)
{
	int i;

	/* if the driver is bound, this triggers its remove() first */
	for (i = NR_PDEVS - 1; i >= 0; i--)
		platform_device_unregister(pdevs[i]);
	pr_info("platform devices unregistered\n");
}

module_init(my_pdev_init);
module_exit(my_pdev_exit);
