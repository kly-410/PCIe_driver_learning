# S1_02 Makefile - 链路层与 Flow Control + DOE 实验
#
# 用法：
#   make doe   ← 编译 DOE 基本操作程序
#   make fc    ← 编译 Flow Control 状态分析程序
#   make link  ← 编译链路状态读取程序（复用 S1_01 代码）
#   make all   ← 编译以上全部程序
#   make clean ← 删除编译产物

# ========== 变量定义 ==========

CC      := gcc
CFLAGS  := -Wall -Wextra -g
LDFLAGS := -lpci

# 本节课新增两个程序：doe（DOE操作）、fc（Flow Control 分析）
# link 程序直接复用 S1_01 的链路状态代码
PROGS   := S1_02_doe S1_02_fc S1_02_link_status

# ========== 伪目标声明 ==========
.PHONY: all clean

# ========== 默认目标 ==========
all:
	@echo "S1_02 可用编译目标："
	@echo "  make doe   - DOE 基本操作（读/写配置对象）"
	@echo "  make fc    - Flow Control 状态分析"
	@echo "  make link  - 链路状态读取"
	@echo "  make all   - 编译以上全部"
	@echo "  make clean - 删除编译产物"

# ========== 编译规则 ==========

# doe 程序：DOE 基本操作演示
# 读 VSEC（Vendor-Specific Extended Capability）中的 DOE 对象
# 输入：S1_02_doe.c → 输出：S1_02_doe
doe: S1_02_doe.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	chmod +x $@

# fc 程序：Flow Control 状态分析
# 分析 PCIe Device Status 和 AER 寄存器
# 输入：S1_02_fc.c → 输出：S1_02_fc
fc: S1_02_fc.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	chmod +x $@

# link 程序：链路状态读取（复用 S1_01 代码）
# 这里复制一份到本目录，保持课程独立性
link: S1_02_link_status.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	chmod +x $@

# ========== 清理规则 ==========
clean:
	rm -f $(PROGS)
