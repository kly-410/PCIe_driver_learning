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
