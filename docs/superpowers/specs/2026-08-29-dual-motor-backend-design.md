# Dual motor backend (HTD-85H / AK70) — Design

**Date:** 2026-08-29  
**Status:** Approved (approach A + board report + PC auto-select)  
**Scope:** Compile-time motor backend on MCU; USART3 reports `backend`; telem_viewer UI selects HTD vs AK70 (auto from board, manual override with mismatch warn)

## Goal

Support two actuator hardware options behind one wing-fold product:

- **HTD-85H** — existing Lobot motor-mode over USART1  
- **AK70-10 KV100** — CubeMars UART speed-mode over USART1  

Outer loop stays the same: FC PWM → BRT38 absolute encoder → position control. Absolute fold endpoints remain on **BRT38**; AK internal encoder is not used for fold stop memory.

PC tool (`tools/telem_viewer`) must show which device profile is active. Operator flashes the matching firmware hex; board reports its compiled backend so the viewer can **auto-check** the UI selection.

## Decisions (locked)

| Topic | Choice |
|-------|--------|
| Who picks MCU backend | **Human** — flash HTD or AK70 firmware build |
| Runtime backend switch on MCU | **No** — compile-time only |
| Who picks PC UI profile | **Auto from board report**, with **manual override** |
| Mismatch (UI ≠ board) | **Warn** in UI; do not block connect; optional soft-guard on jog later |
| PC ↔ motor bus | **No** — viewer stays on USART3 CLI only (same as today) |
| R-LINK / CubeMars Tool | Bench debug only; out of product path |
| Absolute position | **BRT38 only** for both backends |

## Non-goals (this spec)

- Runtime `backend` CLI that reconfigures the driver  
- NVM / Flash persistence of backend choice  
- CAN production interface (UART first for AK70)  
- Viewer direct talk to Lobot or AK protocol  
- Changing PWM / cal / control math for AK (speed command still ±1000 abstract units at `servo_bus` API)

## Firmware

### Build selection

CMake option (name illustrative):

```text
MOTOR_BACKEND=HTD   # default — Lobot HTD-85H
MOTOR_BACKEND=AK70  # CubeMars AK70 UART speed mode
```

- Default remains **HTD** so current boards keep working.  
- Hex / artifact naming should include backend (e.g. stamped `...-htd.hex` / `...-ak70.hex`) so flash mistakes are harder.  
- Compile definition exposes a string/id used by CLI status (e.g. `MOTOR_BACKEND_ID` → `htd` | `ak70`).

### Driver split

Keep public API in `servo_bus.h` unchanged for app/cli/control:

- `servo_bus_init`  
- `servo_bus_set_motor_speed` / `_immediate`  
- `servo_bus_motor_stop` / `servo_bus_ramp_update`  
- Speed range still **±1000** at this boundary  

Implementation:

| Backend | Source (illustrative) | Wire protocol |
|---------|----------------------|---------------|
| HTD | existing `servo_bus.c` (Lobot motor mode) | USART1 half-duplex as today |
| AK70 | new `servo_bus_ak70.c` (or `#if` split) | USART1 UART speed-mode frames per CubeMars servo serial protocol |

App layer (`app.c`, `cli.c`, `control.c`) must not branch on motor brand except where reporting `backend`.

### Board → PC identity

Add read-only field **`backend=htd|ak70`** to human-readable **`status`** line (and `help` mention).

Example (field order may append; parsers should key on `backend=`):

```text
count=... motor=... ... fault=0 backend=ak70
```

Optional (nice-to-have, not required for v1):

- Boot line once on USART3: `boot backend=ak70\r\n`  
- CLI `backend` → `backend=ak70\r\n`

**Do not** append `backend` to every `T,...` telem CSV line in v1 (would break the fixed 12-field parser and waste bandwidth). Identity is obtained on connect via `status` (viewer already can poll status).

### Unchanged

- BRT38 on USART2, PWM on PA0, CLI/telem on USART3  
- `cal` / `motor` / `hold` / `telem` command set and semantics  
- Control tick ~10 ms, ramp behavior at `servo_bus` boundary  

## PC tool (`telem_viewer`)

### UI

- Radio / combo: **HTD-85H** | **AK70**  
- On successful connect (or first good `status`): parse `backend=` → **auto-select** matching UI option  
- User may manually change selection afterward  
- If manual selection ≠ last board `backend`: show persistent warning (e.g. “板子固件=ak70，界面=HTD”)  
- Labels / diagnostics copy may depend on selected profile (e.g. bus hint text); **TX commands stay identical**

### Parse

- Extend `status` / CLI reply parsing to extract `backend`  
- Ignore unknown backends gracefully (show raw string + “未知”)  
- Telem `T,...` parser unchanged in this spec  

### Persistence (optional v1.1)

- Remember last **manual** selection in local settings; still **overwrite from board** when a valid `backend=` arrives after connect  

## Operator workflow

```text
1. Build & flash hex for the physical motor (HTD or AK70)
2. Open telem_viewer, connect USART3
3. Viewer sends status (or reads status reply) → auto-checks UI to backend=
4. Calibrate / jog / telem as today
5. Bench-only: use R-LINK + CubeMars Tool for AK ID/PID if needed
```

## Risks / notes

| Risk | Mitigation |
|------|------------|
| Wrong hex on wrong motor | Distinct hex names; UI mismatch warn |
| AK UART baud / framing differ from Lobot | Document pin-compatible USART1; verify 3.3 V TTL and baud in AK build |
| Status line length | Keep `backend=` short; bump CLI buffer if needed |
| Future CAN | New backend id later; out of this spec |

## Acceptance

- [ ] `MOTOR_BACKEND=HTD` build behaves as current product on HTD-85H  
- [ ] `MOTOR_BACKEND=AK70` build drives AK70 speed mode from same `motor`/`hold`/closed-loop path  
- [ ] `status` always includes correct `backend=` for the build  
- [ ] Viewer auto-selects UI from `backend=` after connect  
- [ ] Manual UI override shows mismatch warning when ≠ board  
- [ ] BRT38 cal endpoints still define fold stops for both builds  

## Follow-ups (not this spec)

- Implementation plan under `docs/superpowers/plans/`  
- AK70 protocol details (frame IDs, baud default, keep-alive) in plan / code comments from official manual  
- Optional telem info line or `backend` CLI if status-only proves awkward in the field  

## Knowledge base

Vendor notes ingested to Obsidian vault **产品数据**:

- `01_电机与电调/CubeMars AK70-10 KV100.md`
- `01_电机与电调/AK-series-driver-manual-V1.0.15-AK-2.0.pdf`
- Indexed in `00_索引.md` / `index.yaml` (updated 2026-08-29)
