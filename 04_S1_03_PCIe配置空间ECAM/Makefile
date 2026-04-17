# S1_03 Makefile - ECAM / 配置空间实验
# 编译：make
# 清理：make clean

CC      := gcc
CFLAGS  := -Wall -Wextra -g
LDFLAGS := -lpci

PROGS   := S1_03_ecam_access S1_03_cap_traverse

.PHONY: all clean

all:
	@echo "S1_03 编译目标："
	@echo "  make ecam_access  - ECAM 直接访问（需 root，不需要 libpci）"
	@echo "  make cap_traverse- Capability 链表遍历（需 libpci）"
	@echo "  make clean       - 清除编译产物"

ecam_access: S1_03_ecam_access.c
	$(CC) $(CFLAGS) -o $@ $<

cap_traverse: S1_03_cap_traverse.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(PROGS)
