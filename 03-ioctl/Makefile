# === 可依你的板子調整這三個變數 ===
# Raspberry Pi 範例: ARCH=arm64, CROSS_COMPILE=aarch64-linux-gnu-
# BeagleBone Black 範例: ARCH=arm, CROSS_COMPILE=arm-linux-gnueabihf-
ARCH          ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-
KDIR          ?= /usr/src/linux-headers-6.18.50+rpt-rpi-v8

ifneq ($(KERNELRELEASE),)
# ---- 由 kbuild 讀取 ----
obj-m := my_cdev.o

else
# ---- 由命令列執行 ----
APP := test_ioctl

# userspace 編譯器沿用同一套交叉工具鏈（用 USER_CC，避免蓋掉 kbuild 的 CC）
USER_CC     := $(CROSS_COMPILE)gcc
USER_CFLAGS := -Wall -Wextra -O2

KMAKE = $(MAKE) -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR)

.PHONY: all clean module app

all: module app

module:
	$(KMAKE) modules

app: $(APP)

# 改了共用的 ioctl header，測試程式也要重編
$(APP): $(APP).c my_cdev_ioctl.h
	$(USER_CC) $(USER_CFLAGS) -o $@ $<

clean:
	$(KMAKE) clean
	rm -f $(APP)

endif
