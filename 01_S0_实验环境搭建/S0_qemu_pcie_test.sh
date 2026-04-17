#!/bin/bash
# S0_qemu_pcie_test.sh - QEMU PCIe RC 测试启动脚本
# 平台：aarch64 virt（ARM 架构，TCG 软件模拟）
# 用法：bash S0_qemu_pcie_test.sh
set -e

# QEMU 安装路径（源码编译安装在 /usr/local/bin/）
QEMU="${QEMU:-/usr/local/bin/qemu-system-aarch64}"
DISK="${DISK:-/tmp/virtpcitest.qcow2}"
MEMSIZE="${MEMSIZE:-512M}"

# 创建磁盘镜像（如果不存在）
[ ! -f "$DISK" ] && qemu-img create -f qcow2 "$DISK" 4G

echo "[S0] 启动 QEMU aarch64 virt PCIe 环境..."
echo "[S0] QEMU: $QEMU"
echo "[S0] DISK: $DISK"
echo "[S0] MEM: $MEMSIZE"

$QEMU \
    -M virt \
    -m $MEMSIZE \
    -nographic \
    -display none \
    -kernel /opt/qemu-9.0.0/bios.bin \
    -initrd /opt/qemu-9.0.0/bios.bin

echo "[S0] QEMU 已退出"
