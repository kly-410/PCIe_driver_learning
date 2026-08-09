---
title: L15_IOMMU深度
type: note
lifecycle_status: active
created: 2026-07-30
modified: '2026-08-07'
summary: flowchart LR
validation: unverified
tags:
- pcie/linux-driver
- type/note
series: Linux PCIe 驱动深度讲义
volume: 卷三·DMA 与数据传输
number: L15
next: L16_MSI-X深度
ai:
  training: true
  rag_priority: high
---

# L15：IOMMU 深度

## 0. 框架定位

```mermaid
flowchart LR
    L10["L10 MMIO 映射"] --> L12["L12 DMA 基础"]
    L12 --> L13["L13 DMA API"]
    L13 --> L14["L14 DMA 方向与一致性"]
    L14 --> L15_here["★ L15 IOMMU 深度"]
    L15_here --> L16["L16 MSI-X 深度"]
    L15_here --> L20["L20 mmap + ioctl"]

    subgraph IOMMU层
        L15_here --> §1["§1 VT-d 硬件原理"]
        L15_here --> §3["§3 源码:intel_iommu_init"]
        L15_here --> §5["§5 GPU:VFIO Passthrough"]
    end

    style L15_here fill:#ff6,stroke:#333
```

L15 是 DMA 子卷的核心枢纽：前几讲（L12-L14）讲的是如何通过 DMA-API 发起传输——但那些 API 最终都经过 IOMMU 翻译。本讲深入 IOMMU/VT-d 的实现：从硬件页表到内核初始化，从 `iommu_map` 到 GPU VFIO 直通。

---



---

## 1. 问题驱动

> 🔍 **你遇到了一个问题：**
>
> 你把一张 GPU 通过 VFIO 直通给虚拟机，
VM 里的 GPU 驱动加载后发起 DMA，
数据写到了宿主机的关键内存区，宿主机直接崩溃。
IOMMU/VT-d 怎么保护系统不被 errant DMA 破坏？
DMA Remapping 到底翻译了几层地址？
>
> **带着这个问题学完本节，你就知道答案了。**

---

## 2. 前置认知



| 前置知识 | 来源 | 必要性 |
|---------|------|--------|
| DMA 基础概念（TLP 寻址、地址转换） | L12 | 缺一不可 |
| DMA-API（dma_alloc_coherent / dma_map_single） | L13 | 缺一不可 |
| DMA 一致性与 cache 同步 | L14 | 理解 IOMMU 的 coherency 控制 |
| MMIO 映射与 PAT/MTRR | L10 | 理解 IOMMU 页表和 CPU 页表的区别 |
| PCIe ACS（Access Control Services） | L05 | IOMMU 组依赖于 ACS 隔离 |

**前置假设**：x86_64 服务器（Intel Xeon / AMD EPYC）。无设备树、无 GIC、无 dma-ranges。内核源码路径 `~/work/code/linux-source/` (v7.0.0)。

---

## 3. 核心原理

### 2.1 为什么需要 IOMMU？

在 L12 中我们看到：设备 DMA 直接访问物理内存地址。带来的三个安全问题：

1. **Buggy 设备损坏内存**：一个有 bug 的驱动让设备写入错误地址 → 破坏其他进程数据
2. **DMA 攻击**：恶意设备可以读取整个系统内存
3. **虚拟化问题**：VM 不能直接给 Guest 设备物理地址——Guest 的"物理地址"是虚拟的

IOMMU（VT-d 在 Intel 上的实现）在设备和内存之间插入一个**硬件页表**：

```
  ┌──────────┐     DMA TLP (I/O Virtual Address)     ┌──────────┐
  │ PCIe EP  │ ──────────────────────────────────→   │ VT-d     │
  │ (GPU/NIC)│  (I/O VA: 0x1000_0000)                │ IOMMU    │
  └──────────┘                                        └────┬─────┘
                                                            │
                                              Root Entry → Context Entry → Page Table
                                                            │
                                                            ↓
                                                    ┌──────────────┐
                                                    │ Physical Mem │
                                                    │ (PA: 0x7f00) │
                                                    └──────────────┘
```

**关键设计思想**：

- 设备看到的地址（I/O Virtual Address, IOVA）和 CPU 看到的物理地址（PA）不同
- IOMMU 翻译是**透明**的——设备不知道它在跟 IOMMU 通信
- 每个设备可以有自己的地址空间——设备 A 的 IOVA=0x1000 映射到 PA=0x7000，设备 B 的 IOVA=0x1000 映射到 PA=0x8000

### 2.2 VT-d 硬件架构

Intel VT-d 的核心结构是**三级页表**（非通用 CPU 页表，是专用格式）：

```
  DMAR（DMA Remapping Reporting Structure）
  ┌─────────────────────────────────────────┐
  │ ACPI DMAR Table                         │
  │   ├── DRHD (DMA Remap Hardware Desc)    │ ← IOMMU硬件寄存器基址
  │   │   ├── Device Scope (哪些设备归它管)  │
  │   │   └── INTR (中断重映射)              │
  │   ├── RMRR (Reserved Memory Region)     │ ← BIOS预留区域（如USB键盘唤醒）
  │   ├── ATSR (Address Translation Svc)    │ ← ATS 支持声明
  │   └── SATC (Shared ATS Control)         │ ← 共享 ATS
  └─────────────────────────────────────────┘
```

VT-d 地址翻译的硬件结构（L15 先聚焦 DMA Remapping，中断重映射在 §2.4）：

```
  Root Table (4KB, 256 entries)
  ┌──────┐
  │ [0]  │──→ Context Table (4KB, 256 entries)
  │ [1]  │    ┌──────┐
  │ ...  │    │ [0]  │──→ Second-Level Page Table (SL page table)
  │ [255]│    │ [1]  │     ┌─────────┐
  └──────┘    │ ...  │     │ SL PTE  │──→ 4KB physical page
              │ [255]│     │ SL PTE  │
              └──────┘     │ ...     │
                           └─────────┘
```

- **Root Entry**：BDF（Bus:Device.Function）的 bus 号索引（`iommu_context_addr` 函数内部做 `root_entry_lctp()`）
- **Context Entry**：由 `bus:devfn` 索引，包含 Domain ID、页表指针、翻译类型（多级/设备 IOTLB）
- **Second-Level Page Table**：SL page table 结构，页大小 4KB/2MB/1GB，物理地址到物理地址的直接映射

#### 2.2.1 DMA Remapping（DMA 重映射）

当 PCIe 设备发起 DMA 读取（MRd）或写入（MWr）时，TLP 的目标地址是 IOMMU 的 IOVA 空间。VT-d 硬件在 TLP 到达 RC（Root Complex）之前拦截：

```
  PCIe EP DMA TLP:  地址=0xFFFF_9000_0000
                           │
                           ▼
                    VT-d 硬件拦截
                           │
                    ┌──────┴──────┐
                    │ Root Entry  │ (bus索引)
                    └──────┬──────┘
                           │
                    ┌──────┴──────┐
                    │ Context Entry│ (devfn索引)
                    │ Domain ID    │
                    │ SL PTPtr     │
                    └──────┬──────┘
                           │
                    ┌──────┴──────┐
                    │ SL Page     │
                    │ Table Walk  │
                    └──────┬──────┘
                           │
                    TLP 重写: 地址=0x0000_0000_7F00_0000
                           │
                           ▼
                    Root Complex → Memory Controller
```

- 如果不命中任何 Context Entry（未映射）→ IOMMU 生成**DMA 重映射故障**（DMAR Fault），记录在 `dmesg` 中
- 翻译发生在 TLP 地址域，不修改 TLP 的其他部分

#### 2.2.2 Interrupt Remapping（中断重映射）

中断重映射是 VT-d 的第二个核心功能：重写 MSI/MSI-X 写入中的地址和数据。

当设备写 MSI 地址 (`0xFEE0_0000`) 时，VT-d 硬件拦截并查 Interrupt Remapping Table：

```
  设备写 MSI Addr=0xFEE0_0n00 Data=XXXX
                      │
                      ▼
            ┌─────────────────┐
            │ Interrupt       │
            │ Remapping Table │  (IR Table, 1MB max, 65536 entries)
            ├─────────────────┤
            │ [0] IRTE        │
            │ [1] IRTE        │──→ remap entry: {vector, dest, trigger mode}
            │ ...             │
            └─────────────────┘
                      │
                      ▼
            APIC: 最终向量号+目标 CPU → 注入中断
```

- IRTE（Interrupt Remapping Table Entry）包含：向量号、目标 CPU（xAPIC/x2APIC ID）、触发模式（edge/level）、是否 masked
- 每个 MSI 向量通过 `intel_setup_irq_remapping` (`drivers/iommu/intel/irq_remapping.c:520`) 分配一个 IRTE 条目
- **隔离保证**：VM 中的设备不能伪造中断向量——IRTE 只允许预设的向量

> 📌 协议对照：DMA Remapping 对应 PCIe TLP 的 **Address 域修改**；Interrupt Remapping 对应 **MSI/MWSI TLP 的地址+数据域重写**（PCIe Base Spec §6.1）

### 2.3 IOMMU API 设计

`include/linux/iommu.h` 定义了一组通用 API，各厂商（Intel/AMD/ARM SMMU）通过 `struct iommu_ops` 提供实现：

| API | 用途 | Intel 实现 |
|-----|------|-----------|
| `iommu_map(domain, iova, paddr, size, prot, gfp)` | 映射 IOVA → PA | `intel_iommu_map` → SL page table |
| `iommu_unmap(domain, iova, size)` | 解除映射 | `intel_iommu_unmap` |
| `iommu_attach_device(domain, dev)` | 设备绑定到 domain | `intel_iommu_attach_device` |
| `iommu_detach_device(domain, dev)` | 解绑设备 | `intel_iommu_detach_device` |
| `iommu_domain_alloc(bus_type)` | 创建 domain | `intel_iommu_domain_alloc_paging_flags` |
| `iommu_domain_free(domain)` | 释放 domain | `intel_iommu_domain_free` |

```c
// include/linux/iommu.h:918-919
extern int iommu_map(struct iommu_domain *domain, unsigned long iova,
                     phys_addr_t paddr, size_t size, int prot, gfp_t gfp);

// include/linux/iommu.h:924-925
extern size_t iommu_unmap(struct iommu_domain *domain, unsigned long iova,
                          size_t size);

// include/linux/iommu.h:911-912
extern int iommu_attach_device(struct iommu_domain *domain,
                               struct device *dev);
```

**语义**：
- `iommu_attach_device`：将设备绑定到 domain。设备的所有 DMA 请求都经过该 domain 的页表翻译。domain 可以是 **identity**（1:1 映射，绕过 IOMMU）、**paging**（页表翻译）、或 **blocking**（拒绝所有 DMA）
- `iommu_map`：在 domain 的页表中建立 IOVA → PA 映射
- `iommu_unmap`：解除映射，同时可选的 IOTLB 无效化

### 2.4 IOMMU Domain 类型

```c
// include/linux/iommu.h:180
#define __IOMMU_DOMAIN_PAGING    (1U << 0)  /* 支持 iommu_map/unmap */
#define __IOMMU_DOMAIN_DMA_API   (1U << 1)  /* DMA-API 使用的 domain */
#define __IOMMU_DOMAIN_IDENTITY  (1U << 2)  /* 1:1 映射 */
#define __IOMMU_DOMAIN_DMA_FQ   (1U << 3)  /* 带 flush queue 的 DMA domain */
#define __IOMMU_DOMAIN_BLOCKED   (1U << 4)  /* 阻塞所有 DMA */
```

Intel IOMMU 的三种 domain 特化（`drivers/iommu/intel/iommu.c:3882-3929`）：

| Domain | ops | 行为 | 用途 |
|--------|-----|------|------|
| `identity_domain` | `identity_domain_attach_dev` | 不翻译 → 设备直接访问物理地址 | `iommu=pt` 启动参数 |
| `intel_fs_paging_domain` | `intel_iommu_attach_device` | First-Stage 页表翻译 | 默认 DMA 域（SVA 场景） |
| `intel_ss_paging_domain` | `intel_iommu_attach_device` | Second-Stage 页表翻译 | 虚拟化/VFIO 场景 |
| `blocking_domain` | `device_block_translation` | 拒绝所有 DMA | 初始状态/失败回退 |

### 2.5 IOMMU 组（Group）

IOMMU 组是 IOMMU 的最小隔离粒度：同一组内的设备共享同一个 I/O 地址空间，无法互相隔离。

```c
// drivers/iommu/iommu.c:1603-1665
struct iommu_group *pci_device_group(struct device *dev)
```

分组逻辑（`drivers/iommu/iommu.c:1603`）：

```
  PCIe Topology            IOMMU Group
  ┌──────────────────┐    ┌───────────┐
  │ Root Port        │    │ Group #0  │── Function 0
  │  (ACS enabled)   │    │           │── Function 1
  │   ├─ EP Func 0 ──┤    └───────────┘
  │   ├─ EP Func 1 ──┤    ┌───────────┐
  │   └─ EP Func 2 ──┤    │ Group #1  │── Downstream EP (no ACS)
  └──────────────────┘    └───────────┘
```

- 如果 ACS 在该 PCIe 拓扑链路上启用 → 每个 endpoint 单独分组
- 如果无 ACS → 整条链路共享一个组 → 无法单独对某设备做 passthrough

---

## 4. 内核源码带读

### 3.1 `intel_iommu_init()` —— VT-d 初始化全过程

源码：`drivers/iommu/intel/iommu.c:2552`

```
  调用链：
  start_kernel()
    → x86_init.iommu.iommu_init = intel_iommu_init()  // dmar.c:939 设置
    → rest_init() → kernel_init() → do_basic_setup()
       → acpi_scan_init() → acpi_init() → dmar_init()
          → dmar_parse_dmar_table() → parse DMAR ACPI table
          → x86_init.iommu.iommu_init()  ← 最终回调 intel_iommu_init()
```

**主流程**：

```c
// == 步骤 1: 初始化 DMAR 表结构 ==
int __init intel_iommu_init(void)                          // iommu.c:2552
{
    int ret = -ENODEV;
    struct dmar_drhd_unit *drhd;
    struct intel_iommu *iommu;

    force_on = (!intel_iommu_tboot_noforce && tboot_force_iommu()) ||  // :2562
               platform_optin_force_iommu();

    down_write(&dmar_global_lock);
    if (dmar_table_init()) {                                 // :2566
        if (force_on)
            panic("tboot: Failed to initialize DMAR table\n");
        goto out_free_dmar;                                  // == 异常路径 ==
    }

    if (dmar_dev_scope_init() < 0) {                         // :2572
        if (force_on)
            panic("tboot: Failed to initialize DMAR device scope\n");
        goto out_free_dmar;                                  // == 异常路径 ==
    }
    up_write(&dmar_global_lock);

    dmar_register_bus_notifier();                            // :2584 — 注册 PCI bus notifier
                                                             //   新设备枚举时自动触发 IOMMU probe

    // == 步骤 2: 跳过或初始化 ==
    down_write(&dmar_global_lock);

    if (!no_iommu)
        intel_iommu_debugfs_init();                          // :2589 — debugfs 接口

    if (no_iommu || dmar_disabled) {                         // :2591
        // == 异常路径: IOMMU 被禁用 ==
        // 清理 PMRs (Protected Memory Regions) - tboot 遗留
        if (intel_iommu_tboot_noforce) {
            for_each_iommu(iommu, drhd)
                iommu_disable_protect_mem_regions(iommu);    // :2601-2602
        }
        intel_disable_iommus();                              // :2610 — 关闭 IOMMU 硬件
        goto out_free_dmar;
    }

    // 打印 RMRR/ATSR/SATC 信息
    if (list_empty(&dmar_rmrr_units))                        // :2614
        pr_info("No RMRR found\n");
    // ... ATSR, SATC 类似

    init_no_remapping_devices();                             // :2623

    // == 核心: 初始化 DMAR 硬件 ==
    ret = init_dmars();                                      // :2625
    if (ret) {
        if (force_on)
            panic("tboot: Failed to initialize DMARs\n");
        pr_err("Initialization failed\n");
        goto out_free_dmar;                                  // == 异常路径 ==
    }
    up_write(&dmar_global_lock);

    init_iommu_pm_ops();                                     // :2634 — 电源管理 ops

    // == 步骤 3: 注册 IOMMU 设备 ==
    down_read(&dmar_global_lock);
    for_each_active_iommu(iommu, drhd) {                     // :2637
        // 虚拟化场景禁用 flush queue batching
        if (cap_caching_mode(iommu->cap) &&
            !first_level_by_default(iommu)) {                // :2645
            pr_info_once("IOMMU batching disallowed...\n");
            iommu_set_dma_strict();
        }
        // /sys/kernel/iommu_groups/ 下创建设备
        iommu_device_sysfs_add(&iommu->iommu, NULL,          // :2650
                           intel_iommu_groups, "%s", iommu->name);
        up_read(&dmar_global_lock);
        iommu_device_register(&iommu->iommu,                 // :2659
                          &intel_iommu_ops, NULL);
        down_read(&dmar_global_lock);
        iommu_pmu_register(iommu);                           // :2662
    }

    if (probe_acpi_namespace_devices())                      // :2665
        pr_warn("ACPI name space devices didn't probe correctly\n");

    // == 步骤 4: 启用 DMA 重映射 ==
    for_each_iommu(iommu, drhd) {                            // :2669
        if (!drhd->ignored && !translation_pre_enabled(iommu))
            iommu_enable_translation(iommu);                 // :2671
        iommu_disable_protect_mem_regions(iommu);            // :2673
    }
    up_read(&dmar_global_lock);

    pr_info("Intel(R) Virtualization Technology for Directed I/O\n");  // :2677
    intel_iommu_enabled = 1;                                 // :2679
    return 0;

out_free_dmar:                                               // :2683 — 资源清理
    intel_iommu_free_dmars();
    up_write(&dmar_global_lock);
    return ret;
}
```

**异常场景汇总**：

| 条件 | 行为 | dmesg 日志 | 排查方法 |
|------|------|-----------|---------|
| `dmar_table_init()` 失败 + `force_on` | `panic()` | "tboot: Failed to initialize DMAR table" | `acpidump > acpi.dat; acpixtract acpi.dat; iasl -d DMAR.dat` 检查 DMAR 表完整性 |
| `dmar_dev_scope_init()` 失败 + `force_on` | `panic()` | "tboot: Failed to initialize DMAR device scope" | 检查 ACPI 设备枚举，`ls /sys/firmware/acpi/tables/DMAR` 是否存在 |
| `no_iommu` 或 `dmar_disabled` | 清理后跳转 | 无（默认不打印） | 检查启动参数是否包含 `intel_iommu=off` 或 `iommu=off` |
| `init_dmars()` 失败 + `force_on` | `panic()` | "tboot: Failed to initialize DMARs" | 检查 BIOS IOMMU 是否开启；`dmesg \| grep DMAR` 看详细错误 |
| `init_dmars()` 失败 + 非 force_on | 打印错误后 g_o | "Initialization failed" | `dmesg \| grep -i "DMAR\|IOMMU"` |
| kdump 中 translation_pre_enabled 但无法 copy | 回退 disable | "Failed to copy translation tables" | kdump 时加 `intel_iommu=on` 或 `disable_cpu_apicid=` |
| 非 kdump 中 translation_pre_enabled | IOMMU disable | "Translation was enabled for ... but we are not in kdump mode" | BIOS 没关闭 IOMMU？检查 BIOS VT-d 设置 |

**⚠ 对验证的影响**：

1. `init_dmars()` 一旦失败 → 整个 IOMMU 子系统不可用 → 确认服务器 `dmesg | grep -i "DMAR\|IOMMU"` 成功打印 `"Intel(R) Virtualization Technology for Directed I/O"`
2. `force_on` 通常由 TXT/tboot 触发——仅安全启动场景关心
3. kdump 场景的 copy_translation_tables 失败会 disable IOMMU → kdump capture kernel 中所有 DMA 走 bypass → 测试验证需要标注这个差异

### 3.2 `init_dmars()` —— IOMMU 硬件初始化

源码：`drivers/iommu/intel/iommu.c:1611`

```c
static int __init init_dmars(void)                           // iommu.c:1611
{
    // == 阶段 1: 遍历每个 IOMMU 硬件单元 ==
    for_each_iommu(iommu, drhd) {                            // :1617
        if (drhd->ignored) {
            iommu_disable_translation(iommu);                // :1619 — 忽略的 IOMMU
            continue;
        }

        // 计算系统最大 PASID
        if (pasid_supported(iommu)) {
            u32 temp = 2 << ecap_pss(iommu->ecap);
            intel_pasid_max_id = min_t(u32, temp,            // :1629-1632
                                  intel_pasid_max_id);
        }

        intel_iommu_init_qi(iommu);                          // :1635 — 初始化 Queued Invalidation

        // kdump 场景：如果之前已启用翻译，尝试复制旧页表
        if (translation_pre_enabled(iommu) && !is_kdump_kernel()) {  // :1638
            iommu_disable_translation(iommu);
            clear_translation_pre_enabled(iommu);
            pr_warn("Translation was enabled for %s but we are not in kdump mode\n",
                iommu->name);
        }

        // 分配 Root Entry Table（4KB）
        ret = iommu_alloc_root_entry(iommu);                 // :1650
        if (ret) goto free_iommu;

        // kdump: 尝试拷贝旧 translation tables
        if (translation_pre_enabled(iommu)) {
            ret = copy_translation_tables(iommu);            // :1657
            if (ret) {
                pr_err("Failed to copy translation tables...\n");
                iommu_disable_translation(iommu);            // :1670 — 失败回退
                clear_translation_pre_enabled(iommu);
            }
        }
        intel_svm_check(iommu);                              // :1678 — SVM (Shared Virtual Memory) 检查
    }

    // == 阶段 2: 设置 Root Entry 并刷新缓存 ==
    for_each_active_iommu(iommu, drhd) {                     // :1686
        iommu_flush_write_buffer(iommu);                     // :1687
        iommu_set_root_entry(iommu);                         // :1688 — WRITE Root Entry → 硬件开始使用
    }
    // ... 初始化域和地址空间
    return ret;
}
```

**⚠ 关键点**：

- `intel_iommu_init_qi()` 必须在写 Root Entry 之前调用——Queued Invalidation 需要先准备好
- `copy_translation_tables()` 只在 kdump 中成功——非 kdump 中 `translation_pre_enabled` 会直接 disable IOMMU
- `iommu_alloc_root_entry()` 分配物理连续 4KB（VTD_PAGE_SIZE），**不能跨页**

### 3.3 `domain_context_mapping()` —— 设备上下文映射

源码：`drivers/iommu/intel/iommu.c:1209`

当设备 attach 到 domain 时（`dmar_domain_attach_device` → `domain_context_mapping`），函数在 IOMMU 的根表 + 上下文表中建立设备条目：

```c
// == 入口：domain_context_mapping (wrap 函数) ==
static int
domain_context_mapping(struct dmar_domain *domain, struct device *dev)  // :1209
{
    struct device_domain_info *info = dev_iommu_priv_get(dev);
    struct intel_iommu *iommu = info->iommu;
    u8 bus = info->bus, devfn = info->devfn;
    int ret;

    if (!dev_is_pci(dev))                                    // :1216
        return domain_context_mapping_one(domain, iommu, bus, devfn);
    // 非 PCI 设备 → 直接映射一个 context

    // == 核心：遍历 DMA alias ==
    // PCIe 设备可能存在 DMA alias（例如 PCIe-to-PCI bridge 后的设备）
    ret = pci_for_each_dma_alias(to_pci_dev(dev),            // :1219
                 domain_context_mapping_cb, domain);
    if (ret) return ret;

    iommu_enable_pci_ats(info);                              // :1224 — 如果支持 ATS，启用

    return 0;
}

// == 回调：每个 alias 调一次 ==
static int domain_context_mapping_cb(struct pci_dev *pdev,   // :1197
                 u16 alias, void *opaque)
{
    struct device_domain_info *info = dev_iommu_priv_get(&pdev->dev);
    struct intel_iommu *iommu = info->iommu;
    struct dmar_domain *domain = opaque;

    return domain_context_mapping_one(domain, iommu,          // :1204
                  PCI_BUS_NUM(alias), alias & 0xff);
}

// == 核心：填写一个 Context Entry ==
static int domain_context_mapping_one(struct dmar_domain *domain,  // :1142
                  struct intel_iommu *iommu,
                  u8 bus, u8 devfn)
{
    struct device_domain_info *info =
            domain_lookup_dev_info(domain, iommu, bus, devfn);    // :1146
    u16 did = domain_id_iommu(domain, iommu);                     // :1148 — Domain ID
    int translation = CONTEXT_TT_MULTI_LEVEL;                     // :1149 — 默认多级页表
    struct pt_iommu_vtdss_hw_info pt_info;                        // :1150 — 页表硬件信息
    struct context_entry *context;
    int ret;

    if (WARN_ON(!intel_domain_is_ss_paging(domain)))              // :1154
        return -EINVAL;                                     // == 异常路径 ==
    // ⚠ 只有 Second-Stage paging domain 才能走到这里
    // First-Stage domain（SVA）走 intel_pasid_setup_first_level

    pt_iommu_vtdss_hw_info(&domain->sspt, &pt_info);              // :1157 — 获取页表物理地址+位宽

    spin_lock(&iommu->lock);
    ret = -ENOMEM;
    context = iommu_context_addr(iommu, bus, devfn, 1);           // :1164
    // 在 Root Table 中查找/创建 Context Entry
    // 参数 1=true 表示"不存在则分配"
    if (!context)
        goto out_unlock;                                    // == 异常路径 ==

    ret = 0;
    if (context_present(context) && !context_copied(iommu, bus, devfn))  // :1169
        goto out_unlock;                                    // == 跳过: 已存在 ==

    // 清除旧的 context
    copied_context_tear_down(iommu, context, bus, devfn);         // :1172
    context_clear_entry(context);                                 // :1173
    context_set_domain_id(context, did);                          // :1174 — 写 Context Entry

    // 选择翻译类型
    if (info && info->ats_supported)                               // :1176
        translation = CONTEXT_TT_DEV_IOTLB;                       // :1177 — 设备支持 IOTLB
                                                                  //  允许设备缓存地址翻译

    // 设置页表根指针和地址宽度
    context_set_address_root(context, pt_info.ssptptr);           // :1181 — SL page table base
    context_set_address_width(context, pt_info.aw);               // :1182 — 地址宽度
    context_set_translation_type(context, translation);            // :1183
    context_set_fault_enable(context);                            // :1184
    context_set_present(context);                                 // :1185

    // 如果 IOMMU 寄存器不支持硬件 coherency → 手动刷新 cache
    if (!ecap_coherent(iommu->ecap))
        clflush_cache_range(context, sizeof(*context));           // :1187
    // ⚠ clflush: x86 上刷 cache line，确保 IOMMU 硬件读到最新值

    context_present_cache_flush(iommu, did, bus, devfn);          // :1188 — IOTLB 无效化

    ret = 0;
out_unlock:
    spin_unlock(&iommu->lock);
    return ret;
}
```

**异常路径 + goto 目标汇总**：

| goto 标签 | 触发条件 | 返回值 | 对系统影响 |
|-----------|---------|--------|-----------|
| `out_unlock` | `iommu_context_addr` 分配失败 | `-ENOMEM` | 设备 DMA 全被 blocking domain 拒绝 |
| `out_unlock` | context 已存在且 present | 0 | 跳过，复用已有 mapping |
| `WARN_ON` | domain 不是 SS paging | `-EINVAL` | domain 类型不匹配 → 驱动错误 |
| `out_unlock` | context 分配 + 填写成功 | 0 | 正常返回 |

**⚠ 对验证的影响**：

1. ATS（Address Translation Services）：如果设备 + 平台支持 ATS，`translation = CONTEXT_TT_DEV_IOTLB` → 设备可以缓存 IOMMU 翻译结果，减少延迟。验证时注意 ATS 是否正确的表现——如果有 ATS 但 IOMMU 没配好，设备读到 stale 翻译
2. `ecap_coherent()`：x86 上大部分 IOMMU 硬件支持 snoop coherency（`ecap_coherent` 返回 true）→ 不需要 `clflush`。如果不支持 → 每次写 Context Entry 后必须 `clflush`，这是性能热点
3. `domain_context_mapping` 通过 `pci_for_each_dma_alias` 遍历所有 alias——PCIe-to-PCI bridge、多功能设备的 alias 都包含在内，确保整个 alias 链都正确映射

### 3.4 `iommu_map()` —— Generic IOMMU 映射

源码：`drivers/iommu/iommu.c:2680`

```c
int iommu_map(struct iommu_domain *domain, unsigned long iova,
          phys_addr_t paddr, size_t size, int prot, gfp_t gfp)
{
    int ret;

    ret = iommu_map_nosync(domain, iova, paddr, size, prot, gfp);  // :2685
    if (ret)
        return ret;

    ret = iommu_sync_map(domain, iova, size);                 // :2689
    if (ret)
        iommu_unmap(domain, iova, size);                      // :2691 — 回滚

    return ret;
}
```

分两步：`iommu_map_nosync` → 实际页表修改；`iommu_sync_map` → IOTLB 同步。两步分离允许批量映射后一次性同步，减少 IOTLB 刷新次数。

`iommu_map_nosync` (`iommu.c:2654`)：
- 使用通用页表（`struct pt_iommu`）：`pt->ops->map_range()` 走 `iommupt` 页表框架
- 在 Intel 上最终走到 `driver/iommu/intel/iommu.c` 的 `map_pages` 回调
- 支持 4KB/2MB/1GB 大页，自动合并

---

## 5. x86 关联

### 4.1 x86 IOMMU 启动顺序

x86 上 IOMMU 初始化是一个延迟初始化：

```
  ACPI 表解析阶段 (acpi_init):
    dmar_init() → dmar_parse_dmar_table()
      → 读取 DMAR ACPI 表，解析 DRHD/RMRR/ATSR/SATC
      → 设置 x86_init.iommu.iommu_init = intel_iommu_init

  设备枚举阶段 (do_basic_setup):
    → pci_subsys_init()
    → intel_iommu_init()  ← 此时 PCI 枚举已完成
```

**关键时序依赖**：PCI 枚举必须在 IOMMU 初始化**之前**完成。因为 IOMMU 初始化时需要知道已枚举的设备信息（`device_domain_info` 依赖于 PCI device 结构），而设备枚举时也需要知道 IOMMU 是否可用（ACS 策略依赖于 `iommu_detected = 1`）。

### 4.2 缓存一致性控制（x86 专属）

```c
// drivers/iommu/intel/iommu.c:1186-1187
if (!ecap_coherent(iommu->ecap))
    clflush_cache_range(context, sizeof(*context));
```

- `ecap_coherent`：IOMMU Extended Capability 寄存器的 Coherent 位。如果硬件支持 snoop → IOMMU 自动观察 CPU cache 变更 → 不需要 `clflush`
- 如果硬件不支持（旧平台 VT-d 1.0）→ 每次修改 Context Entry 后必须 `clflush`，否则 IOMMU 可能看到 stale 的页表数据
- 这是 x86 平台特有的问题——ARM SMMU 的设备 side 一致性模型不同

### 4.3 SFENCE 与 IOTLB 无效化

```c
// drivers/iommu/intel/iommu.c:1188
context_present_cache_flush(iommu, did, bus, devfn);
```

内部包含：
1. `iommu->flush.flush_context()` → 通过 Queued Invalidation 发送 IOTLB 无效化描述符
2. 发送前需要 `wmb()`（`sfence`）确保 IOMMU 页表的写已经对硬件可见
3. 等待完成（poll 状态寄存器或 wait descriptor 完成）

### 4.4 x86 平台 IOVA 分配

DMA-API 路径中（`__iommu_dma_map` → `iommu_dma_alloc_iova`），IOVA 分配器处理以下 x86 专属约束：

- **32-bit 设备兼容**：如果设备 DMA mask 是 32-bit（`dma_mask=0xFFFFFFFF`），IOVA 必须在 32-bit 地址空间内
- **PCI 32-bit workaround**：`dev->iommu->pci_32bit_workaround` 控制是否先在 32-bit 低区分配
- **DAC 模式**：`iommu.forcedac=1` 强制设备使用 DAC（Double Address Cycle），IOVA 可以在 64-bit 空间
- **IOMMU 地址宽度**：`DEFAULT_DOMAIN_ADDRESS_WIDTH = 57`（`iommu.c:46`），在 x86 5-level paging 系统上支持高达 57-bit IOVA

---

## 6. GPU 关联

### 5.1 GPU VFIO Passthrough 中的 IOMMU

GPU passthrough（直通）给 VM 时，IOMMU 是关键保障：

```
  Host (Dom0):
  ┌─────────────────────────────────────────┐
  │     VFIO Driver                         │
  │  ┌─────────────────────────────────┐   │
  │  │ IOMMU Domain (Second-Stage)     │   │
  │  │  IOVA 0x0000_0000 → PA 0x7f00  │   │
  │  │  IOVA 0x1000_0000 → PA 0x8a00  │   │
  │  └─────────────────────────────────┘   │
  │         │ attach_dev                    │
  │         ▼                               │
  │  NVIDIA GPU (0000:03:00.0)              │
  └─────────────────────────────────────────┘
```

- VFIO 使用 `iommu_attach_group()` 把 GPU 的 IOMMU 组 attach 到一个 VFIO 管理的 domain
- 然后通过 `iommu_map()` 建立 Guest 物理地址到 Host 物理地址的映射
- GPU 的 BAR 也通过 IOMMU 翻译：Guest BAR 地址 → Host 物理 BAR 地址

**为什么需要 IOMMU 组**：

```c
// drivers/iommu/iommu.c:1603
struct iommu_group *pci_device_group(struct device *dev)
```

如果 GPU 和它的 audio function 在同一个 IOMMU 组（不满足 ACS），VFIO 必须一起 passthrough——不能只透传 GPU 而把 audio 留给 Host：

```
  IOMMU Group #16 (PCI ACS 未隔离):
    ├── 03:00.0 VGA Compatible Controller (NVIDIA GPU)
    └── 03:00.1 Audio Device (NVIDIA HDMI Audio)

  → VFIO 必须同时接管两者 → VM 中需要装 GPU + Audio 驱动
```

**排查方法**：`ls /sys/kernel/iommu_groups/` → 检查 `16/devices/` 目录下的设备列表。

### 5.2 SR-IOV 与 IOMMU 组

SR-IOV 场景中，PF 和 VF 的 IOMMU 组关系：

```
  ┌────────────────────────────────┐
  │ PCIe EP (NVIDIA A100/H100)     │
  │                                │
  │ PF  (00.0)  → Group #17       │
  │   └─ VF 0   → Group #18       │
  │   └─ VF 1   → Group #19       │
  │   └─ VF 2   → Group #20       │
  │   └─ VF 3   → Group #21       │
  └────────────────────────────────┘
```

每个 VF 有自己的独立 IOMMU 组（因为 SR-IOV 保证了 VF 之间的隔离）。这允许把不同 VF 直通给不同 VM。

**⚠ GPU 验证工程师注意**：`iommu=pt`（pass-through 模式）下 IOMMU 不对 DMA 做翻译，但 IOMMU 组结构仍然存在。某些 GPU 驱动的错误会误判 IOMMU 组的一致性——验证时需要确认：
1. `dmesg | grep "IOMMU group"` 检查分组
2. `iommu=pt` 下仍然需要 VFIO 管理组（只是不走 page table walk）
3. 如果 `ACS` 在 upstream switch 上没启用 → 多个 VF 可能分在同一组 → 无法独立 passthrough

### 5.3 GPU 大页映射（IOMMU HugePages）

现代 GPU（NVIDIA H100/AMD MI300）有数十 GB 显存，IOMMU 页表需要高效映射：

```c
// iommu_map_nosync → pt->ops->map_range
// → 自动合并 4KB → 2MB → 1GB 大页映射
```

物理地址连续的大块显存 BAR → IOMMU 可以映射为 1GB 大型页表条目 → 减少 TLB miss。

**验证**：`cat /sys/kernel/debug/iommu/intel/domain_page_table` 查看 domain 的页表级别。

---

## 7. 思考题

1. **[场景题]** 你的 x86 服务器上 `dmesg | grep DMAR` 显示 `"No RMRR found"` 和 `"No ATSR found"`，但 IOMMU 正常工作。RMRR 和 ATSR 分别是什么？为什么没有它们也能工作？

2. **[排查题]** 一个 PCIe 设备的 DMA 写入一直触发 DMAR Fault，`dmesg` 显示 `"DMAR: [DMA Write] Request device [03:00.0] fault addr 0x1000_0000"`。列出至少三种可能的根因和对应的排查步骤。

3. **[设计题]** `domain_context_mapping_one` 中 `ecap_coherent()` 返回 false 时需要 `clflush`。为什么 x86 上有些 IOMMU 硬件不支持 coherency？`clflush` 在这里替代了什么硬件机制？

4. **[源码题]** `intel_iommu_init()` 的 `out_free_dmar` 标签会调用 `intel_iommu_free_dmars()`。阅读 `intel_iommu_free_dmars` 的代码（`iommu.c`），它释放了哪些资源？如果 `init_dmars()` 在第 3 个 IOMMU 上失败，前 2 个已经初始化的 IOMMU 是否正确释放？

5. **[对比题]** `iommu_attach_device(domain, dev)` 和 `iommu_attach_group(domain, group)` 有什么区别？在 GPU VFIO 场景中应该用哪个？为什么？

---

## 6b. 参考答案

**Q1**：RMRR（Reserved Memory Region Reporting）是 BIOS 在 ACPI DMAR 表中描述的预留内存区域——某些设备（如旧的 USB 控制器、Azalia audio）在启动阶段或 S3 唤醒时需要访问物理地址，这些地址必须始终被 IOMMU 映射。没有 RMRR 也能工作：现代设备（包括几乎所有 PCIe 原生设备）不需要 BIOS 预留的 DMA 地址，使用标准的 `dma_alloc_coherent` 分配的地址。ATSR（Address Translation Services Reporting）声明设备是否支持 ATS——没有 ATSR 只是说平台没有报告 ATS 信息，设备可以正常工作但可能无法启用 ATS 优化（缓存 IOMMU 翻译，减少延迟）。所以没有 RMRR 和 ATSR 都是合法的，不报错。

**Q2**：三种可能根因：

| 根因 | 排查步骤 |
|------|---------|
| ① 设备 DMA 地址未通过 IOMMU 映射：驱动使用了错误的物理地址进行 DMA | 检查 `iommu_iova_to_phys(domain, addr)` 确认该地址是否映射；检查驱动代码的 `dma_map_single` 返回值和实际 DMA 地址 |
| ② IOVA 分配在 32-bit 空间但设备的 DMA mask 是 64-bit——IOVA 分配器给了一个 32-bit 地址，但设备写了 64-bit | `cat /sys/bus/pci/devices/0000:03:00.0/dma_mask` 检查 DMA mask；检查 `iommu_dma_forcedac` 设置 |
| ③ 设备 attach 的 IOMMU 组错误：组内另一个设备触发了 DMAR fault | `ls /sys/kernel/iommu_groups/$(readlink /sys/bus/pci/devices/0000:03:00.0/iommu_group)` 查看同组设备 |

进阶排查：启用 IOMMU debugfs → `echo 1 > /sys/kernel/debug/iommu/intel/dmar_translation` → 重现 DMAR fault 看更多上下文。

**Q3**：`ecap_coherent` 位对应 VT-d Extended Capability Register 的 bit-0（`ECAP_C`）。支持 coherency 的 IOMMU 硬件会**snoop** CPU cache——当 IOMMU 读取 page table 时，硬件通过 snoop 协议检查 CPU cache 中是否有 dirty 数据，有则取 cache 中的最新值。老平台（VT-d 1.0）的 IOMMU 不接入 cache coherency 协议（QPI/UPI snoop 域），所以软件必须手动 `clflush` 保证 IOMMU 看到的是最新数据。`clflush` 替代了**硬件 snoop 机制**——它在 x86 指令层面驱逐 cache line，迫使下一次 IOMMU 读从内存获取。

**Q4**：`intel_iommu_free_dmars()`（`iommu.c:2178`）释放的是 ACPI 层面的数据结构——不是 IOMMU 硬件资源。具体释放：

- `dmar_rmrr_units` 链表：RMRR 结构体（`list_del` + `dmar_free_dev_scope` + `kfree`）
- `dmar_atsr_units` 链表：ATSR 结构体（`list_del` + `intel_iommu_free_atsr`）
- `dmar_satc_units` 链表：SATC 结构体（`list_del` + `dmar_free_dev_scope` + `kfree`）

**不释放** root_entry、qi 队列等 IOMMU 硬件资源。

实际上 `init_dmars()`（`iommu.c:1611`）有自己的错误处理 `free_iommu` 标签（`iommu.c:1732`），在该标签内调用 `disable_dmar_iommu` + `free_dmar_iommu` 释放**每个活跃 IOMMU 的硬件资源**（root_entry、irq、qi）。因此如果 `init_dmars()` 在第 3 个 IOMMU 上失败：

1. `init_dmars` 内部的 `goto free_iommu` 会遍历前 2 个已初始化的 IOMMU → 调用 `free_dmar_iommu` 正确释放 root_entry 和 qi
2. 返回 `intel_iommu_init` → `goto out_free_dmar` → `intel_iommu_free_dmars()` 释放 ACPI 层面数据

所以**清理是完整的**——不会漏释放。两个错误路径分别处理硬件和软件层的资源。

**Q5**：区别在于使用粒度：

- `iommu_attach_device`（`iommu.c:2171`）：attach 单个设备。先检查 `iommu_group->devices` 计数 == 1，如果不是则返回 `-EINVAL`——**只能用于独立组的设备**。
- `iommu_attach_group`（`iommu.c` 内部）：attach 整个 IOMMU 组的所有设备。不检查设备计数。

GPU VFIO 场景中必须使用 `iommu_attach_group`——因为 VFIO 总是操作整个 IOMMU 组（`vfio_iommu_type1` 通过 `iommu_attach_group` 绑定）。如果 GPU 在 IOMMU 组中有多个设备（如 GPU + audio function），`iommu_attach_device` 会失败（`list_count_nodes != 1`），而 `iommu_attach_group` 能正确 attach 整组。

---

## 8. 渐进式代码构建

在 L13 的 DMA coherent alloc 基础上，增加 IOMMU 状态查询和调试信息。

```c
// L15: IOMMU 状态查询 + DMA 映射走 IOMMU 路径验证
// 在 L13 代码基础上增加

#include <linux/iommu.h>
#include <linux/dmar.h>

static int pci_demo_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
    struct device *device = &dev->dev;
    struct iommu_domain *domain;
    struct iommu_group *group;
    dma_addr_t dma_handle;
    void *cpu_addr;
    int ret;

    // ---- 原有 L13 代码: DMA coherent alloc ----
    ret = pcim_enable_device(dev);
    if (ret) return ret;

    cpu_addr = dma_alloc_coherent(device, 4096, &dma_handle, GFP_KERNEL);
    if (!cpu_addr)
        return -ENOMEM;

    pr_info("L13: dma_alloc_coherent -> dma_handle=0x%llx cpu=%p\n",
            dma_handle, cpu_addr);

    // ---- L15 新增: IOMMU 状态查询 ----
    group = iommu_group_get(device);
    if (group) {
        domain = iommu_group_default_domain(group);
        if (domain) {
            // 通过 iommu_iova_to_phys 验证 DMA 地址的后端物理页
            phys_addr_t pa = iommu_iova_to_phys(domain, dma_handle);
            pr_info("L15: iommu_iova_to_phys(0x%llx) = 0x%llx\n",
                    dma_handle, pa);

            // 确认 IOMMU 翻译有效: 翻译结果应与实际物理页一致
            if (pa == virt_to_phys(cpu_addr))
                pr_info("L15: IOMMU translation verified OK\n");
            else
                pr_warn("L15: IOMMU translation mismatch! "
                        "pa=0x%llx cpu=0x%llx\n",
                        pa, virt_to_phys(cpu_addr));

            // 打印 domain 信息
            pr_info("L15: domain type=0x%x geometry={%llx-%llx}\n",
                    domain->type,
                    domain->geometry.aperture_start,
                    domain->geometry.aperture_end);
        }
        iommu_group_put(group);
    } else {
        pr_warn("L15: No IOMMU group for this device\n");
    }

    // 检查 CONFIG_INTEL_IOMMU 是否启用
    if (IS_ENABLED(CONFIG_INTEL_IOMMU)) {
        pr_info("L15: Intel IOMMU is compiled in\n");
        if (intel_iommu_enabled)
            pr_info("L15: Intel IOMMU is enabled\n");
    }

#if IS_ENABLED(CONFIG_IOMMU_DMA)
    pr_info("L15: DMA-IOMMU layer active\n");
#endif

    // ---- 原有 L13 代码: 清理 ----
    // (remove 中 dma_free_coherent)
    // ...

    return 0;
}

static void pci_demo_remove(struct pci_dev *dev)
{
    // 和 L13 相同: dma_free_coherent
    // ...
}

static struct pci_driver pci_demo_driver = {
    .name     = "pci_demo_l15",
    .id_table = pci_demo_ids,
    .probe    = pci_demo_probe,
    .remove   = pci_demo_remove,
};

module_pci_driver(pci_demo_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("L15: IOMMU verification with DMA alloc");
```

**编译运行验证**：

```bash
# 编译
make -C ~/work/code/linux-source/ M=$PWD modules

# 加载后查看 dmesg
sudo insmod pci_demo_l15.ko
dmesg | tail -20

# 应看到:
# [  ] L13: dma_alloc_coherent -> dma_handle=0x1000_0000 cpu=...
# [  ] L15: iommu_iova_to_phys(0x1000_0000) = 0x7f00_0000
# [  ] L15: IOMMU translation verified OK
# [  ] L15: domain type=0x3 geometry={0-fffffffffffff}
```

**预期输出说明**：
- `dma_handle` 在 IOMMU 开启时是 IOVA（如 `0x1000_0000`），不等于物理地址
- `iommu_iova_to_phys` 返回真实的物理地址
- `type=0x3` = `__IOMMU_DOMAIN_PAGING | __IOMMU_DOMAIN_DMA_API` — 标准 DMA domain
- `geometry` 显示 domain 的 IOVA 空间范围

---

> **下篇预告**：L16 MSI-X 深度 —— 从 MSI 寄存器编程到 irq_domain 的完整中断路径，以及 IOMMU Interrupt Remapping 的中断隔离。
