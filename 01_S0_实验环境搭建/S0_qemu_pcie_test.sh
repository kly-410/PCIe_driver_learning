#!/bin/bash
# S0_qemu_pcie_test.sh - QEMU PCIe RC 测试启动脚本
# 用法：bash S0_qemu_pcie_test.sh
set -e
QEMU="${QEMU:-/usr/local/bin/qemu-system-x86_64}"
KERNEL="/boot/vmlinuz-$(uname -r)"
INITRD="/boot/initrd.img-$(uname -r)"
DISK="${DISK:-/tmp/virtpcitest.qcow2}"
MEMSIZE="${MEMSIZE:-512M}"
[ ! -f "$DISK" ] && qemu-img create -f qcow2 "$DISK" 4G
echo "[S0] 启动 QEMU virt PCIe 环境..."
$QEMU \
    -M virt -m $MEMSIZE \
    -kernel "$KERNEL" -initrd "$INITRD" \
    -append "console=ttyS0 root=/dev/sda ro" \
    -drive file="$DISK",format=qcow2 \
    -nographic -enable-kvm \
    -device virtio-net-pci,addr=01.0 \
    -device nvme,addr=02.0,serial=S0TEST \
    -s -S
echo "[S0] QEMU 已退出"
