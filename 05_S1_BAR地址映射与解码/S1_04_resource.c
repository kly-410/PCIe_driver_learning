/*
 * S1_04_resource.c - BAR 资源管理：pci_request_regions 演示
 *
 * 编译：gcc -o S1_04_resource S1_04_resource.c -lpci
 * 运行：sudo ./S1_04_resource [域:]总线:设备.功能
 *
 * 功能：
 *   1. 读取设备的 BAR 信息
 *   2. 演示 pci_request_regions 的目的（防止 BAR 冲突）
 *   3. 打印每个 BAR 的 resource 路径（/proc/iomem 中的位置）
 *
 * 资源管理流程：
 *   pci_enable_device() → pci_request_regions() → pci_iomap()
 *                        → 访问 BAR
 *                        → pci_iounmap() → pci_release_regions() → pci_disable_device()
 *
 * 参考：drivers/pci/pci.c, drivers/pci/setup-bus.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pci/pci.h>

int main(int argc, char **argv)
{
    struct pci_access *pacc = pci_alloc();
    pci_init(pacc);
    pci_scan_bus(pacc);

    const char *dev_id = argc > 1 ? argv[1] : "01:00.0";
    unsigned b, dev, fn;

    if (sscanf(dev_id, "%x:%x.%x", &b, &dev, &fn) != 3) {
        fprintf(stderr, "用法: %s [域:]总线:设备.功能\\n", argv[0]);
        pci_cleanup(pacc);
        return 1;
    }

    struct pci_dev *p = pci_get_dev(pacc, 0, b, dev, fn);
    if (!p) { fprintf(stderr, "设备未找到\\n"); return 1; }

    u16 vendor, device;
    pci_read_config_word(p, PCI_VENDOR_ID, &vendor);
    pci_read_config_word(p, PCI_DEVICE_ID, &device);

    printf("设备 %s: Vendor=0x%04x Device=0x%04x\\n", dev_id, vendor, device);
    printf("\\nBAR 资源信息（从 pci_dev->resource[] 读取）：\\n");

    /*
     * pci_dev->resource[i] 是 struct resource，
     * start/end/flags 描述该 BAR 在系统内存中的位置
     */
    for (int i = 0; i < 6; i++) {
        struct resource *res = &p->resource[i];
        if (!res || res->start == 0 && res->end == 0) {
            printf("  BAR%d: 未分配\\n", i);
            continue;
        }

        const char *type =
            (res->flags & IORESOURCE_IO) ? "I/O" :
            (res->flags & IORESOURCE_MEM_PREFETCH) ? "MEM (PREFETCH)" :
            "MEM";

        printf("  BAR%d: [%s] 0x%lx-0x%lx (size=0x%lx)\\n",
               i, type,
               (unsigned long)res->start,
               (unsigned long)res->end,
               (unsigned long)(res->end - res->start + 1));
    }

    printf("\\n说明：\\n");
    printf("  pci_request_regions() 在 /sys/bus/pci/devices/%04x:%02x:%02x.%x/resource* 中标记已占用。\\n",
           0, b, dev, fn);
    printf("  驱动加载前，这些字段是 0（未被分配）。\\n");
    printf("  pci_enable_device() 后，内核 PCI core 分配了地址，写入 BAR 配置空间。\\n");

    pci_free_dev(p);
    pci_cleanup(pacc);
    return 0;
}
