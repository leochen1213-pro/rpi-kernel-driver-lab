# rpi-kernel-driver-lab

在 macOS（Apple Silicon）上用 Docker 交叉編譯，為 Raspberry Pi 3（64 位元）練習 Linux kernel module 與 driver 開發。

## 環境

| 項目 | 內容 |
|---|---|
| 目標板 | Raspberry Pi 3，64 位元 Raspberry Pi OS（Debian trixie 系） |
| 核心 | 6.18.x `+rpt-rpi-v8`（以 Pi 上 `uname -r` 為準） |
| 主機 | macOS Apple Silicon + Docker |
| 工具鏈 | 容器內 `aarch64-linux-gnu-gcc`（`debian:trixie`） |

## 練習清單

| 資料夾 | 內容 | 重點 |
|---|---|---|
| `01-hello` | Hello World kernel module | 交叉編譯流程、`insmod` / `rmmod` / `dmesg` |
| `02-chardrv1` | 字元裝置 + userspace 測試程式 `AP1` | `cdev`、`file_operations`、`class_create` + `device_create` 自動建 `/dev` 節點、`simple_read_from_buffer`、mutex |
| `03-lm75` | LM75 I2C 溫度感測器 driver | device tree overlay、`i2c_driver` probe/remove、sysfs |

## 使用方式

### 1. 建立編譯容器（只需一次）

```bash
docker build -t pi-kernel-cross docker/
```

### 2. 準備 headers（核心版本每次變動都要重做）

在 Pi 上安裝（套件名稱依系統版本而定，可用 `apt-cache search linux-headers` 確認）：

```bash
sudo apt update
sudo apt install linux-headers-$(uname -r)
ls /usr/src/linux-      # 會有 rpi-v8 與 common-rpi 兩個目錄
```

在 Mac 的 repo 根目錄，兩個目錄都要拷回來：

```bash
scp -r <user>@<pi-ip>:/usr/src/linux-headers-<版本>+rpt-rpi-v8 ./
scp -r <user>@<pi-ip>:/usr/src/linux-headers-<版本>+rpt-common-rpi ./
```

這兩個目錄已列入 `.gitignore`，不會被提交。

### 3. 進容器編譯

```bash
./scripts/pi-shell.sh          # 預設 KBASE=6.18.50+rpt
# 核心版本不同時：
KBASE=6.18.60+rpt ./scripts/pi-shell.sh
```

容器內：

```bash
cd 02-chardrv1
make
```

### 4. 傳到 Pi 測試

```bash
scp charDrv1.ko AP1 <user>@<pi-ip>:~/
```

Pi 上：

```bash
sudo insmod charDrv1.ko
ls -l /dev/chardrv1
./AP1
dmesg | tail
sudo rmmod charDrv1
```

## 踩坑筆記

- **headers 要跟 Pi 實際核心完全一致**：版本對不上，`insmod` 會報 `Invalid module format`。`rpi-v8` 與 `common-rpi` 兩個目錄都要拷，而且要掛載回原本的 `/usr/src/...` 路徑，因為前者的 Makefile 用絕對路徑引用後者。
- **新版 Raspberry Pi OS 找不到 `raspberrypi-kernel-headers`**：改用 `linux-headers-$(uname -r)`。
- **`class_create()` 在核心 6.4 之後只吃一個參數**（舊範例多一個 `THIS_MODULE`，會編譯失敗）。
- **只用 `cdev_add` 不會自動建 `/dev` 節點**：要手動 `mknod`，或用 `class_create` + `device_create` 讓 udev 處理。`rmmod` 也不會刪掉手動 `mknod` 的節點。
- **`sudo echo x > /dev/foo` 會失敗**：重新導向由當前 shell 執行，不是 `sudo`。改用 `echo x | sudo tee /dev/foo`。
- **`sudo -i` 會切到 `/root`**：相對路徑的 `.ko` 會找不到，用 `sudo -s` 或直接在指令前加 `sudo`。
- **`dmesg -C`**：測試前先清空 log，才能確定看到的是這一次操作的紀錄。
