# Wing Fold Actuator Firmware

STM32F103C8T6（Blue Pill）机翼折叠丝杆执行器固件。当前 Task 1 仅提供
可编译的 STM32 HAL/CMake 空工程和外设初始化骨架；控制、舵机和编码器逻辑
将在后续任务中实现。

## 引脚与初始配置

| 功能 | 外设 / 引脚 | 初始配置 |
| --- | --- | --- |
| 飞控 PWM 输入 | TIM2_CH1 / PA0 | 输入捕获，1 us 计数分辨率 |
| HTD-85H 舵机总线 | USART1 / PA9 TX、PA10 RX | 115200，8N1；外接半双工缓冲 |
| BRT38 编码器 | USART2 / PA2 TX、PA3 RX | 9600，8N1；最终协议参数以后续手册核对为准 |
| 调试 CLI | USART3 / PB10 TX、PB11 RX | 115200，8N1 |
| 状态 LED | PC13 | 推挽输出，当前固件每 500 ms 翻转 |
| 调试接口 | PA13 SWDIO、PA14 SWCLK | Serial Wire |

时钟使用 Blue Pill 常见的 8 MHz HSE，经 PLL 倍频至 72 MHz。

## 供电与接线

- 舵机功率端使用独立 VBAT，不能由 MCU 3.3 V 引脚供电。
- MCU 逻辑侧保持 3.3 V；舵机单线总线应通过合适的半双工缓冲器连接。
- 舵机电源、MCU 和飞控必须共地。
- 上电前确认外设逻辑电平与接口类型，尤其是编码器是否需要额外收发器。

## 编译

要求 `cmake`、Ninja 和 Arm GNU Toolchain（`arm-none-eabi-*`）已加入 `PATH`：

```powershell
cmake --preset Debug
cmake --build --preset Debug
```

输出文件：

- `build/Debug/wing_fold_actuator.elf`
- `build/Debug/wing_fold_actuator.hex`

## 烧录

使用 ST-Link 与 SWD 接口连接后，可任选一种方式烧录：

```powershell
STM32_Programmer_CLI -c port=SWD -w build/Debug/wing_fold_actuator.hex -v -rst
```

也可使用 OpenOCD：

```powershell
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg `
  -c "program build/Debug/wing_fold_actuator.elf verify reset exit"
```

硬件烧录不是 Task 1 的验收门槛。
