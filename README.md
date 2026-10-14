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

### 3. 進容器編譯

```bash
docker run --rm -it \
  -v $(pwd):/work \
  -v $(pwd)/linux-headers-6.18.50+rpt-rpi-v8:/usr/src/linux-headers-6.18.50+rpt-rpi-v8 \
  -v $(pwd)/linux-headers-6.18.50+rpt-common-rpi:/usr/src/linux-headers-6.18.50+rpt-common-rpi \
  pi-kernel-cross bash
```
