# FC MAVLink on USART3 — Implementation Plan

> **For agentic workers:** Implement task-by-task. USB CDC debug is **out of this plan** (no USB in `.ioc` yet).

**Goal:** MCU sends fold status to the flight controller on **USART3** as MAVLink `NAMED_VALUE_FLOAT`, while **PA0 PWM** remains the fold command input — so you can verify with a PC sniffer or ArduPilot Lua.

**Architecture:** Hand-rolled MAVLink v1 packer (no full library). Compile flag `FC_MAVLINK=1` switches USART3 from ASCII CLI/telem to FC link. CLI still loads NVM when UART is NULL. Default build unchanged (`FC_MAVLINK` off).

**Tech Stack:** STM32F103 HAL UART, host C tests, optional Python pymavlink sniff.

## Global Constraints

- PWM command path unchanged  
- BRT38 absolute outer loop unchanged  
- Do not mix ASCII telem and MAVLink on the same USART3 session  
- USB CDC debug → later plan  

---

### Task 1: MAVLink NAMED_VALUE_FLOAT packer + host test

**Files:** `App/mavlink_nvf.h`, `App/mavlink_nvf.c`, `tests/host/test_mavlink_nvf.c`

- [x] Pack v1 frame `0xFE`, msgid `251`, crc_extra `170`
- [x] Host test added (`tests/host/test_mavlink_nvf.c`) — needs host GCC to run

### Task 2: `fc_link` on USART3

**Files:** `App/fc_link.h`, `App/fc_link.c`, wire in `app.c` / `stm32f1xx_it.c` / `CMakeLists.txt`

- [x] ~10 Hz emit: `fold_pct`, `fold_cnt`, `fold_flt`, `fold_pwm`, `fold_hld`
- [x] `FC_MAVLINK=1`: `cli_init(NULL)`, `fc_link_init(&huart3)`, no CLI RX on USART3
- [x] Preset `FcMavlink` builds hex successfully

### Task 3: Bench verify helper

**Files:** `tools/fc_mavlink_sniff.py`, note in `docs/PROTOCOL_NOTES.md`

- [x] Script prints named floats from serial
- [x] Document cmake `-DFC_MAVLINK=ON` and wiring
