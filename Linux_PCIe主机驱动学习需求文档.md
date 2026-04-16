---
created: 2026-04-16
modified: 2026-04-16
type: learn
lifecycle_status: active
summary: Linux PCIe主机驱动AI执行手册。5阶段路径，每阶段含讲义/代码/实验/自验清单。讲义标准：原理+设计原因+错误案例+调试方法。代码标准：可直接编译运行。实验标准：分步+明确预期+验收命令。
tags:
  - type/规划
  - type/learn
  - Linux/PCIe驱动
  - AI执行手册
---

## 零、GitHub 仓库

**仓库**：`https://github.com/kly-410/pcie-linux-driver-learning`

**本地路径**：`/home/kly/Desktop/filedisk/code/`

**每次交付后操作**：
1. 将新交付的文件复制到 `/home/kly/Desktop/filedisk/code/` 下对应目录
2. `git add . && git commit -m "S{n}: {阶段主题} - {本次交付内容}"`
3. `git push`
4. 同时将文件写入 Obsidian vault 路径（`002_Areas/02_技术精进/05_PCIe业务/01_PCIe_linux驱动/`）

**注意**：Obsidian vault 是知识管理，GitHub 是备份和协作。两者同步，但 vault 是工作目录，GitHub 是镜像。

---



# Linux PCIe 主机驱动 — AI 执行手册

> 本文档是 AI coach 的执行规范。AI 阅读本文档后直接输出学习产物，kly 执行实验并反馈，AI 评估后输出下一阶段内容。
>
> **本文档基于阶段1实际交付经验更新（2026-04-16）**

---

## 一、硬件平台

### 主平台：QEMU virt（x86_64）

QEMU 支持完整的 PCIe RC 模拟（ECAM/MSI/AER/ATU），足够完成阶段 1-4 所有实验。

### 辅助平台：x86 PC

学习 PCI Core / EP 驱动 / MSI / AER。不能写 RC 驱动（Intel RC 驱动是内核源码的一部分）。

### 可选：树莓派 4

BCM2711 有 PCIe RC，配合 NVMe 转接卡可写真实 RC 驱动。

---

## 二、AI 执行规范（核心）

### 2.1 交付物（每个阶段固定 9 个）

| # | 类型 | 命名规范 | 说明 |
|---|------|---------|------|
| 1 | 讲义 | `S{n}_0N_标题.md` | Obsidian格式，含frontmatter |
| 2 | 代码 | `S{n}_xxx.c` | 可直接编译运行的完整源码 |
| 3 | 代码 | `Makefile` | 所有模块的编译模板 |
| 4 | 实验手册 | `S{n}_experiments.md` | 分步+预期结果+验收命令 |
| 5 | 自验清单 | `S{n}_checklist.md` | kly自验+AI评估 |

### 2.2 讲义编写标准（强制）

每份讲义必须包含以下四部分，缺一不可：

**A. 原理（是什么）**
- 简洁定义 + 在PCIe驱动中的典型使用场景
- 不要展开历史或无关背景

**B. 为什么这样设计**
- 为什么要这样做，不这样做的后果
- 和其他方案的对比

**C. 常见错误案例**
- 至少 1 个真实错误（会导致bug的错误写法）
- 调试方法（dmesg/panic信息/排查命令）

**D. 决策树**
- 遇到问题时"用哪个、怎么选"的判断流程
- 最好是 `遇到X → 是→A，否→B` 的树状图

**格式要求**：
- 每个章节 800-1500 字
- 代码块用 ` ```c ` 标注语言
- 表格用于对比场景（不是罗列参数）
- 结论前置（先说结论，再说原因）

### 2.3 代码编写标准（强制）

- **完整可运行**：所有 `include` / `extern` / 函数签名完整，不需要补全任何代码
- **工程化注释**：`// 错误示例` / `// 正确写法` / `// 为什么` 标注关键决策点
- **错误处理完整**：`probe` 有完整的 error unwind，`remove` 有对应的 cleanup
- **测试入口明确**：代码开头注释包含编译/加载/测试命令

```c
/*
 * S1_kmod_skeleton.c - 最小可运行内核模块
 * 编译：make
 * 加载：sudo insmod kmod_skeleton.ko
 * 检查：dmesg | tail；lsmod | grep skeleton
 * 卸载：sudo rmmod kmod_skeleton
 */
```

### 2.4 实验手册标准（强制）

每节实验包含：

```markdown
**实验N：标题**
- **目标**：这次实验验证什么
- **步骤**：
  ```bash
  # 分步命令，每步一行
  # 预期输出或状态
  ```
- **预期结果**：明确的数据或日志输出
- **验收标准**：kly 能自验的勾选项
- **思考题**（选）：延伸问题
```

### 2.5 自验清单标准（强制）

```markdown
## 自验清单

- [ ] 实验1-N 全部完成，dmesg输出与预期一致
- [ ] 能向AI解释：概念X（不是背书，要理解）
- [ ] 能向AI解释：概念Y
- [ ] 能独立写一个XXXX（不看讲义）

## AI评估区
评估问题示例...
```

### 2.6 AI评估流程

kly 完成后：
1. 检查自验清单完成情况
2. 用"评估问题"通过对话评估理解深度
3. 回答错误 → AI输出补充解释 → kly重学 → 重新评估
4. 全部通过 → AI输出阶段总结 → 进入下一阶段

---

## 三、前置知识检测（AI首次对话执行）

在阶段1开始前，通过3-5个问题确认kly具备基础知识。

| 知识项 | 检测问题 | 达标标准 |
|-------|---------|---------|
| C指针 | `void *p; int *q = p;` 对吗，为什么 | 能解释类型转换规则 |
| Git基础 | `git stash`和`git reset --hard`的区别 | stash保留改动，reset丢弃 |
| 交叉编译 | 为什么需要aarch64-linux-gnu-gcc | 目标架构和宿主架构不同 |
| /proc | `cat /proc/self/maps` 显示什么 | 内存映射区域 |
| PCIe基础 | MRd为什么需要CplD，MWr不需要 | Posted vs Non-Posted |

任一项不达标 → AI输出500字以内补齐材料 → 重新检测 → 通过后进入阶段1

---
## 四、课程总览与交付规划

### 4.1 课程覆盖

kly 日常工作涉及 **RP（Root Port）+ EP（Endpoint）+ Switch** 全链路，由 kly 一个人完成。课程覆盖三者，但 Switch 不单独成阶段，内容并入 S3 RC 驱动中。

### 4.2 讲义详细程度

**全部保持 S1 的详细程度**（每讲 3000-4000 字，包含原理+设计原因+错误案例+决策树），因为这是 kly 工作的核心支撑。

### 4.3 交付节奏

**分批交付，学完再下一批**。每批 1-2 个讲义，kly 反馈后 AI 评估，评估通过再继续。

### 4.4 课程总览

| 阶段 | 主题 | 讲义数 | 交付批次 | 状态 |
|------|------|--------|---------|--------|
| S1 | Linux 内核编程基础 | 4 | 第0批（已交付）| ✅ 已交付 |
| S2 | PCI 子系统框架 | 4 | 第1批 | 待交付 |
| S3 | PCIe RC 主机驱动（含 Switch）| 4 | 第2批 | 待交付 |
| S4 | DMA 和高速数据通路 | 2 | 第3批 | 待交付 |
| S5 | 调试和 bring-up 实战 | 2 | 第4批 | 待交付 |

**总讲义数：16 个**

---

## 五、阶段1：Linux内核编程基础

**交付路径**：`002_Areas/02_技术精进/05_PCIe业务/01_PCIe_linux驱动/S1_内核编程基础/`

### 5.1 交付物（9个）

| 文件 | 说明 |
|------|------|
| `S1_01_内核模块编程.md` | MODULE_LICENSE/__init/EXPORT_SYMBOL/modinfo/printk/module_param |
| `S1_02_内核内存管理.md` | kmalloc/vmalloc/ioremap/GFP_*/devm_/内存屏障 |
| `S1_03_同步原语.md` | spinlock/mutex/completion/deadlock案例/lockdep |
| `S1_04_字符设备驱动.md` | cdev/file_operations/copy_from_user/ioctl/mmap |
| `S1_kmod_skeleton.c` | 最小可运行内核模块（带module_param和debug）|
| `S1_char_device.c` | 完整字符设备驱动（open/read/write/ioctl/mmap+auto-mknod）|
| `Makefile` | 编译模板 |
| `S1_experiments.md` | 6个实验 |
| `S1_checklist.md` | 自验清单 |

### 5.2 讲义核心内容要求

**S1_01 内核模块编程.md 必须包含**：
- MODULE_LICENSE五种许可证及工程影响（必须用GPL）
- `__init`/`__exit`本质（函数标记+内存释放时机）
- EXPORT_SYMBOL机制（模块间调用）
- modpost阶段的作用
- printk日志级别和dev_err/pr_err的工程选择
- module_param的典型使用场景（debug开关/msi_vectors数量）

**S1_02 内核内存管理.md 必须包含**：
- kmalloc/vmalloc/ioremap三者的选择决策树（物理连续？→是→kmalloc；MMIO？→是→ioremap；否则→vmalloc）
- GFP_KERNEL vs GFP_ATOMIC的使用场景和死锁原因
- ioremap后必须用readl/writel（不能直接解引用）
- devm_函数的优势（自动管理，减少remove代码）
- wmb()/rmb()/mb()的作用时机（PCIe驱动写寄存器后为什么要屏障）

**S1_03 同步原语.md 必须包含**：
- spinlock为什么不能睡眠（禁用抢占+自旋=死锁）
- spin_lock_irqsave的三合一操作（关中断+关抢占+加锁）
- mutex vs spinlock的选择（持锁时间长短+是否在中断上下文）
- completion的一次性同步场景（模块卸载等待/DMA完成）
- 两个真实deadlock案例：中断和进程竞争同一把锁（未关中断）/ ABBA死锁（加锁顺序不一致）
- lockdep的使用方法（CONFIG_PROVE_LOCKING）

**S1_04 字符设备驱动.md 必须包含**：
- alloc_chrdev_region vs MKDEV（动态vs静态，动态避免设备号冲突）
- cdev生命周期（init→add→del→unregister）
- read/write必须检查copy_from_user/copy_to_user返回值（无效地址→-EFAULT）
- ioctl命令设计（_IOR/_IOW/_IO + 幻数）
- mmap在PCIe驱动中的价值（把BAR映射到用户态直接读写寄存器）

### 5.3 实验清单

| 实验 | 目标 | 验收标准 |
|------|------|---------|
| 1-最小模块 | 运行第一个内核模块 | insmod后dmesg有loaded，rmmod后有unloaded |
| 2-模块参数 | 理解module_param | debug=0和debug=1行为正确，modinfo看到参数 |
| 3-字符设备 | /dev/skel的open/read/write/ioctl | echo/cat正常工作，RESET后buffer清空 |
| 4-并发保护 | spinlock保护共享数据 | 并发读无数据错误，无panic |
| 5-copy_from_user安全 | 无效地址返回-EFAULT | 内核不panic，返回-1 errno=EFAULT |
| 6-模块依赖（选） | EXPORT_SYMBOL机制 | 先app后base失败，先base后app成功 |

### 5.4 自验清单核心问题

- [ ] 能解释为什么module_init返回0表示成功
- [ ] 能解释GFP_ATOMIC什么时候用，不用的后果
- [ ] 能解释ioremap和vmalloc的本质区别
- [ ] 能解释spinlock为什么不能睡眠
- [ ] 能解释copy_from_user为什么必须检查返回值
- [ ] 能独立写一个字符设备驱动（不看讲义）

---

## 六、阶段2：PCI子系统框架

**目标**：能写PCI/PCIe EP设备驱动，理解Linux PCI Core工作原理

**预计时间**：第1-2个月

**前置条件**：阶段1全部通过

**交付路径**：`002_Areas/02_技术精进/05_PCIe业务/01_PCIe_linux驱动/S2_PCI子系统框架/`

### 6.1 交付物（9个）

| 文件 | 说明 |
|------|------|
| `S2_01_PCI总线模型.md` | sysfs视图/pci_dev字段/probe时机/枚举流程 |
| `S2_02_配置空间访问.md` | ECAM原理/pci_read_*/Capability链表/关键Cap速查 |
| `S2_03_BAR资源管理.md` | pci_request_regions/pci_iomap/64-bit BAR |
| `S2_04_MSI中断处理.md` | MSI本质（Memory Write TLP）/MSI-X区别/request_threaded_irq |
| `S2_pci_skeleton.c` | 完整PCI驱动模板（probe/remove/BAR/MSI）|
| `Makefile` | 编译模板 |
| `S2_experiments.md` | 6个实验 |
| `S2_checklist.md` | 自验清单 |

### 6.2 讲义核心内容要求

**S2_01 PCI总线模型.md 必须包含**：
- /sys/bus/pci/devices/下每个文件内容的含义
- struct pci_dev核心字段（vendor/device/class/irq/devfn/bus）
- .probe的调用时机（总线扫描+id_table匹配后）
- lspci输出解析（Vendor:Device [Class Subclass]）
- pcim_enable_device的作用（使能设备+设置Command位）

**S2_02 配置空间访问.md 必须包含**：
- ECAM原理（4GB窗口，Bus/Dev/Func/Reg索引）
- pci_read_config_*底层调用路径
- Capability链表的通用遍历代码模板
- 关键Capability速查表：PM(0x01)/MSI(0x05)/PCIe(0x10)/AER(0x01+next)

**S2_03 BAR资源管理.md 必须包含**：
- pci_request_regions和直接pci_iomap的区别（冲突检查）
- pci_iomap vs ioremap（pci_iomap是pci设备专用的ioremap封装）
- pci_resource_flags解析（IO位/MEM位/PREFETCH位）
- 64-bit BAR的处理（BAR0声明类型，BAR1作为高位地址）

**S2_04 MSI中断处理.md 必须包含**：
- MSI本质是Memory Write TLP（写入特定地址触发中断，向量号在数据字段）
- MSI和INTx的对比（无共享/无引脚限制/支持更多向量）
- pci_alloc_irq_vectors参数和返回值含义
- pci_free_irq_vectors必须在remove中调用
- request_threaded_irq vs request_irq（thread_fn在进程上下文执行）
- MSI-X和MSI的区别（表结构不同，支持2048个向量）

### 6.3 实验清单

| 实验 | 目标 | 验收标准 |
|------|------|---------|
| 1-PCI驱动加载 | QEMU中加载驱动看到probe | dmesg有probe ok |
| 2-读取配置空间 | 读vendor/device/class | 和lspci -nn对比一致 |
| 3-MSI中断触发 | 手动触发MSI看中断日志 | dmesg有MSI IRQ日志 |
| 4-BAR读写测试 | 写入读回验证 | 写入0x12345678读回一致 |
| 5-MSI次数统计 | 中断计数 | /proc/interrupts显示次数 |
| 6-remove路径 | rmmod无resource leak | dmesg无泄漏日志 |

### 6.4 自验清单核心问题

- [ ] 能解释.pci_driver的.probe何时被调用（完整流程）
- [ ] 能解释BAR映射和ioremap的关系
- [ ] 能解释MSI和INTx的本质区别
- [ ] 能解释pci_alloc_irq_vectors返回值的含义
- [ ] 能写一个PCI驱动（识别设备→映射BAR→分配MSI→读写寄存器）

---

## 七、阶段3：PCIe RC主机驱动开发（含Switch集成）

**目标**：能开发PCIe RC/Switch主机控制器驱动，完成EP枚举和多端口管理

**预计时间**：第3-4个月

**前置条件**：阶段2全部通过 + 有真实硬件（树莓派4或RK3588）

**交付路径**：`002_Areas/02_技术精进/05_PCIe业务/01_PCIe_linux驱动/S3_RC主机驱动/`

**注意**：Switch不单独成阶段，Switch的多端口配置和拓扑发现并入本阶段讲义中。

### 7.1 交付物（9个）

| 文件 | 说明 |
|------|------|
| `S3_01_PCIe_RC架构（含Switch多端口）.md` | RC/EP/Switch区别/Type1Header/pci_host_bridge/多端口管理 |
| `S3_02_ATU地址窗口配置.md` | Outbound/Inbound配置步骤/对齐要求/验证方法 |
| `S3_03_链路训练监控.md` | LTSSM状态机/失败排查/pcie_wait_for_link |
| `S3_04_设备树集成（含Switch拓扑）.md` | DT节点/ranges属性/interrupt-map/多Bus拓扑 |
| `S3_pcie_rc_driver.c` | 完整RC驱动（probe/atu/irq/枚举）|
| `Makefile` | 编译模板 |
| `S3_experiments.md` | 5个bring-up实验 |
| `S3_checklist.md` | 自验清单 |

### 7.2 讲义核心内容要求

**S3_01 RC架构（含Switch多端口）.md 必须包含**：
- RC vs EP vs Switch的本质区别
- Switch多Host Bridge管理（每个Upstream Port是独立RC，Downstream Port是独立EP）
- RC的Type1 Header（和EP的Type0区别）
- struct pci_host_bridge核心字段（bus/windows/ops/private）
- .map_bus回调实现（通过DBI或ECAM访问对端配置空间）
- .align_resources回调（内存对齐约束，ATU要求4KB对齐）
- pci_host_bridge_register()注册流程
- 多端口场景：多个pci_host_bridge实例管理

**S3_02 ATU地址窗口配置.md 必须包含**：
- Outbound（CPU→PCIe）：CPU虚拟地址转换到PCIe地址
- Inbound（PCIe→CPU）：PCIe地址转换到CPU物理地址
- ATU配置四步骤：基址→目标地址→窗口大小→使能
- 4KB对齐要求的原因（路由粒度）
- 验证方法：读回寄存器确认+发起MRd验证路由正确
- 常见错误：窗口重叠/对齐错误/窗口大小为0

**S3_03 链路训练监控.md 必须包含**：
- LTSSM完整状态转换：Detect→Polling→Configuration→L0→Recovery
- 链路训练失败排查：RefClk问题/SerDes配置/Lane极性/对端未上电
- pcie_wait_for_link实现（轮询链路状态寄存器，超时处理）
- 速率协商结果读取（PORT_LINK_STATUS，Gen和Width）
- Switch多端口链路训练（每个Port独立LTSSM）

**S3_04 设备树集成（含Switch拓扑）.md 必须包含**：
- PCIe DT节点模板（reg/clocks/resets/interrupt-parent/ranges）
- `ranges`属性：CPU地址到PCIe地址的转换表
- `interrupt-map`：MSI中断映射到GIC
- compatible string和driver匹配
- Switch多Bus拓扑的DT描述（subordinate bus/secondary bus）
- pci_host_bridge_probe的设备树路径

### 7.3 实验清单

| 实验 | 目标 | 验收标准 |
|------|------|---------|
| 1-RC驱动加载 | 驱动加载不panic | dmesg无错误，lspci有RC设备 |
| 2-链路训练 | 链路进入L0 | 链路状态寄存器显示L0，速率宽度正确 |
| 3-EP枚举 | lspci看到EP设备 | EP的Vendor/Device ID正确 |
| 4-地址窗口 | RC能访问EP BAR | MRd收到正确CplD |
| 5-MSI中断 | EP发送MSI，RC收到 | dmesg有中断日志 |

### 7.4 自验清单核心问题

- [ ] 能解释.map_bus回调的实现（通过DBI访问对端配置空间）
- [ ] 能解释Outbound和Inbound地址窗口的区别
- [ ] 能解释ATU配置的4KB对齐要求
- [ ] 能解释为什么LTSSM可能进入Recovery而不是L0
- [ ] 能独立编写一个新芯片的RC驱动（给定寄存器地址）

---

## 八、阶段4：DMA和高速数据通路

**目标**：掌握DMA引擎框架，实现RC-EP高速数据传输

**预计时间**：第5-6个月

**前置条件**：阶段3全部通过

**交付路径**：`002_Areas/02_技术精进/05_PCIe业务/01_PCIe_linux驱动/S4_DMA高速数据通路/`

### 8.1 交付物（6个）

| 文件 | 说明 |
|------|------|
| `S4_01_DMA引擎框架.md` | dmaengine子系统/通道/描述符/SGL |
| `S4_02_PCIe_DMA传输.md` | outbound/inbound DMA/ATU/IOMMU/Cache一致性 |
| `S4_dma_driver.c` | DMA传输驱动 |
| `Makefile` | 编译模板 |
| `S4_experiments.md` | DMA性能测试 |
| `S4_checklist.md` | 自验清单 |

### 8.2 讲义核心内容要求

**S4_01 DMA引擎框架.md 必须包含**：
- struct dma_device（通道管理/状态/prep/issue接口）
- struct dma_chan（代表一个DMA通道）
- 传输流程：request_chan→engine_prep_*→submit→issue_pending
- SGL（Scatter-Gather List）：物理页不连续时的处理

**S4_02 PCIe DMA传输.md 必须包含**：
- outbound DMA（RC读EP内存，MRd）
- inbound DMA（RC写EP内存，MWr）
- ATU窗口配置（对应阶段3内容）
- IOMMU配置（IOVA分配器/dma_map_*）
- Cache一致性（dma_sync_*/DMA_TO_DEVICE/DMA_FROM_DEVICE）

### 8.3 自验清单核心问题

- [ ] 能解释DMA引擎的prep_submit_issue流程
- [ ] 能解释SGL和物理页不连续的处理
- [ ] 能解释IOMMU启用和关闭时DMA映射的区别
- [ ] 能写一个完整的DMA传输驱动

---

## 九、阶段5：调试和bring-up实战

**目标**：掌握bring-up调试能力，能独立完成bring-up测试报告

**预计时间**：第7个月

**前置条件**：阶段4全部通过

**交付路径**：`002_Areas/02_技术精进/05_PCIe业务/01_PCIe_linux驱动/S5_调试Bringup实战/`

### 9.1 交付物（5个）

| 文件 | 说明 |
|------|------|
| `S5_01_调试方法论.md` | early_printk/ftrace/perf/panic分析 |
| `S5_02_PCIe错误处理.md` | AER框架/错误注入/恢复 |
| `S5_experiments.md` | 压力测试/错误注入 |
| `S5_bringup_report.md` | 完整bring-up测试报告模板 |
| `S5_checklist.md` | 自验清单 |

### 9.2 讲义核心内容要求

**S5_01 调试方法论.md 必须包含**：
- early_printk/earlycon（内核启动早期串口输出）
- ftrace追踪PCIe函数（function_graph模式）
- perf性能分析（瓶颈定位）
- panic/oops调用栈解读（objdump -d + addr2line）
- /proc/pci和/sys/bus/pci/的使用

**S5_02 PCIe错误处理.md 必须包含**：
- AER框架（Advanced Error Reporting）
- pci_enable_pcie_error_reporting()
- UE（不可纠正错误）和CE（可纠正错误）处理流程
- 错误注入方法（pci_err_inject或硬件触发）
- 链路重训练恢复

---

## 十、总体进度追踪

| 阶段 | 开始日期 | 结束日期 | 状态 | AI评估意见 |
|------|---------|---------|------|-----------|
| 阶段1：内核基础 | | | 已交付 | |
| 阶段2：PCI子系统 | | | 未开始 | |
| 阶段3：RC+Switch驱动 | | | 未开始 | |
| 阶段4：DMA | | | 未开始 | |
| 阶段5：bring-up调试 | | | 未开始 | |

---

## 十一、kly专属备注

```
[2026-04-16] 初始评估完成。
学习目标：Linux PCIe主机驱动开发，终点能独立开发RC驱动。
覆盖范围：RP + EP + Switch全链路，Switch不单独成阶段，并入S3。
讲义详细程度：全部保持S1的详细程度。
交付节奏：分批交付，学完再下一批，每批1-2个讲义。
S1已交付（9个文件，4讲义+2代码+实验+清单）。

[2026-04-16] 硬件平台确认：
kly 无真实硬件环境，学习全程使用 QEMU virt 支撑。
QEMU 版本要求：8.0+
所有实验优先在 QEMU 中验证。
```


## 十、kly专属备注

```
[2026-04-16] 初始评估完成。
硬件平台：无真实硬件，使用QEMU virt作为主平台。
当前最大短板：Linux内核编程基础（不是PCIe协议）。
阶段1已交付完成（9个文件，含4讲义+2代码+实验+清单）。
讲义标准：原理+设计原因+常见错误+决策树。
代码标准：完整可运行+工程化注释+错误处理完整。
```
