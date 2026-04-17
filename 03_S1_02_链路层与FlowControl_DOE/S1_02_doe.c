/*
 * S1_02_doe.c - DOE Mailbox 遍历与 VPD 访问演示
 *
 * 编译：gcc -o S1_02_doe S1_02_doe.c -lpci
 * 运行：sudo ./S1_02_doe [域:]总线:设备.功能
 *
 * 功能：
 *   1. 扫描设备的 DOE Mailbox（Cap ID = 0x23）
 *   2. 读取 DOE_CAP_HDR，获取 Vendor ID 和版本
 *   3. 发起标准 VPD DOE 读（Object Type = 3）
 *   4. 轮询 DOE_INT_STATUS 等待完成
 *
 * 参考：PCIe Base Spec Rev 5.0 Chapter 6.30
 * 源码：drivers/pci/pci/doe.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pci/pci.h>
#include <stdint.h>
#include <errno.h>

#define PCI_DOE_CAP_HDR      0x04
#define PCI_DOE_INT_STATUS   0x08
#define PCI_DOE_OBJECT_HDR   0x10
#define PCI_DOE_DATA         0x14

#define PCI_DOE_VENDOR_VPD   3   /* VPD 对象 */
#define PCI_DOE_VENDOR_CIS   4   /* CIS 对象 */
#define PCI_DOE_TIMEOUT_MS   1000

/* 在配置空间中查找 DOE Cap（Cap ID = 0x23）*/
static int find_doe_cap(struct pci_dev *dev)
{
    u8 pos = 0, id, next;
    int ttl = 48;

    pci_read_config_byte(dev, PCI_CAPLIST_PTR, &pos);
    while (ttl-- && pos) {
        pci_read_config_byte(dev, pos, &id);
        pci_read_config_byte(dev, pos + 1, &next);
        if (id == 0x23)           /* DOE Cap ID */
            return pos;
        if (id == 0xff) break;    /* 无效 Cap */
        pos = next;
    }
    return 0;  /* 未找到 */
}

/* 轮询等待 DOE 完成（DONE 位=1 或 BUSY 位清零）*/
static int doe_poll_done(struct pci_dev *dev, int offset)
{
    int timeout = PCI_DOE_TIMEOUT_MS;
    u32 status;

    while (timeout--) {
        pci_read_config_dword(dev, offset + PCI_DOE_INT_STATUS, &status);

        /* BUSY 位 (bit 31) = 1: 还在处理，继续等 */
        if (status & 0x80000000) {
            usleep(1000);
            continue;
        }

        /* DONE 位 (bit 0) = 1: 完成 */
        if (status & 0x1)
            return 0;

        /* 其他错误 */
        return -EIO;
    }

    return -ETIMEDOUT;
}

/* 发起 DOE 读（同步阻塞）*/
static int doe_read_object(struct pci_dev *dev, int doe_offset,
                          u16 object_type, u32 *out_data)
{
    u32 hdr;
    int ret;

    /* 写 Object Header: type[31:16] | vendor_id[15:0] = 0 (PCI-SIG) */
    hdr = (object_type << 16);  /* vendor_id = 0 for standard objects */
    pci_write_config_dword(dev, doe_offset + PCI_DOE_OBJECT_HDR, hdr);

    /* 清 DONE 状态，触发 DOE 事务 */
    pci_write_config_dword(dev, doe_offset + PCI_DOE_INT_STATUS, 0x00000001);

    /* 轮询等待完成 */
    ret = doe_poll_done(dev, doe_offset);
    if (ret < 0)
        return ret;

    /* 读响应数据（第一个 DW）*/
    pci_read_config_dword(dev, doe_offset + PCI_DOE_DATA, out_data);

    /* 清 DONE 状态 */
    pci_write_config_dword(dev, doe_offset + PCI_DOE_INT_STATUS, 0x00000001);

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
        fprintf(stderr, "示例: %s 00:00.0\\n", argv[0]);
        pci_cleanup(pacc);
        return 1;
    }

    struct pci_dev *p = pci_get_dev(pacc, 0, b, dev, fn);
    if (!p) {
        fprintf(stderr, "设备 %s 未找到\\n", dev_id);
        pci_cleanup(pacc);
        return 1;
    }

    int doe_offset = find_doe_cap(p);
    if (!doe_offset) {
        printf("设备 %s: 无 DOE Mailbox（正常，许多设备不支持）\\n", dev_id);
        pci_free_dev(p);
        pci_cleanup(pacc);
        return 0;
    }

    printf("设备 %s: DOE Mailbox at offset 0x%02x\\n", dev_id, doe_offset);

    /* 读 DOE Capability Header */
    u32 cap_hdr;
    pci_read_config_dword(p, doe_offset + PCI_DOE_CAP_HDR, &cap_hdr);
    u16 vendor_id = cap_hdr & 0xffff;
    u8  version   = (cap_hdr >> 16) & 0xff;
    printf("  DOE Vendor ID = 0x%04x, Version = %d\\n", vendor_id, version);

    /* 尝试读 VPD */
    u32 vpd_data = 0;
    int ret = doe_read_object(p, doe_offset, PCI_DOE_VENDOR_VPD, &vpd_data);
    if (ret == -ETIMEDOUT)
        printf("  VPD DOE: 超时（设备可能不支持）\\n");
    else if (ret == -EIO)
        printf("  VPD DOE: 协议错误\\n");
    else
        printf("  VPD DOE: 读成功，数据=0x%08x\\n", vpd_data);

    pci_free_dev(p);
    pci_cleanup(pacc);
    return 0;
}
