# S2_06 — LTR 延迟报告机制：自验清单

## 自验清单

完成全部实验后，在下方勾选：

### 实验完成情况

- [ ] **实验1**：使用 setpci 读取了设备的 DEVCAP2 和 DEVCTL2，正确解析了 bit11（支持）和 bit10（启用）
- [ ] **实验2**：`ltr_decode` 用户态工具编译成功，`sudo ./ltr_decode` 运行正常并输出了格式化报告
- [ ] **实验3**：`make modules` 成功，`insmod ltr_kernel_check.ko` 加载成功，`dmesg | grep LTR` 有输出
- [ ] **实验4**（可选）：QEMU 虚拟机环境验证完成
- [ ] **实验5**：能用 lspci -t 区分 RC 和 EP，理解了 ltr_path=1 和 ltr_path=0 的区别
- [ ] **实验6**：Python 延迟值计算验证正确（0x200 × 1024 ns = 512 μs = 0.512 ms）

### 概念理解

- [ ] 能用自己的话解释 LTR 的作用（EP 告诉 RC 自己能容忍多长的链路延迟）
- [ ] 能说出 Max_Snoop 和 Max_NoSnoop 的区别（CPU cache snoop vs DMA direct access）
- [ ] 能解释 Scale 编码：scale=0 是 ×1 ns，scale=4 是 ×1 ms
- [ ] 能解释 `ltr_path` 的作用：整条 RC→Switch→EP 路径验证都通过后 LTR 才生效
- [ ] 能解释热添加场景（`pci_bridge_reconfigure_ltr`）中为什么要先 re-enable bridge 的 LTR

### 源码理解

- [ ] 能找到 `pci_configure_ltr()` 在 `aspm.c` 的位置（aspm.c:1208）
- [ ] 能说出 `pci_find_ext_capability(dev, PCI_EXT_CAP_ID_LTR)` 的作用（找 LTR 扩展能力的配置空间偏移）
- [ ] 能说出 `DEVCAP2[11]` 和 `DEVCTL2[10]` 的区别（支持 vs 启用）
- [ ] 能说出 `ltr_path` 标志在 struct pci_dev 中的位置（include/linux/pci.h:438）
- [ ] 能解释 `native_ltr` 的作用（ACPI/platform 决定是否允许使用 native LTR）

---

## AI 评估区

完成自验清单后，向 AI 回答以下问题：

### 评估问题 1：LTR 路径验证

**场景**：一个 PCIe Switch 不支持 LTR（`ltr_path=0`），但连接在它下面的 EP 本身支持 LTR。

**问题**：这个 EP 的 LTR 能否被启用？如果不能，内核源码中哪一行会阻止它？

<details>
<summary>答案提示</summary>

EP 调用 `pci_configure_ltr()` 时，检查 `bridge = pci_upstream_bridge(pdev)`，如果 `bridge->ltr_path == 0`，EP 的 LTR 不会被启用。看 aspm.c 中 `if (bridge && bridge->ltr_path)` 判断处。

</details>

---

### 评估问题 2：延迟值编码

**场景**：某设备报告 `Max_Snoop_Latency = 0x3E8, Scale = 3`（即 0x3E8 × 32768 ns）。

**问题**：这个延迟是多少毫秒？这个值偏大还是偏小？它对 ASPM 策略意味着什么？

<details>
<summary>计算</summary>

0x3E8 = 1000（十进制）
1000 × 32768 ns = 32,768,000 ns = 32.768 ms

这个值非常大，说明设备能容忍 32ms 的延迟才响应。这意味着链路空闲时可以大胆进 L1.2 省电。

</details>

---

### 评估问题 3：suspend/resume

**场景**：系统 suspend 前 `pci_save_ltr_state()` 成功保存了 LTR 寄存器，但 resume 时 `pci_restore_ltr_state()` 找不到 suspend buffer（`save_state == NULL`）。

**问题**：这种情况下 LTR 状态会怎样？`aspm.c:55` 行打印的 `pci_err` 消息是什么？

<details>
<summary>答案</summary>

打印 `pci_err(dev, "no suspend buffer for LTR; ASPM issues possible after resume\n");`

后果：resume 后 LTR 寄存器没有被正确恢复，可能还是之前残留的值，ASPM 策略可能做出错误判断。

</details>

---

### 评估问题 4：LTR 和 L1.2 的关系

**场景**：某 EP 报告 `Max_NoSnoop_Latency = 1000 ns (scale=0)`，非常小。

**问题**：这个 EP 在判断是否进入 L1.2 时，会不会进得比较频繁还是不怎么进？为什么？

<details>
<summary>答案提示</summary>

L1.2 进入条件：`LTR ≥ L1.2_THRESHOLD`。L1.2_THRESHOLD 由 RC 配置。如果 LTR 只有 1μs，说明设备只能容忍 1μs 的延迟，链路必须很快唤醒——这意味着 ASPM 策略不会让链路进太深的 L1（尤其是 L1.2），因为唤醒代价太高。

</details>

---

## 完成标志

全部评估问题回答正确 + 实验完成 → 本节 **S2_06 LTR 延迟报告机制** ✅ 通过。

下一步：**S2_07 — SR-IOV 与 VF 管理**
