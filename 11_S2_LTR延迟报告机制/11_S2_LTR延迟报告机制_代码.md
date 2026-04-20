# S2_06 — LTR 延迟报告机制：代码

本文档包含本节所有源码文件，用 `---` 分隔多个 code block。编译时提取对应 block 即可。

---

## 文件1：S2_06_ltr_decode.c — 用户态工具：读取并解析 LTR 配置

```c
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
```

---

## 文件2：S2_06_ltr_kernel_check.c — 内核模块：检查设备 LTR 路径状态

```c
/**
 * S2_06_ltr_kernel_check.c — 内核模块：检查 PCIe 设备 LTR 状态和 ltr_path
 *
 * 编译：make（使用同级 Makefile）
 * 加载：sudo insmod ltr_kernel_check.ko
 * 检查：dmesg | tail；lsmod | grep ltr_kernel_check
 * 卸载：sudo rmmod ltr_kernel_check
 *
 * 功能：
 *   1. 遍历所有 PCIe 设备，检查 LTR 支持和启用状态
 *   2. 打印 DEVCAP2[LTR] / DEVCTL2[LTR_EN] / ltr_path 字段
 *   3. 如果 ltr_path == 0，说明 LTR 未在路径上生效
 *
 * 适用：验证 bring-up 阶段 LTR 路径传播是否正确
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PCIe Driver Learning");
MODULE_DESCRIPTION("S2_06: LTR (Latency Tolerance Reporting) path checker");

/* 从 DEVCAP2[11] 读 LTR 支持位 */
static bool dev_supports_ltr(struct pci_dev *dev)
{
    u32 devcap2;
    pcie_capability_read_dword(dev, PCI_EXP_DEVCAP2, &devcap2);
    return !!(devcap2 & PCI_EXP_DEVCAP2_LTR);   /* DEVCAP2 bit 11 */
}

/* 从 DEVCTL2[10] 读 LTR 启用位 */
static bool ltr_is_enabled(struct pci_dev *dev)
{
    u32 devctl2;
    pcie_capability_read_dword(dev, PCI_EXP_DEVCTL2, &devctl2);
    return !!(devctl2 & PCI_EXP_DEVCTL2_LTR_EN); /* DEVCTL2 bit 10 */
}

/* 打印单个设备的 LTR 状态 */
static void check_device_ltr(struct pci_dev *dev, void *data)
{
    bool supports = dev_supports_ltr(dev);
    bool enabled  = ltr_is_enabled(dev);

    /* 只关注 PCIe 设备 */
    if (!pci_is_pcie(dev))
        return;

    pci_info(dev, "LTR: supports=%d enabled=%d ltr_path=%d [%s]\n",
             supports, enabled, dev->ltr_path,
             pci_name(dev));
}

/* 入口：模块加载时遍历所有 PCIe 设备 */
static int __init ltr_check_init(void)
{
    pr_info("=== S2_06 LTR Path Checker loaded ===\n");

    /* 遍历 PCIe 总线上的所有设备 */
    pci_walk_bus(&pci_root_bus, check_device_ltr, NULL);

    pr_info("=== LTR check complete ===\n");
    return 0;
}

static void __exit ltr_check_exit(void)
{
    pr_info("=== S2_06 LTR Path Checker unloaded ===\n");
}

module_init(ltr_check_init);
module_exit(ltr_check_exit);
```

---

## 文件3：S2_06_ltr_force_enable.c — 内核模块：强制启用设备 LTR（实验用）

```c
/**
 * S2_06_ltr_force_enable.c — 内核模块：强制启用指定设备的 LTR
 *
 * 编译：make（使用同级 Makefile）
 * 加载：sudo insmod ltr_force_enable.ko dev=00:02.0
 * 卸载：sudo rmmod ltr_force_enable
 *
 * 警告：此模块仅用于实验/调试。生产系统不应强制启用 LTR。
 *       LTR 路径验证失败时强制启用可能导致 ASPM 决策错误。
 *
 * 参数：
 *   dev - 目标设备 BDF（默认 00:02.0）
 *
 * 功能：
 *   读取目标设备 DEVCAP2[LTR] / DEVCTL2[LTR_EN]
 *   如果设备支持 LTR 但未启用，强制写入 DEVCTL2[LTR_EN]
 *   如果是 EP，检查上游 bridge 是否也支持 LTR（ltr_path）
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/errno.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PCIe Driver Learning");
MODULE_DESCRIPTION("S2_06: Force-enable LTR on a PCIe device (debug only)");

static char *devname = "00:02.0";
module_param(devname, charp, 0644);
MODULE_PARM_DESC(devname, "Target device BDF, e.g. 00:02.0");

/* 检查 Root Port / Switch 端口是否启用了 LTR（作为上游）*/
static bool upstream_port_ltr_ok(struct pci_dev *bridge)
{
    u32 devcap2, devctl2;

    if (!bridge)
        return false;

    pcie_capability_read_dword(bridge, PCI_EXP_DEVCAP2, &devcap2);
    if (!(devcap2 & PCI_EXP_DEVCAP2_LTR)) {
        pci_err(bridge, "upstream port does not support LTR\n");
        return false;
    }

    pcie_capability_read_dword(bridge, PCI_EXP_DEVCTL2, &devctl2);
    if (!(devctl2 & PCI_EXP_DEVCTL2_LTR_EN)) {
        pci_info(bridge, "upstream port LTR disabled, re-enabling...\n");
        pcie_capability_set_word(bridge, PCI_EXP_DEVCTL2, PCI_EXP_DEVCTL2_LTR_EN);
    }

    return true;
}

static int __init ltr_force_init(void)
{
    struct pci_dev *dev = NULL;
    u32 devcap2 = 0, devctl2 = 0;
    int ret;

    /* 根据 BDF 查找设备 */
    ret = pci_get_dev_by_name(&pci_root_bus, NULL, devname, &dev);
    if (ret || !dev) {
        pr_err("Cannot find device %s\n", devname);
        return -ENODEV;
    }

    pci_info(dev, "LTR Force-Enable: starting...\n");

    /* ① 确认是 PCIe 设备 */
    if (!pci_is_pcie(dev)) {
        pci_err(dev, "Not a PCIe device, LTR not applicable\n");
        pci_dev_put(dev);
        return -ENODEV;
    }

    /* ② 读 DEVCAP2[11] */
    pcie_capability_read_dword(dev, PCI_EXP_DEVCAP2, &devcap2);
    if (!(devcap2 & PCI_EXP_DEVCAP2_LTR)) {
        pci_err(dev, "Device does NOT support LTR (DEVCAP2[11]=0)\n");
        pci_dev_put(dev);
        return -EOPNOTSUPP;
    }
    pci_info(dev, "  Device supports LTR (DEVCAP2[11]=1)\n");

    /* ③ 读 DEVCTL2[10] */
    pcie_capability_read_dword(dev, PCI_EXP_DEVCTL2, &devctl2);
    if (devctl2 & PCI_EXP_DEVCTL2_LTR_EN) {
        pci_info(dev, "  LTR already enabled (DEVCTL2[10]=1), nothing to do\n");
        pci_dev_put(dev);
        return 0;
    }

    /* ④ 如果是 EP（不是 RC），先检查上游 bridge */
    if (pci_pcie_type(dev) != PCI_EXP_TYPE_ROOT_PORT) {
        struct pci_dev *bridge = pci_upstream_bridge(dev);
        if (!bridge) {
            pci_err(dev, "No upstream bridge found\n");
            pci_dev_put(dev);
            return -ENODEV;
        }
        if (!bridge->ltr_path) {
            pci_err(dev, "Upstream bridge ltr_path=0, cannot enable LTR on this path\n");
            pci_dev_put(dev);
            return -EPERM;   /* 路径不支持，强制启用有危险 */
        }
        pci_info(dev, "  Upstream bridge ltr_path=1, path validated\n");
    }

    /* ⑤ 强制启用 LTR */
    ret = pcie_capability_set_word(dev, PCI_EXP_DEVCTL2, PCI_EXP_DEVCTL2_LTR_EN);
    if (ret) {
        pci_err(dev, "  Failed to set DEVCTL2[LTR_EN]: %d\n", ret);
    } else {
        pci_info(dev, "  Successfully enabled LTR (DEVCTL2[10]=1)\n");
    }

    pci_dev_put(dev);
    return ret;
}

static void __exit ltr_force_exit(void)
{
    pr_info("S2_06 LTR Force-Enable: unloaded\n");
}

module_init(ltr_force_init);
module_exit(ltr_force_exit);
```
