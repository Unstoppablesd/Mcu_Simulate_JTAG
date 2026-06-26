CMSIS-DAP v2 Debugger for N32G43x
===================================

Project: CMSIS-DAP v2 USB Debugger with JTAG support
Target:  N32G435RB (Cortex-M4, 128KB Flash, 32KB SRAM)
IDE:     Keil MDK-ARM v5

Features:
- USB CMSIS-DAP v2 protocol (Bulk endpoints)
- WinUSB compatible (WCID descriptors, driverless on Windows 8+)
- JTAG debug interface (TCK/TMS/TDI/TDO on GPIOB)
- RT-Thread real-time operating system
- CherryUSB device stack
- UART console output (USART1)
- Clock: HSI PLL 96MHz (USB = 48MHz via PLL/2)

Pin Assignments:
  JTAG:
    TCK  - PB3 (output)
    TMS  - PB4 (output)
    TDI  - PB5 (output)
    TDO  - PB6 (input)
    SRST - PB7 (output, open-drain)

  USB:
    DP   - PA12
    DM   - PA11

  UART (console):
    TX   - PA9  (USART1)
    RX   - PA10 (USART1)

Build:
  1. Open MDK-ARM\CMSIS_DAPv2.uvprojx in Keil MDK
  2. Build (F7)
  3. Flash to target (F8)

PC Tools:
  - openFPGALoader: openFPGALoader -c cmsisDAP2 <bitstream.fs>
  - pyOCD: pyocd commander -t n32g43x
  - OpenOCD: openocd -f interface/cmsis-dap.cfg

References:
  - CMSIS-DAP v2 Protocol: https://arm-software.github.io/CMSIS_5/DAP/html/
  - CherryUSB: https://github.com/cherry-embedded/CherryUSB
  - WinUSB: https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/winusb
