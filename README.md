# rpi-kernel-driver-lab

在 macOS（Apple Silicon）上用 Docker 交叉編譯，為 Raspberry Pi 3（64 位元）練習 Linux kernel module 與 driver 開發。

## 環境

| 項目 | 內容 |
|---|---|
| 目標板 | Raspberry Pi 3，64 位元 Raspberry Pi OS（Debian trixie 系） |
| 核心 | 6.18.x `+rpt-rpi-v8`（以 Pi 上 `uname -r` 為準） |
| 主機 | macOS Apple Silicon + Docker |
| 工具鏈 | 容器內 `aarch64-linux-gnu-gcc`（`debian:trixie`） |


## 使用方式

### 1. 建立編譯容器（只需一次）

```bash
docker build -t pi-kernel-cross docker/
```

```bash
docker run --rm -it \
  -v $(pwd):/work \
  -v $(pwd)/linux-headers-6.18.50+rpt-rpi-v8:/usr/src/linux-headers-6.18.50+rpt-rpi-v8 \
  -v $(pwd)/linux-headers-6.18.50+rpt-common-rpi:/usr/src/linux-headers-6.18.50+rpt-common-rpi \
  pi-kernel-cross bash
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
