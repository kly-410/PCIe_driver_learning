/*
 * S1_02_link_status.c - PCIe 链路状态快速读取（S1_02 简化版）
 *
 * 编译：gcc -o S1_02_link_status S1_02_link_status.c -lpci
 * 运行：sudo ./S1_02_link_status [域:总线.设备.功能]
 *
 * 功能：读取 PCIe Cap Link Status，输出协商速率和宽度
 *
 * 适配：pciutils 3.x（Ubuntu 24.04 libpci）
 */

#include <stdio.h>
#include <stdlib.h>
#include <pci/pci.h>

static const char *gen_str(int gen)
{
    switch (gen) {
        case 1:  return "Gen1 (2.5 GT/s)";
        case 2:  return "Gen2 (5.0 GT/s)";
        case 3:  return "Gen3 (8.0 GT/s)";
        case 4:  return "Gen4 (16.0 GT/s)";
        case 5:  return "Gen5 (32.0 GT/s)";
        default: return "Unknown";
    }
}

static int pcie_cap_find(struct pci_dev *dev)
{
    u8 pos = pci_read_byte(dev, PCI_CAPABILITY_LIST);
    int ttl = 48;
    while (ttl-- && pos) {
        u8 id = pci_read_byte(dev, pos);
        u8 next = pci_read_byte(dev, pos + 1);
        if (id == 0x10) return pos;
        if (id == 0xff) break;
        pos = next;
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct pci_access *pacc = pci_alloc();
    pci_init(pacc);
    pci_scan_bus(pacc);

    const char *dev_id = argc > 1 ? argv[1] : "00:00.0";
    unsigned b, d, f;
    if (sscanf(dev_id, "%x:%x.%x", &b, &d, &f) != 3) {
        fprintf(stderr, "用法: %s [域:]总线:设备.功能\n", argv[0]);
        pci_cleanup(pacc);
        return 1;
    }

    struct pci_dev *p = pci_get_dev(pacc, 0, b, d, f);
    if (!p) { fprintf(stderr, "设备未找到\n"); return 1; }

    int cap = pcie_cap_find(p);
    if (!cap) { printf("无 PCIe Cap\n"); return 1; }

    /* PCIe Cap + 0x12 = Link Status Register (PCI_EXP_LNKSTA) */
    u16 lnksta = pci_read_word(p, cap + 0x12);
    int gen   = lnksta & 0xf;
    int width = (lnksta >> 4) & 0x3f;

    printf("设备 %s:\n", dev_id);
    printf("  协商速率: %s\n", gen_str(gen));
    printf("  协商宽度: x%d\n", width);

    pci_free_dev(p);
    pci_cleanup(pacc);
    return 0;
}
