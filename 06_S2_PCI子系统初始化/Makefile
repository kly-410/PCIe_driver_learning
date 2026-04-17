# S2_01 Makefile - 内核模块编译
#
# 内核模块的编译方式和用户态程序完全不同，使用内核的 kbuild 系统
#
# 用法：
#   make          ← 编译内核模块（必须先装 linux-headers）
#   make clean    ← 删除编译产物（*.ko, *.o, .*.cmd）

# ========== 内核模块名 ==========
# 这里的名字必须和 .c 文件名去掉 .c 后一致
# S2_01_pci_skeleton.c → S2_01_pci_skeleton.ko
obj-m := S2_01_pci_skeleton.o

# ========== 内核源码路径 ==========
# 内核模块编译需要内核源码/headers 的路径
# /lib/modules/$(uname -r)/build 是一个指向内核 headers 目录的符号链接
# uname -r = 当前运行内核的版本（如 6.17.0-20-generic）
KDIR := /lib/modules/$(shell uname -r)/build

# ========== 当前目录 ==========
# M=PWD 指定内核 build system 在哪里找源码
PWD  := $(shell pwd)

# ========== 默认目标 ==========
all:
	@echo "编译内核模块..."
	@echo "KDIR = $(KDIR)"
	@if [ ! -d "$(KDIR)" ]; then \
		echo "错误：找不到内核 headers，请先运行："; \
		echo "  sudo apt install linux-headers-$$(uname -r)"; \
		exit 1; \
	fi
	@echo "编译 S2_01_pci_skeleton.ko ..."
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	@echo ""
	@echo "编译成功，检查生成的文件："
	@ls -lh *.ko 2>/dev/null || echo "没有 .ko 文件"

# ========== 清理规则 ==========
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *.o *.ko *.mod.c .*.cmd Module.symvers modules.order .tmp_versions

# ========== 内核 kbuild 解释 ==========
#
# obj-m := S2_01_pci_skeleton.o
#   obj-m 表示编译成可加载模块（.ko），不是内置（obj-y）
#
# make -C $(KDIR) M=$(PWD) modules
#   -C $(KDIR)：先 cd 到内核源码目录
#   M=$(PWD)：告诉 kbuild 源码在当前目录
#   modules：执行内核 build 系统的 modules 目标
#
# 编译结果：S2_01_pci_skeleton.ko（Kernel Module）
