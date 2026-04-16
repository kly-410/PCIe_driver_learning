/*
 * S1_kmod_skeleton.c - 最小可运行内核模块
 * 编译：make
 * 加载：sudo insmod kmod_skeleton.ko
 * 检查：dmesg | tail；lsmod | grep skeleton
 * 卸载：sudo rmmod kmod_skeleton
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kly");
MODULE_DESCRIPTION("PCIe Driver Learning - Step 1: Kernel Module Skeleton");
MODULE_VERSION("1.0");

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Enable debug output (0=off, 1=on)");

static int __init skeleton_init(void)
{
    printk(KERN_INFO "skeleton: loaded (debug=%d)\n", debug);
    return 0;
}

static void __exit skeleton_exit(void)
{
    printk(KERN_INFO "skeleton: unloaded\n");
}

module_init(skeleton_init);
module_exit(skeleton_exit);
