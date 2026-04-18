/*
 * S1_01_link_status.c - PCIe 链路速率/宽度/LTSSM 状态读取
 *
 * 编译：gcc -o S1_01_link_status S1_01_link_status.c -lpci
 * 运行：sudo ./S1_01_link_status [域:]总线:设备.功能
 *
 * 功能：
 *   读取 PCIe Cap Link Status 寄存器
 *   - Current Link Speed (Gen1~Gen5)
 *   - Negotiated Link Width (x1/x2/x4/x8/x16)
 *   - Link Training 状态
 *
 * 参考：PCIe Base Spec Rev 5.0 Chapter 7.8.3 (Link Status Register)
 *
 * 适配：pciutils 3.x（Ubuntu 24.04 libpci）
 *   - pci_read_byte/word: 返回值方式，不需要指针参数
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

/*
 * pcie_cap_find - 找 PCIe Capability（ID=0x10）
 *
 * 从 PCI_CAPABILITY_LIST（=0x34）开始遍历 Capability 链表
 * 返回：PCIe Capability 起始偏移，找不到返回 0
 */
static int pcie_cap_find(struct pci_dev *dev)
{
    u8 pos = pci_read_byte(dev, PCI_CAPABILITY_LIST);
    int ttl = 48;
    while (ttl-- && pos) {
        u8 id   = pci_read_byte(dev, pos);
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

    int gen    = lnksta & 0xf;           /* Bit[3:0]  = Negotiated Link Speed */
    int width  = (lnksta >> 4) & 0x3f; /* Bit[9:4] = Negotiated Link Width */
    int traing = (lnksta >> 11) & 0x1; /* Bit[11]  = Link Training (PCI_EXP_LNKSTA_LT) */

    printf("设备 %s:\n", dev_id);
    printf("  协商速率  : %s\n", gen_str(gen));
    printf("  协商宽度  : x%d\n", width);
    printf("  LTSSM     : %s\n", traing ? "L0（训练完成）" : "训练中 / Recovery");

    /* PCIe Cap + 0x0C = Link Capabilities Register */
    u16 lcap = pci_read_word(p, cap + 0x0C);
    int max_gen   = lcap & 0xf;
    int max_width = (lcap >> 4) & 0x3f;
    printf("  硬件支持最大速率: Gen%d (%s)\n", max_gen, gen_str(max_gen));
    printf("  硬件支持最大宽度: x%d\n", max_width);

    pci_free_dev(p);
    pci_cleanup(pacc);
    return 0;
}
