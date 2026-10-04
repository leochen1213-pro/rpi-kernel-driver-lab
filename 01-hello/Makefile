# KVER 由 scripts/pi-shell.sh 以環境變數帶入；此處為備用預設值
ARCH          ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-
KVER          ?= 6.18.50+rpt-rpi-v8
KDIR          ?= /usr/src/linux-headers-$(KVER)

obj-m += hello.o

.PHONY: all clean

all:
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR) clean
