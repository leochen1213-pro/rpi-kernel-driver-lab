# KVER 由 scripts/pi-shell.sh 以環境變數帶入；此處為備用預設值
ARCH          ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-
KVER          ?= 6.18.50+rpt-rpi-v8
KDIR          ?= /usr/src/linux-headers-$(KVER)

obj-m += lm75_demo.o

.PHONY: all dtbo clean

all:
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR) modules

# overlay 在 Pi 上編譯：dtc -@ -I dts -O dtb -o lm75-demo.dtbo lm75-demo-overlay.dts

clean:
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR) clean
