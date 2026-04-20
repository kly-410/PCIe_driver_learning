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
