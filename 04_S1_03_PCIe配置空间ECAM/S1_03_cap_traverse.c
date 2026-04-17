/*
 * S1_03_cap_traverse.c - Capability 链表遍历
 *
 * 编译：gcc -o S1_03_cap_traverse S1_03_cap_traverse.c -lpci
 * 运行：sudo ./S1_03_cap_traverse [域:]总线:设备.功能
 * 示例：
 *   sudo ./S1_03_cap_traverse 00:00.0   # RC
 *   sudo ./S1_03_cap_traverse 01:00.0   # EP
 *
 * 功能：
 *   1. 遍历 Capability 链表，打印每个 Cap 的 ID 和偏移
 *   2. 识别关键 Cap（PM / MSI / PCIe / MSI-X / DOE）
 *   3. 打印 PCIe Cap 的 Device Type 和 Max_Payload_Size
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pci/pci.h>

/* 标准 Capability ID 名称 */
static const char *cap_name(unsigned char id)
{
    switch (id) {
        case 0x01: return "PM (Power Management)";
        case 0x03: return "VPD (Vital Product Data)";
        case 0x04: return "Slot ID";
        case 0x05: return "MSI (Message Signaled Interrupt)";
        case 0x06: return "CompactPCI Hot Swap";
        case 0x07: return "PCI-X";
        case 0x08: return "HyperTransport";
        case 0x09: return "Vendor-Specific";
        case 0x10: return "PCI Express";
        case 0x11: return "MSI-X";
        case 0x14: return "OBFF (Optimized Buffer Flush/Fill)";
        case 0x18: return "LTR (Latency Tolerance Reporting)";
        case 0x22: return "DOE (Data Object Exchange)";
        default:   return "Unknown";
    }
}

/* 查找指定 ID 的 Capability，返回其配置空间偏移 */
static int cap_find(struct pci_dev *dev, int cap_id)
{
    u8 pos = 0, id, next;
    int ttl = 48;

    pci_read_config_byte(dev, PCI_CAPLIST_PTR, &pos);
    while (ttl-- && pos) {
        pci_read_config_byte(dev, pos, &id);
        pci_read_config_byte(dev, pos + 1, &next);
        if (id == 0xff) break;         /* 无效 Cap */
        if (id == cap_id) return pos;  /* 找到 */
        pos = next;
    }
    return 0;  /* 未找到 */
}

/* 遍历并打印所有 Capability */
static void cap_dump_all(struct pci_dev *dev)
{
    u8 pos = 0, id, next;
    int ttl = 48;

    pci_read_config_byte(dev, PCI_CAPLIST_PTR, &pos);
    printf("  Capability 链表:\\n");
    while (ttl-- && pos) {
        pci_read_config_byte(dev, pos, &id);
        pci_read_config_byte(dev, pos + 1, &next);
        if (id == 0xff) break;
        printf("    Offset 0x%02x: [0x%02x] %s\\n",
               pos, id, cap_name(id));
        pos = next;
    }
    if (ttl <= 0) printf("    (链表可能断裂，已达最大 Cap 数 48)\\n");
}

/* PCIe Device Type 解码 */
static const char *pcie_type_str(u8 type)
{
    switch (type & 0xf) {
        case 0:  return "PCI Express Endpoint";
        case 1:  return "Legacy PCI Express Endpoint";
        case 4:  return "Root Port of RC";
        case 5:  return "Switch Port (Upstream or Downstream)";
        case 6:  return "PCIe-to-PCI/PCI-X Bridge";
        case 7:  return "PCI/PCI-X-to-PCIe Bridge";
        default: return "Other";
    }
}

/* 打印 PCIe Cap 关键字段 */
static void pcie_cap_dump(struct pci_dev *dev, int off)
{
    u16 pcie_hdr, dev_cap;
    pci_read_config_word(dev, off, &pcie_hdr);
    pci_read_config_word(dev, off + 4, &dev_cap);

    u8 dev_type = (pcie_hdr >> 4) & 0xf;
    int max_payload = 128 << (dev_cap & 0x7);  /* Max_Payload_Size Supported */

    printf("  PCIe Cap (offset 0x%02x):\\n", off);
    printf("    Device Type : %s\\n", pcie_type_str(dev_type));
    printf("    Max_Payload_Size: %d bytes\\n", max_payload);
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

    u16 vendor, device;
    pci_read_config_word(p, PCI_VENDOR_ID, &vendor);
    pci_read_config_word(p, PCI_DEVICE_ID, &device);
    printf("设备 %s: Vendor=0x%04x Device=0x%04x\\n",
           dev_id, vendor, device);

    cap_dump_all(p);

    int pcie_cap = cap_find(p, 0x10);
    if (pcie_cap)
        pcie_cap_dump(p, pcie_cap);
    else
        printf("  (无 PCIe Cap，传统 PCI 设备)\\n");

    int msi_cap = cap_find(p, 0x05);
    if (msi_cap) {
        u16 ctrl;
        pci_read_config_word(p, msi_cap + 2, &ctrl);
        int vectors = 1 << ((ctrl >> 1) & 0x7);
        printf("  MSI Cap (offset 0x%02x): %d vectors enabled\\n",
               msi_cap, vectors);
    }

    pci_free_dev(p);
    pci_cleanup(pacc);
    return 0;
}
