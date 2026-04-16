---
created: 2026-04-17
modified: 2026-04-17
type: learn
lifecycle_status: draft
summary: S1第4课：BAR地址映射与解码。覆盖BAR类型判断（IO/MEM/32bit/64bit）、pci_request_regions、pci_iomap/pci_ioremap、内存屏障与MMIO读写、内核setup-bus.c解析。
tags:
  - type/learn
  - Linux/PCIe驱动
  - BAR
  - 地址映射
  - MMIO
  - S1
---

# S1_04 — BAR 地址映射与解码

## 1. 本节目标

- 理解 BAR（Base Address Register）的作用：申请 I/O 或 Memory 地址空间
- 掌握 BAR 类型判断：IO vs MEM，32-bit vs 64-bit，PREFETCH
- 理解 BAR 解析过程（软件写入全 1，读回得到解码后的地址）
- 掌握内核 `pci_request_regions()` / `pci_iomap()` / `ioremap()` 的使用
- 能够对着内核 `setup-bus.c` 追踪 BAR 分配流程

## 2. 前置依赖

S1_01（TLP 类型）、S1_03（配置空间）。

## 3. 源码位置

```
内核源码：
  drivers/pci/setup-bus.c        （BAR 分配核心逻辑）
  drivers/pci/pci.c              （pci_request_regions, pci_iomap）
  arch/x86/pci/mmconfig_64.c    （ECAM MMIO 访问）
  include/asm-generic/io.h       （ioremap/ioread32 定义）
  include/uapi/linux/pci_regs.h  （BAR 寄存器偏移）

规范参考：
  PCI Express Base Spec Rev 5.0 Chapter 7.5 - Base Address Registers
  PCI Local Bus Spec Rev 3.0 Chapter 6.2.5 - Base Address Registers
```

---

## 4. BAR 的作用

### 4.1 BAR 的本质

BAR 是一个寄存器，位于 EP 配置空间（Offset 0x010~0x027，共 6 个 BAR 寄存器）。

软件通过 BAR 告诉 EP：**"你的寄存器/内存映射到哪个 CPU 地址，这样 CPU 才能访问你。"**

### 4.2 BAR 类型判断

读取 BAR 的 bit 0：

```c
// drivers/pci/setup-bus.c
// pci_read_bar() 解析 BAR 类型
int bar_is_io(u32 bar)
{
    return bar & PCI_BASE_ADDRESS_SPACE_IO;  // bit 0 = 1 → IO BAR
}

int bar_is_mem(u32 bar)
{
    return bar & PCI_BASE_ADDRESS_SPACE_MEMORY;  // bit 0 = 0 → MEM BAR
}
```

**BAR 类型对照表**：

| BAR[bit 0] | BAR[bit 2:1] | 类型 | 说明 |
|------------|-------------|------|------|
| 0 | 00 | 32-bit MEM | 普通内存，不预取 |
| 0 | 01 | 64-bit MEM | 低 32 位，bit 1=01 表示高位在下一个 BAR |
| 0 | 10 | 32-bit MEM | 预取内存（可合并写）|
| 0 | 11 | 保留 | — |
| 1 | 00 | IO | IO 端口（x86 专用）|

### 4.3 为什么 bit 3 表示预取？

PCIe 允许写合并（Write Combining），预取（Prefetchable）意味着：
- 可以合并多次写操作
- 读不会改变 EP 状态（读一个地址不会产生副作用）

**常见的非预取 BAR**：设备控制寄存器（写会改变状态，不能预取）。

---

## 5. BAR 解析过程

### 5.1 软件如何知道 EP 需要多大地址空间？

**方法：写入全 1，读回。**

```
步骤 1：写入 0xFFFFFFFF 到 BAR
         BAR 值：0xFFFF_FFF0（IO）或 0xFFFF_FFF0（MEM）

步骤 2：读回 BAR
         假设返回 0xFDF_0004：
         
         对于 MEM BAR（bit 0 = 0）：
           - 低 4 bits (0x4) 包含类型信息（bit 0=0=Mem, bit 3=1=64bit？不，这里假设 32-bit）
           - 取反得到 size：~0x4 = 0xFFFF_FFFB → & ~0xF = 0xFDF_0000
           - 所以这个 BAR 申请了 0xFDF_0000 大小（2MB）

步骤 3：将 BAR 解码后的基址写入配置空间
         ECAM 地址写入
         系统分配地址后，写回 BAR
```

### 5.2 内核 BAR 解析代码

```c
// drivers/pci/setup-bus.c
// pci_bar_decode() 计算 BAR size
static void pci_read_bases(struct pci_dev *dev, int rom)
{
    unsigned long size;
    struct resource *res = rom ? &dev->resource[PCI_ROM_RESOURCE]
                               : &dev->resource[bar];
    
    // 写 0xFFFFFFFF，读回
    pci_write_config_dword(dev, base, 0xFFFFFFFF);
    pci_read_config_dword(dev, base, &sz);
    
    // 恢复原值
    pci_write_config_dword(dev, base, bar);
    
    if (!sz || sz == 0xFFFFFFFF)
        return;
    
    // 解析 size（根据类型）
    if (bar_is_io(sz))
        size = sz & PCI_BASE_ADDRESS_IO_MASK;
    else
        size = sz & PCI_BASE_ADDRESS_MEM_MASK;
    
    res->start = 0;
    res->end = size;
    res->flags = (bar_is_io(sz) ? IORESOURCE_IO : IORESOURCE_MEM)
                 | (bar_type << 1);
}
```

---

## 6. 64-bit BAR 处理

### 6.1 64-bit BAR 的编码

```
BAR0 (低 32 bits)：  0xFDF_0004  → bit 1:0 = 01b，表示 64-bit
BAR1 (高 32 bits)：  0x0000_0000  → 存放地址的高 32 bits

实际上：EP 申请 4GB 空间（0xFDF_0000 << 32 | 0x0000_0000）
       系统分配 0x4000_0000_0000 ~ 0x4000_0000_0FFF_FFFF
```

### 6.2 为什么需要 64-bit BAR？

32-bit 地址只能访问 4GB 空间（PA）。
如果 EP 需要 > 4GB 的 MMIO 空间，必须用 64-bit BAR。

### 6.3 64-bit BAR 的约束

- BAR0 和 BAR1 必须配对使用
- BAR2 和 BAR3 可以配对（如果需要）
- BAR4 和 BAR5 可以配对（如果需要）
- 但不能 BAR0 和 BAR2 配对

---

## 7. MMIO 读写

### 7.1 为什么 ioremap？

CPU 只能通过虚拟地址访问内存。EP 的 BAR 地址是 **物理地址（PCIe 地址）**，在内核态需要通过 `ioremap()` 建立页表映射，才能用指针访问。

```c
// arch/x86/include/asm/io.h
// ioremap：物理地址 → 内核虚拟地址
void __iomem *ioremap(resource_size_t phys_addr, size_t size);

// iounmap：取消映射
void iounmap(volatile void __iomem *addr);

// MMIO 读写（不要直接解引用 __iomem 指针！）
u32 ioread32(void __iomem *addr);
void iowrite32(u32 val, void __iomem *addr);

// 8/16/64 bit 版本也可用
```

### 7.2 为什么要用 readl/writel？

```c
// 错误做法（不要这样！）：
u32 val = *(volatile u32 *)addr;  // 可能被编译器优化或 CPU 重排

// 正确做法：
u32 val = ioread32(addr);          // 带屏障，保证按序访问
iowrite32(val, addr);              // 写 posted，不保证 EP 收到顺序
```

### 7.3 内存屏障

```c
// drivers/pci/pci.c 中的 MMIO 屏障
void pci_write_config(struct pci_dev *dev, int offset, u32 val)
{
    // 写配置寄存器
    writew(val, (void __iomem *)config_addr);
    
    // 为什么需要 wmb()？
    // PCIe 是点对点网络，写操作可能 Posted（不等待完成）
    // wmb() 保证"写配置寄存器"在"写数据寄存器"之前被 EP 看到
    wmb();
}
```

屏障类型：

| 屏障 | 作用 |
|------|------|
| `rmb()` | 读屏障：保证 rmb() 之后的读操作不会被 CPU 重排到 rmb() 之前 |
| `wmb()` | 写屏障：保证 wmb() 之后的写操作不会被 CPU 重排到 wmb() 之前 |
| `mb()` | 读写全屏障 |
| `dma_rmb/wmb()` | DMA 场景专用（考虑 Device 侧）|

### 7.4 pci_iomap vs ioremap

```c
// drivers/pci/pci.c
// pci_iomap：对 pci 设备更友好的 ioremap 封装
void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long max)
{
    // 1. 检查 BAR 有效性
    // 2. 拿到 resource
    // 3. ioremap(resource_start, resource_size)
}

// 推荐用法（自动处理 NULL）
void __iomem *pci_iomap_wc(struct pci_dev *dev, int bar, unsigned long max)
{
    // ioremap_wc：Write-Combining，允许写合并
    // 适用于 framebuffer、video 驱动等大块顺序写入
}
```

---

## 8. pci_request_regions

### 8.1 为什么要 request？

系统中可能有多个驱动。如果两个驱动同时 `ioremap` 同一个 BAR，会冲突。
`pci_request_regions()` 在 `/sys/bus/pci/devices/XX:XX.X/resource*` 标记这个 BAR 已被占用。

### 8.2 完整使用流程

```c
// drivers/pci/pci.c
int pci_request_regions(struct pci_dev *dev, const char *res_name)
{
    // 遍历 dev->resource[]，对每个 IORESOURCE_MEM 类型的 BAR
    // 调用 request_resource(&iomem_resource, &dev->resource[i])
}

// 正确使用流程
static int my_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
    // 1. 使能设备
    if (pci_enable_device(dev))
        return -EIO;
    
    // 2. request BAR
    if (pci_request_regions(dev, KBUILD_MODNAME))
        return -EBUSY;
    
    // 3. iomap BAR0
    void __iomem *bar0 = pci_iomap(dev, 0, 0);
    if (!bar0) {
        pci_release_regions(dev);
        return -ENOMEM;
    }
    
    // 4. 读写 MMIO
    iowrite32(0x12345678, bar0 + 0x04);
    
    return 0;
}

static void my_remove(struct pci_dev *dev)
{
    // remove 必须按序逆清理
    if (bar0)
        pci_iounmap(dev, bar0);
    pci_release_regions(dev);
    pci_disable_device(dev);
}
```

---

## 9. 内核 BAR 分配流程（setup-bus.c）

```c
// drivers/pci/setup-bus.c
// 关键函数：pci_assign_unassigned_bridge_resources()
void pci_assign_unassigned_bridge_resources(struct pci_host_bridge *bridge)
{
    // 1. 先让 EP 报告 BAR 大小（写全 1，读回）
    list_for_each_entry(dev, &bus->devices, bus_list) {
        if (dev->hdr_type == PCI_HEADER_TYPE_NORMAL)
            pci_read_bases(dev, 6);  // EP 的 6 个 BAR
    }
    
    // 2. 按大小排序（大的先分配）
    // 3. 从系统内存顶端向下分配
    // 4. 写回配置空间 BAR
}
```

---

## 10. 实验

### 实验 1：观察 QEMU virt 中 NVMe 的 BAR

```bash
# 启动 QEMU + NVMe 设备
/opt/qemu/bin/qemu-system-x86_64 \
  -M virt \
  -m 512M \
  -nographic \
  -enable-kvm \
  -drive file=/tmp/nvme.qcow2,if=none,id=disk0 \
  -device nvme,drive=disk0,serial=1234 \
  2>&1 &

# 在 QEMU 内：
# lspci -nn -vv
# 00:01.0 NVMe: 看到 BAR0 = 64-bit MEM，申请了 4KB
```

### 实验 2：BAR 解析练习

```bash
# 假设 BAR 返回 0xFDF_0004（MEM，32-bit，不预取）
# 计算申请的大小：
python3 -c "print(hex(~0x04 & ~0xF))"
# 预期：0xFDF_0000 → 大小 0xFDF_0000 bytes ≈ 248 MB
```

### 实验 3：pci_iomap 追踪

```bash
# 在内核源码中追踪 pci_iomap
grep -n "pci_iomap\|pci_iounmap" \
  /usr/src/linux-headers-6.17.0-20-generic/drivers/pci/pci.c | head -20
```

---

## 11. bring-up 关联

| Bring-up 阶段 | 本节技能用途 |
|--------------|-------------|
| 芯片 bring-up | BAR 分配是第一个要验证的——确认系统分配的地址 = EP 看到地址 |
| 地址映射错误 | ioremap 失败 → EP 寄存器无法访问 → DMA/中断都不工作 |
| 地址重叠 | BAR 和系统内存重叠 → 数据写入 EP 实际写到了内存，反之亦然 |

---

## 12. 常见错误

### 错误 1：ioremap 成功，但读写全 F

**现象**：`ioremap()` 返回非 NULL，但 `readl()` 返回 0xFFFFFFFF。

**原因**：EP 未使能，或 BAR 未正确解码。

**排查**：
```bash
# 检查 Command Register 的 Memory Space Enable 位
setpci -s XX:XX.X COMMAND.w
# bit 1 (Memory Space) 应该为 1
```

### 错误 2：64-bit BAR 申请失败

**现象**：BAR0 是 64-bit，但 BAR1 被另一个功能占用。

**原因**：64-bit BAR 必须连续（BAR0+BAR1），但 BAR1 已被其他 BAR 使用。

**解决**：内核在 `pci_read_bases()` 时会检测到这种冲突，不申请 64-bit。

### 错误 3：没有 wmb() 导致寄存器写入顺序错误

**现象**：先写控制寄存器（启动 DMA），再写数据地址，但 EP 先收到数据地址（被 Posted），再收到控制寄存器（启动命令晚到）。

**解决**：
```c
// 正确顺序
iowrite32(data_addr, bar + DMA_ADDR_REG);
wmb();  // 保证 addr 在 cmd 之前被 EP 看到
iowrite32(START_CMD, bar + DMA_CTRL_REG);
```

---

## 13. 自测问题

1. BAR[bit 0] = 0 表示什么？BAR[bit 3] = 1 表示什么？两者可以同时为 1 吗？
2. 写入 0xFFFFFFFF 读回 0xFDF_0004，这个 BAR 申请了多少空间？用 Python 计算。
3. pci_request_regions() 和 ioremap() 的区别是什么？为什么要先 request 再 ioremap？
4. 为什么 MMIO 读写要用 ioread32/iowrite32，不能直接解引用指针？
5. 在真实 bring-up 中，如果 BAR 分配后 EP 读到的地址和系统分配的不一致，第一个怀疑什么？（提示：ATU）

---

## 14. 下一步

前 4 节完成，进入 **S2_01 — PCI 子系统初始化与驱动注册模型**。
