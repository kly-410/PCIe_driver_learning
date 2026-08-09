---
title: L10_MMIO映射机制
type: note
lifecycle_status: active
created: 2026-07-31
modified: '2026-08-07'
summary: flowchart LR
validation: unverified
tags:
- pcie/linux-driver
- type/note
series: Linux PCIe 驱动深度讲义
volume: 卷二·配置空间与资源管理
number: L10
next: L11_PCIe排序模型
ai:
  training: true
  rag_priority: high
---

# L10：MMIO 映射机制

## 0. 框架定位

```mermaid
flowchart LR
    L08["L08 BAR"] --> L09["L09 资源树"] --> L10_here["★ L10 MMIO 映射"]
    L10_here --> L12["L12 DMA 基础"]
```



---

## 1. 问题驱动

> 🔍 **你遇到了一个问题：**
>
> 你在 GPU 驱动的 probe 函数里写 `writel(0x1, bar0_base + 0x100);`
然后用 `readl` 读回来——等了 5 秒，返回的是 `0`。
MMIO 映射到 CPU 地址空间到底做了什么？
ioremap 和普通的 malloc 有什么本质区别？
>
> **带着这个问题学完本节，你就知道答案了。**

---

## 2. 前置认知



**前置**：L08 BAR → L09 资源冲突检测后，资源属于你了。现在需要把物理地址映射到内核虚拟地址空间——CPU 才能读写设备寄存器。

**核心问题**：`ioremap` 在 CPU 页表层面做了什么？PAT 和 MTRR 怎么影响映射的内存类型？WC/WB/UC 的性能差异有多大？

## 3. 核心原理

### 2.1 ioremap：物理地址 → 虚拟地址

```c
void __iomem *ioremap(phys_addr_t phys_addr, unsigned long size);
```

内部分三步：
1. 在 vmalloc 区域分配虚拟地址空间
2. 设置 CPU 页表（PGD→P4D→PUD→PMD→PTE），每条 PTE 指向物理页，**内存类型默认设为 UC-**
3. 刷新 TLB

`devm_ioremap_resource()` 最方便：内部做 `request_mem_region` + `ioremap`，并在驱动 remove 时自动 unmapped + released。

### 2.2 内存类型（PAT + MTRR）

x86 通过 **PAT**（Page Attribute Table）和 **MTRR**（Memory Type Range Register）共同决定内存类型。最终类型取两者的 **最严格** 交集。

| 类型 | 含义 | 速度 | 副作用 | PCIe 用途 |
|------|------|------|--------|-----------|
| UC | Uncacheable | 最慢 | 无 | 寄存器 MMIO（ioremap 默认） |
| UC- | Uncacheable Minus | 慢 | 无 | 同上 |
| WC | Write-Combining | 写快 | 弱序 | Framebuffer 大块 DMA |
| WB | Write-Back | 最快 | Cache | 普通 RAM |
| WP | Write-Protected | — | — | 少见 |

**重点**：如果 MTRR 设 UC 而 PAT 设 WC，MTRR 赢。最终是 UC。所以即使你用 `ioremap_wc()`，如果 BIOS 把这段物理空间的 MTRR 设为 UC，结果还是 UC。验证：`cat /proc/mtrr` + `cat /sys/kernel/debug/x86/pat_memtype_list`。

**ioremap_wc()** → 设置 PAT PTE entry 为 WC。比 `ioremap()`（UC）的写入吞吐高 10x+。

### 2.3 WC 的陷阱

WC 内存是**完全弱序**的——CPU 可以把多个写合并、重排。写入 WC 后立即读：读不一定看到最新写值（可能还在 CPU store buffer 里没 flush）。正确用法：

```c
writel(val, wc_reg);   // 写入 WC-mapped 寄存器
wmb();                  // ★ sfence：刷 store buffer
val = readl(uc_reg);    // 从 UC-mapped 寄存器读确认
```

## 4. 内核源码带读

> x86_64 v7.0。ioremap + PAT/MTRR 冲突解析的关键路径。

### 3.1 `devm_ioremap_resource()` —— 一步到位的 MMIO 映射

源码：`lib/devres.c:170`。三步合一：`devm_request_mem_region()` + `devm_ioremap()` + 失败自动回滚。PCI 驱动支持 `pcim_iomap_table` 的 managed API。

### 3.2 `ioremap_wc()` —— 设置 PAT 为 Write-Combining

源码：`arch/x86/mm/ioremap.c:403`。内部调用 `__ioremap_caller(phys, size, _PAGE_CACHE_MODE_WC)` → 设置 PTE PAT bit 编码 WC。在此之前检查 MTRR：如果 MTRR 设 UC → 回退到 `ioremap_uc()`。

### 3.3 MTRR 优先于 PAT 的实现

`mtrr_type_lookup()` 查询 BIOS 配置的 MTRR 范围。如果某物理地址范围的 MTRR 类型 = UC，即使 PAT 试图设 WC → 最终内存类型取最严格的 = UC。`cat /proc/mtrr` 验证。

**⚠ x86 GPU 专属**：framebuffer 用 `ioremap_wc()` 映射，寄存器用 `ioremap()`（UC）。MTRR 如果设错了（UC 而非 WC），GPU 的 framebuffer 写入吞吐从 20GB/s 掉到 200MB/s——10x 差异。排查：`cat /proc/mtrr | grep -i vga`。
## 5. x86 关联

x86 的 PAT 有 8 个条目，由 `IA32_PAT` MSR（0x277）配置，默认映射 PA0=WB, PA1=WC, PA2=UC-, PA3=UC, PA4=WB, PA5=WC, PA6=UC-, PA7=UC。MTRR 优先级高于 PAT——MTRR 如果设为 UC，PAT 无法覆盖。

**多 socket 验证**：每个 socket 有自己的 MTRR 寄存器（per-core）。BIOS 启动时配置 MTRR，同一物理地址在不同 socket 上不应有不同的 MTRR 值——否则 cross-socket DMA 的行为不一致。

## 6. GPU 关联

GPU framebuffer（BAR0 256MB）用 `ioremap_wc()` → WC。GPU 寄存器（BAR1 16MB）用 `ioremap()` → UC。

**验证陷阱**：如果你用 `ioremap_wc()` 映射了寄存器空间——对寄存器的写可能被重排。例如：写 command FIFO → 写 trigger 寄存器 → 先收到 trigger 再收到 command → GPU 执行空 FIFO。

CUDA 的 mmap 场景（L20）：用户态 `mmap` GPU BAR → `remap_pfn_range` 设置 PTE，需要匹配同样的 PAT 属性。`pgprot_writecombine(PAGE_SHARED)` → WC。

## 7. 思考题

1. `ioremap` 返回的虚拟地址在 x86_64 的哪个区域？和 vmalloc 区域有什么关系？
2. PAT 和 MTRR 冲突时——PAT=WC、MTRR=UC——最终内存类型是？为什么这样设计？
3. WC 内存的写入什么时候到达设备？如果不加 `wmb()`，写入可能在哪里？

## 6b. 参考答案

**Q1**：vmalloc 区域。`ioremap` 内部用 `get_vm_area_caller()` 在 vmalloc 地址范围分配虚拟空间，然后设置 PTE。和 `module_alloc()`（L01）是同一个地址区域的不同用途——都在 vmalloc 预留区内。

**Q2**：UC。MTRR 优先于 PAT。设计理念：MTRR 由 BIOS 配置，反映主板设计者知道的物理限制（如这段地址是 ROM，不能写）。PAT 由 OS 配置，OS 可能不知道硬件限制。保守策略：信任 MTRR 比 OS 更了解物理限制。实现：`ioremap_wc()` 源码中 `pat_enabled()` 会检查 `mtrr_type_lookup()` 结果——如果 MTRR 是 UC → 拒绝 WC → 回退 UC。

**Q3**：在 CPU store buffer 中。WC 内存的写先进入 store buffer，按 burst 组合后一次发出。`wmb()`（`sfence`）强制刷 store buffer 到总线→设备可见。不加 `wmb()`：写可能无限期停留在 store buffer 中，直到 store buffer 满了被迫 flush 或者被后续 `mfence` 带出。设备永远看不到写入。

## 8. 渐进式代码构建

```c
// 在 probe 中 ioremap BAR0
struct resource *res = &dev->resource[0];
void __iomem *bar0;

ret = pci_request_region(dev, 0, "pci_demo");
if (!ret) {
    bar0 = pci_ioremap_bar(dev, 0);  // 自动选 ioremap 还是 ioremap_wc
    if (bar0) {
        u32 id = ioread32(bar0);  // 读 BAR0 的第一个 dword
        pr_info("L10: BAR0 first dword=0x%08x\n", id);
        iounmap(bar0);
    }
    pci_release_region(dev, 0);
}
```
