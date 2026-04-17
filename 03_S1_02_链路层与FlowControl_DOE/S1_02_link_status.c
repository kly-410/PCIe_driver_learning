/*
 * S1_02_link_status.c - PCIe 链路状态读取
 *
 * 编译：gcc -o S1_02_link_status S1_02_link_status.c -lpci
 * 运行：sudo ./S1_02_link_status [域:]总线:设备.功能
 *
 * 功能：读取 PCIe Cap Link Status 寄存器
 *   - 协商速率 (Gen1/2/3/4/5)
 *   - 协商宽度 (x1/x2/x4/x8/x16)
 *   - LTSSM 状态 (L0 / Recovery / Polling ...)
 *
 * 参考：PCIe Base Spec Rev 5.0 Chapter 7.8.3 (Link Status Register)
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

/* 在配置空间中查找 PCIe Cap（Cap ID = 0x10）*/
static int find_pcie_cap(struct pci_dev *dev)
{
    u8 pos = 0, id, next;
    int ttl = 48;

    pci_read_config_byte(dev, PCI_CAPLIST_PTR, &pos);
    while (ttl-- && pos) {
        pci_read_config_byte(dev, pos, &id);
        pci_read_config_byte(dev, pos + 1, &next);
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
    unsigned b, dev, fn;

    if (sscanf(dev_id, "%x:%x.%x", &b, &dev, &fn) != 3) {
        fprintf(stderr, "用法: %s [域:]总线:设备.功能\\n", argv[0]);
        pci_cleanup(pacc);
        return 1;
    }

    struct pci_dev *p = pci_get_dev(pacc, 0, b, dev, fn);
    if (!p) {
        fprintf(stderr, "设备 %s 未找到\\n", dev_id);
        pci_cleanup(pacc);
        return 1;
    }

    int cap = find_pcie_cap(p);
    if (!cap) {
        printf("设备 %s: 无 PCIe Cap（传统 PCI 设备）\\n", dev_id);
        pci_free_dev(p);
        pci_cleanup(pacc);
        return 0;
    }

    /*
     * PCIe Cap + 0x12 = Link Status Register (PCI_EXP_LNKSTA)
     *   bit [3:0]  : Current Link Speed
     *   bit [9:4]  : Negotiated Link Width
     *   bit [15]   : Link Training (1=L0, 0=in progress/Recovery)
     */
    u16 lnksta;
    pci_read_config_word(p, cap + 0x12, &lnksta);

    int gen    = lnksta & 0xf;
    int width  = (lnksta >> 4) & 0x3f;
    int traing = (lnksta >> 15) & 0x1;

    printf("设备 %s:\\n", dev_id);
    printf("  协商速率  : %s\\n", gen_str(gen));
    printf("  协商宽度  : x%d\\n", width);
    printf("  LTSSM 状态: %s\\n", traing ? "L0（训练完成）" : "训练中 / Recovery");

    pci_free_dev(p);
    pci_cleanup(pacc);
    return 0;
}
