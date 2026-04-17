/*
 * S1_04_mmio.c - MMIO 读写与屏障演示
 *
 * 编译：gcc -o S1_04_mmio S1_04_mmio.c
 * 运行：./S1_04_mmio
 *
 * 说明：本程序演示 MMIO 读写和屏障的 API 使用，
 *       不访问真实硬件（没有 ioremap）。
 *       真实 EP 驱动中的 ioremap 需要有效的 BAR 地址。
 *
 * MMIO 访问三原则：
 *   1. 用 ioread32/iowrite32，不用直接解引用指针
 *   2. Posted 写之后用 wmb() 保证顺序
 *   3. 读返回数据之前用 rmb() 保证读到最新值
 *
 * DMA 描述符下发示例（带屏障）：
 *   iowrite32(desc_addr, bar + DMA_DESC_PTR);
 *   wmb();                              // 保证 desc 内容在 cmd 之前被 EP 看到
 *   iowrite32(START_BIT, bar + DMA_CTRL);
 *
 * MSI 中断触发示例（Posted MWr）：
 *   // MSI 本质是写入 RC 分配的 MSI 地址（Posted），
 *   // CPU 不等待 EP 确认就继续执行下一条指令
 *   iowrite32(vector, msi_addr);       // Posted，不需要等
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/*
 * 假设 bar0 是 ioremap 后的虚拟地址（这里只是演示偏移定义）
 */
#define CTRL_REG    0x00
#define STATUS_REG  0x04
#define ADDR_REG    0x08
#define LEN_REG     0x0C
#define DMA_START   0x01
#define DMA_DONE    0x02

/* 模拟 DMA 描述符下发（演示屏障作用）*/
void dma_desc_submit_example(void)
{
    printf("=== DMA 描述符下发（错误顺序）===\\n");
    printf("错误做法（无屏障）：\\n");
    printf("  iowrite32(desc_addr, bar + ADDR_REG);\\n");
    printf("  iowrite32(START,     bar + CTRL_REG); // 可能先到！\\n");
    printf("\\n正确做法（有 wmb）：\\n");
    printf("  iowrite32(desc_addr, bar + ADDR_REG);\\n");
    printf("  wmb(); // 保证 addr 在 cmd 之前被 EP 看到\\n");
    printf("  iowrite32(START,     bar + CTRL_REG);\\n");
    printf("\\n屏障后的顺序：ADDR_REG → (wmb) → CTRL_REG\\n");
}

/* 模拟 MSI 门铃（Posted MWr）*/
void msi_trigger_example(void)
{
    printf("\\n=== MSI 中断触发（Posted MWr）===\\n");
    printf("MSI 本质是 Posted Memory Write（MsgD）：\\n");
    printf("  iowrite32(vector_num, msi_addr);  // Posted，写完即完成\\n");
    printf("  // CPU 不等待 EP 确认，继续执行\\n");
    printf("  // MSI 路由到 RC 侧 MSI 中断控制器，产生 CPU 中断\\n");
}

/* MMIO 屏障决策树 */
void barrier_decision_tree(void)
{
    printf("\\n=== MMIO 屏障决策树 ===\\n");
    printf("写 EP 寄存器（iowrite32）后，还要写同一个 EP 的另一个寄存器？\\n");
    printf("  → 是 → 需要 wmb()（屏障之前的写不被重排到屏障之后）\\n");
    printf("\\n读 EP 寄存器（ioread32）后，要依赖返回值做判断？\\n");
    printf("  → 是 → 需要 rmb()（保证读完成后再继续）\\n");
    printf("\\n既有读又有写，且有依赖关系？\\n");
    printf("  → 需要 mb()（读写全屏障）\\n");
    printf("\\nposted 写之间没有依赖（如日志写入）？\\n");
    printf("  → 不需要屏障\\n");
}

/*
 * PCIe Posted Write 的屏障必要性：
 * x86 的 store 指令是 weakly ordered（弱序），
 * Posted MWr 不会触发 cache writeback，
 * wmb() 强制刷写 store buffer，
 * 确保 TLP 在链路上按顺序发出。
 */
void why_wmb_needed(void)
{
    printf("\\n=== 为什么 wmb() 必须？ ===\\n");
    printf("Posted MWr TLP 不会从 CPU 收到 ACK，CPU 认为'写完成'了。\\n");
    printf("但实际上数据可能还在 store buffer 里，没有到达 PCIe 链路。\\n");
    printf("wmb() 强制 flush store buffer，保证 TLP 被 PCIe 控制器发出。\\n");
    printf("没有 wmb()：\\n");
    printf("  CPU: store [addr] → store [cmd]  (重排后)\\n");
    printf("  EP:  先收到 cmd (不知道 addr) → 再收到 addr\\n");
    printf("正确顺序：\\n");
    printf("  CPU: store [addr] → wmb() → store [cmd]\\n");
    printf("  EP:  先收到 addr → 后收到 cmd\\n");
}

int main(int argc, char **argv)
{
    printf("S1_04_mmio - MMIO 读写与屏障演示\\n");
    printf("注意：本程序不访问真实硬件，仅演示 API 使用\\n\\n");

    dma_desc_submit_example();
    msi_trigger_example();
    barrier_decision_tree();
    why_wmb_needed();

    return 0;
}
