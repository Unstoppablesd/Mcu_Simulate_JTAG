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

## 原理问答

### Q: 为什么 FPGA 可以走 CMSIS-DAP 协议烧录？它不是给 ARM 调试用的吗？

CMSIS-DAP 本质上是一个**通用 JTAG 位操作传输协议**，不限定目标设备类型。

```
openFPGALoader (PC)
    │  CMSIS-DAP 协议 (USB HID)
    │  命令: SWJ_CLK, SWJ_SEQUENCE, JTAG_SEQUENCE
    ▼
N32G43x (CMSIS-DAP 固件)
    │  纯 GPIO 位操作
    │  TCK 高低、TMS 设置、TDI 输出、TDO 读取
    ▼
GW1N FPGA (JTAG 接口)
```

**CMSIS-DAP 固件只做 3 件事：**
1. 收到 `JTAG_SEQUENCE` 命令 → 按 TMS 值翻转 TCK，按 TDI 数据输出，捕获 TDO
2. 收到 `SWJ_SEQUENCE` 命令 → 按 TMS 序列翻转 TCK（导航 TAP 状态机）
3. 收到 `SWJ_CLK` 命令 → 设置时钟频率

**它不知道也不关心目标是什么芯片。**

**PC 端才是"大脑"** — openFPGALoader 内置了各厂家的 JTAG 编程算法：

```
Gowin SRAM 烧录序列 (由 openFPGALoader 生成):
  IR ← 0x11 (配置指令)
  DR ← 擦除命令
  IR ← 0x12 (数据指令)
  DR ← 码流数据 (lm910.fs)
  IR ← 0x13 (完成指令)
  DR ← 启动命令
```

每一步都翻译成 CMSIS-DAP 的 TMS/TDI/TDO 序列，通过 USB 发给固件，固件纯粹"执行"这些 GPIO 操作。

**类比**：CMSIS-DAP 就像 JTAG 的"远程控制"——PC 想怎么翻 TCK/TMS/TDI 都可以，N32G43x 只是个听话的执行器。所以 ARM 单片机、RISC-V、FPGA，只要是 JTAG 接口都能用。

---

### Q: N32G43x 固件具体通过 USB 实现了哪些步骤？

#### 一、USB 层面：让 PC 认识我们

**把自己注册成 HID 设备**（`usb_descriptor.c`）：

PC 插上 USB 线后会问："你是个啥？" 我们回答：

```
我叫 "CMSIS-DAP v2 (N32G43x)"
我是 HID 设备（人体学输入设备）
我有两个数据通道：
  - 通道 0x02：PC → 我（接收命令）
  - 通道 0x81：我 → PC（返回结果）
每个通道一次传 64 字节
```

**数据收发**（`main.c`）：

```
PC 发来 64 字节 HID 报告
    ↓
CherryUSB 收进缓冲区
    ↓
调用 usbd_hid_out_handler()  ← 我们的回调
    ↓
解析命令 → 执行操作 → 准备好 64 字节响应
    ↓
usbd_ep_write() 发回 PC
```

#### 二、协议层面：理解 PC 在说什么

PC 通过 USB 发来的是一条条 **CMSIS-DAP 命令**（`dap_main.c`），共 10 种：

| 命令 | 编号 | PC 在说什么 | 我们做什么 |
|------|------|-------------|------------|
| `INFO` | 0x00 | "你是谁？支持 JTAG 吗？" | 返回版本号、能力字 |
| `HOSTSTATUS` | 0x01 | "我连上了/断开了" | 确认收到 |
| `CONNECT` | 0x02 | "启动 JTAG，准备干活" | 初始化 GPIO，复位 TAP |
| `DISCONNECT` | 0x03 | "干完了，收工" | 标记断开 |
| `WRITE_ABORT` | 0x08 | "紧急停止当前操作" | 确认（当前为空操作） |
| `DELAY` | 0x09 | "等 X 微秒" | 软件延时 |
| `RESET_TARGET` | 0x0A | "复位目标芯片" | 拉低 SRST，等 10ms，释放 |
| `SWJ_CLK` | 0x11 | "JTAG 时钟跑 X MHz" | 设置 NOP 延迟参数 |
| `SWJ_SEQUENCE` | 0x12 | "按这些 TMS 值翻转 TCK" | 纯 GPIO 翻转 |
| `JTAG_SEQUENCE` | 0x14 | "TMS=X, TDI=数据, 捕获 TDO 返回" | GPIO 翻转 + 读 TDO |

#### 三、物理层面：用 GPIO 模拟 JTAG

这是最核心的工作（`jtag_driver.c` + `dap_main.c` 里的 bit-banging 循环）：

**例：PC 要读 FPGA 的 IDCODE**

PC 发来一串命令序列：

```
步骤1: SWJ_SEQUENCE { TMS: 1,1,1,1,1, 0, 1,0,0 }  ← 9 个 TCK
       含义：复位 TAP → 进入 Shift-DR

步骤2: JTAG_SEQUENCE { TMS=0, TDI=全1, 发送32个TCK, 捕获TDO }
       含义：在 Shift-DR 状态移动 32 位，读回 TDO
```

**步骤1 代码做的事：**

```c
for 每个 TMS bit:
    if (TMS_bit == 1) PB4 输出高;
    else              PB4 输出低;

    延时;              // 等信号稳定
    PB3 输出高;        // TCK 上升沿（FPGA 采样 TMS）
    延时;
    PB3 输出低;        // TCK 下降沿
```

**步骤2 代码做的事：**

```c
for 32 次:
    if (TDI_bit == 1) PB5 输出高;  // 设置 TDI
    else              PB5 输出低;

    PB4 输出低;  // TMS=0（保持在 Shift-DR）

    延时;
    PB3 输出高;  // TCK 上升沿
    延时;

    读 PB6 引脚 → 这是 TDO，FPGA 从 IDCODE 寄存器移出的 1 bit
    存入响应缓冲区

    PB3 输出低;  // TCK 下降沿
```

32 次循环后，响应缓冲区就有 4 字节：`1B 68 20 01`，发回 PC，PC 算出 `0x0120681B` = GW1N-2。

#### 四、数据流全景

```
PC (openFPGALoader)
  │  "读 IDCODE" 翻译成 TMS + TDI bit 序列
  │  打包成 CMSIS-DAP JTAG_SEQUENCE 命令
  │
  ▼ USB HID 64 字节包
┌──────────────────────────┐
│  N32G43x 固件             │
│                          │
│  CherryUSB 收到数据       │
│       ↓                  │
│  usbd_hid_out_handler()  │ ← main.c
│       ↓                  │
│  dap_process_command()   │ ← dap_main.c (协议)
│       ↓                  │
│  根据 cmd 编号分派:       │
│    0x14 → JTAG_SEQUENCE  │
│       ↓                  │
│  for 每 bit:             │
│    设 PB5 (TDI)           │
│    设 PB4 (TMS)           │
│    翻 PB3 (TCK)           │
│    读 PB6 (TDO)           │ ← 纯 GPIO 操作
│       ↓                  │
│  收集 32 bit → 4 字节     │
│       ↓                  │
│  usbd_ep_write() 回 PC   │ ← 64 字节 HID 报告
└──────────────────────────┘
  │
  ▼ USB HID 64 字节包
PC (openFPGALoader)
  │  收到 TDO 字节 → 算出 IDCODE
  │  匹配数据库 → "GW1N-2"
  │
  │  继续发更多的 JTAG 命令...
  │  (擦除 SRAM → 写码流 → CRC 校验)
  ▼
烧录完成 ✅
```

**一句话总结**：N32G43x 就是一个 **USB 转 GPIO 的桥**。不缓存、不解析目标芯片、不知道 JTAG 状态机——那些全是 PC 端的活。

---

## 经验教训

1. **调试时加 UART 日志非常关键** — 打印原始 TDO 字节直接揭示了偏移问题
2. **理解 host 端工具的数据读取方式** — openFPGALoader 的 `memmove(_buffer, _ll_buffer, 63)` 2 字节偏移是理解 DAP_INFO 格式的关键
3. **索引/偏移 bug 最难找** — `resp_idx` 从 2 开始但 `resp_data` 指向 `resp_buffer+2`，双重偏移
4. **HID vs WinUSB** — 不同工具支持不同的 CMSIS-DAP 传输层，openFPGALoader 只支持 HID
5. **时钟公式需要校准** — 软件 NOP 延迟要计入循环开销（不是 1 cycle/NOP）

---

## TODO / 待办事项

### 高优先级

- [ ] **USB 回调线程化** — 目前 `usbd_hid_out_handler` 在中断/UAC 回调上下文中直接处理 CMSIS-DAP 命令（含 JTAG 位操作），可能阻塞 USB 中断。改为信号量通知独立线程：

```
当前架构：
  USB 中断 → usbd_hid_out_handler → dap_process_command (含 GPIO 操作)

改进架构：
  USB 中断 → usbd_hid_out_handler → 收数据 → rt_sem_release()
  dap_thread → rt_sem_take() → dap_process_command (GPIO 在线程上下文)
```

- [ ] **LED 指示线程** — 添加连接状态 LED（PB 其他引脚或 PA8）
- [ ] **烧录速度优化** — 当前 ~100kHz JTAG，SRAM 466 字节用时正常，大码流可能需要提速

### 低优先级

- [ ] **SWD 支持** — 当前仅 JTAG，添加 SWD 可兼容更多 ARM 目标
- [ ] **CMSIS-DAP v1 HID 模式** — 兼容旧版工具
- [ ] **pyOCD / OpenOCD 兼容性测试**
- [ ] **openFPGALoader IDCODE 字节序 bug 上报** — `0x681B0002` vs `0x0120681B` 是固件 bug，已修复，可向 openFPGALoader 提交 Gowin IDCODE 补充（如果需要）

---

## RT-Thread 学习路径

> 基于本项目已有的 RT-Thread 使用经验，推荐学习路径如下。

### 已掌握 ✓

- `board.c` — SysTick 心跳、堆内存设置、中断向量表
- `rtconfig.h` — 内核裁剪
- `rt_thread_init()` + `rt_thread_startup()` — 静态线程创建
- `rt_thread_delay()` — 延时
- `rt_kprintf()` — 串口输出

### 下一步重点：线程间通信（IPC）

| 机制 | 用途 | 本项目可应用的场景 |
|------|------|-------------------|
| 信号量 `rt_sem` | 线程同步、事件通知 | USB 收到数据 → 释放信号量 → dap_thread 醒来处理 |
| 互斥锁 `rt_mutex` | 保护共享资源 | 多个线程访问 JTAG GPIO 时的互斥 |
| 消息队列 `rt_mq` | 线程间传数据 | dap_thread 把 JTAG 命令放入队列，jtag_thread 取出执行 |

### 之后：设备驱动框架

```c
rt_device_t dev = rt_device_find("uart1");
rt_device_open(dev, RT_DEVICE_FLAG_RDWR);
rt_device_write(dev, 0, "hello", 5);
```

本项目已用 `rt_console_set_device("usart1")` 注册控制台。可进一步把 `jtag_driver.c` 封装成 RT-Thread 设备模型。

### 再之后：组件生态

```
CherryUSB (已用) → FATFS (文件系统) → lwIP (TCP/IP) → ...
```

### 推荐实践

先把 TODO 列表里的**"USB 回调线程化"**做了——这个改动小（20 行代码），但能让你真正理解：
1. 信号量的 create / take / release 生命周期
2. 中断上下文 vs 线程上下文的区别
3. 为什么 RTOS 里不应该在中断回调里做复杂操作
