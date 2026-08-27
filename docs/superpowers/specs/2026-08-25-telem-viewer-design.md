# Wing-fold actuator telemetry viewer — Design

**Date:** 2026-08-25  
**Status:** Approved (approach A + option C data path)  
**Scope:** Firmware CSV telem stream on USART3 + PC PySide6 analyzer (charts, text, auto events, record/replay)

## Goal

Capture control-loop samples at the same rate as the firmware tick so mid-stroke hunting (especially PWM 1000→2000) is visible. Provide a desktop GUI with live charts, raw serial text, rule-based event text, and CSV/log capture so control parameters can be corrected from evidence.

## Decisions (locked)

| Topic | Choice |
|-------|--------|
| Data path | **C:** continuous `telem` CSV + also record raw `status`/CLI text |
| PC UI | **1:** Python + PySide6 + pyqtgraph + pyserial |
| Architecture | **A:** firmware stream + PC viewer (not poll-only, not binary frames) |
| Analysis coverage | All five: hunt, error, speed match, PWM map, settle chatter |
| Auto-fix firmware | Out of scope — tool emits tags + suggestions only |

## Non-goals (v1)

- Sniffing Lobot / BRT38 physical buses
- Cloud upload
- Writing Flash / NVM from the PC tool
- Replacing an oscilloscope for PWM edges (use firmware-decoded `pwm_us` only)

## Firmware changes (USART3 CLI)

### New commands

- `telem on` — enable stream
- `telem off` — disable stream (default at boot: **off**)
- `telem` — print `telem=on|off`

Existing `status`, `cal`, `motor`, `hold`, `help` unchanged.

### Stream format

One line per control tick when enabled (**10 ms** period, aligned with `app_tick` / control update):

```text
T,ms,pwm,count,tgt,err,spd_cmd,spd_out,hold,settled,last_dir,fault\r\n
```

| Field | Meaning |
|-------|---------|
| `T` | Fixed marker |
| `ms` | `HAL_GetTick()` or same clock used by app |
| `pwm` | Captured pulse width (µs), 0 if invalid/hold path as firmware already exposes |
| `count` | Same value fed into `control_update` (filtered count when filter valid; else raw) |
| `tgt` | Position target from control state |
| `err` | `tgt - count` (signed) |
| `spd_cmd` | `control_state_t.speed_cmd` (before USART ramp) |
| `spd_out` | Last speed written to servo after ramp (`app` tracked out) |
| `hold` | 0/1 |
| `settled` | 0/1 from control state |
| `last_dir` | -1 / 0 / +1 |
| `fault` | servo fault flag 0/1 |

Bandwidth check: ~80 bytes/line × 100 Hz ≈ 8 KB/s at 115200 — acceptable.

When `telem` is on, CLI replies still interleave; PC parser must tolerate mixed lines (`T,...` vs `OK ...` vs `count=...`).

### Optional companion (scheme C)

PC may periodically send `status\n` (e.g. 2–5 Hz) while telem runs, solely to cross-check human-readable fields in `raw.log`. Not required for charts.

## PC tool layout

**Root:** `firmware/wing_fold_actuator/tools/telem_viewer/`

```text
tools/telem_viewer/
  README.md
  requirements.txt          # pyside6, pyqtgraph, pyserial
  telem_viewer/
    __main__.py
    __init__.py
    serial_worker.py        # QThread: open port, RX parse, TX commands
    parser.py               # T-lines + pass-through raw
    model.py                # ring buffer of samples
    diagnostics.py          # rule engine → events
    main_window.py          # UI: charts, logs, controls
    replay.py               # load CSV / re-run rules
  captures/                 # gitignore contents; keep .gitkeep
```

**Run:** from `tools/telem_viewer`:

```bash
python -m telem_viewer
```

Default serial: 115200 8N1, user selects COM port.

## UI

- Controls: port combo, Connect, `telem on/off`, Record, Clear, Export, Open capture
- Live readouts: pwm, err, spd_cmd, spd_out
- Charts (pyqtgraph, toggleable):
  1. `count`, `tgt`
  2. `err`
  3. `spd_cmd`, `spd_out`, `rpm_est` (from Δcount/Δt)
  4. `pwm`
- Text panes:
  - Events / diagnostics (auto)
  - Raw serial (TX+RX)

## Diagnostics (v1 thresholds — tunable in UI or constants)

| ID | Detects | Rule sketch | Suggestion text |
|----|---------|-------------|-----------------|
| `HUNT` | Mid-stroke reverse chatter | ≥N sign flips of `spd_out` or `last_dir` in 1 s while \|err\| > deadzone-ish | Increase `CONTROL_REVERSE_LOCK` / soften vmax |
| `LAG` | Persistent lag | \|err\| > E for T ms while pwm commanded moving | Cruise window / vmax / mapping |
| `OVERSHOOT` | Overshoot | err crosses 0 then grows opposite with reverse cmd | Reverse lock / brake zone |
| `STALL` | Commanded but stuck | \|spd_cmd\| > S and \|Δcount\| tiny for T ms | Load / motor / bus / ramp |
| `RAMP_LAG` | Out vs cmd | \|spd_out − spd_cmd\| large sustained | Ramp rate |
| `PWM_GLITCH` | PWM map | pwm jumps or pwm changes without tgt change | PWM capture / FC |
| `SETTLE_CHATTER` | At target | `settled` true then false repeatedly | Deadzone / hysteresis |

Events written to UI and `events.json`.

## Capture / replay

Directory: `captures/YYYYMMDD_HHMMSS/`

| File | Content |
|------|---------|
| `telem.csv` | Header + numeric rows (same columns as `T` line without marker, plus optional `rpm_est`) |
| `raw.log` | All TX/RX text with PC timestamps |
| `events.json` | List of `{t_ms, type, message, hint}` |

Replay: open folder or CSV → replot + re-run `diagnostics.py`.

## Integration with firmware fix loop

1. Record sweep (e.g. PWM 1000→2000 slow)
2. Read events + charts
3. Human/agent adjusts `control.c` / constants
4. Flash new hex, re-record, compare captures

## Testing

- Host unit tests for `parser.py` and `diagnostics.py` (synthetic CSV → expected events)
- Firmware: host or bench — `telem on` produces valid lines; `telem off` stops; CLI still works
- Manual: connect CH340, enable telem, move PWM, see charts update

## Docs to update after implementation

- `firmware/wing_fold_actuator/README.md` — link to telem viewer
- `docs/PROTOCOL_NOTES.md` — `T,...` line and CLI commands

## Risks

| Risk | Mitigation |
|------|------------|
| USART TX blocks control loop | Non-blocking / short snprintf; drop line if busy rather than stall motor |
| Mixed CLI + telem parsing | Strict `T,` prefix; ignore other lines for charts |
| Encoder already at 115200 | Telem shares USART3 only; no change to USART2 |
| Ring buffer memory on PC | Cap N samples (e.g. 60 s @ 100 Hz) with scrollback |

## Success criteria

- Live charts show count/tgt/err/spd/pwm during a PWM sweep without manual `status` spam
- Raw log contains both `T,...` and occasional `status` replies when enabled
- At least one of HUNT / OVERSHOOT / SETTLE_CHATTER fires on a known hunting capture (or synthetic test)
- Capture folder can be reopened offline with same plots/events
