/*
 * S1_04_mmio.c - MMIO 读写与屏障演示
 *
 * 编译：gcc -o S1_04_mmio S1_04_mmio.c
 * 运行：./S1_04_mmio
 *
 * 说明：本程序演示 MMIO 读写和屏障的 API 使用，
 *       不访问真实硬件（没有 ioremap）。
 *
 * MMIO 访问三原则：
 *   1. 用 ioread32/iowrite32，不用直接解引用指针
 *   2. Posted 写之后用 wmb() 保证顺序
 *   3. 读返回数据之前用 rmb() 保证读到最新值
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* 假设 bar0 是 ioremap 后的虚拟地址（这里只是演示偏移定义）*/
#define CTRL_REG    0x00
#define STATUS_REG  0x04
#define ADDR_REG    0x08
#define LEN_REG     0x0C
#define DMA_START   0x01
#define DMA_DONE    0x02

/* ======================== DMA 描述符下发 ======================== */
void dma_desc_submit_example(void)
{
    puts("\
╔══════════════════════════════════════════════════════════════╗\n\
║  场景：DMA 描述符下发（写两个寄存器，顺序不能错）            ║\n\
╚══════════════════════════════════════════════════════════════╝\n");

    puts("  【错误做法 — 无屏障】");
    puts("  --------------------------------------------------");
    puts("  iowrite32(desc_addr, bar + ADDR_REG);  // 写描述符地址");
    puts("  iowrite32(START,     bar + CTRL_REG);  // 启动 DMA");
    puts("              ↑ EP 可能先收到这条，比 addr 还早到！\n");

    puts("  【正确做法 — 加 wmb()】");
    puts("  --------------------------------------------------");
    puts("  iowrite32(desc_addr, bar + ADDR_REG);  // 写描述符地址");
    puts("  wmb();                                     // 强制刷 store buffer");
    puts("  iowrite32(START,     bar + CTRL_REG);  // 现在 EP 一定先看到 addr\n");

    puts("  EP 接收顺序：ADDR_REG → (wmb) → CTRL_REG\n");
}

/* ======================== MSI 中断触发 ======================== */
void msi_trigger_example(void)
{
    puts("\
╔══════════════════════════════════════════════════════════════╗\n\
║  场景：MSI 中断触发（Posted MWr，不等 ACK）                  ║\n\
╚══════════════════════════════════════════════════════════════╝\n");

    puts("  MSI 本质是 Posted Memory Write（MsgD），写完即完成：\n");
    puts("  iowrite32(vector_num, msi_addr);  // Posted，CPU 不等 EP 确认");
    puts("  // CPU 继续执行下一条指令，不阻塞");
    puts("  // MSI 路由到 RC 侧 MSI 控制器，产生 CPU 中断\n");
}

/* ======================== MMIO 屏障决策树 ======================== */
void barrier_decision_tree(void)
{
    puts("\
╔══════════════════════════════════════════════════════════════╗\n\
║  MMIO 屏障决策树 — 什么时候需要屏障？                        ║\n\
╚══════════════════════════════════════════════════════════════╝\n");

    puts("  Q1. 写 EP 寄存器后，还要写同一个 EP 的另一个寄存器？");
    puts("      → YES → 需要 wmb()\n");

    puts("  Q2. 读 EP 寄存器后，要依赖返回值做判断？");
    puts("      → YES → 需要 rmb()\n");

    puts("  Q3. 既有读又有写，且有依赖关系？");
    puts("      → YES → 需要 mb()\n");

    puts("  Q4. posted 写之间没有依赖（如日志写入）？");
    puts("      → YES → 不需要屏障\n");
}

/* ======================== 为什么 wmb 必须 ======================== */
void why_wmb_needed(void)
{
    puts("\
╔══════════════════════════════════════════════════════════════╗\n\
║  为什么 wmb() 必须？（x86 store buffer 弱序问题）           ║\n\
╚══════════════════════════════════════════════════════════════╝\n");

    puts("  Posted MWr TLP 不会从 EP 收到 ACK，");
    puts("  CPU 认为'写完成'了，但数据可能还在 store buffer 里。\n");

    puts("  【没有 wmb() — 可能被 CPU 重排】");
    puts("  --------------------------------------------------");
    puts("  程序员意图：   store [addr] → store [cmd]");
    puts("  CPU 实际执行： store [cmd] → store [addr]  ← 重排！");
    puts("  EP 看到：      先收到 cmd → 再收到 addr（DMA 跑飞）\n");

    puts("  【有 wmb() — 强制顺序】");
    puts("  --------------------------------------------------");
    puts("  程序员意图：   store [addr] → wmb() → store [cmd]");
    puts("  EP 看到：      先收到 addr → 后收到 cmd（正确）\n");

    puts("  wmb() 干了什么：发 mfence 指令，flush store buffer，");
    puts("                  保证所有写都到达 PCIe 控制器后再继续。\n");
}

int main(int argc, char **argv)
{
    (void)argc;  (void)argv;  /* 未使用参数 */

    puts("S1_04_mmio - MMIO 读写与屏障演示\n");
    puts("注意：本程序不访问真实硬件，仅演示 API 使用\n");

    dma_desc_submit_example();
    msi_trigger_example();
    barrier_decision_tree();
    why_wmb_needed();

    return 0;
}
