/*
 * S1_02_fc.c - Flow Control Credit 信息读取（通过 AER Cap）
 *
 * 编译：gcc -o S1_02_fc S1_02_fc.c -lpci
 * 运行：sudo ./S1_02_fc [域:]总线:设备.功能
 *
 * 功能：
 *   读取 PCIe Device Capabilities 寄存器，获取 Max_Payload_Size
 *   读取 PCIe Device Control 寄存器，获取 Max_Read_Request_Size
 *   读取 Link Capabilities 寄存器，获取 Max Link Speed 和 Width
 *
 * 这三个参数直接影响 Flow Control Credit 的消耗速度，
 * 是排查 FC 超额问题的关键数据。
 *
 * 参考：PCIe Base Spec Rev 5.0 Chapter 7.8 (PCI Express Capability)
 */

#include <stdio.h>
#include <stdlib.h>
#include <pci/pci.h>

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
    if (!p) { fprintf(stderr, "设备 %s 未找到\\n", dev_id); return 1; }

    int cap = find_pcie_cap(p);
    if (!cap) { printf("无 PCIe Cap\\n"); return 1; }

    /*
     * PCIe Cap 结构：
     *   +0x00: PCI Express Capability Header
     *   +0x04: Device Capabilities (DCAP)
     *   +0x06: Device Control (DC)
     *   +0x0C: Link Capabilities (LCAP)
     *   +0x12: Link Status Control (LSC)
     */
    u16 dcap, dctrl, lcap;
    pci_read_config_word(p, cap + 0x04, &dcap);   /* Device Capabilities */
    pci_read_config_word(p, cap + 0x06, &dctrl);  /* Device Control */
    pci_read_config_word(p, cap + 0x0C, &lcap);   /* Link Capabilities */

    int max_payload  = 128 << (dcap & 0x7);          /* Max_Payload_Size Supported */
    int max_read_req = 128 << ((dctrl >> 12) & 0x7); /* Max_Read_Request_Size */
    int max_link_spd = lcap & 0xf;                     /* Max Link Speed */
    int max_link_wid = (lcap >> 4) & 0x3f;            /* Max Link Width */

    printf("设备 %s (PCIe Cap at 0x%02x):\\n", dev_id, cap);
    printf("  Max_Payload_Size        : %d bytes (%s)\\n",
           max_payload, max_payload <= 256 ? "推荐值" : "注意硬件限制");
    printf("  Max_Read_Request_Size   : %d bytes\\n", max_read_req);
    printf("  Max Link Speed         : Gen%d\\n", max_link_spd);
    printf("  Max Link Width         : x%d\\n", max_link_wid);

    /* FC 超额警告（仅提示，非精确值）*/
    if (max_payload > 256 && max_read_req > 256)
        printf("\\n  [注意] Max_Payload + Max_ReadReq 均 > 256B，"
               "FC Credit 消耗较快，链路不稳定时优先降到 256B 试试。\\n");

    pci_free_dev(p);
    pci_cleanup(pacc);
    return 0;
}
