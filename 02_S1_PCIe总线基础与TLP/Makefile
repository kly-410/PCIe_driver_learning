# S1_01 Makefile - PCIe 总线基础与 TLP 实验
#
# 用法：
#   make scan         ← 只编译 pci_scan 程序
#   make tlp_type   ← 只编译 tlp_type 程序
#   make link_status ← 只编译 link_status 程序
#   make all        ← 编译全部三个程序
#   make clean      ← 删除所有编译产物（可执行文件）

# ========== 变量定义 ==========

CC      := gcc            # 编译器：用 gcc
CFLAGS  := -Wall -Wextra -g
#   -Wall      开启所有警告（写错了代码会报错）
#   -Wextra    开启额外警告（更严格的检查）
#   -g         带上调试符号（gdb 调试时能看到变量名和行号）

LDFLAGS := -lpci
#   -lpci      链接 libpci 库
#   libpci 是 Linux 系统库，提供访问 PCIe 配置空间的能力
#   pci_scan、tlp_type、link_status 三个程序都需要访问 PCIe 配置空间
#   所以编译时都要链接 -lpci

PROGS   := S1_01_tlp_type S1_01_link_status S1_01_pci_scan
#   PROGS      定义要生成哪些可执行文件（用空格分隔）
#   这里是三个独立程序，互不影响

# ========== 伪目标声明 ==========
# .PHONY 表示这些不是真正的文件名，只是"动作"名称
# 不加 .PHONY 的话，如果恰好有个文件叫 "clean"，make clean 会报错

.PHONY: all clean

# ========== 默认目标 ==========
# 输入 "make"（不带参数）时执行 all
all:
	@echo "S1_01 可用编译目标："
	@echo "  make tlp_type   - TLP 类型识别 + PCIe Cap 解析（需 libpci）"
	@echo "  make link_status- 链路速率/宽度/LTSSM 读取（需 libpci）"
	@echo "  make scan       - 扫描 PCIe 总线所有设备（需 libpci）"
	@echo "  make all        - 编译以上全部三个程序"
	@echo "  make clean      - 删除编译产物"
#   @ 符号表示不打印这行命令本身，只打印 echo 的输出

# ========== 编译规则 ==========
# 格式：目标: 依赖文件
#         recipe（Tab开头，不能用空格）
#   $<  自动替换为第一个依赖文件（源文件 .c）
#   $@  自动替换为目标文件（可执行文件名）

# tlp_type 程序的编译规则
# 输入：S1_01_tlp_type.c
# 输出：S1_01_tlp_type（可执行文件）
# 依赖：libpci（链接选项）
tlp_type: S1_01_tlp_type.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
#   展开后：gcc -Wall -Wextra -g -o S1_01_tlp_type S1_01_tlp_type.c -lpci

# link_status 程序的编译规则
# 输入：S1_01_link_status.c
# 输出：S1_01_link_status
link_status: S1_01_link_status.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# scan 程序的编译规则
# 输入：S1_01_pci_scan.c
# 输出：S1_01_pci_scan
scan: S1_01_pci_scan.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# ========== 清理规则 ==========
# make clean 删除所有编译产物，还原到干净状态
clean:
	rm -f $(PROGS)
#   rm          删除命令
#   -f          强制删除，不提示确认
#   $(PROGS)    展开为 "S1_01_tlp_type S1_01_link_status S1_01_pci_scan"
