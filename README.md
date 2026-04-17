# PCIe Linux 主机驱动 — 学习路径

> 方案 B · 30 节 · 顺序索引
> 内核源码是教材，PCIe 规范是参考

---

## 学习顺序（从第 1 课开始，依次学完）

| # | 章节 | 主题 | 内核源码 | 状态 |
|---|------|------|---------|------|
| 1 | S0_01 | 实验环境搭建 | — | ✅ |
| 2 | S1_01 | PCIe 总线基础与 TLP | Spec | ✅ |
| 3 | S1_02 | 链路层与 Flow Control + DOE | Spec | ✅ |
| 4 | S1_03 | PCI/PCIe 配置空间（ECAM）| `pci.c` | ✅ |
| 5 | S1_04 | BAR 地址映射与解码 | `setup-bus.c` | 🔨 下一课 |
| 6 | S2_01 | PCI 子系统初始化 + 内核编程入门 | `bus.c:pci_scan_root_bus` | ⬜ |
| 7 | S2_02 | pci_driver 注册模型 | `pci-driver.c` | ⬜ |
| 8 | S2_03 | 资源管理（sysfs / /proc/iomem）| `pci.c`/`resource.c` | ⬜ |
| 9 | S2_04 | MSI/MSI-X 中断路由 | `msi/msi.c` | ⬜ |
| 10 | S2_05 | 电源管理 + Fundamental Reset + NPEM | `pci.c:pci_pm_*` | ⬜ |
| 11 | S2_06 | LTR 延迟报告机制 | `pci.c:ltr_*` | ⬜ |
| 12 | S2_07 | SR-IOV 与 VF 管理 | `iov.c` | ⬜ |
| 13 | S3_01 | RC 驱动框架与注册 | `pci-driver.c` | ⬜ |
| 14 | S3_02 | BAR 空间与 MMIO 读写 | `pci_iomap`/`ioremap` | ⬜ |
| 15 | S3_03 | Outbound DMA 引擎 | `dmaengine` | ⬜ |
| 16 | S3_04 | MSI-X 中断处理实战 | `msi/irq.c` | ⬜ |
| 17 | S3_05 | Inbound DMA 与 ATU | `pci.c:atu_*` | ⬜ |
| 18 | S3_06 | EP 驱动视角 | `endpoint/*` | ⬜ |
| 19 | S3_07 | Endpoint 框架与 CONFIGFS | `endpoint/configfs.c` | ⬜ |
| 20 | S4_01 | Switch 内部架构 | `switch/*` | ⬜ |
| 21 | S4_02 | Switch 路由与 VC 仲裁 + PCIE_BUS_* | `pcie/aer.c` | ⬜ |
| 22 | S4_03 | 多主机 Switch / 拓扑调试 | `pci/proc.c` | ⬜ |
| 23 | S5_01 | VirtIO PCI 原理 | `virtio/virtio_pci.c` | ⬜ |
| 24 | S5_02 | Inbound DMA 深度（P2PDMA + SMMU）| `p2pdma.c`/`iommu/*` | ⬜ |
| 25 | S5_03 | ATS / PRI / PASID / TPH | `ats.c`/`pci.c` | ⬜ |
| 26 | S5_04 | DOE 协议深度 + P2PDMA 高级 | `doe.c` | ⬜ |
| 27 | S6_01 | AER 错误处理（CXL_AER / EDR / DPC）| `aer/*` | ⬜ |
| 28 | S6_02 | Bring-up 实战流程 | — | ⬜ |
| 29 | S6_03 | 内核调试工具链（ftrace / perf / eBPF + PTM）| `pcie/ptm.c` | ⬜ |
| 30 | Final | 综合项目：QEMU 实现简易 PCIe RC 驱动 | — | ⬜ |

**✅ = 已交付 · 🔨 = 当前 · ⬜ = 待交付**

---

## 当前进度

**第 1-5 课已交付（S0_01 ~ S1_04）**

下一课：**第 6 课 · S2_01 — PCI 子系统初始化 + 内核编程入门**

---

## 每课交付物（5 个文件）

```
S{n}_{XX}_讲义.md   ← 原理 + 设计原因 + 错误案例 + 决策树
S{n}_{XX}_代码.md   ← 完整 C 源码（2-3 个 .c 文件）
S{n}_{XX}_Makefile  ← 编译模板
S{n}_{XX}_实验.md   ← 实验步骤 + 验收标准
S{n}_{XX}_自验.md   ← 自验勾选 + AI 评估问题
```

---

## 学习节奏

每批交付 1-2 课，学完做自验，通过后再继续下一课。

遇到问题 → 看决策树 → dmesg / log 排查 → 还解决不了再问。

---

## 仓库

GitHub: https://github.com/kly-410/pcie-linux-driver-learning
本地: `/home/kly/Desktop/filedisk/code/PCIe_driver_learning/`
