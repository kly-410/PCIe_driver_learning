/*
 * S1_02_doe.c - DOE（Data Object Exchange）基本操作演示
 *
 * 编译：gcc -o S1_02_doe S1_02_doe.c -lpci
 * 运行：sudo ./S1_02_doe
 *
 * 功能：
 *   遍历所有 PCIe 设备，寻找 VSEC（Vendor-Specific Extended Cap）
 *   在 VSEC 内找 DOE Capability（ID = 0x02）
 *   读取 DOE 对象 header
 *
 * DOE（Data Object Exchange）：
 *   PCIe r6.0 (2022) 引入的带内配置协议，用于访问扩展配置对象
 *   通过 MWr/MRd TLP 携带 DOE 负载，传递 Data Object
 *   常用于：CMA（配置管理器访问）、IDE（带内调试）
 *
 * 适配：pciutils 3.x（Ubuntu 24.04 libpci）
 *   - pci_read_byte/word：返回值方式，不需要指针参数
 *   - Extended Capability 从 0x100 开始遍历
 */

#include <stdio.h>
#include <stdlib.h>
#include <pci/pci.h>

/*
 * vsce_find_doe - 在 VSEC 内找 DOE Capability（ID = 0x02）
 *
 * VSEC 内 Capability 链表遍历：
 *   从 VSEC 起始 + 0x0C 开始（DOE 通常在这个偏移之后）
 *   每个 Capability：Byte[0]=ID, Byte[1]=Next Pointer
 *
 * 返回：DOE Capability 偏移，找不到返回 0
 */
static int vsce_find_doe(struct pci_dev *dev, int vsce_base)
{
    u8 pos = pci_read_byte(dev, vsce_base + 0x0C);  /* 第一个 DOE 指针 */
    int ttl = 32;
    while (ttl-- && pos) {
        u8 id = pci_read_byte(dev, pos);
        u8 next = pci_read_byte(dev, pos + 1);
        if (id == 0xFF) break;   /* 链表结束 */
        if (id == 0x02) {        /* DOE Capability ID = 0x02 */
            return pos;
        }
        pos = next;
    }
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    struct pci_access *pacc = pci_alloc();
    pci_init(pacc);
    pci_scan_bus(pacc);

    printf("=== S1_02 DOE 检测 ===\n");
    printf("遍历所有设备，寻找 VSEC + DOE Capability\n\n");

    int total_devices = 0;
    int vsce_count = 0;
    int doe_count = 0;

    for (struct pci_dev *p = pacc->devices; p; p = p->next) {
        char addr[16];
        snprintf(addr, sizeof(addr), "%04x:%02x:%02x.%x",
                 p->domain_16, p->bus, p->dev, p->func);

        u16 vendor = pci_read_word(p, PCI_VENDOR_ID);
        if (vendor == 0xFFFF) continue;
        total_devices++;

        /* PCIe Extended Capability 链表遍历（从 0x100 开始）*/
        u16 next = 0x100;
        int ttl = 64;
        while (ttl-- && next) {
            u32 ehdr = pci_read_long(p, next); /* Extended Cap Header */
            u16 vid = ehdr & 0xFFFF;          /* 低16位 = VID */
            u16 next_off = (ehdr >> 16) & 0xFFFC; /* 高16位低14位 = Next Offset */
            if (vid == 0xFFFF) break;           /* 链表结束 */
            if (vid == 0x000B) {                /* VSEC = Vendor-Specific Extended Cap */
                /* VSEC Header: [0x00] VSEC ID, [0x02] VSEC Rev, [0x03] VSEC Length */
                u16 vsce_id = pci_read_word(p, next + 4);
                u8 rev = pci_read_byte(p, next + 6);
                vsce_count++;
                printf("设备 %s: VSEC at 0x%03x, VSEC_ID=0x%04x, Rev=0x%02x\n",
                       addr, next, vsce_id, rev);

                /* 在 VSEC 内找 DOE */
                int doe_off = vsce_find_doe(p, next);
                if (doe_off) {
                    u16 doe_hdr = pci_read_word(p, doe_off);
                    doe_count++;
                    printf("  → DOE at 0x%02x, Header=0x%04x\n", doe_off, doe_hdr);
                }
            }
            next = next_off;
            if (next == 0) break;
        }
    }

    printf("\n=== 统计 ===\n");
    printf("总设备数 : %d\n", total_devices);
    printf("有 VSEC  : %d\n", vsce_count);
    printf("有 DOE   : %d\n", doe_count);

    if (doe_count == 0)
        printf("\n（没有找到 DOE 设备，这是正常的——DOE 不是所有 PCIe 设备都有）\n");

    printf("\n=== DOE 概念说明 ===\n");
    printf("  DOE = Data Object Exchange（PCIe r6.0, sec 6.30）\n");
    printf("  通过 MWr/MRd TLP 携带 DOE Data Object\n");
    printf("  常用于：CMA / IDE（带内调试）\n");
    printf("  DOE 不走 ID 路由，靠数据链路层传输\n");

    pci_cleanup(pacc);
    return 0;
}
