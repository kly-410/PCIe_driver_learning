/*
 * S1_01_pci_scan.c - 扫描 PCIe 总线，打印所有设备
 *
 * 编译：gcc -o S1_01_pci_scan S1_01_pci_scan.c -lpci
 * 运行：sudo ./S1_01_pci_scan
 *
 * 功能：
 *   遍历 PCI 总线所有设备，显示：
 *     Bus:Dev.Fn  Vendor  Device  Class  [类型]
 *
 *   帮助理解 PCIe 拓扑：
 *     00:00.0 通常是 RC（Host Bridge）
 *     01:00.0+ 是 EP 设备
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pci/pci.h>

static int pcie_cap_find(struct pci_dev *dev)
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

    printf("%-12s %-6s %-6s %-8s %-20s %s\\n",
           "PCI ID", "Vendor", "Device", "Class", "Type", "TLP Role");
    printf("%-12s %-6s %-6s %-8s %-20s %s\\n",
           "------", "------", "------", "-----", "----", "--------");

    for (struct pci_dev *p = pacc->devices; p; p = p->next) {
        char addr[13];
        pci_get_devaddr(p, addr, sizeof(addr));

        u16 vendor, device, class_code;
        u8 hdr_type;
        pci_read_config_word(p, PCI_VENDOR_ID, &vendor);
        pci_read_config_word(p, PCI_DEVICE_ID, &device);
        pci_read_config_word(p, PCI_CLASS_DEVICE, &class_code);
        pci_read_config_byte(p, PCI_HEADER_TYPE, &hdr_type);

        if (vendor == 0xffff) continue;  /* 空槽 */

        /* Class: upper 8 bits = base, lower 8 bits = sub */
        int base = class_code >> 8;
        int sub  = class_code & 0xff;

        const char *type_str = (hdr_type & 0x7f) == 0 ? "Type 0 (EP)" : "Type 1 (Bridge/RC)";
        const char *tlp_role = "—";

        int pcie_cap = pcie_cap_find(p);
        if (pcie_cap) {
            u16 pcie_hdr;
            pci_read_config_word(p, pcie_cap, &pcie_hdr);
            u8 dev_type = (pcie_hdr >> 4) & 0xf;
            const char *type_map[] = {
                "PCIe EP", "Legacy EP", "?", "?", "RC Root Port",
                "Switch Port", "Bridge", "Bridge"
            };
            if (dev_type <= 7)
                type_str = type_map[dev_type];

            if (dev_type == 4)
                tlp_role = "发起 MRd/MWr/CfgRd → 枚举和管理 EP";
            else if (dev_type == 0 || dev_type == 1)
                tlp_role = "响应 MRd/CfgRd → 发 MSI 中断";
            else if (dev_type == 5)
                tlp_role = "转发 TLP（地址路由 / ID 路由）";
        }

        printf("%-12s 0x%04x  0x%04x  %02x.%02x   %-20s %s\\n",
               addr, vendor, device, base, sub, type_str, tlp_role);
    }

    pci_cleanup(pacc);
    return 0;
}
