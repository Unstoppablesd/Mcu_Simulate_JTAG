# N32G43x CMSIS-DAP v2 调试器 — 开发文档

## 项目概览

用 N32G435RB 开发板实现 USB CMSIS-DAP v2 调试器，通过 openFPGALoader 烧录 Gowin GW1N-2 FPGA。

### 硬件引脚

| 信号 | GPIO | 方向 |
|------|------|------|
| TCK  | PB3  | 输出 |
| TMS  | PB4  | 输出 |
| TDI  | PB5  | 输出 |
| TDO  | PB6  | 输入 |
| SRST | PB7  | 输出 |
| USB DP | PA12 | USB |
| USB DM | PA11 | USB |

### 关键参数

- MCU: N32G435RB (Cortex-M4, 128KB Flash, 32KB SRAM)
- 系统时钟: HSI_PLL 96MHz → USB 48MHz (PLL/2)
- USB: Full Speed 12Mbps, HID 设备
- VID/PID: 0x0D28 / 0x0204 (ARM mbed CMSIS-DAP)
- RT-Thread: 3.1.4, tick 100Hz
- CherryUSB: HID 设备栈

---

## 编译方法

### 环境要求

- Keil MDK-ARM v5 + N32G43x DFP 包
- 或 ARM GCC + Makefile

### Keil MDK 编译

1. 打开 `project/CMSIS_DAPv2/MDK-ARM/CMSIS_DAPv2.uvprojx`
2. 确认 Device 为 `N32G435RB`
3. 编译 (F7)
4. 烧录 (F8)
5. 输出文件: `MDK-ARM/Objects/CMSIS_DAPv2.axf` 和 `MDK-ARM/bin/CMSIS_DAPv2.bin`

### 包含路径 (Keil C/C++ 设置)

```
firmware\CMSIS\core
firmware\CMSIS\device
firmware\n32g43x_std_periph_driver\inc
middlewares\rt-thread\include
middlewares\rt-thread\components\drivers\include
middlewares\rt-thread\components\drivers\cherryusb\core
middlewares\rt-thread\components\drivers\cherryusb\common
middlewares\rt-thread\components\drivers\cherryusb\port\fsdev
middlewares\rt-thread\components\drivers\cherryusb\class\hid
project\CMSIS_DAPv2\inc
project\DeviceDrivers\gpio\inc
project\DeviceDrivers\uart\inc
```

### 预定义宏

```
N32G43X, USE_STDPERIPH_DRIVER, SYSCLK_FREQ=96000000
```

---

## 项目结构

```
project/CMSIS_DAPv2/
├── inc/
│   ├── board.h          # SRAM 配置
│   ├── dap_main.h       # CMSIS-DAP v2 协议常量
│   ├── jtag_driver.h    # JTAG GPIO 驱动接口
│   ├── main.h           # 系统时钟宏
│   ├── n32g43x_it.h     # 中断声明
│   └── rtconfig.h       # RT-Thread 内核配置
├── src/
│   ├── board.c          # 板级初始化
│   ├── dap_main.c       # CMSIS-DAP v2 协议处理
│   ├── jtag_driver.c    # JTAG GPIO 位操作
│   ├── main.c           # 入口 + USB 初始化
│   ├── n32g43x_it.c     # 中断处理
│   └── usb_descriptor.c # USB HID 描述符
├── MDK-ARM/
│   └── CMSIS_DAPv2.uvprojx
└── readme.txt
```

---

## 调试流程 (2026-06-26)

### 第一阶段：设备枚举

**现象**: Windows 设备管理器显示 "CMSIS-DAP V2 (N32G43x)" 但 openFPGALoader 报 `cmsisDAP2 not found`

**根因**: openFPGALoader 使用 **HID 传输** (hidapi)，而非 WinUSB Bulk。

**修复**: 将 USB 描述符从 WinUSB Bulk (class 0xFF) 改为 HID (class 0x03)，端点类型从 Bulk (0x02) 改为 Interrupt (0x03)。

```
接口类: 0x03 (HID)
子类:   0x00 (None)
协议:   0x00 (None)
端点0x02: Interrupt OUT, 64 bytes, 1ms
端点0x81: Interrupt IN,  64 bytes, 1ms
HID Report: 64 字节 vendor-defined Input + Output
```

**涉及文件**: `usb_descriptor.c`, `main.c`, `MDK-ARM/CMSIS_DAPv2.uvprojx`

### 第二阶段：HID 报告描述符

**现象**: 设备管理器代码 10 — "HID 报表描述符未通过验证"

**根因**: HID Report Descriptor 中 INPUT/OUTPUT 主项缺少对应的 USAGE。

**修复**: 在 Collection 内部每个 INPUT/OUTPUT 前添加 USAGE：
```
USAGE (Vendor 2) → INPUT (64 bytes)
USAGE (Vendor 3) → OUTPUT (64 bytes)
```
描述符从 30 字节变为 34 字节。

**涉及文件**: `usb_descriptor.c`

### 第三阶段：String 描述符长度

**现象**: 串口日志 `[E/USB] descriptor <type:3,index:3> not found!`

**根因**: String 2 (Product) 的 `bLength` 写成了 `0x24` (36字节)，实际 "CMSIS-DAP v2 (N32G43x)" 有 22 个字符 = 46 字节 (`0x2E`)。

**修复**: `bLength` 改为 `0x2E`。

**涉及文件**: `usb_descriptor.c`

### 第四阶段：CMSIS-DAP 协议命令 ID

**现象**: 设备能枚举但 openFPGALoader 报 `Error timeout` + `JTAG is not supported by the probe`

**根因 1**: HID 报告必须正好 64 字节，我们只发了几个字节。

**修复 1**: `usbd_hid_out_handler` 中始终 `usbd_ep_write(HID_IN_EP, resp_buffer, 64, NULL)`

**根因 2**: DAP_INFO 响应格式不匹配 openFPGALoader。openFPGALoader 的 `read_info` 调用 `xfer(2, _buffer, 63)` → `memmove(_buffer, _ll_buffer, 63)` 产生 2 字节偏移 (`_buffer = _ll_buffer + 2`)。

**修复 2**: DAP_INFO 响应改为标准 CMSIS-DAP 格式 `[len_lo][len_hi][data...]`（2 字节 header，不是 4 字节）。

**涉及文件**: `main.c`, `dap_main.c`

### 第五阶段：命令 ID 对齐

**现象**: DAP_INFO 通过后，报 `TDO is stuck at 0`

**根因**: openFPGALoader 的 CMSIS-DAP 命令 ID 和我们定义的不同：

| 功能 | openFPGALoader | 我们原来 | 
|------|----------------|----------|
| 时钟设置 | 0x11 (SWJ_CLK) | 0x11 (JTAG_CONFIGURE) |
| TMS 序列 | 0x12 (SWJ_SEQUENCE) | 0x12 (JTAG_IDCODE) |
| JTAG 操作 | 0x14 (JTAG_SEQUENCE) | 0x10 (JTAG_SEQUENCE) |

**修复**: 完全重写命令处理器，对齐 openFPGALoader 的协议。

新增处理函数:
- `dap_handle_hoststatus` (0x01)
- `dap_handle_swj_clk` (0x11) — 设置 JTAG 时钟
- `dap_handle_swj_sequence` (0x12) — TMS 序列
- `dap_handle_jtag_sequence_14` (0x14) — JTAG TDI/TDO/TMS 位操作

删除旧的:
- `dap_handle_led`, `dap_handle_jtag_sequence`, `dap_handle_jtag_configure`
- `dap_handle_jtag_idcode`, `dap_handle_jtag_device_count`

**涉及文件**: `dap_main.h`, `dap_main.c`, `jtag_driver.h`, `jtag_driver.c`

### 第六阶段：IDCODE 读取出错 ✅ 关键修复

**现象**: openFPGALoader 报 `Unknown device with IDCODE: 0x681b0002`，期望 `0x0120681B`

**调试过程**:
1. 添加 UART 打印原始 TDO 字节: `02 00 1b 68`
2. 发现 `1b 68` 是正确的 IDCODE 低 16 位，但有 2 字节前导 `02 00`
3. 尝试改采样边沿、JTAG 时钟等，前导字节始终存在
4. **最终发现**: `resp_idx` 初始化为 2，但 `resp_data = &resp_buffer[2]`，TDO 数据写入 `resp_data[resp_idx++]` = `resp_buffer[2 + 2]` = `resp_buffer[4]`，偏移了 2 字节！
5. `resp_buffer[2]` 和 `resp_buffer[3]` 是上次命令的残留数据

**修复**:
```c
// 修复前 (BUG):
uint32_t resp_idx   = 2;
uint8_t *resp_data  = &resp_buffer[2];
resp_data[resp_idx++] = tdo_byte;  // 写入 resp_buffer[4]!

// 修复后:
uint32_t resp_idx   = 0;   // 相对于 resp_data
uint8_t *resp_data  = &resp_buffer[2];
resp_data[resp_idx++] = tdo_byte;  // 写入 resp_buffer[2] ✓
*resp_len = resp_idx + 2;  // 总长度 = 2 字节 header + TDO 数据
```

**涉及文件**: `dap_main.c`

### 第七阶段：JTAG 时钟公式

**现象**: JTAG 时序偶尔不稳定

**根因**: `jtag_set_clock()` 公式 `48 / clk_khz` 少约 1000 倍。请求 100kHz 时 `48/100 = 0 → 1 NOP`，实际 JTAG ≈ 50MHz。

**修复**: 改为 `10000 / clk_khz`，最小 2。

```
100kHz: 10000/100 = 100 NOPs → ~5μs 半周期 ✓
1MHz:   10000/1000 = 10 NOPs → ~520ns 半周期 ✓
```

**涉及文件**: `jtag_driver.c`

---

## 验证结果

```bash
# 检测设备
$ ./openFPGALoader.exe -c cmsisdap --detect
index 0:
        idcode 0x120681b       # ← 正确！
        manufacturer Gowin
        family GW1N
        model  GW1N-2
        irlength 8

# 烧录 SRAM
$ ./openFPGALoader.exe -c cmsisdap lm910.fs
Load SRAM: [==================================================] 100.00%
CRC check: Success

# 烧录 Flash
$ ./openFPGALoader.exe -c cmsisdap -f lm910.fs
```

---

## PC 端工具

- **openFPGALoader**: 需要从源码编译启用 CMSIS-DAP 支持
  ```bash
  git clone https://github.com/trabucayre/openFPGALoader
  cd openFPGALoader && mkdir build && cd build
  cmake -D ENABLE_CMSISDAP=ON ..
  make
  ```
- **pyOCD**: `pip install pyocd` → `pyocd list`
- **OpenOCD**: 支持 CMSIS-DAP 接口

---

## 经验教训

1. **调试时加 UART 日志非常关键** — 打印原始 TDO 字节直接揭示了偏移问题
2. **理解 host 端工具的数据读取方式** — openFPGALoader 的 `memmove(_buffer, _ll_buffer, 63)` 2 字节偏移是理解 DAP_INFO 格式的关键
3. **索引/偏移 bug 最难找** — `resp_idx` 从 2 开始但 `resp_data` 指向 `resp_buffer+2`，双重偏移
4. **HID vs WinUSB** — 不同工具支持不同的 CMSIS-DAP 传输层，openFPGALoader 只支持 HID
5. **时钟公式需要校准** — 软件 NOP 延迟要计入循环开销（不是 1 cycle/NOP）
