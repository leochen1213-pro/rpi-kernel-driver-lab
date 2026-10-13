# 06：用 `module_platform_driver()` 接 04 的 misc device

05 把 char device 接上了 platform driver，但還需要自己寫 `module_init` / `module_exit`。這一課把它拿掉。

## 為什麼 05 不能用 `module_platform_driver()`

05 的 `my_init()` 裡有兩樣東西是**兩個裝置共用的**，必須在第一次 `probe` 之前建立、最後一次 `remove` 之後才釋放：

```c
alloc_chrdev_region(...)   /* dev_t 範圍 */
class_create("my_class")   /* class */
```

`module_platform_driver()` 產生的 init 只會呼叫 `platform_driver_register()`，沒有地方放這些。

## misc 讓共用資源消失

改用 04 的 misc device 之後：

| 共用資源 | 05（cdev） | 06（misc） |
|---|---|---|
| dev_t 範圍 | `alloc_chrdev_region` | misc core 擁有 major 10 |
| minor | 自己配 | `MISC_DYNAMIC_MINOR` |
| class | `class_create` | misc core 擁有 `/sys/class/misc` |
| `/dev` 節點 | `device_create` | `misc_register` 一次完成 |

模組層級沒有共用的東西了，每個裝置的建立和拆除都可以完全放在 `probe` / `remove` 裡，`module_init` 也就不需要了：

```c
module_platform_driver(my_pdrv);
```

它展開後就是：

```c
static int __init my_pdrv_init(void)
{ return platform_driver_register(&my_pdrv); }
static void __exit my_pdrv_exit(void)
{ platform_driver_unregister(&my_pdrv); }
module_init(my_pdrv_init);
module_exit(my_pdrv_exit);
```

## 從 04 搬了什麼

```
04: my_init()                          06: my_probe(pdev)     ← 每個裝置一次
    for (i = 0; i < NR_DEVS; i++) {        i = pdev->id
        mutex_init                         snprintf(name)
        snprintf(name)                     misc 欄位設定 + parent = &pdev->dev
        misc 欄位設定                       misc_register
        misc_register
    }                                  06: my_remove(pdev)
                                           misc_deregister
04: my_exit()
    for (...) misc_deregister          06: module_platform_driver(my_pdrv)
```

`open` / `read` / `write` / `release`、`struct my_client` 都**沒有改**，`test_devs.c` 直接沿用 04 的版本。

## 兩個要注意的細節

**1. mutex 改成靜態初始化**

沒有 `module_init` 之後，就沒有「載入時只執行一次」的地方可以呼叫 `mutex_init`。放在 `probe` 裡也不行：如果 unbind 時還有人開著檔案，再 bind 回來時，`probe` 會重新初始化一把可能正被使用的鎖。

所以改成在編譯期就初始化好：

```c
static struct my_dev devs[NR_DEVS] = {
	[0] = { .lock = __MUTEX_INITIALIZER(devs[0].lock) },
	[1] = { .lock = __MUTEX_INITIALIZER(devs[1].lock) },
};
```

**2. 每次 `probe` 都要重設 `misc.minor`**

`misc_deregister()` 不會把 `minor` 還原成 `MISC_DYNAMIC_MINOR`（Linux 6.12 原始碼已確認）。如果 `probe` 不重設，unbind 再 bind 時，就會沿用上一次拿到的號碼。

## 測試

```sh
make

# 先清掉其他課的模組，避免撞名
lsmod | grep my_
sudo rmmod my_misc my_cdev my_cdev_pdrv my_cdev_pdev 2>/dev/null

# 兩種順序都可以
sudo insmod my_misc_pdev.ko
sudo insmod my_misc_pdrv.ko
dmesg | tail -2              # probe -> /dev/my_misc0、my_misc1

./test_devs /dev/my_misc0 /dev/my_misc1

ls -l /sys/class/misc/my_misc0
# -> ../../devices/platform/my_misc_pdev.0/misc/my_misc0   ← 有 parent
# 04 是 -> ../../devices/virtual/misc/my_misc0

# 只拆一個
echo my_misc_pdev.1 | sudo tee /sys/bus/platform/drivers/my_misc_pdrv/unbind
echo my_misc_pdev.1 | sudo tee /sys/bus/platform/drivers/my_misc_pdrv/bind
dmesg | tail -2              # 注意 minor 號碼

sudo rmmod my_misc_pdrv my_misc_pdev
```

## 剩下的限制（下一課）

和 05 一樣，`devs[]` 仍然是靜態陣列，用 `pdev->id` 當索引：

- 裝置數量寫死，而且每加一個裝置就要多寫一行 `__MUTEX_INITIALIZER`
- 改用 Device Tree 時 `pdev->id` 通常是 `-1`，這個索引方式就不能用了

下一步是在 `probe` 裡動態配置 `my_dev`：每個裝置在 `probe` 時 `kzalloc`，並在那時 `mutex_init`，靜態陣列和上面的兩個細節就都不需要了。代價是要處理生命週期：檔案還開著時不能 `kfree`，所以要加 `kref`。
