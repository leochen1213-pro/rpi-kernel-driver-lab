// SPDX-License-Identifier: GPL-2.0
/*
 * lm75_demo.c - 教學用的 LM75 I2C 溫度感測器 driver
 *
 * 目標：搞懂 I2C 子系統怎麼跟 device tree 接起來、
 *       probe/remove 的生命週期、以及透過 sysfs 把資料
 *       暴露給 userspace。
 *
 * LM75 暫存器說明：
 *   0x00 溫度暫存器，16-bit，但只有高 11 bit 有效，
 *        每個 LSB 代表 0.125°C，採二補數(signed)格式。
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/device.h>
#include <linux/err.h>

#define LM75_REG_TEMP	0x00

struct lm75_demo_data {
	struct i2c_client *client;
};

/*
 * sysfs 讀取函式：對應 /sys/.../temperature 這個檔案
 * 被 cat 的時候會呼叫這裡。
 */
static ssize_t temperature_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	s32 raw;
	s16 temp_raw;
	int temp_milli;

	/*
	 * i2c_smbus_read_word_swapped()：LM75 的暫存器是
	 * big-endian(高位元組在前)，但 SMBus word 讀取預設是
	 * little-endian，所以要用 "swapped" 版本自動幫你交換
	 * 兩個 byte 的順序，否則數值會錯得離譜。
	 */
	raw = i2c_smbus_read_word_swapped(client, LM75_REG_TEMP);
	if (raw < 0) {
		dev_err(dev, "讀取溫度暫存器失敗: %d\n", raw);
		return raw;
	}

	temp_raw = (s16)raw;
	temp_raw >>= 5;		/* 只有高 11 bit 是有效數據 */
	temp_milli = temp_raw * 125;	/* 每個 LSB = 0.125°C = 125 毫度 */

	return sprintf(buf, "%d\n", temp_milli);
}
static DEVICE_ATTR_RO(temperature);

static struct attribute *lm75_demo_attrs[] = {
	&dev_attr_temperature.attr,
	NULL,
};
ATTRIBUTE_GROUPS(lm75_demo);

/*
 * probe()：當 i2c core 發現 device tree 裡的節點跟這個
 * driver 的 of_match_table 對得上時，就會呼叫這裡。
 * 可以想成是 driver 的「建構子」。
 */
static int lm75_demo_probe(struct i2c_client *client)
{
	struct lm75_demo_data *data;

	dev_info(&client->dev,
		 "lm75_demo: probe 成功，裝置位址 0x%02x\n", client->addr);

	/*
	 * devm_kzalloc：用 device-managed 的配置方式，
	 * 裝置被移除時核心會自動幫你釋放記憶體，
	 * 不用自己在 remove() 裡手動 kfree()。
	 */
	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	i2c_set_clientdata(client, data);

	return 0;
}

/*
 * remove()：裝置被移除(例如 rmmod，或 device tree overlay
 * 被卸載)時呼叫，可以想成是「解構子」。
 */
static void lm75_demo_remove(struct i2c_client *client)
{
	dev_info(&client->dev, "lm75_demo: remove，裝置移除\n");
}

/* 對應 device tree 裡的 compatible 字串 */
static const struct of_device_id lm75_demo_of_match[] = {
	{ .compatible = "myvendor,lm75-demo" },
	{ }
};
MODULE_DEVICE_TABLE(of, lm75_demo_of_match);

/* 沒有 device tree 時的 fallback 比對表(例如手動 instantiate) */
static const struct i2c_device_id lm75_demo_id[] = {
	{ "lm75-demo", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm75_demo_id);

static struct i2c_driver lm75_demo_driver = {
	.driver = {
		.name = "lm75-demo",
		.of_match_table = lm75_demo_of_match,
		.dev_groups = lm75_demo_groups,
	},
	.probe = lm75_demo_probe,
	.remove = lm75_demo_remove,
	.id_table = lm75_demo_id,
};

module_i2c_driver(lm75_demo_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Leo");
MODULE_DESCRIPTION("教學用 LM75 I2C 溫度感測器 driver");
