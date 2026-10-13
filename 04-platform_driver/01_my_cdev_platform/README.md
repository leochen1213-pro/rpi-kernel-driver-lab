# 05：用 platform driver 接 03 的 char device

`struct my_dev` 和 `file_operations`（open / read / write / release）**完全沿用 03**。唯一改變的是：**誰決定 `/dev/my_cdevN` 什麼時候出現**。

## 檔案

| 檔案 | 作用 |
|---|---|
| `my_cdev_pdev.c` | 「硬體端」：註冊兩個 platform device `my_cdev_pdev.0`、`.1` |
| `my_cdev_pdrv.c` | 「驅動端」：03 的 char device + platform driver |
| `test_devs.c` | 直接沿用 03 的測試程式 |
| `Makefile` | 沿用 04 的寫法 |

## 從 03 到 05 搬了什麼

```
03: my_init()                         05: my_init()
    alloc_chrdev_region                    alloc_chrdev_region
    class_create                           class_create
    for (i = 0; i < NR_DEVS; i++) {        mutex_init × NR_DEVS
        mutex_init                         platform_driver_register  ← 等裝置出現
        cdev_init / cdev_add
        device_create                  05: my_probe(pdev)      ← 每個裝置呼叫一次
    }                                      i = pdev->id
                                           cdev_init / cdev_add
                                           device_create(parent = &pdev->dev)
                                           platform_set_drvdata

03: undo 迴圈 / my_exit 的迴圈          05: my_remove(pdev)     ← 每個裝置呼叫一次
    device_destroy                         device_destroy
    cdev_del                               cdev_del
```

一句話：**03 的 for 迴圈「內容」變成 `probe()`，「迴圈本身」交給 platform bus。**

### 其他小改動

- `device_create()` 的 parent 從 `NULL` 改成 `&pdev->dev`，sysfs 會顯示這個節點屬於哪個 platform device。
- `mutex_init` 從迴圈裡移到 `my_init()` 只做一次。如果裝置被 unbind 再 bind，而有人還開著檔案，`probe()` 不能重新初始化一把可能正被使用的鎖。
- `platform_driver_register()` 放在 `my_init()` 的**最後**，因為 `probe()` 可能在這個呼叫裡就被執行，class 和 dev_t 必須已經準備好。
- 為什麼還是用 `module_init` 而不是 `module_platform_driver()`？因為 dev_t 範圍和 class 是兩個裝置**共用**的，必須在第一次 probe 前建立、最後一次 remove 後才釋放。

## 編譯

```sh
make            # 模組 + test_devs，預設 arm64 / RPi
```

## 在樹莓派上測試

```sh
# 1. 只載 driver：沒有裝置，什麼都不會發生
sudo insmod my_cdev_pdrv.ko
ls /dev/my_cdev*                        # 沒有
dmesg | tail -1                          # "driver registered ... waiting for devices"

# 2. 載入裝置：probe() 被呼叫兩次
sudo insmod my_cdev_pdev.ko
dmesg | tail -3                          # "probe -> /dev/my_cdev0" ×2
ls /dev/my_cdev*                        # my_cdev0 my_cdev1

# 3. 03 的測試照樣通過
./test_devs /dev/my_cdev0 /dev/my_cdev1

# 4. 看綁定關係
ls /sys/bus/platform/drivers/my_cdev_pdrv/      # my_cdev_pdev.0  my_cdev_pdev.1
ls /sys/devices/platform/my_cdev_pdev.0/my_class/   # my_cdev0

# 5. 手動解綁一個裝置：只有 my_cdev1 消失
echo my_cdev_pdev.1 | sudo tee /sys/bus/platform/drivers/my_cdev_pdrv/unbind
ls /dev/my_cdev*                        # 只剩 my_cdev0
echo my_cdev_pdev.1 | sudo tee /sys/bus/platform/drivers/my_cdev_pdrv/bind

# 6. 卸載裝置模組：remove() 被呼叫兩次
sudo rmmod my_cdev_pdev
sudo rmmod my_cdev_pdrv
```

也可以反過來先載 `my_cdev_pdev.ko` 再載 driver，`probe()` 會在第二個 `insmod` 時執行，順序無關。

## 刻意保留的簡化（下一課要處理）

`devs[]` 仍然是**靜態陣列**，用 `pdev->id` 當索引。這樣改動最小，但有兩個限制：

1. **裝置數量寫死**：`id` 超過 `NR_DEVS - 1` 會被拒絕。改用 DT 時，`pdev->id` 通常是 `-1`，這個寫法就不能用了。
2. **unbind 時檔案還開著**：靜態記憶體不會被釋放，所以不會 crash。但如果這時再 bind 回來，`probe()` 會對一個仍被開著的檔案引用的 `cdev` 重新 `cdev_init()`。正常練習流程不會碰到，但正式 driver 不能這樣寫。

下一步的改法是：在 `probe()` 裡動態配置 `my_dev`、用 IDA 配 minor，並用 refcount 管理生命週期（`cdev_device_add()` + `struct device` 的 `release()`）。
