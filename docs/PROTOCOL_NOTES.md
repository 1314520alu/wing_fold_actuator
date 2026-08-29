# BRT38M-R0M1024D24 编码器协议说明

本项目使用 `BRT38M-R0M1024D24_RT1` 多圈绝对值编码器。接口为
RS485 Modbus RTU，不是可直接连接 STM32 USART 的 TTL 电平。

## 串口与电气连接

- 默认从站地址：`1`
- 串口：固件上电以 9600 写入波特率寄存器 `0x0005=4`，随后使用 **115200 8N1**（掉电记忆）；若改写失败则回退 9600
- MCU：USART2，PA2/TX、PA3/RX
- 本机为 TTL 版：MCU TX↔编码器 RX，MCU RX↔编码器 TX，共地
- RS485 差分版需外接收发器（见手册）

## 位置读取

使用功能码 `0x03`（Read Holding Registers），一次读取两个只读寄存器：

- `0x0002`：多圈值 `turns`，16 位无符号数
- `0x0003`：单圈值 `single`，范围 `0..1023`

请求帧（CRC 已列出，Modbus CRC 低字节在前）：

```text
01 03 00 02 00 02 65 CB
```

正常响应格式：

```text
01 03 04 TT TT SS SS CRC_LO CRC_HI
```

其中寄存器数据采用高字节在前；RTU CRC16 初值 `0xFFFF`、多项式
`0xA001`，线上发送低字节后高字节。驱动校验从站地址、功能码、字节数、
CRC，并拒绝超出 `0..1023` 的单圈值。

项目绝对计数合成为：

```text
count = (int32_t)turns * 1024 + (int32_t)single
```

`ENCODER_SMOKE_TEST=1` 时，固件每 200 ms 从 USART2 读取一次，并通过
USART3（PB10，115200 8N1）打印 `ENC <count>`；默认值为 `0`，因此正常
固件仍保持 LED 闪烁。

---

## USART3 遥测流（`telem`）

默认固件：调试 CLI 走 **USART3**（PB10 TX / PB11 RX），115200 8N1。

**`UsbDebug` 固件**：同一套 CLI / `telem` 改走 **USB CDC**（板载 USB，Windows 出现新 COM）。USART3 空闲，留给后续飞控 MAVLink。

```powershell
cmake --preset UsbDebug
cmake --build --preset UsbDebug
# 烧录 build/UsbDebug/wing_fold_actuator.hex
# 插 USB，设备管理器找 ST Virtual COM；telem_viewer / 串口助手连该 COM
```

除既有
`status` / `motor` / `cal` 等命令外，固件提供周期性 CSV 遥测流，供 PC
端 [`tools/telem_viewer/`](../tools/telem_viewer/README.md) 解析绘图。

### CLI 命令

| 命令 | 响应 | 说明 |
|------|------|------|
| `telem on` | `OK telem on\r\n` | 开启遥测流 |
| `telem off` | `OK telem off\r\n` | 关闭遥测流 |
| `telem` | `telem=on\r\n` 或 `telem=off\r\n` | 查询当前状态 |

**上电默认：遥测关闭。** 开启后每 **10 ms**（与 `app_tick` / 控制环对齐）
输出一行；USART3 TX 忙时**丢弃该帧**，不阻塞控制路径。CLI 应答与 `T,...`
行可交错出现，PC 端应只解析以 `T,` 开头的行。

### 行格式（12 个逗号分隔字段）

```text
T,ms,pwm,count,tgt,err,spd_cmd,spd_out,hold,settled,last_dir,fault\r\n
```

| 字段 | 含义 |
|------|------|
| `T` | 固定标记 |
| `ms` | `HAL_GetTick()` 毫秒时间戳 |
| `pwm` | 捕获脉宽（µs）；无效 / HOLD 路径时为 0 |
| `count` | 送入 `control_update` 的位置（滤波有效时用滤波值，否则 raw） |
| `tgt` | 控制目标 count |
| `err` | `tgt - count`（有符号） |
| `spd_cmd` | 闭环速度命令（USART 斜坡前） |
| `spd_out` | 经斜坡后写入舵机总线的速度 |
| `hold` | 0/1，是否 HOLD |
| `settled` | 0/1，控制是否判定到位 |
| `last_dir` | -1 / 0 / +1，最近一次有效运动方向 |
| `fault` | 0/1，舵机总线故障闩锁 |

示例：

```text
T,1234,1500,8164,12000,3836,500,480,0,0,1,0\r\n
```

带宽约 80 字节/行 × 100 Hz ≈ 8 KB/s，115200 波特可接受。

---

## USART3 → 飞控 MAVLink（`FC_MAVLINK`）

编译选项 `-DFC_MAVLINK=ON` 时：**USART3 专用于飞控**，不再跑 ASCII CLI / `telem`。
PA0 PWM 仍为折叠指令输入。调试 CLI 待 USB CDC（后续）。

### 构建

```powershell
cmake --preset <your-preset> -DFC_MAVLINK=ON
cmake --build --preset <your-preset>
```

或在已有 build 目录：`cmake -DFC_MAVLINK=ON ..` 后重新编译。

### 报文

MAVLink v1 **`NAMED_VALUE_FLOAT` (251)**，约 10 Hz 轮询发送（每次一帧）：

| name | 含义 |
|------|------|
| `fold_pct` | 行程百分比 0–100（相对 NVM `count_a`/`count_b`） |
| `fold_cnt` | 编码器 count |
| `fold_flt` | 舵机故障闩锁 0/1 |
| `fold_pwm` | 捕获 PWM µs |
| `fold_hld` | HOLD 0/1 |

- sysid=1，compid=191  
- 115200 8N1，PB10 TX / PB11 RX  

### PC 验证

```powershell
pip install pymavlink
python tools/fc_mavlink_sniff.py COM5
```

接飞控时：飞控 `SERIALn` 接 USART3，Lua 可用 `mavlink` 收 `NAMED_VALUE_FLOAT` 或后续再桥接。
