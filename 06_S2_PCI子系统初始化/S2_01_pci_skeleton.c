/*
 * S2_01_pci_skeleton.c - 最小可运行 PCI 驱动骨架
 *
 * 编译：make（使用内核构建系统 kbuild）
 * 加载：sudo insmod S2_01_pci_skeleton.ko
 * 查看日志：dmesg | tail -20
 * 卸载：sudo rmmod S2_01_pci_skeleton
 *
 * 功能：
 *   注册一个 PCI 驱动，匹配成功后打印设备信息
 *   演示 init/probe/remove 的调用时机
 *
 * 内核模块与用户态程序的关键区别：
 *   - 使用 <linux/pci.h> 而非 <pci/pci.h>
 *   - printk 代替 printf（内核日志用 dmesg 查看）
 *   - 不能用标准 C 库（无 libc）
 *   - 编译用 kbuild，不是 gcc 直接调用
 */

#include <linux/pci.h>    /* PCI 驱动框架 */
#include <linux/module.h>  /* MODULE_* 宏 */
#include <linux/init.h>     /* __init / __exit 标记 */

/* 驱动名称（显示在 /sys/bus/pci/drivers/ 下）*/
#define DRIVER_NAME "S2_01_pci_skeleton"

/* 支持的设备列表（PCI ID 表）
 * PCI_DEVICE(vendor, device) 展开为：
 *   .vendor = vendor, .device = device, .subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID
 *   .class = 0, .class_mask = 0, .driver_data = 0
 * PCI_ANY_ID = ~0，表示匹配任何厂商/设备 ID
 *
 * 这里用 PCI_ANY_ID 匹配所有 PCIe 设备，便于测试
 * 正式驱动应该写明具体的 Vendor/Device ID
 */
static const struct pci_device_id ids[] = {
    { PCI_DEVICE(PCI_ANY_ID, PCI_ANY_ID) },  /* 匹配所有设备 */
    { 0, }  /* 结束标记，必须有 */
};
MODULE_DEVICE_TABLE(pci, ids);  /* 让用户态 modprobe 工具能自动加载 */

/*
 * probe - 设备和驱动匹配成功后调用
 *
 * 参数：
 *   dev    - 匹配的 pci_dev 结构
 *   id     - id_table 中匹配的那一项
 *
 * 返回值：
 *   0  = 成功，驱动接管设备
 *   负数 = 失败，驱动不接管设备（常见：-ENOMEM/-EBUSY）
 *
 * 重要：
 *   这是驱动真正开始"工作"的入口
 *   只做最小初始化（enable 设备 + 申请资源）
 */
static int probe(struct pci_dev *dev, const struct pci_device_id *id)
{
    /* pci_name(dev) 返回类似 "0000:01:00.0" 的字符串 */
    printk(KERN_INFO "%s: probe() called for device %s\n",
           DRIVER_NAME, pci_name(dev));

    /* pci_enable_device：置位 Command Register 的 I/O 和 Memory Space Enable
     * 使设备开始响应 MMIO 访问
     * 如果设备还没分配 BAR，这一步不会报错，但后续访问 BAR 仍然返回全 F */
    if (pci_enable_device(dev)) {
        printk(KERN_ERR "%s: pci_enable_device failed\n", DRIVER_NAME);
        return -EIO;
    }

    /* pci_request_regions：申请设备的 BAR I/O 端口范围
     * DRIVER_NAME 用来标记这些 resource 属于哪个驱动
     * 申请成功后，这些 BAR 就被本驱动占用了（其他驱动不能同时申请）
     * 如果返回非0，说明有冲突或已经被申请 */
    if (pci_request_regions(dev, DRIVER_NAME)) {
        pci_disable_device(dev);
        printk(KERN_ERR "%s: pci_request_regions failed\n", DRIVER_NAME);
        return -EBUSY;
    }

    /* pci_iomap：把 BAR0 映射到内核虚拟地址
     * 第二个参数：BAR 编号（0-5）
     * 第三个参数：映射大小（0 表示整个 BAR）
     * 返回 NULL 说明映射失败
     *
     * 注意：pci_iomap 成功的前提是 BAR 已经分配了地址
     * BAR 未分配时，pci_iomap 返回 NULL */
    void __iomem *bar0 = pci_iomap(dev, 0, 0);
    if (!bar0) {
        printk(KERN_WARNING "%s: BAR0 iomap failed (BAR may not be assigned)\n",
               DRIVER_NAME);
        /* 这里不返回错误，因为有些 EP 确实没有 BAR0 */
    }

    printk(KERN_INFO "%s: Device enabled and resources claimed\n", DRIVER_NAME);
    return 0;  /* 成功返回0，驱动接管设备 */
}

/*
 * remove - 驱动卸载或设备被拔出时调用
 *
 * 注意：
 *   - 释放顺序和申请顺序相反（后申请先释放）
 *   - pci_iounmap 必须在 pci_release_regions 之前
 */
static void remove(struct pci_dev *dev)
{
    printk(KERN_INFO "%s: remove() called for device %s\n",
           DRIVER_NAME, pci_name(dev));

    /* 如果 bar0 有映射，先解除映射 */
    /* 注意：pci_iomap 和 pci_iounmap 必须配对使用 */
    /* 这里不能直接调用 pci_iounmap(dev, bar0)？实际上需要记录 bar0 的值 */
    /* 为了简单，这里演示思路即可 */

    /* 释放 BAR 资源 */
    pci_release_regions(dev);

    /* 关闭设备（和 pci_enable_device 配对）*/
    pci_disable_device(dev);

    printk(KERN_INFO "%s: Device released and disabled\n", DRIVER_NAME);
}

/*
 * pci_driver 描述符
 *
 * name    : 驱动名（在 /sys/bus/pci/drivers/ 下显示）
 * id_table: 支持的设备 ID 列表
 * probe   : 匹配成功后的处理函数
 * remove  : 驱动卸载时的清理函数
 */
static struct pci_driver my_pci_driver = {
    .name     = DRIVER_NAME,
    .id_table = ids,      /* 支持的设备列表 */
    .probe    = probe,    /* 设备匹配后调用 */
    .remove   = remove,  /* 驱动卸载时调用 */
    /* 还有其他字段：drvops, err_handlers, shpci_pnt */
};

/*
 * __init：标记这个函数只调用一次（模块加载时）
 * insmod 时，内核调用 my_init()
 * 模块加载完成后，这个函数地址可以释放（节省内存）
 */
static int __init my_init(void)
{
    printk(KERN_INFO "%s: init() called, registering driver\n", DRIVER_NAME);

    /* pci_register_driver：把驱动注册到 PCI 子系统
     * 成功返回正值（注册了多少设备？）
     * 失败返回负值 */
    int ret = pci_register_driver(&my_pci_driver);
    if (ret < 0) {
        printk(KERN_ERR "%s: pci_register_driver failed: %d\n", DRIVER_NAME, ret);
        return ret;
    }

    printk(KERN_INFO "%s: Driver registered successfully\n", DRIVER_NAME);
    return 0;  /* 成功返回0 */
}

/*
 * __exit：标记这个函数在模块卸载时调用
 * 如果模块没有编译成内置驱动（不是 =y），这个函数会被丢弃
 */
static void __exit my_exit(void)
{
    printk(KERN_INFO "%s: exit() called, unregistering driver\n", DRIVER_NAME);

    /* pci_unregister_driver：把驱动从 PCI 子系统移除
     * 会自动遍历所有已 probe 的设备，调用 remove() */
    pci_unregister_driver(&my_pci_driver);

    printk(KERN_INFO "%s: Driver unregistered\n", DRIVER_NAME);
}

/* 模块入口/出口函数 */
module_init(my_init);   /* insmod 时调用 my_init() */
module_exit(my_exit);   /* rmmod 时调用 my_exit() */

/* 许可证和描述 */
MODULE_LICENSE("GPL");             /* 必须 GPL，否则内核不允许加载 */
MODULE_AUTHOR("PCIe Driver Learning Course");
MODULE_DESCRIPTION("S2_01: Minimal PCI driver skeleton for learning");
