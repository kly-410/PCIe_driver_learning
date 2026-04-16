---
created: 2026-04-16
modified: 2026-04-16
type: learn
lifecycle_status: active
summary: S1阶段5个实验的完整步骤和预期结果。
tags:
  - type/learn
  - Linux/PCIe驱动
  - S1/实验手册
---

# S1_experiments.md — 阶段1实验手册

## 实验1：最小内核模块

**目标**：运行第一个内核模块，理解模块加载/卸载流程

**步骤**：
```bash
# 1. 写代码（S1_kmod_skeleton.c 已准备好）
# 2. 编译
make

# 3. 查看生成的.ko文件
ls -lh *.ko

# 4. 加载模块
sudo insmod kmod_skeleton.ko

# 5. 检查dmesg
dmesg | tail -3
# 预期输出：skeleton: loaded (debug=0)

# 6. 查看模块信息
lsmod | grep skeleton
# 预期：模块在列表中

# 7. 查看模块元信息
modinfo kmod_skeleton.ko
# 预期：看到license=GPL, author, description等

# 8. 卸载模块
sudo rmmod kmod_skeleton

# 9. 检查dmesg
dmesg | tail -3
# 预期输出：skeleton: unloaded
```

**验收标准**：
- dmesg 有 "skeleton: loaded" 和 "skeleton: unloaded"
- lsmod 能看到/看不到模块（加载后有，卸载后无）
- modinfo 输出正确

**思考题**：MODULE_LICENSE 为什么必须是 GPL？如果改成 "Proprietary" 会怎样？

---

## 实验2：内核模块参数

**目标**：理解 module_param 和调试输出控制

**步骤**：
```bash
# 1. 修改 S1_kmod_skeleton.c（已在代码中定义 debug 参数）

# 2. 重新编译
make clean && make

# 3. 以默认参数加载
sudo insmod kmod_skeleton.ko
dmesg | grep skeleton
# 预期：debug=0

# 4. 卸载
sudo rmmod kmod_skeleton

# 5. 以 debug=1 加载
sudo insmod kmod_skeleton.ko debug=1
dmesg | grep skeleton
# 预期：debug=1

# 6. 查看参数描述
modinfo kmod_skeleton.ko | grep -A2 "^parm:"
# 预期：看到 debug 的描述

# 7. 卸载
sudo rmmod kmod_skeleton
```

**验收标准**：
- 不传参时 debug=0
- 传 debug=1 时输出 debug=1
- modinfo 能看到参数

**思考题**：为什么 module_param 的权限是 0644 而不是 0777？生产代码应该用什么权限？

---

## 实验3：字符设备驱动

**目标**：实现 /dev/skel 的 open/read/write/ioctl

**步骤**：
```bash
# 1. 修改 Makefile 添加模块
# 在 Makefile 中加入：
# obj-m += skel_char_device.o

# 或者直接编译单个模块：
make skel_char_device

# 2. 加载
sudo insmod skel_char_device.ko

# 3. 检查设备节点（devm自动创建）
ls -l /dev/skel
# 预期：/dev/skel 存在，类型是字符设备

# 4. 测试写
echo "hello_pcie" > /dev/skel
# 预期：无报错

# 5. 测试读
cat /dev/skel
# 预期：输出 "hello_pcie"

# 6. 测试 ioctl
# 获取 buffer 长度
sudo ./my_test_prog SKEL_GET_COUNT
# 预期：输出 11（"hello_pcie" 的长度）

# 7. 触发 RESET
sudo ./my_test_prog SKEL_RESET

# 8. 再读
cat /dev/skel
# 预期：空

# 9. 卸载
sudo rmmod skel_char_device
# 预期：/dev/skel 自动消失
```

**验收标准**：
- /dev/skel 自动创建和删除
- echo/cat 正常工作
- RESET 后 buffer 清空

---

## 实验4：并发保护（spinlock）

**目标**：验证 spinlock 保护共享数据

**步骤**：
```bash
# 1. 写一个压力测试脚本
# 同时从多个进程读同一个模块
for i in $(seq 1 10); do
  (while true; do cat /dev/skel > /dev/null; done) &
done

# 2. 同时触发中断（如果驱动有timer模拟中断）
# 观察 /var/log/kern.log 是否有数据错误
# 预期：无错误，counter 稳定增长

# 3. 杀掉所有进程
killall cat
```

**验收标准**：
- 多次读操作不出现数据不一致
- 无 "BUG:" 或 "WARNING:" 日志

---

## 实验5：copy_from_user 安全

**目标**：验证 copy_from_user 失败处理

**步骤**：
```bash
# 1. 写一个测试程序，传入无效地址触发 copy_from_user 失败
# cat > test_bad_write.c << 'EOF'
#include <unistd.h>
#include <fcntl.h>
int main() {
    int fd = open("/dev/skel", O_WRONLY);
    // 传入一个肯定无效的地址
    write(fd, (void*)0xdead0000, 100);
    close(fd);
}
# EOF
# gcc -o test_bad_write test_bad_write.c

# 2. 运行
# ./test_bad_write
# 预期：write 返回 -1，errno=EFAULT
# 内核 dmesg 无 panic

# 3. 检查dmesg
dmesg | grep -i error
# 预期：无 ERROR
```

**验收标准**：
- 内核不 panic
- 返回 -EFAULT

---

## 实验6（选做）：模块依赖

**目标**：理解 EXPORT_SYMBOL 和模块依赖

**步骤**：
```bash
# 1. 创建两个模块：base.ko 和 app.ko
# base.ko 导出一个函数
# app.ko 调用该函数

# 2. 先加载 app.ko（会失败）
# sudo insmod app.ko
# 预期：insmod: ERROR: Unknown symbol in module

# 3. 先加载 base.ko
# sudo insmod base.ko

# 4. 再加载 app.ko（成功）
# sudo insmod app.ko

# 5. 查看符号依赖
# cat /proc/kallsyms | grep my_symbol
```

**验收标准**：
- 先 app 后 base 失败
- 先 base 后 app 成功
