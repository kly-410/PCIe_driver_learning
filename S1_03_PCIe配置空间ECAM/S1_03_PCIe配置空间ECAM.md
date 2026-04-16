---
created: 2026-04-17
modified: 2026-04-17
type: learn
lifecycle_status: draft
summary: S1第3课：PCIe配置空间与ECAM机制。覆盖Type0/Type1Header、Capability链表、BAR/PMCAP/MSICAP/PCIeCAP详解、内核pci_read_config_*路径。
tags:
  - type/learn
  - Linux/PCIe驱动
  - 配置空间
  - ECAM
  - S1
---

# S1_03 — PCI/PCIe 配置空间（ECAM）

## 1. 本节目标

- 理解 ECAM（Enhanced Configuration Access Mechanism）原理
- 掌握 Type 0 Header（EP）与 Type 1 Header（RC/bridge/Switch）的区别
- 掌握 Capability 链表结构与遍历方法
- 掌握关键 Capability：PMCAP、MSICAP、PCIeCAP、AER
- 能够对着内核 `pci.c` 追踪 `pci_read_config_*` 的完整调用路径

## 2. 前置依赖

S1_01（TLP 类型）。

## 3. 源码位置

```
内核源码：
  drivers/pci/pci.c              （pci_read_config_* 实现）
  drivers/pci/access.c           （配置空间访问层）
  drivers/pci/proc.c             （/proc/bus/pci/ 访问）
  include/linux/pci.h             （struct pci_dev, struct pci_saved_state）
  include/uapi/linux/pci_regs.h   （配置空间寄存器定义）

规范参考：
  PCI Express Base Spec Rev 5.0 Chapter 7 - Configuration Space
  PCI Local Bus Spec Rev 3.0 Chapter 6 - Configuration Space
```

---

## 4. ECAM 原理

### 4.1 背景：为什么需要 ECAM？

PCI 用 IO 空间访问配置空间（CONFIG_ADDRESS / CONFIG_DATA IO 端口），但：
- x86 IO 空间只有 64 bytes，容量太小
- PCIe 设备数量多（256 Bus × 32 Dev × 8 Func × 4KB = 8GB 配置空间）

ECAM 将配置空间映射到 **内存地址空间**，消除 IO 空间限制。

### 4.2 ECAM 地址计算

```c
// drivers/pci/access.c
// ECAM 地址格式：
// 47:24  Reserved（通常为 0）
// 31:20  Bus Number (8 bits)
// 19:15  Device Number (5 bits)
// 14:12  Function Number (3 bits)
// 11:02  Register Number (Byte offset / 4)
// 01:00  Byte Enable (always 00b for 32-bit access)

u64 pci_config_address(struct pci_bus *bus, unsigned int devfn, int reg)
{
    u32 size = sizeof(u32);
    return PCI_ECAM_OFFSET |
           (bus->number << 20) |    // Bus << 20
           (devfn << 12) |          // Devfn << 12  (PCI_DEVFN(dev,fn))
           (reg & ~0x3);            // Byte offset, aligned to 4
}
// 实际：pci_bus_address(bus, devfn, type) 返回物理地址
```

### 4.3 QEMU virt 中的 ECAM

```bash
# 在 QEMU virt 中，ECAM 基址默认 0xef800000（256MB 窗口）
# 查看 RC 的 MCFG 表（ACPI）
cat /sys/firmware/acpi/tables/MCFG
# 或直接读
setpci -s 00:00.0 COMMAND
```

---

## 5. 配置空间结构

### 5.1 256 bytes 基础配置空间布局

```
Offset  0x000: Device ID / Vendor ID （必须，Offset 0）
Offset  0x002: Status / Command
Offset  0x004: Class Code / Revision ID
Offset  0x008: Cache Line Size / Latency Timer / Header Type
Offset  0x00C: BIST / Header Type / LATENCY_TIMER / CACHE_LINE_SIZE
Offset  0x010: Base Address 0 (BAR0)
Offset  0x014: Base Address 1 (BAR1)
Offset  0x018: Base Address 2 (BAR2) [CardBus only, or secondary BAR]
Offset  0x01C: Base Address 3 (BAR3)
Offset  0x020: Base Address 4 (BAR4)
Offset  0x024: Base Address 5 (BAR5)
Offset  0x028: CardBus CIS Pointer
Offset  0x02C: Subsystem Vendor ID / Subsystem ID
Offset  0x030: Expansion ROM Base Address
Offset  0x034: Capabilities Pointer (Ptr to first Capability)
Offset  0x038: —
Offset  0x03C: Max_Lat / Min_Gnt / INT Pin / INT Line
```

### 5.2 Type 0 Header（Endpoint）

```
Offset  0x0B: Header Type = 0x00
Offset  0x0E: BIST = 0x00（Endoint 没有 BIST）
Offset  0x010~0x024: 6 个 BAR 寄存器（EP 有 6 个 BAR）
Offset  0x034: CardBus CIS Pointer = 0（EP 不支持 CardBus）
```

### 5.3 Type 1 Header（RC / Switch / Bridge）

```
Offset  0x0B: Header Type = 0x01
Offset  0x010~0x024: BAR 不适用（PB 不映射 BAR）
Offset  0x028: CardBus CIS Pointer = 0
Offset  0x030: Subsystem Vendor ID / Subsystem ID
Offset  0x034: Capabilities Pointer
Offset  0x058: Primary Bus Number / Secondary Bus Number / Subordinate Bus Number
Offset  0x05C: Secondary Status / I/O Limit / I/O Base / ...
Offset  0x068: Memory Limit / Memory Base / Prefetchable Memory Limit / Base
Offset  0x070: Bridge Window Size
Offset  0x080: Capabilities Pointer 2（Root Port 有第二个 Cap）
```

**判断 Type 0 还是 Type 1**：读 Offset 0x0E，bit 7 = 1 表示多功能设备，lower 7 bits = 0 是 Type 0，= 1 是 Type 1。

---

## 6. Capability 链表

### 6.1 链表结构

每个 Capability 占用至少 4 bytes：

```c
// include/uapi/linux/pci_regs.h
#define PCI_CAP_LIST_ID      0    // Capability ID
#define PCI_CAP_LIST_NEXT    1    // Pointer to next Capability (0 = end)
#define PCI_CAP_LIST_CAP      2    // Capability-specific registers start here

// 常见 Capability IDs
#define PCI_CAP_ID_PM         0x01  // Power Management
#define PCI_CAP_ID_AGP        0x02  // AGP（废弃）
#define PCI_CAP_ID_VPD        0x03  // Vital Product Data
#define PCI_CAP_ID_SLOTID     0x04  // Slot Identification
#define PCI_CAP_ID_MSI        0x05  // Message Signaled Interrupts
#define PCI_CAP_ID_CHSWP      0x06  // CompactPCI Hot Swap
#define PCI_CAP_ID_PCIX       0x07  // PCI-X
#define PCI_CAP_ID_HT         0x08  // HyperTransport
#define PCI_CAP_ID_VNDR       0x09  // Vendor-specific
#define PCI_CAP_ID_DOE        0x22  // DOE Mailbox（PCIe 6.x）
#define PCI_CAP_ID_EXP        0x10  // PCI Express
#define PCI_CAP_ID_MSIX       0x11  // MSI-X
#define PCI_CAP_ID_OBFF       0x14  // Optimized Buffer Flush/Fill
#define PCI_CAP_ID_LTR        0x18  // Latency Tolerance Reporting
```

### 6.2 遍历 Capability 链表

```c
// drivers/pci/pci.c
// 通用 Capability 遍历模板
u8 pci_find_next_capability(struct pci_dev *dev, u8 pos, int cap)
{
    int ttl = 48;  // PCI_MAX_CAP（最多 48 个 Cap）
    
    while (ttl--) {
        u8 id = pci_read_config_byte(dev, pos);
        if (id == 0xff)  // 无效 Cap，结束
            break;
        if (id == cap)
            return pos;  // 找到了
        pos = pci_read_config_byte(dev, pos + 1);  // 读 next pointer
    }
    return 0;  // 没找到
}
```

### 6.3 关键 Capability 详解

#### PMCAP（Power Management，Offset 0x00）

```c
// Offset 0x00: PMC (Power Management Capabilities)
//   bit[2:0]  : Version（01b 或 10b）
//   bit[3]    : PME Clock（废弃）
//   bit[4]    : Reserved
//   bit[5]    : DSI（Device Specific Initialization）
//   bit[8:6]  : Aux Current（辅助电源需求）
//   bit[11:9] : D1 Support
//   bit[12]   : D2 Support
//   bit[15:13]: PME Support（D0/D1/D2/D3hot/D3cold）
```

#### MSICAP（MSI，Offset 由 Cap Ptr 指向）

```c
// Offset 0x00: MSICAPID (0x05)
// Offset 0x02: MC (Message Control)
//   bit[7:0]   : Multiple Message Capable（1/2/4/8/16/32 vectors）
//   bit[7]     : Multiple Message Enable
//   bit[8]     : MSI Enable
// Offset 0x04: MA (Message Address - 低 32 bits)
// Offset 0x08: MUA (Message Upper Address - 仅 64-bit)
// Offset 0x0C: MD (Message Data - 低 16 bits)
// Offset 0x10: MASK（仅 64-bit MSI）- 每 vector 1 bit
// Offset 0x14: PENDING（仅 64-bit MSI）
```

#### PCIeCAP（PCI Express Capability，Offset 由 Cap Ptr 指向）

```c
// Offset 0x00: PCAPID (0x10)
// Offset 0x02: PCC (PCI Express Capabilities)
//   bit[3:0]   : PCI Express Device Type
//     0b0000 = PCI Express Endpoint
//     0b0001 = Legacy PCI Express Endpoint
//     0b0100 = Root Port of PCIe Root Complex
//     0b0101 = Switch Port (Upstream or Downstream)
// Offset 0x04: DCAP (Device Capabilities)
//   bit[2:0]   : Max_Payload_Size Supported（0=128B, 1=256B, 2=512B, ...）
//   bit[3]     : Phantom Functions Supported
//   bit[4]     : Extended Tag Field Supported
//   bit[12:7]  : Endpoint L0s Acceptable Latency
//   bit[15:13] : Endpoint L1 Acceptable Latency
// Offset 0x06: DC (Device Control)
//   bit[0]     : Enable No Snoop
//   bit[1]     : Enable Relaxed Ordering
//   bit[2]     : Max Read Request Size
```

---

## 7. 内核 pci_read_config_* 调用路径

### 7.1 完整调用链

```c
// drivers/pci/pci.c
// 上层 API
int pci_read_config_byte(const struct pci_dev *dev, int where, u8 *val)
int pci_read_config_word(const struct pci_dev *dev, int where, u16 *val)
int pci_read_config_dword(const struct pci_dev *dev, int where, u32 *val)

// 实际调用路径（以 pci_read_config_dword 为例）
pci_read_config_dword()
  └→ bus->ops->read(bus, devfn, where, size, val)
       └→ struct pci_bus_ops {
            .read = pci_bus_read_config_word,  // 来自 pci_direct_read()
         }
              └→ pci_conf1_read()
                   └→ outb(0xcf8, PCI_CONFIG_ADDRESS)  // IO 端口方式
                   或
                   └→ *addr = val;  // MMIO（ECAM）方式
```

### 7.2 ECAM vs CONF1 vs CONF2

```c
// drivers/pci/host/pci.c 有三种配置访问机制
// CONF1（IO 端口，PCI 2.3 以前）
//   0xcf8：CONFIG_ADDRESS（写 BDF + Register）
//   0xcfc：CONFIG_DATA（读/写数据）
//
// CONF2（IO 端口，映射到 0xC000~0xCFFF，仅 16 devices）
//   不推荐使用
//
// ECAM（内存映射，PCIe）
//   基址 + (bus<<20) + (devfn<<12) + (reg)
//   可以直接通过指针解引用访问
```

### 7.3 内核如何选择访问方式？

```c
// drivers/pci/bus.c
pci_read_config()
  └→ dev->bus->ops->read()
       └→ 在 pci_scan_root_bus() 时，根据硬件 ID 选择：
          - x86：使用 pci_mmconf_config_access()（ECAM）
          - ARM：使用 pci_generic_config_read()
          - 特殊 RC：自定义 .map_bus() 回调
```

---

## 8. 实验

### 实验 1：遍历 Capability 链表

```bash
# 在宿主机查看一个 PCIe 设备的 Capability 链表
lspci -nn -xxx -s 01:00.0 | grep -A2 "Capabilities"
# 对照 spec 确认每个 Cap ID

# 用 setpci 读特定 Capability
setpci -s 01:00.0 34.L    # 读 Capabilities Pointer（Offset 0x34）
# 假设返回 0xD0，则第一个 Cap 在 Offset 0xD0
setpci -s 01:00.0 D0.L    # 读 Cap ID + Next Ptr
```

### 实验 2：读 BAR 值（未映射状态）

```bash
# BAR 在驱动加载前是"未映射"状态，记录的是申请的大小
# 驱动加载后，BAR 被写入实际地址，这里读是解码后的地址
lspci -nn -xxx -s 01:00.0 | grep "Region"
# 预期：显示每个 BAR 的当前解码地址和大小
```

### 实验 3：对照内核源码追踪

```bash
# 打开内核源码，找到 pci_read_config_dword 实现
cat /usr/src/linux-headers-6.17.0-20-generic/drivers/pci/pci.c | \
  grep -A15 "pci_read_config_dword"
```

---

## 9. bring-up 关联

| Bring-up 阶段 | 本节技能用途 |
|--------------|-------------|
| 芯片上电 | 首先读 Vendor/Device ID，确认芯片正常响应 ECAM |
| 枚举 EP | 遍历 Capability，确认 MSI Cap / PCIe Cap 存在 |
| 调试 BAR | 确认 BAR 分配正确（驱动加载后读到的地址 = 申请的地址）|
| 读取 VPD | DOE 访问 VPD（产品序列号），生产测试用 |

---

## 10. 常见错误

### 错误 1：Capability 链表断裂（读出 0xFF）

**现象**：`pci_find_capability()` 返回 0，`lspci -xxx` 显示 Capability 区域有 0xFF。

**原因**：配置空间损坏，或者 Endpoint 没有正确实现 Capability 结构。

**排查**：
```bash
# 确认 Vendor/Device ID 正确（0xFFFF 表示无设备）
setpci -s XX:XX.X 0.w
# 对照原理图确认 Bus/Dev/Func 正确
```

### 错误 2：ECAM 地址不在预期范围

**现象**：MMIO 访问配置空间返回 -1 或触发 Page Fault。

**排查**：
```bash
# 确认 MCFG 表的 ECAM 基址
dmesg | grep MCFG
# 或
cat /sys/firmware/acpi/tables/MCFG | xxd | head -20
```

---

## 11. 自测问题

1. ECAM 地址 (bus << 20) | (devfn << 12) | offset，如果 bus=1, dev=0, fn=0, offset=0，计算 ECAM 物理地址（假设基址 0xef800000）。
2. Type 0 Header 和 Type 1 Header 的本质区别是什么？EP 能否转成 RC？
3. Capability 链表的遍历终止条件是什么？next_ptr = 0 表示什么？
4. pci_read_config_dword() 最终发出的是什么 TLP（CfgRd 还是 MRd）？
5. 为什么 BAR 的低 4 bits 有特殊含义？bit 0 = 1 和 bit 0 = 0 有什么区别？

---

## 12. 下一步

进入 **S1_04 — BAR 地址映射与解码**。

---

## 参考答案

**1. ECAM 地址计算：bus=1, dev=0, fn=0, offset=0，基址 0xef800000，计算物理地址。**

答案：ECAM 地址 = 基址 + (bus << 20) + (devfn << 12) + offset。devfn = (dev << 3) | fn = (0 << 3) | 0 = 0。计算：0xef800000 + (1 << 20) + (0 << 12) + 0 = 0xef800000 + 0x0010_0000 = 0xef900000。Python 验证：
```python
print(hex(0xef800000 + (1 << 20)))
# 0xef900000
```

**2. Type 0 Header 和 Type 1 Header 的本质区别是什么？EP 能否转成 RC？**

答案：Type 0（Endpoint）用于终端设备，配置空间只有 BAR（没有总线相关字段）。Type 1（Bridge/Switch）用于桥接设备，包含 Primary/Secondary/Subordinate Bus Number 字段（用于路由判断）。本质区别：Type 1 能转发 TLP（因为有 Bus 号范围），Type 0 不能。EP 不能转成 RC——RC 是 CPU 侧的 Root Complex，是硬件拓扑决定的，不是软件配置出来的。

**3. Capability 链表的遍历终止条件是什么？next_ptr = 0 表示什么？**

答案：遍历从 Offset 0x34（Header Type 0 设备）或 0x38（Type 1）的 Capabilities Pointer 开始，每读到一个 Cap 就读它的 Next Pointer（偏移+1 字节），直到读到 next_ptr = 0x00（链表结束）。如果读到 0xFF 表示 Capability 结构损坏（无效值），应终止遍历。

**4. pci_read_config_dword() 最终发出的是什么 TLP？**

答案：CfgRd（Configuration Read），通过 ID 路由，Header 包含 Bus#、Dev#、Func#、寄存器偏移。CfgRd 是 Non-Posted，EP 会返回 CplD（携带寄存器值）。如果访问的是 ECAM 空间，在物理层会通过 Memory Read TLP 访问 ECAM 窗口（因为 ECAM 本身是内存映射），但从 PCIe 协议层看发起的是 CfgRd TLP。

**5. 为什么 BAR 的低 4 bits 有特殊含义？bit 0 = 1 和 bit 0 = 0 有什么区别？**

答案：BAR 的 bit 0 表示类型（Space Indicator）：bit 0 = 0 表示 Memory Space，bit 0 = 1 表示 I/O Space。Memory BAR 的 bit 2:1 表示类型（00=32-bit，01=64-bit，10=预取），bit 3 表示预取属性。写入全 1 再读回时，bit 0~2 用于解码判断类型和 size，bit 0 的值不会被置 1，因为它是类型标志位，不是地址位。
