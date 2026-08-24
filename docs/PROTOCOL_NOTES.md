# BRT38M-R0M1024D24 编码器协议说明

本项目使用 `BRT38M-R0M1024D24_RT1` 多圈绝对值编码器。接口为
RS485 Modbus RTU，不是可直接连接 STM32 USART 的 TTL 电平。

## 串口与电气连接

- 默认从站地址：`1`
- 串口：`9600 baud, 8 data bits, no parity, 1 stop bit`（8N1）
- MCU：USART2，PA2/TX、PA3/RX
- 编码器：RS485 A/B 差分总线
- USART2 与 A/B 之间必须使用 MAX485、SP3485 等 RS485 收发器
- 当前 PCB/固件未预留 DE/RE GPIO；应使用带自动收发方向控制的外部
  RS485 模块。不得把编码器 A/B 直接接到 PA2/PA3。

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
