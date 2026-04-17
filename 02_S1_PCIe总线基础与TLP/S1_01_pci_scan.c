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
 *
 * 适配：pciutils 3.x（Ubuntu 24.04 libpci）
 *   - pci_read_byte/word: 返回值方式，不需要指针参数
 *   - 地址用 p->bus / p->dev / p->func 手动拼接
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pci/pci.h>

/*
 * pcie_cap_find - 在配置空间里找 PCIe Capability 位置
 *
 * PCIe Capability ID = 0x10
 * 每个 Capability 的结构：Byte[0]=ID, Byte[1]=Next Cap Pointer
 * 从 PCI_CAPABILITY_LIST（=0x34）开始遍历链表
 *
 * 返回： PCIe Capability 的起始偏移（字节），找不到返回 0
 */
static int pcie_cap_find(struct pci_dev *dev)
{
    u8 pos = pci_read_byte(dev, PCI_CAPABILITY_LIST);  /* 0x34 = 第一个 Cap 指针 */
    int ttl = 48;

    while (ttl-- && pos) {
        u8 id   = pci_read_byte(dev, pos);        /* Byte 0 = Capability ID */
        u8 next = pci_read_byte(dev, pos + 1);   /* Byte 1 = Next Cap Pointer */
        if (id == 0x10)
            return pos;  /* 找到 PCIe Capability（ID=0x10）*/
        if (id == 0xff)
            break;  /* 链表结束 */
        pos = next;  /* 跳到下一个 Capability */
    }
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;  /* 未使用参数，避免警告 */

    struct pci_access *pacc = pci_alloc();
    pci_init(pacc);       /* 初始化 libpci */
    pci_scan_bus(pacc);   /* 扫描总线，填充 devices 链表 */

    printf("%-12s %-6s %-6s %-8s %-20s %s\n",
           "PCI ID", "Vendor", "Device", "Class", "Type", "TLP Role");
    printf("%-12s %-6s %-6s %-8s %-20s %s\n",
           "------", "------", "------", "-----", "----", "--------");

    /* 遍历总线上的每一个设备 */
    for (struct pci_dev *p = pacc->devices; p; p = p->next) {
        /* libpci 3.x 没有 pci_get_slot，手动拼接地址字符串 */
        char addr[16];
        snprintf(addr, sizeof(addr), "%04x:%02x:%02x.%x",
                 p->domain_16, p->bus, p->dev, p->func);

        /* 读取配置空间标准寄存器（返回值方式，无指针参数）*/
        u16 vendor     = pci_read_word(p, PCI_VENDOR_ID);
        u16 device     = pci_read_word(p, PCI_DEVICE_ID);
        u16 class_code = pci_read_word(p, PCI_CLASS_DEVICE);
        u8 hdr_type    = pci_read_byte(p, PCI_HEADER_TYPE);

        if (vendor == 0xffff)
            continue;  /* 空槽（没有插设备），跳过 */

        /* class_code: upper 8 bits = base class, lower 8 bits = sub class */
        int base = class_code >> 8;
        int sub  = class_code & 0xff;

        /* 默认类型 */
        const char *type_str = (hdr_type & 0x7f) == 0 ? "Type 0 (EP)" : "Type 1 (RC/Bridge)";
        const char *tlp_role = "—";

        /* 找 PCIe Capability，从里面读 Device/Port Type */
        int pcie_cap = pcie_cap_find(p);
        if (pcie_cap) {
            u16 pcie_hdr = pci_read_word(p, pcie_cap);
            u8 dev_type = (pcie_hdr >> 4) & 0xf;

            /* PCIe Spec 定义的各种设备类型 */
            const char *type_map[] = {
                "PCIe EP",         /* 0: PCI Express Endpoint          */
                "Legacy EP",       /* 1: Legacy PCI Express Endpoint   */
                "?",               /* 2: Reserved                       */
                "?",               /* 3: Reserved                       */
                "RC Root Port",    /* 4: Root Port of RC               */
                "Switch Port",     /* 5: Switch Port                   */
                "Bridge",          /* 6: PCIe-to-PCI Bridge           */
                "Bridge"           /* 7: PCIe-to-PCI Bridge (alt)     */
            };
            if (dev_type <= 7)
                type_str = type_map[dev_type];

            /* 根据设备类型，说明它的 TLP 角色 */
            if (dev_type == 4)
                tlp_role = "发起 MRd/MWr/CfgRd → 枚举和管理 EP";
            else if (dev_type == 0 || dev_type == 1)
                tlp_role = "响应 MRd/CfgRd → 发 MSI 中断";
            else if (dev_type == 5)
                tlp_role = "转发 TLP（地址路由 / ID 路由）";
        }

        printf("%-12s 0x%04x  0x%04x  %02x.%02x   %-20s %s\n",
               addr, vendor, device, base, sub, type_str, tlp_role);
    }

    pci_cleanup(pacc);  /* 释放资源 */
    return 0;
}
