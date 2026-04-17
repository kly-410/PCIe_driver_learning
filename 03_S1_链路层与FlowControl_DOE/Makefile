# S1_02 Makefile - DOE / 链路层实验
# 编译：make
# 清理：make clean

CC      := gcc
CFLAGS  := -Wall -Wextra -g
LDFLAGS := -lpci

SRC_DIR := .

# 目标：三个独立程序
PROGS   := S1_02_doe S1_02_link_status S1_02_fc

.PHONY: all clean

all:
	@echo "S1_02 编译目标："
	@echo "  make doe          - DOE Mailbox 遍历（需 libpci）"
	@echo "  make link_status - PCIe 链路状态读取（需 libpci）"
	@echo "  make fc          - Flow Control 相关寄存器（需 libpci）"
	@echo "  make clean       - 清除编译产物"

doe: S1_02_doe.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

link_status: S1_02_link_status.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

fc: S1_02_fc.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(PROGS)
