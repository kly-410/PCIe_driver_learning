/**
 * S2_06_ltr_decode.c — 用户态工具：读取和解析 PCIe 设备的 LTR 扩展能力
 *
 * 编译：gcc -o ltr_decode S2_06_ltr_decode.c -lpci
 * 用法：sudo ./ltr_decode [Bus:Dev:Func]  （默认 00:02.0）
 *
 * 功能：
 *   1. 读取设备 PCIe Extended Capability，找到 LTR（ID=0x18）
 *   2. 读取 Max_Snoop_Latency 和 Max_NoSnoop_Latency 寄存器
 *   3. 解码 scale × value，返回实际延迟（ns）
 *   4. 读取 DEVCTL2[LTR_EN] 判断 LTR 是否启用
 *
 * 依赖：libpci-dev（Ubuntu: sudo apt install libpci-dev）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pci/pci.h>

/* PCIe 扩展能力 ID：LTR = 0x18 */
#define PCI_EXT_CAP_ID_LTR         0x18

/* LTR 寄存器偏移（相对于 LTR 扩展能力起始地址）*/
#define PCI_LTR_MAX_SNOOP_LAT      0x04
#define PCI_LTR_MAX_NOSNOOP_LAT    0x08

/* LTR 编码 mask / shift */
#define PCI_LTR_VALUE_MASK         0x000003ff
#define PCI_LTR_SCALE_MASK         0x00001c00
#define PCI_LTR_SCALE_SHIFT        10

/* DEVCAP2 / DEVCTL2 LTR 相关位 */
#define PCI_EXP_DEVCAP2_LTR        0x00000800   /* Device Capability 2, bit 11 */
#define PCI_EXP_DEVCTL2_LTR_EN     0x00000400   /* Device Control 2, bit 10 */
#define PCI_EXP_DEVCTL2            4            /* DEVCTL2 偏移（相对于 PCI_EXP_CAP*）*/
#define PCI_EXP_DEVCAP2            12           /* DEVCAP2 偏移 */

/* Scale × 值 → ns */
static const char *scale_name[] = { "1 ns", "32 ns", "1 us", "32 us",
                                     "1 ms", "33.5 ms", "reserved" };

/**
 * decode_ltr_register - 解码 LTR 寄存器值（10bit value + 3bit scale）
 * @reg: 从 PCI_LTR_MAX_*_LAT 读出的原始 32bit 值
 * @label: 打印用的标签（"Snoop" 或 "No-Snoop"）
 *
 * 注意：Max_NoSnoop 的 value 在 bits[27:16]，而不是[15:0]
 * 但 PCI_LTR_MAX_NOSNOOP_LAT 偏移处读取的 DWORD 格式如下：
 *   [31:16] = Max_NoSnoop_Value [27:16]（因为有 reserved bit 在中间）
 *   [15:0]  = Max_Snoop_Value
 * 所以解码逻辑统一处理低16bit 即可
 */
static void decode_ltr_register(u32 reg, const char *label)
{
    unsigned int value, scale;
    u64 latency_ns;

    if (reg == 0 || reg == 0xffffffff) {
        printf("  %-12s: not implemented or unreachable (0x%08x)\n", label, reg);
        return;
    }

    value = reg & PCI_LTR_VALUE_MASK;
    scale = (reg & PCI_LTR_SCALE_MASK) >> PCI_LTR_SCALE_SHIFT;

    /* scale=6 是 reserved，不应使用 */
    if (scale > 6)
        scale = 6;

    /* 计算实际延迟 */
    switch (scale) {
    case 0: latency_ns = value * 1;         break;
    case 1: latency_ns = value * 32;       break;
    case 2: latency_ns = value * 1024;     break;
    case 3: latency_ns = value * 32768;     break;
    case 4: latency_ns = value * 1048576;   break;
    case 5: latency_ns = value * 33554432;  break;
    default: latency_ns = 0;                break;
    }

    printf("  %-12s: raw=0x%04x, value=%3u, scale=%u (%s), latency=%llu ns",
           label, reg, value, scale, scale_name[scale], latency_ns);

    /* 友好显示 */
    if (latency_ns >= 1000000000ULL)
        printf(" (%.1f s)\n", latency_ns / 1000000000.0);
    else if (latency_ns >= 1000000ULL)
        printf(" (%.1f ms)\n", latency_ns / 1000000.0);
    else if (latency_ns >= 1000ULL)
        printf(" (%.1f us)\n", latency_ns / 1000.0);
    else
        printf("\n");
}

int main(int argc, char *argv[])
{
    struct pci_access *pacc;
    struct pci_dev *dev;
    char addr[13] = "00:02.0";
    u32 devcap2 = 0, devctl2 = 0;
    int ltr_offset = 0;
    u32 snoop_lat = 0, nosnoop_lat = 0;

    if (argc > 1)
        strncpy(addr, argv[1], sizeof(addr) - 1);

    /* 初始化 libpci */
    pacc = pci_alloc();
    pci_init(pacc);
    pci_scan_bus(pacc);

    /* 解析设备地址 */
    dev = pci_get_dev(pacc, 0, 0,
                      pci_parse_addr(addr, NULL), 0);
    if (!dev) {
        fprintf(stderr, "Error: cannot find device %s\n", addr);
        pci_cleanup(pacc);
        return 1;
    }

    printf("=== LTR (Latency Tolerance Reporting) Analysis ===\n");
    printf("Device: %s\n", addr);

    /* 读取 DEV_CAP2 和 DEVCTL2 */
    pci_read_config_dword(dev, pci_read_cap_off(dev, PCI_EXP_DEVCAP2, NULL), &devcap2);
    pci_read_config_dword(dev, pci_read_cap_off(dev, PCI_EXP_DEVCTL2, NULL), &devctl2);

    printf("\n[1] LTR Capability Check\n");
    if (devcap2 & PCI_EXP_DEVCAP2_LTR)
        printf("  Device supports LTR: YES (DEVCAP2[11] = 1)\n");
    else
        printf("  Device supports LTR: NO  (DEVCAP2[11] = 0) -- LTR not applicable\n");

    printf("\n[2] LTR Enable Status\n");
    if (devctl2 & PCI_EXP_DEVCTL2_LTR_EN)
        printf("  LTR Enabled:         YES  (DEVCTL2[10] = 1)\n");
    else
        printf("  LTR Enabled:         NO   (DEVCTL2[10] = 0) -- LTR not active\n");

    /* 找到 LTR 扩展能力偏移 */
    ltr_offset = pci_find_ext_cap(dev, PCI_EXT_CAP_ID_LTR);
    if (!ltr_offset) {
        printf("\n  LTR Extended Capability not found in this device.\n");
        printf("  (Not an PCIe device, or capability not implemented)\n");
        pci_cleanup(pacc);
        return 0;
    }

    printf("\n[3] LTR Extended Capability\n");
    printf("  LTR Cap Offset:      0x%02x\n", ltr_offset);

    /* 读取 LTR 延迟寄存器（DWORD 访问）*/
    pci_read_config_dword(dev, ltr_offset + PCI_LTR_MAX_SNOOP_LAT, &snoop_lat);
    /* NoSnoop 字段在同一个 DWORD 的高16bit */
    pci_read_config_dword(dev, ltr_offset + PCI_LTR_MAX_NOSNOOP_LAT, &nosnoop_lat);

    printf("\n[4] Latency Values\n");
    decode_ltr_register(snoop_lat & 0xffff, "Max_Snoop");
    decode_ltr_register((nosnoop_lat >> 16) & 0xffff, "Max_NoSnoop");

    /* 判断 LTR 路径有效性（用户态无法直接读 kernel 的 ltr_path，但可以推测）*/
    printf("\n[5] Practical Interpretation\n");
    if (!(devcap2 & PCI_EXP_DEVCAP2_LTR)) {
        printf("  -> This device does not support LTR.\n");
    } else if (!(devctl2 & PCI_EXP_DEVCTL2_LTR_EN)) {
        printf("  -> Device supports LTR but it is disabled.\n");
        printf("     Possible reasons:\n");
        printf("       - Platform ACPI DisableNativeLTR=1\n");
        printf("       - Upstream bridge (RC/Switch) does not support LTR\n");
        printf("       - Hot-added device, path not reconfigured yet\n");
    } else {
        printf("  -> LTR is active. System ASPM can make informed power decisions.\n");
        if (snoop_lat >= (2 << PCI_LTR_SCALE_SHIFT)) /* scale ≥ 1ms means deeply idle tolerant */
            printf("     Device is highly latency-tolerant (>1ms). L1/L1.2 entry likely.\n");
        else
            printf("     Device has moderate latency tolerance. ASPM L0s likely, L1 less common.\n");
    }

    printf("\n");
    pci_cleanup(pacc);
    return 0;
}
