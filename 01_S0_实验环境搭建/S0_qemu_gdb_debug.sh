#!/bin/bash
# S0_qemu_gdb_debug.sh - QEMU + GDB 内核调试启动
# 平台：aarch64 virt（注意：aarch64 不支持 KVM 加速，使用 TCG 软件模拟）
# 用法：bash S0_qemu_gdb_debug.sh
# 然后另一个终端：gdb /usr/lib/debug/boot/vmlinuz-$(uname -r)
# (gdb) target remote localhost:1234
set -e

# QEMU 安装路径（源码编译安装在 /usr/local/bin/）
QEMU="${QEMU:-/usr/local/bin/qemu-system-aarch64}"
DISK="${DISK:-/tmp/virtpcitest.qcow2}"

echo "[S0] 启动 QEMU aarch64 virt + GDB 调试模式..."
echo "[S0] QEMU: $QEMU"
echo "[S0] 等待 GDB 连接 localhost:1234 ..."

# 注意：-enable-kvm 不支持（aarch64 桌面 CPU 无 ARM 虚拟化扩展）
# -s: GDB 监听 1234 端口
# -S: 启动后等待 GDB 连接再继续（freeze）
$QEMU \
    -M virt \
    -m 512M \
    -nographic \
    -display none \
    -drive file="$DISK",format=qcow2 \
    -s -S

echo "[S0] QEMU 已退出"
