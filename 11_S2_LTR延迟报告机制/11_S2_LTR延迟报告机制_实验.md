# S2_06 — LTR 延迟报告机制：实验手册

## 实验环境

- **平台**：QEMU x86_64 virt（或物理机）
- **内核**：6.17.0（已编译的内核源码）
- **工具**：setpci、lspci、内核模块编译环境

---

## 实验1：使用 setpci 读取 LTR 扩展能力

### 目标

验证目标设备的 LTR 扩展能力结构是否可访问，读出 Max_Snoop / Max_NoSnoop 延迟值。

### 步骤

```bash
# 1. 查看当前系统的 PCIe 设备，找到你想测试的设备
# （物理机通常选择 00:02.0 或 01:00.0 这样的 PCIe 设备）
lspci -nn | grep -i "PCIe\|SSD\|Ethernet" | head -10

# 2. 找到设备的 BDF，然后找它的 PCIe Extended Capability
#    LTR Extended Capability ID = 0x18
#    用 setpci 遍历扩展能力查找：
#    PCIe 配置空间头部：0x100 开始是第一个扩展能力

# 示例：读取设备 00:02.0 的 DEVCAP2 和 DEVCTL2（标准 PCI Express Capability）
sudo setpci -s 00:02.0 CAP_EXP+0x0c.w   # DEVCAP2（偏移 0x0c from PCIe Capability）
sudo setpci -s 00:02.0 CAP_EXP+0x08.w   # DEVCTL2（偏移 0x08 from PCIe Capability）

# 3. 解析 DEVCAP2[11]（LTR 支持位）
#    DEVCAP2_W = 你上面读到的值
#    bit 11 = (DEVCAP2_W >> 11) & 1
python3 -c "v=int('0xDEVCAP2值',16); print('LTR supported:', bool((v>>11)&1))"

# 4. 解析 DEVCTL2[10]（LTR 启用位）
#    DEVCTL2_W = 你上面读到的值
#    bit 10 = (DEVCTL2_W >> 10) & 1
python3 -c "v=int('0xDEVCTL2值',16); print('LTR enabled:', bool((v>>10)&1))"

# 5. 找 LTR 扩展能力偏移（扩展能力 ID=0x18）
#    读取 PCIe Capability List 指针（配置空间 0x34）
LST_PTR=$(sudo setpci -s 00:02.0 34.b)
echo "PCI Capability List Pointer = 0x${LST_PTR}"

# 6. 遍历扩展能力直到找到 ID=0x18
#    扩展能力结构：Offset +0 = Header(Dword): {ID(16bit), Version(4bit), Next(12bit)}
#    然后 +4 = Max_Snoop_Latency, +8 = Max_NoSnoop_Latency
```

### 预期结果

- DEVCAP2[11] = 1 表示设备支持 LTR
- DEVCTL2[10] = 1 表示 LTR 已启用
- LTR 扩展能力偏移处可读出两个延迟值

### 验收标准

- [ ] 能够用 setpci 读出 DEVCAP2 和 DEVCTL2 的值
- [ ] 能用 Python 脚本正确解析 bit11（LTR 支持）和 bit10（LTR 启用）
- [ ] 如果设备支持 LTR，记录 Max_Snoop 和 Max_NoSnoop 值

---

## 实验2：编译并运行用户态 LTR 解码工具

### 目标

编译 `S2_06_ltr_decode.c`，运行后输出格式化的 LTR 状态报告。

### 步骤

```bash
# 1. 进入本节代码目录
cd ~/work/code/PCIe_driver_learning/11_S2_LTR延迟报告机制/

# 2. 安装 libpci-dev（如果还没装）
sudo apt install libpci-dev -y

# 3. 编译用户态工具
make usertool

# 4. 以 root 权限运行（setpci / PCI config access 需要 root）
#    默认检查 00:02.0，也可以指定其他设备
sudo ./ltr_decode 00:02.0

# 5. 检查 QEMU 虚拟机的 PCIe RC 设备
#    QEMU virt 的 RC 通常在 00:02.0
sudo ./ltr_decode 00:02.0
```

### 预期结果

```
=== LTR (Latency Tolerance Reporting) Analysis ===
Device: 00:02.0

[1] LTR Capability Check
  Device supports LTR: YES/NO (DEVCAP2[11] = ?)

[2] LTR Enable Status
  LTR Enabled:         YES/NO (DEVCTL2[10] = ?)

[3] LTR Extended Capability
  LTR Cap Offset:      0x??

[4] Latency Values
  Max_Snoop:    raw=0x????, value=???, scale=?, latency=??? ns
  Max_NoSnoop:  raw=0x????, value=???, scale=?, latency=??? ns

[5] Practical Interpretation
  -> ...
```

### 验收标准

- [ ] `ltr_decode` 成功编译，无链接错误
- [ ] `sudo ./ltr_decode` 能运行并输出报告
- [ ] 延迟值解码正确（scale × value 计算验证正确）

---

## 实验3：编译并加载内核模块 `ltr_kernel_check`

### 目标

加载内核模块，遍历系统所有 PCIe 设备并打印 LTR 支持/启用状态/路径。

### 步骤

```bash
# 1. 确保内核 headers 已安装
dpkg -l | grep linux-headers-$(uname -r) || \
  sudo apt install linux-headers-$(uname -r) -y

# 2. 编译内核模块
make modules

# 3. 加载模块（会触发遍历所有 PCIe 设备）
sudo insmod S2_06_ltr_kernel_check.ko

# 4. 查看内核日志
dmesg | grep LTR

# 5. 也可以看完整输出
dmesg | tail -50

# 6. 卸载模块
sudo rmmod S2_06_ltr_kernel_check
```

### 预期结果

内核日志中每行格式：
```
[  XXX] pci 0000:00:02.0: LTR: supports=1 enabled=1 ltr_path=1 [0000:00:02.0]
```

### 验收标准

- [ ] `make modules` 成功，生成 `.ko` 文件
- [ ] `insmod` 成功加载，无 module 错误
- [ ] `dmesg | grep LTR` 输出了设备 LTR 状态
- [ ] 能区分 supports=1 但 enabled=0 的设备

---

## 实验4（可选）：QEMU 中启用 LTR 并验证

### 目标

在 QEMU 模拟器中添加支持 LTR 的 PCIe 设备，验证 LTR 功能。

### 步骤

```bash
# 1. 启动 QEMU，启用 PCIe RC + LTR 支持的虚拟机
#    -device pcie-root-port 会在 DEVCTL2 默认启用 LTR
qemu-system-x86_64 \
  -M q35 \
  -kernel /home/kly/work/code/linux-source/arch/x86/boot/bzImage \
  -initrd /tmp/initrd.img \
  -append "console=ttyS0 root=/dev/sda1" \
  -hda /tmp/qemu-test.img \
  -net none \
  -nographic \
  -device pcie-root-port,id=rp1 \
  -device ich9-intel-hda \
  -device pcie-hdmi

# 2. 在 QEMU 虚拟机内运行 lspci 检查
lspci -nn -vv | grep -i ltr

# 3. 运行 ltr_decode 工具
sudo ./ltr_decode 00:02.0
```

### 验收标准

- [ ] QEMU 虚拟机正常启动
- [ ] lspci 能看到 PCIe 设备
- [ ] LTR 相关输出正常

---

## 实验5：验证 ltr_path 传播机制（观察 RC vs EP）

### 目标

观察 RC（Root Port）和其他 PCIe 设备的 ltr_path 差异，理解路径验证逻辑。

### 步骤

```bash
# 加载内核模块
cd ~/work/code/PCIe_driver_learning/11_S2_LTR延迟报告机制/
make modules
sudo insmod S2_06_ltr_kernel_check.ko

# 查看 dmesg，区分 RC 设备和 EP 设备
# RC（Root Port）的 ltr_path=1（作为路径起点）
# Switch Downstream Port 的 ltr_path 取决于 RC 是否启用
# EP 设备的 ltr_path 取决于整条路径是否都通过

# 用 lspci -t 看树状拓扑
lspci -t

# 用 lspci -vv 看设备详细信息
# 找 Device Capabilities 里的 LTR 支持位
lspci -vv -s 00:02.0 | grep -i "latency\|LTR\|L0s\|L1"
```

### 预期结果

```
# lspci -t 输出示例
-[0000:00]-+-00.0  Root Complex (ltr_path=1)
           +-02.0  Root Port (ltr_path=1)
           \-01:00.0 Ethernet Controller EP (ltr_path=1 or 0 depending on path)
```

### 验收标准

- [ ] 能正确区分 RC 和 EP 的 ltr_path 值
- [ ] 如果 EP ltr_path=0，能分析原因（RC 未启用 / Switch 不支持）

---

## 实验6：思考题动手验证

### Q1 延迟值计算验证

```python
# 用 Python 计算：Max_NoSnoop = 0x200, Scale = 2 时，延迟是多少 ms？
python3 -c "
value = 0x200  # 512
scale = 2     # ×1024 ns
latency_us = value * (32 if scale==1 else (1024 if scale==2 else 0))
latency_ms = latency_us / 1000.0
print(f'Max_NoSnoop = 0x{value:03x}, scale={scale}, latency={latency_ms:.1f} ms')
"
```

### Q2 LTR 路径验证逻辑

```bash
# 思考：如果 RC 没有启用 LTR，但 EP 支持，EP 的 ltr_path 是多少？
# 在模块代码中找答案：pci_configure_ltr() 的逻辑

# 查看内核源码
grep -A5 'ltr_path = 1' ~/work/code/linux-source/drivers/pci/pcie/aspm.c | head -20
```
