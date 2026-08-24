# Wing Fold Actuator Firmware

STM32F103C8T6（Blue Pill）机翼折叠丝杆执行器固件。主循环每 10 ms
执行一次 PWM 目标映射、绝对编码器反馈和舵机速度闭环，并提供串口 CLI
用于标定、状态检查和手动点动。

## 引脚与初始配置

| 功能 | 外设 / 引脚 | 初始配置 |
| --- | --- | --- |
| 飞控 PWM 输入 | TIM2_CH1 / PA0 | 输入捕获，1 us 计数分辨率 |
| HTD-85H 舵机总线 | USART1 / PA9 TX、PA10 RX | 115200，8N1；外接半双工缓冲 |
| BRT38 编码器 | USART2 / PA2 TX、PA3 RX | 9600，8N1；最终协议参数以后续手册核对为准 |
| 调试 CLI | USART3 / PB10 TX、PB11 RX | 115200，8N1 |
| 状态 LED | PC13 | 推挽输出；RUN 500 ms、HOLD 1000 ms、FAULT 125 ms 翻转 |
| 调试接口 | PA13 SWDIO、PA14 SWCLK | Serial Wire |

时钟使用 Blue Pill 常见的 8 MHz HSE，经 PLL 倍频至 72 MHz。

## 控制与安全行为

- PWM 1000–2000 us 线性映射到标定端点 `count_a`–`count_b`。
- 默认参数：死区 20 count、`kp=500`（比例尺 1000）、`vmax=800`、
  PWM 超时 150 ms。
- 上电先发送停转命令；无有效 PWM 或 PWM 超时后进入 HOLD，速度命令为 0。
- 编码器连续读取失败达到 5 次后立即停转并进入 FAULT；读取恢复后退出故障。
- CLI `motor <spd>`（范围 -500–500）进入 MANUAL，闭环暂停写速度；
  `hold` 或 5 s 超时后返回 AUTO。
- Flash 链接区长度为 63 KiB，末尾 1 KiB 页 `0x0800FC00` 专用于标定 NVM。

## CLI

USART3（115200 8N1）支持：

```text
cal a | cal b | cal save | cal show
status
motor <spd>
hold
help
```

先用 `motor` 点动到两个机械端点，依次执行 `cal a`、`cal b` 和
`cal save`。机械端点附近应低速操作，并保留断电或急停手段。

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

## 实机参数

> **状态：台架/实机验收待完成。** 下列标定值须在实物台架完成后填写；勿使用占位值上机。

| 参数 | 首版默认 | 台架实测 | 备注 |
| --- | --- | --- | --- |
| `count_a`（PWM 1000 µs 端点） | — | **TBD** | `cal a` + `cal save` 后填入 |
| `count_b`（PWM 2000 µs 端点） | — | **TBD** | `cal b` + `cal save` 后填入 |
| 死区（count） | 20 | **TBD** | 到位停稳后微调 |
| `kp`（×1000 比例尺） | 500 | **TBD** | 过冲/振荡时降低 |
| `vmax` | 800 | **TBD** | 装丝杆首测建议先降至 400 |
| PWM 超时（ms） | 150 | 150 | 与设计一致 |

台架完成后执行 `cal show` 或 `status`，将 Flash 中的 A/B 与调参结果更新上表。

### 待完成台架步骤

- [ ] **Step 1 — 无负载标定**：CLI 点动、`cal a` / `cal b` / `cal save`，确认编码器读数连续。
- [ ] **Step 2 — PWM 扫行程**：1000–2000 µs 缓慢扫描；拔 PWM 验证 150 ms 后 HOLD。
- [ ] **Step 3 — 装丝杆慢速全行程**：`vmax` 减半（建议 400）；确认无机械干涉；两端留软件死区，勿硬顶死。
- [ ] **Step 4 — 接飞控地面折叠**：舵机通道脉宽与 MP/参数一致；丢信号保持。
- [ ] **Step 5 — 回写本表**：将最终 `count_a/b`、Kp、死区、`vmax` 写入上表并提交 README。

详细验收项见 [`docs/BENCH_CHECKLIST.md`](docs/BENCH_CHECKLIST.md)。

## 台架验收

1. 机械脱载或可靠限位，确认舵机独立供电且 MCU、舵机、飞控共地。
2. 完成两端点标定后，从 1000 us 缓慢扫到 2000 us；确认目标连续变化，
   到达目标死区后电机停稳，且方向与机构一致。
3. 保持运动命令时拔掉 PWM；150 ms 后速度应变为 0，LED 进入 HOLD
   （每 1000 ms 翻转）。
4. 恢复 PWM 后，再拔掉编码器通信线；连续 5 次读取失败后电机应停转，
   LED 进入 FAULT（每 125 ms 翻转）。
5. 输入 `motor 100`，确认 AUTO 闭环不覆盖手动速度；输入 `hold` 应立即
   停转并返回 AUTO。再次点动但不输入命令，5 s 后应自动返回 AUTO。

硬件系统验收需在实物台架执行；主机测试仅覆盖状态机和故障路径，不能替代
机械限位、方向、负载和电气安全验证。
