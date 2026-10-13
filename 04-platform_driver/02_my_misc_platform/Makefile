# === 可依你的板子調整這三個變數 ===
# Raspberry Pi 範例: ARCH=arm64, CROSS_COMPILE=aarch64-linux-gnu-
# BeagleBone Black 範例: ARCH=arm, CROSS_COMPILE=arm-linux-gnueabihf-
ARCH          ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-
KDIR          ?= /usr/src/linux-headers-6.18.50+rpt-rpi-v8

ifneq ($(KERNELRELEASE),)
# ---- 由 kbuild 讀取：只需要告訴它要編哪個模組 ----
obj-m := my_misc_pdev.o my_misc_pdrv.o

else
# ---- 由命令列執行 ----

# userspace 測試程式（沿用 04 的 test_devs.c）
APP := test_devs

# userspace 編譯器沿用同一套交叉工具鏈
# 用 USER_CC 而不是 CC，避免蓋掉 kbuild 自己的 CC
USER_CC     := $(CROSS_COMPILE)gcc
USER_CFLAGS := -Wall -Wextra -O2

KMAKE = $(MAKE) -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR)

.PHONY: all clean module app

# make 或 make all：模組 + userspace 程式（有設定 APP 時）
all: module app

# 只編 kernel module
module:
	$(KMAKE) modules

# 只編 userspace 執行檔
app: $(APP)

ifneq ($(APP),)
$(APP): $(APP).c $(wildcard *.h)
	$(USER_CC) $(USER_CFLAGS) -o $@ $(APP).c
endif

clean:
	$(KMAKE) clean
	rm -f $(APP)

endif
