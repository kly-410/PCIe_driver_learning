# S1_04 Makefile - BAR / MMIO 实验
# 编译：make
# 清理：make clean

CC      := gcc
CFLAGS  := -Wall -Wextra -g
LDFLAGS := -lpci

PROGS   := S1_04_bar_decode S1_04_mmio S1_04_resource

.PHONY: all clean

all:
	@echo "S1_04 编译目标："
	@echo "  make bar_decode  - BAR 大小解析（需 libpci）"
	@echo "  make mmio        - MMIO 读写与屏障演示（不需 libpci，无需硬件）"
	@echo "  make resource   - BAR 资源管理（需 libpci）"
	@echo "  make clean     - 清除编译产物"

bar_decode: S1_04_bar_decode.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

mmio: S1_04_mmio.c
	$(CC) $(CFLAGS) -o $@ $<

resource: S1_04_resource.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(PROGS)
