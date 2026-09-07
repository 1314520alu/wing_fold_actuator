# 折叠翼执行器固件（非 ArduPilot）

STM32F103CBT6（128 KiB Flash，LQFP48）**机翼折叠丝杆执行器**固件：飞控舵机 PWM → 位置闭环 → HTD-85H 电机模式。  
本仓库**不是** ArduPilot / 飞控固件；飞控工程见 [TRANSWING](https://github.com/1314520alu/TRANSWING)。

飞控输出标准舵机 PWM（1000–2000 us）→ MCU 映射为目标位置 → BRT38 绝对值编码器闭环 → HTD-85H **电机模式**驱动丝杆。一套丝杆同步双翼；**不向飞控回传位置**。

主循环约 **10 ms**。配套 PC 工具：[`tools/telem_viewer`](tools/telem_viewer/README.md)（中文界面，标定 / 手动点动 / 遥测曲线）。

可选构建：

- **`UsbDebug`**：CLI / `telem` 走 **USB CDC**（板载 USB）
- **`FcMavlink`**：USART3 向飞控发 MAVLink（可与 USB 调试组合，后续）

见 [`docs/PROTOCOL_NOTES.md`](docs/PROTOCOL_NOTES.md)。

---

## 1. 系统概览

```text
飞控 PWM --PA0---> 脉宽捕获 ---> 目标 tgt
BRT38 ----USART2---> 位置 count ---> 闭环 control ---> 速度指令
HTD-85H --USART1---> 电机模式执行
调试 PC --USB CDC--> CLI / 遥测（UsbDebug 固件）
调试 PC --USART3---> CLI / 遥测（默认固件）
飞控    --USART3---> MAVLink（FcMavlink 固件）
```

| 角色 | 接口 | 说明 |
|------|------|------|
| 飞控指令 | PA0 PWM | 脉宽 → 目标位置 |
| 调试（推荐） | USB CDC（`UsbDebug`） | Windows 虚拟串口；`telem_viewer` 选该 COM |
| 调试（旧） | USART3 | 默认固件 CLI + `telem` |
| 飞控回传 | USART3（`FcMavlink`） | `fold_pct` 等命名浮点 |
| 编码器 | USART2 | 合成 `count` |
| 舵机 | USART1 | Lobot 电机模式 ±1000 |

---

## 2. 硬件连接

| 功能 | 引脚 | 说明 |
|------|------|------|
| 飞控 PWM | **PA0** | EXTI 测脉宽；下拉；与飞控 **共地** |
| HTD-85H 总线 | USART1 **PA9** TX / **PA10** RX | 115200 8N1；Lobot 电机模式 |
| AK70-10 总线 | USART1 **PA9** TX / **PA10** RX | 115200 8N1；CubeMars 速度环（`MOTOR_BACKEND=AK70` 固件） |
| BRT38 编码器 | USART2 **PA2** TX / **PA3** RX | 上电协商 **115200**（出厂 9600 会改写）；TTL 交叉 |
| 调试 CLI | USART3 **PB10** TX / **PB11** RX | 115200 8N1；接 CH340（关 DTR/RTS） |
| 状态 LED | **PC13** | 板载灯（低电平亮） |
| SWD | PA13 / PA14 | 烧录调试 |

**供电**

- 舵机功率：**独立 VBAT（约 9–14.8 V，以手册为准）**，禁止从 MCU 3.3 V 取电  
- MCU：3.3 V  
- **舵机电源、MCU、飞控必须共地**

**时钟**：8 MHz HSE → PLL → 72 MHz。

---

## 3. 快速上手

1. 烧录 `build/Debug/wing_fold_actuator.hex`（或同批带时间戳 hex）  
2. USART3：115200，关闭 DTR/RTS，命令以换行结束  
3. 上电可见 `NVM calibration loaded` 或 `Using default a=0 b=24000`  
4. PA0 有合法 PWM 即可跟位；台架点动：`motor 300` / `hold`  

默认行程可用。改端点时再标定并 `cal save`。

若板内已有旧参数，烧录新固件后建议：

```text
set kp 1500
set cruise 800
set save
cal show
```

---

## 4. 位置与闭环

### 4.1 编码器

BRT38M：24 圈 x 单圈 1024。

```text
count = 圈数 x 1024 + 单圈值    # 约 0～24575
```

### 4.2 PWM → 目标

| 脉宽 | 默认目标 |
|------|----------|
| 1000 us | `count_a`（默认 0） |
| 1500 us | 线性中间 |
| 2000 us | `count_b`（默认 24000） |

脉宽先夹到 `pwm_min..pwm_max`（默认 1000–2000），再线性映射。  
**目标始终在 a–b 线段上**；实测 `count` 可在区间外——上电后若位置出界，用查看器**手动模式**点动回区间，或给 PWM 后闭环拉回。

### 4.3 速度曲线（当前默认）

| 阶段 | 条件 | 行为 |
|------|------|------|
| 巡航 | `\|err\| >= cruise`（默认 **800**） | 输出 **vmax**（默认 1000） |
| 刹车 | `dz <= \|err\| < cruise` | `spd ≈ kp*(\|e\|-dz)/1000`，不低于 **MIN_BRAKE=200** |
| 到位 | `\|err\| < 死区` | `settled`，速度 0 |
| 滞回 | 已 settled | 误差需超过约 **4x死区** 才再启动 |
| 反向锁 | 单向超调后 | `\|err\| < 400` 先滑行，不立刻反转 |

设计目标：**离开巡航进入减速后，约 1 s 内进入停稳**（过小 `kp` 会在终点前长时间蠕动）。

### 4.4 安全

| 条件 | 行为 |
|------|------|
| 无 PWM / 脉宽超时（400 ms） | **HOLD**：速度 0 |
| 编码器连续失败 >= 5 | **FAULT**：停转 |
| 舵机总线连续发送失败 | **FAULT** 闩锁 |
| 上电 | 先发停转 |
| 未标定（`cal=no`） | 强制 HOLD（出厂默认端点视为已标定） |

### 4.5 手动覆盖（CLI / 查看器）

| 命令 | 作用 |
|------|------|
| `motor <spd>` | 开环覆盖，暂停 PWM 闭环（-1000..1000） |
| `motor 0` | 保持手动模式、电机停 |
| `hold` | 解除手动覆盖，恢复 PWM 闭环 |

查看器：**开启手动模式** → `±` 单击切换点动（速度 ±500）→ **退出手动模式**。

---

## 5. 状态 LED（PC13）

| 模式 | 周期 | 含义 |
|------|------|------|
| RUN | ~500 ms | 有效 PWM，闭环中 |
| HOLD | ~1000 ms | 无 PWM / 超时保持 |
| FAULT | ~125 ms | 编码器或舵机总线故障 |

---

## 6. CLI（USART3）

```text
help
status
cal a | cal b | cal save | cal show
set dz|kp|vmax|cruise <n> | set save
motor <spd>     # -1000..1000；带载建议 >=200
hold
telem on | telem off | telem
```

### 6.1 `status` 示例

```text
count=8164 motor=0 cal=yes enc_fail=0 pwm=1500 raw=1500 irq=1234 age=20ms hold=0 tgt=12000 spd=500/500 fault=0
```

| 字段 | 含义 |
|------|------|
| `count` | 编码器位置 |
| `motor` | CLI 手动速度记忆 |
| `cal` | 是否按已保存标定运行 |
| `pwm` / `raw` / `irq` / `age` | 脉宽、原始脉宽、边沿计数、距上次有效边沿 |
| `hold` | 1=保持 |
| `tgt` | 目标 count |
| `spd` | 闭环命令 / 总线实际输出 |
| `fault` | 1=故障闩锁 |

部分 USB 串口会把命令回显粘在应答前（如 `cal showa=0...`）；查看器已做剥离解析。

### 6.2 标定

```text
motor ±300   → hold → cal a
motor ±300   → hold → cal b
cal save
cal show
```

`cal a` / `cal b` 采**当时编码器读数**，不能手填。Flash 页 `0x0801FC00`（1 KiB，128 KiB 器件末页）。有有效 NVM 用 Flash；否则默认 `a=0` / `b=24000`。

### 6.3 遥测

上电默认 **关**。`telem on` 后每 10 ms 一行：

```text
T,ms,pwm,count,tgt,err,spd_cmd,spd_out,hold,settled,last_dir,fault
```

字段说明见 [`docs/PROTOCOL_NOTES.md`](docs/PROTOCOL_NOTES.md)。

---

## 7. 默认参数

| 参数 | 默认 | 说明 |
|------|------|------|
| `count_a` / `count_b` | 0 / 24000 | PWM 1000 / 2000 us 端点 |
| 死区 `dz` | 150 | 滞回约 x4；仅死区内 settle |
| `vmax` | 1000 | 电机模式上限 |
| `kp` | **1500** | 刹车 P 增益（/1000） |
| `cruise` | **800** | 之外全速；过大则减速段变长 |
| 最低制动 | **200**（固件常数） | 死区外 P 更低时抬升，避免爬行 |
| PWM 超时 | 400 ms | 短丢帧不进 HOLD |
| PWM 范围 | 1000–2000 us | 只采高电平 |

运行时改参：`set kp 1500` 等，再 `set save` 写 Flash。

---

## 8. 遥测查看器

```powershell
cd tools/telem_viewer
python -m pip install -r requirements.txt
python -m telem_viewer
```

（须在 `tools/telem_viewer` 目录启动，不要进入内层包目录。）

功能摘要：

- 连接 / 遥测开关 / 录制导出  
- 实时指标与四宫格曲线  
- **标定 / 舵机**：采集 A/B、保存 Flash、读取参数  
- **手动模式**：暂停 PWM，±500 点动回 a–b 区间  
- 诊断事件（HUNT / STALL 等，仅建议，不自动写参）  

详见 [`tools/telem_viewer/README.md`](tools/telem_viewer/README.md)。

---

## 9. 编译与烧录

依赖：`cmake`、Ninja、`arm-none-eabi-gcc`。

```powershell
cmake --preset Debug          # HTD 后端（默认）
cmake --build --preset Debug

cmake --preset UsbDebug       # USB 调试 + HTD
cmake --build --preset UsbDebug

cmake --preset UsbDebug-Ak70  # USB 调试 + AK70
cmake --build --preset UsbDebug-Ak70
```

产物（hex 名含后端，避免烧错电机）：

- `build/<预设>/wing_fold_actuator-htd.hex` — HTD-85H  
- `build/<预设>/wing_fold_actuator-ak70.hex` — AK70  
- 同目录带 `YYYYMMDD_HHMMSS` 时间戳备份  

```powershell
STM32_Programmer_CLI -c port=SWD -w build/Debug/wing_fold_actuator-htd.hex -v -rst
```

OpenOCD：

```powershell
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg `
  -c "program build/Debug/wing_fold_actuator.elf verify reset exit"
```

### USART3 烟测（可选）

```powershell
cmake --preset Usart3Smoke
cmake --build --preset Usart3Smoke
```

CH340：RX→PB10，TX→PB11，共地；应周期性打印 `USART3 OK ...`。

---

## 10. 台架检查清单

- [ ] `motor 300` / `hold` 正常；`status` 中 `count` 变化、`enc_fail=0`  
- [ ] PA0 有 PWM：`pwm≈1000..2000`、`hold=0`、`irq` 递增  
- [ ] 慢扫 1000<->2000：方向正确；减速到停稳约 **<=1 s**，无明显正反抖  
- [ ] 拔 PWM：约 400 ms 内 HOLD，LED 变慢闪  
- [ ] 断编码器：FAULT，LED 急闪，电机停  
- [ ] 飞控通道与脉宽范围与标定一致；共地可靠  
- [ ] （可选）查看器 `telem on` 录慢扫：无持续 `HUNT` / 到位抖振  

更细项：[`docs/BENCH_CHECKLIST.md`](docs/BENCH_CHECKLIST.md)。

---

## 11. 模块结构

```text
App/
  app.c          主循环集成
  control.c      PWM→目标、巡航/刹车、死区滞回、反向锁
  pwm_in.c       PA0 EXTI + TIM2 测脉宽
  encoder.c      BRT38 Modbus 位置
  servo_bus.c    Lobot 电机模式 + 斜坡/保活
  cli.c / nvm.c  CLI、标定与 Flash
  led_status.c   PC13 指示
tools/telem_viewer/   PC 遥测与标定工具
```

---

## 12. 相关文档

| 文档 | 内容 |
|------|------|
| [`docs/PROTOCOL_NOTES.md`](docs/PROTOCOL_NOTES.md) | 编码器协议、遥测帧格式 |
| [`docs/BENCH_CHECKLIST.md`](docs/BENCH_CHECKLIST.md) | 详细台架清单 |
| [`tools/telem_viewer/README.md`](tools/telem_viewer/README.md) | 查看器安装与操作 |
