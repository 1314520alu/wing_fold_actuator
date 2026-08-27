# Wing Fold Telemetry Viewer

PySide6/pyqtgraph desktop viewer for the wing-fold actuator telemetry stream.
It displays position, error, speed, estimated count rate, and PWM while keeping
raw serial output and diagnostic suggestions visible. A calibration panel drives
existing firmware CLI commands for encoder endpoints and servo status.

## Install and run

### 发给其他电脑（推荐 exe）

1. 本机打包：

```powershell
cd tools/telem_viewer
powershell -ExecutionPolicy Bypass -File .\build_exe.ps1
```

2. 把生成的 zip 拷到目标电脑：

`tools/telem_viewer/dist/折叠翼遥测查看器.zip`

3. 解压后双击 **`折叠翼遥测查看器.exe`**（无需安装 Python）。  
   录制文件会写在 exe 同目录下的 `captures\`。  
   目标机需为 **64 位 Windows**；若杀毒误报，加入白名单即可。

### 本机用 Python 运行

Python 3.10 or newer is recommended.

```powershell
cd tools/telem_viewer
python -m pip install -r requirements.txt
python -m telem_viewer
```

Select the actuator's COM port and click **连接**. The expected link is
USART3 at 115200 baud, 8 data bits, no parity, and 1 stop bit.

## Controls

- **连接 / 断开** opens or closes the selected serial port.
- **开启/关闭遥测** sends `telem on` or `telem off`.
- **录制** creates a timestamped directory under `captures/` containing
  `telem.csv`, `raw.log`, and `events.json`.
- **清空** clears plots and both text panes without deleting captures.
- **打开采集** reloads a recorded capture directory.
- **轮询 status（500 ms）** optionally sends `status` while connected.

### 标定 / 舵机

On connect the viewer automatically sends `cal show` and `status`.

| Button | CLI | Effect |
|--------|-----|--------|
| 采集 A | `cal a` | Capture current encoder count as endpoint A (RAM) |
| 采集 B | `cal b` | Capture current encoder count as endpoint B (RAM) |
| 保存到 Flash | `cal save` | Persist endpoints to MCU Flash (confirmation dialog) |
| 读取参数 | `cal show` | Reload saved `a` / `b` and calibrated state |
| 刷新状态 | `status` | Refresh encoder, motor, hold, fault |

Live fields: encoder `count`, `tgt`, `motor`, `fault`, `hold`, `cal`.
Endpoint fields: `cal a`, `cal b`, save state (`已保存` / `未保存`).

Flash is written **only** when you confirm **保存到 Flash**. Values remain after
power loss until the next capture + save cycle.

### 手动模式（点动回 a–b 区间）

固件 `motor <spd>` 会占用开环覆盖，闭环忽略 PWM；`hold` 解除覆盖。

| 操作 | CLI | 说明 |
|------|-----|------|
| 开启手动模式 | `motor 0` | 暂停 PWM 闭环，电机停 |
| 单击 **+** / **−** | `motor ±500` | 再点同向停止（`motor 0`）；点反向则切换方向 |
| 退出手动模式 | `hold` | 恢复 PWM 闭环 |

上电位置若不在 `cal a`–`cal b` 内：先开手动模式，用 ± 挪回区间后再退出手动、用飞控 PWM。

## Bench recipe

1. Flash the firmware and connect USART3 to the PC.
2. Start the viewer, select the COM port, then click **连接**.
3. Click **开启遥测**, then **录制**.
4. Sweep PWM slowly from 1000 to 2000 µs.
5. Stop recording and review the event pane/capture for `HUNT` events.

### Calibration recipe

1. Connect and confirm **读取参数** shows current `cal a` / `cal b`.
2. Move the actuator to endpoint A (manual `motor` via CLI or PWM), then **采集 A**.
3. Move to endpoint B, then **采集 B**.
4. Confirm **保存到 Flash**; status should show `已保存` / `cal=yes`.
5. Power-cycle and **读取参数** to verify endpoints persist.

Telemetry is off at firmware boot. Stop recording before closing the viewer to
flush the final event list; closing the window also performs this cleanup.
