#!/bin/bash
# S0_qemu_gdb_debug.sh - QEMU + GDB 内核调试启动
# 用法：bash S0_qemu_gdb_debug.sh
# 然后另一个终端：gdb /usr/lib/debug/boot/vmlinuz-$(uname -r)
# (gdb) target remote localhost:1234
set -e
QEMU="${QEMU:-/usr/local/bin/qemu-system-x86_64}"
KERNEL="/boot/vmlinuz-$(uname -r)"
INITRD="/boot/initrd.img-$(uname -r)"
DISK="${DISK:-/tmp/virtpcitest.qcow2}"
$QEMU \
    -M virt -m 512M \
    -kernel "$KERNEL" -initrd "$INITRD" \
    -append "console=ttyS0 root=/dev/sda ro" \
    -drive file="$DISK",format=qcow2 \
    -nographic -enable-kvm \
    -s -S   # -s: GDB on 1234; -S: 等待
echo "[S0] QEMU 等待 GDB 连接 localhost:1234"
