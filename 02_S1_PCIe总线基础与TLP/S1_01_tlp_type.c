/*
 * S1_01_tlp_type.c - PCIe 设备扫描与 TLP 类型识别
 *
 * 编译：gcc -o S1_01_tlp_type S1_01_tlp_type.c -lpci
 * 运行：sudo ./S1_01_tlp_type [域:]总线:设备.功能
 * 示例：
 *   sudo ./S1_01_tlp_type 00:00.0   # RC
 *   sudo ./S1_01_tlp_type 01:00.0   # EP
 *
 * 功能：
 *   1. 读取 Vendor/Device ID
 *   2. 通过 PCIe Cap 识别 Device Type（RC/EP/Switch Port）
 *   3. 读 PCIe Cap Link Status（协商速率/宽度）
 *   4. 找 MSI Cap 并报告向量数
 *
 * TLP 类型对应关系：
 *   RC 读 EP → MRd（CplD 返回）→ 地址路由
 *   RC 写 EP → MWr（Posted）→ 地址路由
 *   RC 读 EP 配置 → CfgRd（CplD 返回）→ ID 路由
 *   EP 发 MSI   → MsgD（Posted MWr）→ 地址路由（到 RC MSI 地址）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pci/pci.h>

/* PCIe Device Type（PCIe Spec Rev 5.0 Chapter 7.8）*/
static const char *pcie_type_str(u8 type)
{
    switch (type & 0xf) {
        case 0:  return "PCI Express Endpoint";
        case 1:  return "Legacy PCI Express Endpoint";
        case 4:  return "Root Port of RC";
        case 5:  return "Switch Port (Up/Down)";
        case 6:  return "PCIe-to-PCI/PCI-X Bridge";
        case 7:  return "PCI/PCI-X-to-PCIe Bridge";
        default: return "Other";
    }
}

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

/* 找指定 ID 的 Capability */
static int cap_find(struct pci_dev *dev, int id)
{
    u8 pos = 0, cap_id, next;
    int ttl = 48;
    pci_read_config_byte(dev, PCI_CAPLIST_PTR, &pos);
    while (ttl-- && pos) {
        pci_read_config_byte(dev, pos, &cap_id);
        pci_read_config_byte(dev, pos + 1, &next);
        if (cap_id == 0xff) break;
        if (cap_id == id)   return pos;
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

    u16 vendor, device;
    pci_read_config_word(p, PCI_VENDOR_ID, &vendor);
    pci_read_config_word(p, PCI_DEVICE_ID, &device);

    printf("=== %s ===\\n", dev_id);
    printf("  Vendor ID   : 0x%04x\\n", vendor);
    printf("  Device ID   : 0x%04x\\n", device);

    /* PCIe Cap */
    int pcie_cap = cap_find(p, 0x10);
    if (pcie_cap) {
        u16 pcie_hdr, lnksta;
        pci_read_config_word(p, pcie_cap, &pcie_hdr);
        pci_read_config_word(p, pcie_cap + 0x12, &lnksta);

        u8 dev_type = (pcie_hdr >> 4) & 0xf;
        int gen  = lnksta & 0xf;
        int width = (lnksta >> 4) & 0x3f;

        printf("  PCIe Cap    : offset 0x%02x\\n", pcie_cap);
        printf("  Device Type : %s\\n", pcie_type_str(dev_type));
        printf("  协商速率   : %s\\n", gen_str(gen));
        printf("  协商宽度   : x%d\\n", width);
    } else {
        printf("  PCIe Cap    : 无（传统 PCI 设备）\\n");
    }

    /* MSI Cap */
    int msi_cap = cap_find(p, 0x05);
    if (msi_cap) {
        u16 ctrl;
        pci_read_config_word(p, msi_cap + 2, &ctrl);
        int vectors = 1 << ((ctrl >> 1) & 0x7);
        printf("  MSI Cap     : offset 0x%02x, %d vectors\\n", msi_cap, vectors);
    }

    /* TLP 类型总结（根据 Device Type）*/
    printf("\\n  TLP 角色分析:\\n");
    int pcie = cap_find(p, 0x10);
    if (pcie) {
        u16 hdr;
        pci_read_config_word(p, pcie, &hdr);
        u8 type = (hdr >> 4) & 0xf;
        if (type == 4)
            printf("    本设备是 RC Root Port\\n"
                   "    - 发给 EP 的读: MRd (Non-Posted, CplD 返回)\\n"
                   "    - 发给 EP 的写: MWr (Posted)\\n"
                   "    - 枚举 EP: CfgRd (ID 路由)\\n");
        else if (type == 0 || type == 1)
            printf("    本设备是 EP\\n"
                   "    - 收到 RC 的 MRd → 返回 CplD\\n"
                   "    - 收到 RC 的 MWr → 无需响应（Posted）\\n"
                   "    - 发 MSI 中断 → MsgD (MWr 到 RC MSI 地址)\\n");
        else
            printf("    本设备是 Switch/Bridge\\n"
                   "    - 转发 TLP（地址路由 or ID 路由）\\n");
    }

    pci_free_dev(p);
    pci_cleanup(pacc);
    return 0;
}
