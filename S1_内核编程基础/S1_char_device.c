/*
 * S1_char_device.c - 字符设备驱动完整模板
 * 功能：/dev/skel 实现 open/read/write/ioctl
 * 测试：
 *   make
 *   sudo insmod skel_char_device.ko
 *   sudo mknod /dev/skel c $(cat /proc/devices | grep skel | awk '{print $1}') 0
 *   echo "hello" > /dev/skel && cat /dev/skel
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>

#define DEVICE_NAME "skel"
#define BUF_SIZE 1024

static dev_t devno;
static struct cdev skel_cdev;
static struct class *skel_class;
static char buffer[BUF_SIZE];
static int buf_len;

static int skel_open(struct inode *inode, struct file *filp)
{
    return 0;
}

static ssize_t skel_read(struct file *filp, char __user *buf,
                         size_t count, loff_t *ppos)
{
    if (*ppos >= buf_len) return 0;
    if (*ppos + count > buf_len) count = buf_len - *ppos;
    if (copy_to_user(buf, buffer + *ppos, count))
        return -EFAULT;
    *ppos += count;
    return count;
}

static ssize_t skel_write(struct file *filp, const char __user *buf,
                          size_t count, loff_t *ppos)
{
    if (count > BUF_SIZE - 1) count = BUF_SIZE - 1;
    if (copy_from_user(buffer, buf, count))
        return -EFAULT;
    buffer[count] = '\0';
    buf_len = count;
    return count;
}

#define SKEL_GET_COUNT   _IOR('S', 0x00, int)
#define SKEL_SET_MODE    _IOW('S', 0x01, int)
#define SKEL_RESET       _IO('S',  0x02)

static long skel_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int val;
    switch (cmd) {
    case SKEL_GET_COUNT:
        return buf_len;
    case SKEL_SET_MODE:
        if (copy_from_user(&val, (int __user *)arg, sizeof(val)))
            return -EFAULT;
        return 0;
    case SKEL_RESET:
        buf_len = 0;
        memset(buffer, 0, BUF_SIZE);
        return 0;
    default:
        return -EINVAL;
    }
}

static int skel_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static struct file_operations skel_fops = {
    .owner = THIS_MODULE,
    .open = skel_open,
    .read = skel_read,
    .write = skel_write,
    .unlocked_ioctl = skel_ioctl,
    .release = skel_release,
};

static int __init skel_init(void)
{
    int ret;
    ret = alloc_chrdev_region(&devno, 0, 1, DEVICE_NAME);
    if (ret < 0) return ret;

    cdev_init(&skel_cdev, &skel_fops);
    skel_cdev.owner = THIS_MODULE;
    ret = cdev_add(&skel_cdev, devno, 1);
    if (ret < 0) {
        unregister_chrdev_region(devno, 1);
        return ret;
    }

    /* 自动创建设备节点 /dev/skel */
    skel_class = class_create("skel");
    device_create(skel_class, NULL, devno, NULL, DEVICE_NAME);

    printk(KERN_INFO "skel: registered as /dev/%s (major=%d)\n",
           DEVICE_NAME, MAJOR(devno));
    return 0;
}

static void __exit skel_exit(void)
{
    device_destroy(skel_class, devno);
    class_destroy(skel_class);
    cdev_del(&skel_cdev);
    unregister_chrdev_region(devno, 1);
    printk(KERN_INFO "skel: unregistered\n");
}

module_init(skel_init);
module_exit(skel_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("kly");
MODULE_DESCRIPTION("PCIe Driver Learning - Char Device Skeleton");
