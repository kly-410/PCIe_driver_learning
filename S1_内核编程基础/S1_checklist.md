---
created: 2026-04-16
modified: 2026-04-16
type: learn
lifecycle_status: active
summary: S1阶段自验清单，kly完成后AI评估。
tags:
  - type/learn
  - Linux/PCIe驱动
  - S1/自验清单
---

# S1 自验清单

完成条件：所有条目都是 YES 才能进入阶段2。

## 实验完成

- [ ] 实验1：最小内核模块 - insmod/rmmod/dmesg 输出正确
- [ ] 实验2：模块参数 - debug=0 和 debug=1 加载行为正确
- [ ] 实验3：字符设备驱动 - /dev/skel open/read/write/ioctl 正常
- [ ] 实验4：spinlock - 并发访问无错误
- [ ] 实验5：copy_from_user - 无效地址返回 -EFAULT，内核不 panic

## 概念理解

能向 AI 解释以下问题（不需要背书，理解即可）：

- [ ] 为什么 module_init 返回 0 表示成功，负数表示错误
- [ ] GFP_ATOMIC 什么时候用，不用的后果是什么
- [ ] ioremap 和 vmalloc 的本质区别
- [ ] spinlock 为什么不能睡眠
- [ ] copy_from_user 为什么必须检查返回值
- [ ] alloc_chrdev_region 和 MKDEV 的区别
- [ ] __init 和 __exit 的作用

## 编码能力

- [ ] 能不看讲义，独立写一个字符设备驱动（有 open/read/write/ioctl）
- [ ] 能解释 devm_ 函数相比传统函数的优势
- [ ] 能写出正确的 spin_lock_irqsave 和 spin_unlock_irqrestore 配对

## AI 评估区

AI 通过对话评估 kly 的理解深度。以下任一问题回答错误，AI 输出补充解释，kly 重学后重新评估。

**评估问题示例**：
- "为什么中断里不能用 GFP_KERNEL？"
- "如果忘记调用 iounmap 会怎样？"
- "spin_lock_irqsave 做了哪三件事？"
- "copy_to_user 返回什么值表示失败？"

---

## 进度记录

| 评估项 | 结果 | 日期 |
|-------|------|------|
| 实验完成 | | |
| 概念理解 | | |
| 编码能力 | | |
| AI 综合评估 | | |

