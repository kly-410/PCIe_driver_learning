# Makefile for S1 kernel module
# 编译：make
# 加载：sudo insmod kmod_skeleton.ko
# 检查：dmesg | tail；lsmod | grep kmod_skeleton
# 卸载：sudo rmmod kmod_skeleton

obj-m += kmod_skeleton.o

KVERSION := $(shell uname -r)
KDIR ?= /lib/modules/$(KVERSION)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
