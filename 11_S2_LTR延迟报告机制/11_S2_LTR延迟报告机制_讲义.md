# S2_06 — LTR 延迟报告机制

## 本节目标

1. 理解 LTR（Latency Tolerance Reporting）的作用：EP 告诉 RC 自己能容忍多长的延迟
2. 掌握 LTR 扩展能力结构（PCIe Base Spec r4.0, Sec 6.18）
3. 读懂内核 `pci_configure_ltr()` + `pci_save/restore_ltr_state()` 源码
4. 理解 `ltr_path` 传播机制（RC → Switch → EP 路径验证）
5. 能在 QEMU 中验证 LTR 开关及延迟阈值写入

---

## 1. 原理：LTR 是什么

### 1.1 问题引入

PCIe 是一个异步系统——发送方发出 TLP，接收方在任意时刻回复。然而系统有功耗管理：链路可以进入 L0s（低功耗待机）、L1（更深睡眠）甚至 L2（掉电）。

**问题**：如果 RC 在等待一个Posted MRd的Completion时，把链路切进了 L1，而 EP 还在处理请求——RC 要等多久才能保证不出错？

**答案**：不知道。因为 RC 不知道 EP 处理请求需要多长时间。

**LTR 的本质**：EP 主动告诉 RC："我处理完一个 Non-Posted 请求最多需要 X 纳秒（Max Snoop Latency），我一个 Posted 请求最长在 Y 纳秒内发完（Max No-Snoop Latency）。你根据这个信息决定要不要让我进低功耗。"

### 1.2 规范定义

LTR 是 PCIe 扩展能力（Extended Capability），Capability ID = 0x18（PCI_EXT_CAP_ID_LTR）。

结构如下（偏移从扩展能力指针位置计算）：

| 偏移 | 字段 | 位宽 | 说明 |
|:---|:---|:---|:---|
| 0x0 | Max_Snoop_Latency | 10bit + 3bit scale | RC→EP 方向（带 cache snoop），CPU 读内存 |
| 0x4 | Max_NoSnoop_Latency | 10bit + 3bit scale | RC→EP 方向（不带 cache snoop），DMA 访问 |

**Scale 编码**（3bit，0-6 有意义）：

| Scale 值 | 乘数 |
|:---|:---|
| 0 | ×1 ns |
| 1 | ×32 ns |
| 2 | ×1024 ns (=1 μs) |
| 3 | ×32768 ns (=32 μs) |
| 4 | ×1048576 ns (=1 ms) |
| 5 | ×33554432 ns (=33.5 ms) |
| 6 | Reserved（设备不应使用）|

**示例**：
- `Max_Snoop_Latency = 0x140, scale=1` → 0x140 × 32 = **8192 ns = 8 μs**
- `Max_NoSnoop_Latency = 0x200, scale=2` → 0x200 × 1024 = **1,048,576 ns = 1 ms**

### 1.3 Max Snoop vs Max No-Snoop 的区别

| 场景 | 延迟类型 | 说明 |
|:---|:---|:---|
| CPU 经 RC 读 EP 内存（带 CPU cache snoop）| Max Snoop | CPU 需要知道 L1/L2 cache 是否命中，延迟更高 |
| DMA 引擎读 EP 内存（无 CPU cache 介入）| Max No-Snoop | DMA 直接访问，延迟更可预测 |

**实战理解**：固件/驱动编程中，写 MRd/MWr 到 EP 时，如果使用 CPU 直接搬运数据（copy_to_user / ioremap），关注 **Max Snoop**；如果使用 DMA 引擎，关注 **Max No-Snoop**。

---

## 2. 为什么这样设计

### 2.1 LTR 是 PCIe ASPM（Active State Power Management）的决策依据

ASPM 有 L0s 和 L1 两个低功耗状态：

- **L0s**：链路进入低功耗 standby，收到 LF（Training Sequence）立即唤醒，延迟 < 1 μs
- **L1**：更深度的 power-gating，唤醒延迟 1-10 μs 量级

**没有 LTR 时**：ASPM 策略保守（因为不知道下游设备能不能容忍延迟），倾向于不让链路进入 L1 或频繁切换 L0s，导致**功耗偏高**。

**有 LTR 时**：ASPM 可以根据 EP 报告的延迟容忍度做精确决策——如果所有 EP 的 LTR 都是 1 ms 以上，链路可以大胆进 L1，节省数瓦功耗。

### 2.2 LTR 路径验证（ltr_path）

这是最容易忽略的设计要点：**LTR 不是 RC 单方面决定的**。

PCIe r4.0 Sec 6.18 明确规定：

> Software must not enable LTR in an Endpoint unless the Root Complex and all intermediate Switches indicate support for LTR.

翻译成内核的逻辑（`pci_configure_ltr()`）：
```
if (这是 EP) {
    if (上游 RC 的 ltr_path == 0)  → 不启用 LTR
    if (中间 Switch 的 ltr_path == 0) → 不启用 LTR
    // 即使 EP 自身支持 LTR，如果路径上任何一个节点不支持，LTR 也不启用
}
```

**为什么这样设计**：LTR 延迟是端到端语义。如果 RC 支持 LTR 但中间 Switch 不支持，RC 根据 LTR 值进了 L1，但 Switch 无法在要求时间内完成唤醒 → 丢 TLPs → 系统崩溃。

**`ltr_path` 标志传播机制**：
1. RC（Root Port）被启用 LTR → `ltr_path = 1`
2. EP 启用 LTR 之前，检查 `pci_upstream_bridge()->ltr_path`
3. 如果 bridge 的 `ltr_path == 1`，EP 才能启用 → `ltr_path = 1`
4. 整条路径（RC → Switch → EP）都验证通过后，LTR 才真正生效

### 2.3 LTR 与 L1.2 Substates 的关系

L1 PM Substates 是比 L1 更深度的功耗状态（可达数十毫瓦级省电）。LTR 中的 `L1.2_THRESHOLD` 是一个专门的寄存器字段（PCIe r4.0, Sec 5.5.4），定义如下：

> 端口在最近一次 LTR 值 ≥ L1.2_THRESHOLD 时，才允许进入 L1.2。

这个阈值让 ASPM 策略可以在 LTR 很长（设备能等很久）时主动进 L1.2，而 LTR 很短时不值得进。

---

## 3. 内核源码解析

### 3.1 LTR 相关文件

```
drivers/pci/pcie/aspm.c         ← 核心实现：pci_configure_ltr / pci_save/restore_ltr_state
include/linux/pci.h:438          ← struct pci_dev::ltr_path 字段
include/uapi/linux/pci_regs.h    ← PCI_EXT_CAP_ID_LTR / PCI_LTR_MAX_SNOOP_LAT 等常量
drivers/pci/pci.c               ← pci_save_ltr_state / pci_restore_ltr_state 调用点
```

### 3.2 `pci_configure_ltr()` — 完整源码（aspm.c:1208-1253）

```c
void pci_configure_ltr(struct pci_dev *pdev)
{
    struct pci_host_bridge *host = pci_find_host_bridge(pdev->bus);
    struct pci_dev *bridge;
    u32 cap, ctl;

    // ① 检查是不是 PCIe 设备，非 PCIe 设备没有 LTR
    if (!pci_is_pcie(pdev))
        return;

    // ② 读取 Device Capability 2，看设备是否支持 LTR（DEVCAP2[11] = LTR）
    pcie_capability_read_dword(pdev, PCI_EXP_DEVCAP2, &cap);
    if (!(cap & PCI_EXP_DEVCAP2_LTR))          // DEVCAP2_LTR = 0x800
        return;                                 // 设备不支持 LTR，直接返回

    // ③ 读取 Device Control 2，看 LTR 是否已经启用（DEVCTL2[10] = LTR_EN）
    pcie_capability_read_dword(pdev, PCI_EXP_DEVCTL2, &ctl);
    if (ctl & PCI_EXP_DEVCTL2_LTR_EN) {         // DEVCTL2_LTR_EN = 0x400
        /*
         * LTR 已经启用。此时分两种情况：
         * A. 本设备是 Root Port → ltr_path = 1（自己是起点）
         * B. 本设备是 EP → 检查上游 bridge 是否也支持 LTR
         */
        if (pci_pcie_type(pdev) == PCI_EXP_TYPE_ROOT_PORT) {
            pdev->ltr_path = 1;                  // RC 是路径起点
            return;
        }
        // EP：检查上游 bridge 的 ltr_path 标志
        bridge = pci_upstream_bridge(pdev);
        if (bridge && bridge->ltr_path)
            pdev->ltr_path = 1;                 // 路径验证通过
        return;
    }

    // ④ 如果 Devctl2[LTR_EN] == 0，检查 platform 是否允许（native_ltr）
    if (!host->native_ltr)                       // ACPI / PCIe native 热插拔控制
        return;

    // ⑤ 正式启用 LTR
    if (pci_pcie_type(pdev) == PCI_EXP_TYPE_ROOT_PORT) {
        // RC：首先在 RC 本地启用 LTR
        pcie_capability_set_word(pdev, PCI_EXP_DEVCTL2,
                                 PCI_EXP_DEVCTL2_LTR_EN);
        pdev->ltr_path = 1;                     // RC 启用后，标记路径有效
        return;
    }

    // ⑥ EP：确保上游 bridge 已启用 LTR 后，再启用 EP
    bridge = pci_upstream_bridge(pdev);
    if (bridge && bridge->ltr_path) {
        pci_bridge_reconfigure_ltr(pdev);       // 热添加场景：重新配置 bridge LTR
        pcie_capability_set_word(pdev, PCI_EXP_DEVCTL2,
                                 PCI_EXP_DEVCTL2_LTR_EN);
        pdev->ltr_path = 1;                     // EP 也标记 ltr_path = 1
    }
}
```

### 3.3 `pci_bridge_reconfigure_ltr()` — 热添加场景（aspm.c:1193-1206）

```c
static void pci_bridge_reconfigure_ltr(struct pci_dev *dev)
{
    struct pci_dev *bridge = pci_upstream_bridge(dev);
    u32 ctl;

    // 在给 EP 启用 LTR 之前，先确保上游 bridge 的 LTR 也启用了
    bridge = pci_upstream_bridge(dev);
    if (bridge && bridge->ltr_path) {
        pcie_capability_read_dword(bridge, PCI_EXP_DEVCTL2, &ctl);
        if (!(ctl & PCI_EXP_DEVCTL2_LTR_EN)) {     // bridge 之前未启用
            pci_dbg(bridge, "re-enabling LTR\n");
            pcie_capability_set_word(bridge, PCI_EXP_DEVCTL2,
                                     PCI_EXP_DEVCTL2_LTR_EN);
        }
    }
}
```

**热添加场景说明**：设备是后来加到总线上的（不是 boot 时枚举的）。此时 bridge 可能之前进入了 L1 并禁用了 LTR（为了省电）。新设备加入后，需要先把 bridge 的 LTR 重新启用，才能给自己的 LTR 写延迟值。

### 3.4 `pci_save_ltr_state()` 和 `pci_restore_ltr_state()`（aspm.c:30-67）

```c
// 保存 LTR 状态（suspend / deep sleep 前调用）
void pci_save_ltr_state(struct pci_dev *dev)
{
    int ltr;
    struct pci_cap_saved_state *save_state;
    u32 *cap;

    if (!pci_is_pcie(dev))
        return;

    // 找到 LTR 扩展能力在配置空间中的偏移
    ltr = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_LTR);  // 0x18
    if (!ltr)
        return;

    // 找到 suspend buffer（系统在 suspend 前分配的临时存储）
    save_state = pci_find_saved_ext_cap(dev, PCI_EXT_CAP_ID_LTR);
    if (!save_state) {
        pci_err(dev, "no suspend buffer for LTR; ASPM issues possible after resume\n");
        return;
    }

    /* 有些设备只支持 DWORD 访问 LTR 寄存器（不支持 byte/word）*/
    cap = &save_state->cap.data[0];
    pci_read_config_dword(dev, ltr + PCI_LTR_MAX_SNOOP_LAT, cap);
    // 读取格式：Bits[31:16] = Max_NoSnoop, Bits[15:0] = Max_Snoop
}

// 恢复 LTR 状态（resume 时调用）
void pci_restore_ltr_state(struct pci_dev *dev)
{
    struct pci_cap_saved_state *save_state;
    int ltr;
    u32 *cap;

    save_state = pci_find_saved_ext_cap(dev, PCI_EXT_CAP_ID_LTR);
    if (!save_state)
        return;

    ltr = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_LTR);
    if (!ltr)
        return;

    cap = &save_state->cap.data[0];
    /* 同样是 DWORD 访问 */
    pci_write_config_dword(dev, ltr + PCI_LTR_MAX_SNOOP_LAT, *cap);
}
```

### 3.5 LTR 在 PCI 配置空间中的位置

```
偏移（ECAM）:
  [Bus:Dev:Func] + 0x100 + LTR_Offset(0x18扩展能力内偏移)

LTR Extended Capability Structure:
  +0x00: PCI_EXT_CAP_HEADER（ID=0x18, Version, Next Cap Ptr）
  +0x04: Max_Snoop_Latency  [15:0]  = Value (10bit)
                          [18:16] = Scale (3bit)
                          [31:19] = Reserved
  +0x08: Max_NoSnoop_Latency [15:0]  = Value (10bit)
                          [18:16] = Scale (3bit)
                          [31:19] = Reserved
```

**注意**：`Max_NoSnoop_Latency` 的 Value 字段在 bits[27:16]（不是[15:0]），两个字段在同一个 DWORD 里被打包存储。

---

## 4. 常见错误案例

### 错误1：没有检查 `ltr_path` 就读取 LTR 延迟值

**错误代码**：
```c
// 错误：直接读取 EP 的 LTR 配置空间
int ltr = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_LTR);
pci_read_config_dword(dev, ltr + PCI_LTR_MAX_SNOOP_LAT, &val);

// 但如果 ltr_path == 0（路径验证未通过），LTR 值对 RC 没有意义
```

**正确做法**：
```c
if (!dev->ltr_path) {
    pci_dbg(dev, "LTR not enabled on this path\n");
    return -ENODEV;
}
// 然后再读取 LTR 值
```

**调试方法**：
```bash
# 检查设备 LTR 是否启用
cat /sys/bus/pci/devices/0000:00:02.0/ltr_path    # 如果有的话
# 或者通过 setpci
setpci -s 00:02.0 CAP_EXP+0x28.W   # 读 DEVCTL2，看 bit 10（LTR_EN）
```

### 错误2：在不支持 LTR 的设备上写延迟值

某些虚拟设备（如 QEMU virt）或旧设备没有 LTR 扩展能力（`pci_find_ext_capability` 返回 0），直接写会静默失败或写到了别的能力空间。

**调试方法**：
```c
int ltr = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_LTR);
if (!ltr) {
    pci_err(dev, "LTR not supported\n");
    return -EOPNOTSUPP;
}
// 然后再操作
```

### 错误3：LTR 延迟值 scale 理解错误

例如将 scale=1（×32ns）误认为 ×1 ns，导致计算出的延迟远小于实际值，ASPM 策略过于激进。

---

## 5. 决策树

```
收到一个 PCIe 设备（dev）
         │
         ▼
  ┌─ pci_is_pcie(dev) ─┐
  │  否 → LTR 不适用   │
  │  是 → 继续         │
  └────────────────────┘
         │
         ▼
  ┌─ DEVCAP2[11] LTR ──┐
  │  不支持 → 直接返回 │
  │  支持 → 继续       │
  └────────────────────┘
         │
         ▼
  ┌─ DEVCTL2[10] LTR_EN ──┐
  │  已启用 → 记录ltr_path │
  │  未启用 → 检查native_ltr│
  └─────────────────────────┘
         │
         ▼
  ┌─ native_ltr (host) ───┐
  │  平台禁止 → 不启用    │
  │  允许 → 启用 LTR     │
  └───────────────────────┘
         │
         ▼
  ┌─ 设备类型 ────────────┐
  │ Root Port → ltr_path=1│
  │ EP + bridge.ltr_path=1│
  │   → 启用 + ltr_path=1 │
  │ EP + bridge.ltr_path=0│
  │   → 不启用（路径不支持│
  └───────────────────────┘
```

---

## 6. Bring-up 关联

### 真实芯片调试中何时用到 LTR

**场景1：回片 bring-up 早期**
芯片回片后，首先验证 PCIe 枚举是否正常。在 `lspci -vv` 中检查 `Latency Tolerance Reporting` 是否出现在 `DevCtl2` 中。如果看不到，检查：
- EP 的 LTR 扩展能力是否正确实现（配置空间 0x18 ID 是否可读）
- DEVCAP2[11] 是否为 1（硬件实现检查）

**场景2：ASPM 调试**
在 bring-up 阶段关闭 ASPM（`pcie_aspm=off`），等链路稳定后再逐步开启。如果开启 ASPM 后出现链路 flap（链路反复断开重连），很可能与 LTR 配置有关——某些 EP 报告了过短的 LTR 值，导致 RC 频繁唤醒链路。

**场景3：功耗测试**
芯片 bring-up 完成后，会测试空闲功耗。LTR 配置正确的系统，L1.2 进入率明显更高（链路空闲时大胆进深度省电）。如果功耗不达标，第一个排查点就是 LTR 是否正确传播到整条路径。

---

## 7. 自测问题

1. **LTR 路径验证**：如果一个 Switch 不支持 LTR（`ltr_path=0`），连接在它下面的 EP 还能启用 LTR 吗？为什么？

2. **延迟值计算**：某 NVMe SSD 报告 `Max_NoSnoop_Latency = 0x100, Scale = 2`（即 ×1024 ns）。这个值是多少毫秒？它对 ASPM 策略意味着什么？

3. **热添加场景**：`pci_bridge_reconfigure_ltr()` 在热添加时才被调用。这个函数在 LTR 启用流程中解决了什么问题？

4. **suspend/resume**：系统 suspend 时 LTR 状态被保存，resume 时恢复。如果 save/restore 失败（如没有分配 suspend buffer），会有什么后果？

5. **Max Snoop vs Max No-Snoop**：固件驱动中使用 `copy_to_user()`（CPU 直接读 EP BAR 空间）和使用 DMA 引擎（`dmaengine_prep_dma_memcpy`）访问 EP 内存，分别应该关注 LTR 的哪个字段？
