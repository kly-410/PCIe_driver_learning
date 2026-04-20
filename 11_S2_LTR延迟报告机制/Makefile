# S2_06 Makefile - LTR 延迟报告机制：内核模块编译
#
# 本节课包含3个内核模块：
#   S2_06_ltr_decode.c        — 用户态工具（libpci，非内核模块）
#   S2_06_ltr_kernel_check.c  — 内核模块：检查所有设备 LTR 状态
#   S2_06_ltr_force_enable.c  — 内核模块：强制启用 LTR（实验用）
#
# 用法：
#   make               ← 编译所有
#   make usertool      ← 编译用户态工具（需要 libpci-dev）
#   make modules       ← 编译所有内核模块
#   make clean         ← 清理所有编译产物

# ========== 内核源码路径 ==========
KDIR := /lib/modules/$(shell uname -r)/build

# ========== 当前目录 ==========
PWD  := $(shell pwd)

# ========== 内核模块列表 ==========
# 多模块 kbuild：用空格分隔，.o 后缀
obj-m := S2_06_ltr_kernel_check.o S2_06_ltr_force_enable.o

# ========== 默认目标 ==========
all: usertool modules
	@echo ""
	@echo "=== S2_06 编译结果 ==="
	@ls -lh *.ko 2>/dev/null && echo "内核模块 OK"
	@ls -lh ltr_decode 2>/dev/null && echo "用户态工具 OK"

# ========== 用户态工具 ==========
# S2_06_ltr_decode.c 依赖 libpci（libpci-dev）
usertool:
	@echo "编译用户态工具 ltr_decode（需要 libpci-dev）..."
	@if ! pkg-config --exists libpci 2>/dev/null; then \
		echo "提示：libpci 未安装，运行以下命令安装："; \
		echo "  sudo apt install libpci-dev"; \
		echo "安装后重新运行 make usertool"; \
		exit 1; \
	fi
	gcc -o ltr_decode S2_06_ltr_decode.c \
		$$(pkg-config --cflags --libs libpci) \
		-Wall -O2
	@echo "ltr_decode 编译完成"
	@echo "用法：sudo ./ltr_decode [Bus:Dev:Func]"

# ========== 内核模块 ==========
modules:
	@echo "编译内核模块..."
	@if [ ! -d "$(KDIR)" ]; then \
		echo "错误：找不到内核 headers，请先运行："; \
		echo "  sudo apt install linux-headers-$$(uname -r)"; \
		exit 1; \
	fi
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	@echo ""
	@echo "内核模块编译结果："
	@ls -lh *.ko 2>/dev/null || echo "没有生成 .ko 文件"

# ========== 清理 ==========
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean 2>/dev/null
	rm -f ltr_decode *.o *.ko *.mod.c .*.cmd \
	      Module.symvers modules.order .tmp_versions

# ========== 模块操作目标 ==========
load-check:
	@echo "加载内核模块 S2_06_ltr_kernel_check.ko ..."
	@if [ ! -f S2_06_ltr_kernel_check.ko ]; then \
		echo "错误：请先 make modules"; exit 1; fi
	sudo insmod S2_06_ltr_kernel_check.ko
	@echo "检查 dmesg 输出："
	dmesg | tail -20

unload-check:
	@echo "卸载 S2_06_ltr_kernel_check.ko ..."
	sudo rmmod S2_06_ltr_kernel_check.ko 2>/dev/null || true
	dmesg | tail -5

load-force:
	@echo "加载 S2_06_ltr_force_enable.ko ..."
	@if [ ! -f S2_06_ltr_force_enable.ko ]; then \
		echo "错误：请先 make modules"; exit 1; fi
	sudo insmod S2_06_ltr_force_enable.ko dev=00:02.0
	@echo "检查 dmesg："
	dmesg | tail -10

unload-force:
	@echo "卸载 S2_06_ltr_force_enable.ko ..."
	sudo rmmod S2_06_ltr_force_enable.ko 2>/dev/null || true

# ========== kbuild 变量说明 ==========
#
# obj-m := S2_06_ltr_kernel_check.o S2_06_ltr_force_enable.o
#   obj-m = 编译成可加载模块（.ko）
#   多个模块用空格分隔，文件名必须和 .c 去掉后缀一致
#
# make -C $(KDIR) M=$(PWD) modules
#   -C $(KDIR)：cd 到内核源码目录（/lib/modules/VERSION/build）
#   M=$(PWD)：告诉 kbuild "我在这个目录里找源码"
#   modules：执行内核 build 系统的 modules 目标
#
# 结果：
#   S2_06_ltr_kernel_check.ko
#   S2_06_ltr_force_enable.ko
