/*
 * S1_02_fc.c - Flow Control 状态分析
 *
 * 编译：gcc -o S1_02_fc S1_02_fc.c -lpci
 * 运行：sudo ./S1_02_fc [域:总线.设备.功能]
 *
 * 功能：
 *   读取 PCIe Device Status 寄存器，检查数据链路层错误
 *   通过 AER（Advanced Error Reporting）读取链路错误计数
 *
 * Flow Control 错误类型：
 *   - Receiver Overflow（接收方缓冲区溢出）
 *   - Unexpected Completion（收到不在等待的 Completion）
 *   - Bad DLLP CRC（CRC 校验失败）
 *   - Bad TLP CRC（LCRC 校验失败）
 *
 * 适配：pciutils 3.x（Ubuntu 24.04 libpci）
 */

#include <stdio.h>
#include <stdlib.h>
#include <pci/pci.h>

/* PCIe Device Status Register（DevCtl 寄存器）*/
#define PCI_EXP_DEVSTA   0x0A    /* Device Status，偏移在 PCIe Cap + 0x0A */

/* PCIe Device Status 位定义 */
#define PCI_EXP_DEVSTA_TRNPD   (1 << 5)  /* Transactions Pending */
#define PCI_EXP_DEVSTA_AUXPD   (1 << 6)  /* Auxiliary Power Detected */
#define PCIE_MSG_RCV            (1 << 8)  /* Interrupt pending (INTx) */

/*
 * aer_uncor_find - 找 AER Uncorrectable Error 寄存器偏移
 *
 * AER（Advanced Error Reporting）是 PCIe Extended Capability（EEC）
 * AER Extended Cap ID = 0x0001
 *
 * Extended Capability 链表遍历（从 0x100 开始）：
 *   Extended Cap Header: [15:0]=VID/[31:16]=Next Offset
 *   VID = 0x0001 是 AER
 *
 * 返回：AER 基址，找不到返回 0
 */
static int aer_find(struct pci_dev *dev)
{
    u16 next = 0x100;
    int ttl = 64;
    while (ttl-- && next) {
        u32 ehdr = pci_read_long(dev, next);
        u16 vid = ehdr & 0xFFFF;
        next = (ehdr >> 16) & 0xFFFC;
        if (vid == 0xFFFF) break;  /* 链表结束 */
        if (vid == 0x0001)          /* AER Extended Capability */
            return next;
        if (next == 0) break;
    }
    return 0;
}

/*
 * pcie_cap_find - 找 PCIe Capability（ID = 0x10）
 */
static int pcie_cap_find(struct pci_dev *dev)
{
    u8 pos = pci_read_byte(dev, PCI_CAPABILITY_LIST);
    int ttl = 48;
    while (ttl-- && pos) {
        u8 id = pci_read_byte(dev, pos);
        u8 next = pci_read_byte(dev, pos + 1);
        if (id == 0x10) return pos;
        if (id == 0xff) break;
        pos = next;
    }
    return 0;
}

int main(int argc, char **argv)
{
    struct pci_access *pacc = pci_alloc();
    pci_init(pacc);
    pci_scan_bus(pacc);

    const char *dev_id = argc > 1 ? argv[1] : "00:00.0";
    unsigned b, d, f;
    if (sscanf(dev_id, "%x:%x.%x", &b, &d, &f) != 3) {
        fprintf(stderr, "用法: %s [域:]总线:设备.功能\n", argv[0]);
        pci_cleanup(pacc);
        return 1;
    }

    struct pci_dev *p = pci_get_dev(pacc, 0, b, d, f);
    if (!p) { fprintf(stderr, "设备 %s 未找到\n", dev_id); return 1; }

    printf("=== Flow Control 分析：设备 %s ===\n", dev_id);

    /* 找 PCIe Capability */
    int pcie_cap = pcie_cap_find(p);
    if (!pcie_cap) { printf("无 PCIe Cap\n"); goto out; }

    /* PCIe Device Control/Status（DevCtl/DevSta）*/
    /* DevSta 在 PCIe Cap + 0x0A（16位）*/
    u16 devsta = pci_read_word(p, pcie_cap + 0x0A);
    printf("\n[Device Status 寄存器] 0x%04x:\n", devsta);
    printf("  Transactions Pending : %s\n",
           (devsta & (1 << 5)) ? "有未完成事务（注意）" : "无");
    printf("  AUX Power Detected  : %s\n",
           (devsta & (1 << 6)) ? "是" : "否");
    printf("  INTx Pending       : %s\n",
           (devsta & (1 << 8)) ? "有中断待处理" : "无");

    /* AER Uncorrectable Error Status */
    int aer_base = aer_find(p);
    if (!aer_base) {
        printf("\n[AER] 未找到（设备可能不支持 AER）\n");
    } else {
        /* AER Uncorrectable Error Status 在 AER Base + 0x04 */
        u32 ue_sta = pci_read_long(p, aer_base + 0x04);
        printf("\n[AER Uncorrectable Error Status] 0x%08x:\n", ue_sta);
        if (ue_sta == 0)
            printf("  所有错误状态位 = 0，链路正常\n");
        else {
            if (ue_sta & (1 << 0))  printf("  [ERR] 队友吐血（Unexpected Completion）\n");
            if (ue_sta & (1 << 1))  printf("  [ERR] 接收缓冲溢出\n");
            if (ue_sta & (1 << 2))  printf("  [ERR] 畸形 TLP（Malformed TLP）\n");
            if (ue_sta & (1 << 3))  printf("  [ERR] 链路超时（Timeout）\n");
            if (ue_sta & (1 << 4))  printf("  [ERR] 不支持的 DLLP 或 TLP\n");
            if (ue_sta & (1 << 5))  printf("  [ERR] 错误 TLP（Surprise Down）\n");
            if (ue_sta & (1 << 6))  printf("  [ERR] 畸形数据路径错误\n");
            if (ue_sta & (1 << 7))  printf("  [ERR] Header/DATA LCRC 错误\n");
            if (ue_sta & (1 << 8))  printf("  [ERR] CRC（FC 协议错误）\n");
        }

        /* AER Correctable Error Status */
        u32 ce_sta = pci_read_long(p, aer_base + 0x10);
        printf("\n[AER Correctable Error Status] 0x%08x:\n", ce_sta);
        if (ce_sta == 0)
            printf("  所有可纠正错误状态位 = 0，无 CRC 错误\n");
        else {
            if (ce_sta & (1 << 0)) printf("  [WARN] CRC 错误（接收端 DLLP CRC 错误）\n");
            if (ce_sta & (1 << 1)) printf("  [WARN] DLLP 错误\n");
            if (ce_sta & (1 << 2)) printf("  [WARN] NAK 错误（收到了不应答）\n");
            if (ce_sta & (1 << 3)) printf("  [WARN] Replay Timer 超时\n");
            if (ce_sta & (1 << 4)) printf("  [WARN] Replay Num Rollover（TLP 重传次数超限）\n");
        }
    }

  out:
    pci_free_dev(p);
    pci_cleanup(pacc);
    return 0;
}
