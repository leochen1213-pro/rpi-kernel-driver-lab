# KVER 由 scripts/pi-shell.sh 以環境變數帶入；此處為備用預設值
ARCH          ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-
KVER          ?= 6.18.50+rpt-rpi-v8
KDIR          ?= /usr/src/linux-headers-$(KVER)

CC  := $(CROSS_COMPILE)gcc
APP := AP1

obj-m += charDrv1.o

.PHONY: all module app clean

all: module app

module:
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR) modules

app: $(APP)

$(APP): AP1.c
	$(CC) -Wall -O2 -o $(APP) AP1.c

clean:
	$(MAKE) -C $(KDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR) clean
	rm -f $(APP)
