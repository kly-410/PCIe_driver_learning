# S1_01 Makefile - PCIe 总线基础与 TLP 实验
# 编译：make
# 清理：make clean

CC      := gcc
CFLAGS  := -Wall -Wextra -g
LDFLAGS := -lpci

PROGS   := S1_01_tlp_type S1_01_link_status S1_01_pci_scan

.PHONY: all clean

all:
	@echo "S1_01 编译目标："
	@echo "  make tlp_type   - TLP 类型识别 + PCIe Cap 解析（需 libpci）"
	@echo "  make link_status- 链路速率/宽度/LTSSM 读取（需 libpci）"
	@echo "  make scan      - 扫描 PCIe 总线所有设备（需 libpci）"
	@echo "  make clean    - 清除编译产物"

tlp_type: S1_01_tlp_type.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

link_status: S1_01_link_status.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

scan: S1_01_pci_scan.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(PROGS)
