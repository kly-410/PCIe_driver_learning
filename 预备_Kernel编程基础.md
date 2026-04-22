# PCIe_driver_learning

---

## 内核编程预备知识

### A. 内核模块基础

#### MODULE_LICENSE / MODULE_AUTHOR / MODULE_DESCRIPTION 宏的作用

内核模块必须声明许可证，Linux 根据这个字段决定模块是否在 GPL 兼容模式下工作。未声明 `MODULE_LICENSE("GPL")` 的模块无法访问仅对 GPL 模块导出的符号（如 `__this_module`）。

| 宏 | 作用 |
|---|---|
| `MODULE_LICENSE("GPL")` | 声明许可证，非 GPL 模块无法使用内核私有符号 |
| `MODULE_AUTHOR("name")` | 记录作者信息，不影响功能，仅供元数据 |
| `MODULE_DESCRIPTION("text")` | 模块功能描述，`modinfo` 可查看 |

```c
// include/linux/module.h:expolynomial
#define MODULE_LICENSE(_license) static const char *__mod_license __attribute__((section(".modinfo"))) = "license=" _license
```

加载非 GPL 模块时，内核日志会输出 `module license 'XXX' taints kernel`。

#### module_init() / module_exit() 的意义

内核模块的入口和退出函数通过这两个宏注册，替代了手动符号表操作。`module_init` 将函数地址放入 `.initcall.text` 段，加载时由 do_init_module() 调用；`module_exit` 将函数放入 `.exit.text` 段，卸载时由 SyS_delete_module() 调用。

```c
// include/linux/export.h
#define module_init(fn)  static inline initcall_t __maybe_unused __inittest(void) { return fn; }
#define module_exit(fn)  static inline exitcall_t __maybe_unused __exittest(void) { return fn; }
```

`__init` 标记的函数在初始化完成后释放内存，驱动应在所有 `.c` 文件中为 init 函数加此标记。

#### 编译一个最小内核模块的完整 Makefile

```make
# Makefile for PCIe_driver_learning
obj-m := pcie.o          # 编译成模块，生成 pcie.ko
pcie-objs := main.o proc.o  # 多文件模块

KDIR ?= /lib/modules/$(shell uname -r)/build  # 内核源码树
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all clean
```

`obj-m` 告诉 kbuild 将 `.o` 链接为 `ko` 模块文件。`KDIR` 指向正在运行内核的构建目录，`M=$(PWD)` 使 kbuild 切换到源码树外执行。

---

### B. 内存分配

#### kmalloc() / kfree() — slab 分配，最常用

kmalloc 基于 SLAB/SLUB 分配器，分配 2^n 字节的物理连续内存，最小 32 字节，最大 4MB（取决于 `CONFIG_SLUB` 和架构）。分配成功返回指针，失败返回 NULL。

```c
// mm/slab_common.c:700
static __always_inline void *kmalloc(size_t size, gfp_t flags)
{
    if (__builtin_constant_p(size)) {
        // 编译期大小常量路径，走 ksize() 快速路径
    }
    return __kmalloc(size, flags);
}

// mm/slab.c:4090
void *__kmalloc(size_t size, gfp_t flags)
{
    struct kmem_cache *cachep = kmalloc_slab(size);  // 按大小查 slab cache
    return slab_alloc(cachep, flags, _RET_IP_);
}
```

`kmalloc_slab()` 根据请求字节数查表找到对应的 `kmem_cache`，每个 2^n 大小对应一条 cache。kmalloc 分配出的内存可以用 kfree 直接释放，不要求配对。

#### vmalloc() / vfree() — 虚拟连续、物理不连续，用于大缓冲区

vmalloc 分配虚拟地址连续、但物理地址不连续的大块内存（页对齐，按 PAGE_SIZE 粒度拼接）。用于分配超过 kmalloc 最大限制的缓冲区，或不需要物理连续的 DMA 场景。

```c
// mm/vmalloc.c:1780
void *vmalloc(unsigned long size)
{
    return __vmalloc_node(size, 1, GFP_KERNEL | __GFP_HIGHMEM,
                          PAGE_KERNEL, NUMA_NO_NODE, caller);
}
```

与 kmalloc 对比：

| 特性 | kmalloc | vmalloc |
|---|---|---|
| 物理地址 | 连续 | 不连续 |
| 最大单次 | ~4MB | 受虚拟地址空间限制 |
| 分配延迟 | 快（slab 缓存） | 慢（需映射多个物理页） |
| 使用场景 | 驱动内部小数据结构 | 大缓冲区、DMA 一致性映射（需 bounced） |

#### GFP_KERNEL / GFP_ATOMIC 的区别和选用场景

GFP 是 get free pages 的缩写，是分配请求的 flags 字段，控制分配器行为和限制。

| Flag | 含义 | 可否休眠 | 场景 |
|---|---|---|---|
| `GFP_KERNEL` | 普通内核内存分配 | 可以 | 进程上下文中的数据操作 |
| `GFP_ATOMIC` | 原子分配，不休眠 | 不可 | 中断上下文、持锁状态 |
| `GFP_KERNEL` \| `__GFP_KSWAP` | 允许回收和 swap | 可以 | 通用路径 |
| `GFP_NOIO` | 不触发 I/O 回收 | 可以 | 避免递归 I/O |
| `GFP_NOFS` | 不触发文件系统调用 | 可以 | 避免递归文件系统 |

原子分配（GFP_ATOMIC / GFP_NOWAIT）失败时不等待，直接返回 NULL；内核上下文若拿不到就崩溃，所以原子分配必须配 NULL 检查。`GFP_DMA` flag 强制从低 16MB 物理地址分配，供 ISA DMA 使用。

#### 源码：mm/slab_common.c 中的 kmalloc 定义位置

```c
// mm/slab_common.c:692
static __always_inline void *kmalloc(size_t size, gfp_t flags)
{
    struct kmem_cache *cachep = kmalloc_slab(size);
    return slab_alloc(cachep, flags, _RET_IP_);
}

// mm/slab_common.c:157
static inline struct kmem_cache *kmalloc_slab(size_t size)
{
    if (size <= 192) return kmalloc_caches[kmalloc_index(size)];
    return kmalloc_caches[kmalloc_index(size) - 1];
}
```

`kmalloc_caches[]` 数组按大小分为多个 cache（32, 64, 96, 128, ... 2^14），`kmalloc_index()` 用查表将请求大小映射到 cache 索引。

---

### C. 指针修饰符

#### volatile — 防止编译器优化掉对外设寄存器的读写

`volatile` 告诉编译器每次访问都必须从内存读取/写入，不能将其缓存在寄存器中或重排序。对外设寄存器（MMIO）的读写若没有 volatile，编译器可能将两次读合并为一次，或把写操作移到循环外。

```c
// x86 默认不定义 __raw__{*} 修饰符，此处说明用法
volatile uint32_t *reg = (volatile uint32_t *)0xFED00000;
*reg = 0x5A;           // 写入 → 真实总线周期
uint32_t val = *reg;   // 读取 → 每次都发总线读
```

PCIe 设备 BAR 空间映射到虚拟地址后，访问同样需要 volatile。外设寄存器可能随外设状态变化（不因 CPU 缓存而变），volatile 是正确访问的最低保障。

#### __iomem — 标记这是 I/O 内存地址，与普通指针区分

`__iomem` 是 Linux 内核的标记类型，用于静态分析工具（sparse）区分普通指针和 I/O 内存指针。在启用 CONFIG_DEBUG_SPARSE 的内核上，解引用 `__iomem` 指针会触发告警。

```c
// include/linux/compiler_types.h
#ifndef __iomem
#define __iomem
#endif

// arch/x86/include/asm/io.h
void __iomem *ioremap(phys_addr_t phys_addr, size_t size);
void iounmap(volatile void __iomem *addr);
```

标准用法：设备寄存器先用 `ioremap()` 映射到内核虚拟地址（返回 `void __iomem *`），再用 `readl()` / `writel()` 访问，禁止直接解引用 `__iomem` 指针。

#### __user — 标记这是用户态指针（给 sparse 检查用）

`__user` 标记来自用户空间的指针，提示 sparse 这类指针不能直接解引用，必须通过 `copy_from_user()` / `copy_to_user()` 访问。防止内核误用用户态指针（可能导致内核信息泄露或崩溃）。

```c
// include/linux/compiler_types.h
#ifndef __user
#define __user
#endif

// 典型函数签名
long ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
            // arg 实际上是指向用户空间的指针，需 copy_from_user
```

`copy_from_user(to, from_user_ptr, n)` 检查用户指针有效性并将数据搬入内核。若用户指针指向未映射地址，返回非零，驱动应返回 `-EFAULT`。

---

### D. 同步原语

#### spin_lock() / spin_unlock() — 不可休眠的锁，用于中断上下文

自旋锁的核心特性：获取不到锁时 **忙等**（循环检测），不切换进程。实现基于原子指令（如 x86 的 `xchg`），单核禁止抢占的机器上自旋锁为空操作。

```c
// include/linux/spinlock.h
static inline void spin_lock(spinlock_t *lock)
{
    raw_spin_lock(&lock->rlock);
}

// kernel/locking/spinlock.c:178
void __lockfunc raw_spin_lock(raw_spinlock_t *lock)
{
    __raw_spin_lock(lock);
}

// arch/x86/include/asm/spinlock.h:30
static __always_inline void __raw_spin_lock(raw_spinlock_t *lock)
{
    asm volatile("1:\tlock; incl %0\n\tjz 1b" : "+m" (lock->val) : : "memory", "cc");
}
```

在中断处理函数中必须使用 `spin_lock_irqsave()` / `spin_unlock_irqrestore()`，禁止在持锁路径中调用任何可能休眠的函数（睡眠 = 死锁）。临界区必须极短（微秒级），否则浪费 CPU。

#### mutex_lock() / mutex_unlock() — 可休眠的锁，用于进程上下文

互斥锁允许持锁时睡眠，当无法获取锁时调用调度器让出 CPU。实现基于 `futex`，无需忙等，适合临界区较长的场景。

```c
// kernel/locking/mutex.c:440
void __sched mutex_lock(struct mutex *lock)
{
    might_sleep();   // 调试辅助，标记可在睡眠点
    for (;;) {
        if (atomic_try_acquire_relaxed(&lock->count))
            break;
        schedule();  // 让出 CPU，当前进程进入睡眠
    }
}
```

`might_sleep()` 在启用 DEBUG_ATOMIC_SLEEP 的内核上会在睡眠路径触发栈回溯，帮助发现错误持锁场景。

#### 自旋锁和互斥锁的选用原则

| 维度 | spin_lock | mutex_lock |
|---|---|---|
| 持锁期间能否睡眠 | 不可 | 可以 |
| 适用上下文 | 中断、软中断、持锁极短代码 | 进程上下文 |
| 开销 | 无上下文切换，但 CPU 忙等 | 有上下文切换，延迟不确定 |
| 临界区长度 | 微秒级 | 任意长度 |

**实战原则**：

- 持锁期间若有 I/O、内存分配、copy_from_user 等阻塞操作，**必须用 mutex**
- 中断处理函数中持锁 → `spin_lock_irqsave()`，持锁临界区越小越好
- 同一线程先拿 mutex 再拿 spin_lock：合法（mutex 内部可睡眠）；反过来先拿 spin_lock 再拿 mutex：危险（B 部分不能睡眠）

---

### E. copy_from_user / copy_to_user

#### 为什么驱动不能直接解引用用户指针

用户态指针是虚拟地址，仅对持有它的进程有意义。内核代码持有用户指针时：

1. 该指针所指向的地址可能不在内核地址空间内
2. 对应页面可能未映射，导致页错误（kernel oops）
3. 用户可能伪造指针试图访问内核内存（安全漏洞）

```c
// 用户态指针 vs 内核态指针
unsigned long user_ptr;      // 用户传来
*kernel_var = user_ptr;     // ❌ 可能崩溃或越界
*kernel_var = *(unsigned long *)user_ptr;  // ❌ 绝对禁止直接解引用
```

用户态指针没有保证有效性，必须通过内核提供的拷贝函数访问，并且在使用前检查 copy 函数的返回值。

#### copy_from_user(dst, src, size) — 从用户空间拷贝数据到内核

`copy_from_user` 将数据从用户空间地址安全地拷贝到内核缓冲区。返回值若非零表示拷贝失败（源部分或全部无法访问），驱动应返回 `-EFAULT`。函数内部会检查用户页面的有效性。

```c
// arch/x86/include/asm/uaccess.h:35
unsigned long
copy_from_user(void *to, const void __user *from, unsigned long n)
{
    if (likely(check_copy_size(to, n, true)))
        n = __copy_user_intel(to, from, n);
    else
        n = 6;  // 失败时返回 n（非零）
    return n;
}

// arch/x86/lib/copy_user_generic.c:320
unsigned long __copy_user_intel(void *dst, const void *src, unsigned long size)
{
    // 逐字节拷贝，检查页面边界，返回未完成的字节数
}
```

关键语义：返回 0 = 成功，返回非零 = 还有多少字节未拷贝。驱动必须检查返回值并处理失败。

#### copy_to_user(dst, src, size) — 从内核拷贝数据到用户空间

`copy_to_user` 将内核数据写入用户态地址空间。返回值非零表示写入失败，驱动应返回 `-EFAULT`。

```c
// arch/x86/include/asm/uaccess.h:39
unsigned long
copy_to_user(void __user *to, const void *from, unsigned long n)
{
    if (likely(check_copy_size(to, n, false)))
        n = __copy_user_intel(to, from, n);
    return n;
}
```

两个函数语义对称，但 `__user` 方向不同：copy_from_user 读用户内存，copy_to_user 写用户内存。两者都会在访问无效页面时触发异常，由底层处理。

#### 源码：arch/x86/include/asm/uaccess.h

```c
// arch/x86/include/asm/uaccess.h (内核 6.x 版本结构)
static inline unsigned long __must_check
copy_from_user(void *to, const void __user *from, unsigned long n)
{
    if (check_copy_size(to, n, true))
        n = raw_copy_from_user(to, from, n);
    else
        n = n;  // 超出限制则返回完整 n
    return n;
}

static inline unsigned long __must_check
copy_to_user(void __user *to, const void *from, unsigned long n)
{
    if (check_copy_size(to, n, false))
        n = raw_copy_to_user(to, from, n);
    return n;
}
```

`__must_check` 属性使调用者忽略返回值时产生编译器警告，推荐在所有 copy 调用后加 IS_ERR 检查。

---

### F. 常用内核 API 速查表

#### printk() — 内核日志，优先级和格式

printk 是内核日志系统，比 printf 多了日志级别控制。级别数值越小优先级越高，`/proc/sys/kernel/printk` 控制哪些级别会显示到控制台。

```c
// include/linux/printk.h
#define pr_emerg(fmt, ...)  printk(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_alert(fmt, ...)  printk(KERN_ALERT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_crit(fmt, ...)   printk(KERN_CRIT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err(fmt, ...)    printk(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn(fmt, ...)   printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_notice(fmt, ...) printk(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...)   printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debug(fmt, ...)  printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
```

常用格式符：

| 格式符 | 类型 | 说明 |
|---|---|---|
| `%pd` / `%pD` | 指针 | 打印指针地址，带驱动特定格式修饰符 |
| `%pR` | `resource_size_t` | 打印 I/O 资源地址 |
| `%zu` | `size_t` | 无符号整数，`%zd` 为有符号 |
| `%*ph` | 字节数组 | 打印 hex dump：`%*phC`（带冒号分隔） |

调试时可用 `pr_debug()`，仅在 `DEBUG` 宏定义或 `dynamic_debug` 启用时输出。

#### container_of() — 从结构体成员反推结构体首地址

`container_of` 是 Linux 内核最常用的宏之一，通过指向成员的指针、成员名称和外围结构体类型，反推外围结构体的首地址。典型用法：在自定义链表中通过 `list_entry` 或在 PCI 驱动的 ` pci_driver.probe` 中通过 `struct pci_dev` 反推自定义 `struct mydev`。

```c
// include/linux/container.h
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - __builtin_offsetof(type, member)))

// include/linux/compiler_types.h
#define __builtin_offsetof(type, member) __builtin_offsetof(type, member)
```

实际使用：

```c
struct my_device {
    struct pci_dev *pci_dev;
    void __iomem *bar0;
    struct list_head list;
};
// 在 list_for_each_entry 遍历中
struct my_device *dev = container_of(entry, struct my_device, list);
```

`typeof(ptr)` 确保类型匹配，`__builtin_offsetof` 在 GCC/Clang 下计算 member 在 type 中的字节偏移。

#### likely() / unlikely() — 分支预测提示

这两个宏向编译器提示分支走向概率，引导 GCC 将热路径代码排在一起，减少指令流水线冲刷。PCIe 驱动调试阶段错误路径多，`unlikely()` 可让编译器将错误处理放在冷路径。

```c
// include/linux/compiler.h
#define likely(x)    __builtin_expect(!!(x), 1)   // x 预期为真
#define unlikely(x)  __builtin_expect(!!(x), 0)  // x 预期为假

// x86 实际效果
if (likely(err == 0)) {
    // 热路径：编译器将此处代码放在更优的指令缓存位置
} else {
    // 冷路径：可能放在函数末尾或条件跳到远处
}
```

**误区**：这两只是提示，不改变程序语义，错误使用不影响正确性但无优化效果。PCIe 错误处理路径用 `unlikely()` 标注，`(ret = pci_enable_device(pdev)) != 0` 用 `unlikely()` 是标准写法。