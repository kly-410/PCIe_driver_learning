/*
 * S1_03_ecam_access.c - ECAM 直接访问演示
 *
 * 编译：gcc -o S1_03_ecam_access S1_03_ecam_access.c
 * 运行：sudo ./S1_03_ecam_access [域:总线:设备.功能] [寄存器偏移(hex)]
 * 示例：
 *   sudo ./S1_03_ecam_access 00:01.0 00     # 读 VID/DID
 *   sudo ./S1_03_ecam_access 00:01.0 10     # 读 BAR0
 *   sudo ./S1_03_ecam_access 00:01.0 34     # 读 Capabilities Pointer
 *
 * 注意：需要 root 权限，且系统未启用 CONFIG_IO_STRICT_DEVMEM
 *
 * ECAM 地址格式（PCIe Spec Rev 5.0 Chapter 7）：
 *   [31:20] Bus      (8 bits)
 *   [19:15] Device   (5 bits)
 *   [14:12] Function  (3 bits)
 *   [11:02] Register (byte offset / 4)
 *   [01:00] Byte Enable (00b for 32-bit access)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>

#define ECAM_BASE_DEFAULT  0xef800000UL  /* QEMU virt 默认 ECAM 基址 */
#define PAGE_SIZE          4096

/*
 * 计算 ECAM 物理地址
 */
static inline uint64_t ecam_addr(uint64_t base, int bus, int dev, int fn, int reg)
{
    return base
        | ((uint64_t)bus << 20)
        | ((uint64_t)dev << 15)
        | ((uint64_t)fn  << 12)
        | ((uint64_t)reg << 2);
}

/*
 * 打印 PCIe 配置空间关键字段（仅解析 Offset 0 的 VID/DID）
 */
static void decode_vid_did(uint32_t val)
{
    uint16_t vendor_id = val & 0xffff;
    uint16_t device_id = val >> 16;
    printf("  Vendor ID   : 0x%04x%s\n", vendor_id,
           vendor_id == 0xffff ? " (设备不存在)" : "");
    printf("  Device ID   : 0x%04x\n", device_id);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "用法: %s [域:总线:设备.功能] [寄存器偏移(hex, 默认0)]\\n",
                argv[0]);
        fprintf(stderr, "示例: %s 00:01.0 00   # 读 VID/DID\\n", argv[0]);
        fprintf(stderr, "示例: %s 00:01.0 10   # 读 BAR0\\n", argv[0]);
        return 1;
    }

    int bus, dev, fn;
    /* 支持 "00:01.0" 或 "0000:00:01.0" */
    if (sscanf(argv[1], "%x:%x.%x", &bus, &dev, &fn) != 3) {
        fprintf(stderr, "格式错误，请用 域:总线:设备.功能 或 总线:设备.功能\\n");
        return 1;
    }

    int reg_off = (argc > 2) ? (int)strtol(argv[2], NULL, 16) : 0;

    /* 打开 /dev/mem（需要 root）*/
    int fd = open("/dev/mem", O_RDONLY);
    if (fd < 0) {
        perror("open(/dev/mem) 失败（需要 sudo）");
        return 1;
    }

    /* mmap 一页，ECAM 通常是 256MB 大窗口，不需要多页 */
    uint64_t phys = ecam_addr(ECAM_BASE_DEFAULT, bus, dev, fn, reg_off >> 2);
    uint64_t page_base = phys & ~(PAGE_SIZE - 1);
    uint64_t page_off  = phys - page_base;

    void *mmap_addr = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_PRIVATE, fd, page_base);
    if (mmap_addr == MAP_FAILED) {
        perror("mmap 失败");
        close(fd);
        return 1;
    }

    volatile uint32_t *reg_ptr =
        (volatile uint32_t *)((char *)mmap_addr + page_off);
    uint32_t val = *reg_ptr;

    printf("ECAM addr = 0x%lx | PCI ID = %02x:%02x.%x | Reg = 0x%02x\\n",
           phys, bus, dev, fn, reg_off);
    printf("值 = 0x%08x\\n", val);

    if (reg_off == 0)
        decode_vid_did(val);

    munmap(mmap_addr, PAGE_SIZE);
    close(fd);
    return 0;
}
