/*
 * S1_04_bar_decode.c - BAR 大小解析
 *
 * 编译：gcc -o S1_04_bar_decode S1_04_bar_decode.c -lpci
 * 运行：sudo ./S1_04_bar_decode [域:]总线:设备.功能
 *
 * 功能：
 *   1. 读取 BAR0~BAR5，计算每个 BAR 申请的空间大小
 *   2. 判断 BAR 类型（IO / 32-bit MEM / 64-bit MEM）
 *   3. 识别 PREFETCH 属性
 *
 * 解析原理（PCI Spec）：
 *   写入 0xFFFFFFFF → 读回 → 低 4 bits 是类型编码，其余位是 size
 *   size = ~(read_val & mask) & ~0xf
 *
 * 参考：drivers/pci/setup-bus.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pci/pci.h>

/* BAR 类型解码信息 */
struct bar_info {
    int used;
    int is_io;
    int is_64bit;
    int is_prefetchable;
    uint32_t size;
    uint32_t addr_lo;   /* BAR 值（解码后的当前地址，低32位）*/
    uint32_t addr_hi;   /* 64-bit BAR 的高32位 */
};

/* 判断 BAR 类型并计算 size */
static void decode_bar(struct pci_dev *p, int idx, struct bar_info *info)
{
    int reg = PCI_BASE_ADDRESS_0 + idx * 4;
    uint32_t bar_orig, bar_test;

    pci_read_config_dword(p, reg, &bar_orig);

    /* BAR 未使用 */
    if (bar_orig == 0) {
        info->used = 0;
        return;
    }

    info->used = 1;
    info->addr_lo = bar_orig;

    if (bar_orig & 0x01) {
        /* I/O Space */
        info->is_io = 1;
        /* 写入全 1，计算 I/O mask */
        pci_write_config_dword(p, reg, 0xFFFFFFFF);
        pci_read_config_dword(p, reg, &bar_test);
        pci_write_config_dword(p, reg, bar_orig);
        uint32_t mask = PCI_BASE_ADDRESS_IO_MASK;
        info->size = (~bar_test) & mask;
    } else {
        /* Memory Space */
        info->is_io = 0;
        uint32_t mem_mask = PCI_BASE_ADDRESS_MEM_MASK;

        /* 检查 64-bit（bit 1 = 01）*/
        if (((bar_orig >> 1) & 0x03) == 0x01) {
            info->is_64bit = 1;
            /* 读高 32 位（下一个 BAR）*/
            uint32_t bar_hi;
            pci_read_config_dword(p, reg + 4, &bar_hi);
            info->addr_hi = bar_hi;

            /* 写入全 1（低 32 位）*/
            pci_write_config_dword(p, reg, 0xFFFFFFFF);
            pci_read_config_dword(p, reg, &bar_test);
            pci_write_config_dword(p, reg, bar_orig);
            uint64_t size64 = (~bar_test) & mem_mask;
            info->size = (uint32_t)size64;
        } else {
            info->is_64bit = 0;
            pci_write_config_dword(p, reg, 0xFFFFFFFF);
            pci_read_config_dword(p, reg, &bar_test);
            pci_write_config_dword(p, reg, bar_orig);
            info->size = (~bar_test) & mem_mask;
        }

        /* PREFETCH（bit 3 = 1）*/
        info->is_prefetchable = (bar_orig >> 3) & 0x01;
    }
}

static const char *bar_type_str(struct bar_info *info)
{
    if (!info->used) return "未使用";
    if (info->is_io) return "I/O Space";
    if (info->is_64bit)
        return info->is_prefetchable ? "64-bit MEM (PREFETCH)" : "64-bit MEM";
    return info->is_prefetchable ? "32-bit MEM (PREFETCH)" : "32-bit MEM";
}

static const char *fmt_size(uint32_t size)
{
    static char buf[32];
    if (size >= 1024*1024*1024)
        snprintf(buf, sizeof(buf), "0x%08x (%u GB)", size, size/(1024*1024*1024));
    else if (size >= 1024*1024)
        snprintf(buf, sizeof(buf), "0x%08x (%u MB)", size, size/(1024*1024));
    else if (size >= 1024)
        snprintf(buf, sizeof(buf), "0x%08x (%u KB)", size, size/1024);
    else
        snprintf(buf, sizeof(buf), "0x%08x (%u bytes)", size, size);
    return buf;
}

int main(int argc, char **argv)
{
    struct pci_access *pacc = pci_alloc();
    pci_init(pacc);
    pci_scan_bus(pacc);

    const char *dev_id = argc > 1 ? argv[1] : "01:00.0";
    unsigned b, dev, fn;

    if (sscanf(dev_id, "%x:%x.%x", &b, &dev, &fn) != 3) {
        fprintf(stderr, "用法: %s [域:]总线:设备.功能\\n", argv[0]);
        pci_cleanup(pacc);
        return 1;
    }

    struct pci_dev *p = pci_get_dev(pacc, 0, b, dev, fn);
    if (!p) { fprintf(stderr, "设备未找到\\n"); return 1; }

    printf("设备 %s BAR 解析：\\n", dev_id);
    for (int i = 0; i < 6; i++) {
        struct bar_info info = {0};
        decode_bar(p, i, &info);

        printf("  BAR%d: ", i);
        if (!info.used) {
            printf("未使用\\n");
            continue;
        }

        printf("%s, %s\\n", bar_type_str(&info), fmt_size(info.size));

        if (info.is_64bit) {
            printf("       地址: 0x%08x_%08x\\n", info.addr_hi, info.addr_lo);
            printf("       (64-bit BAR，高32位在 BAR%d)\\n", i+1);
        } else {
            printf("       地址: 0x%08x\\n", info.addr_lo);
        }
    }

    pci_free_dev(p);
    pci_cleanup(pacc);
    return 0;
}
