# Telem Viewer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 10 ms USART3 CSV telemetry from the wing-fold actuator firmware and a PySide6 desktop viewer that charts, diagnoses, records, and replays that stream.

**Architecture:** Firmware extends `app_status_t` with `settled`/`last_dir`, CLI commands `telem on|off`, and `cli_telem_tick()` emits one `T,...` line per control tick when enabled. PC tool parses `T,` lines into a ring buffer, runs rule diagnostics, plots with pyqtgraph, and writes `telem.csv` + `raw.log` + `events.json`.

**Tech Stack:** STM32 C (existing CLI/app), host C tests (`tests/host`), Python 3.10+, PySide6, pyqtgraph, pyserial, pytest.

**Spec:** `docs/superpowers/specs/2026-08-25-telem-viewer-design.md`

## Global Constraints

- Telem line format exactly: `T,ms,pwm,count,tgt,err,spd_cmd,spd_out,hold,settled,last_dir,fault\r\n`
- Telem period: **10 ms**, aligned with encoder/control path in `app_tick`
- Boot default: telem **off**
- USART3 115200 8N1; do not change Lobot/encoder UARTs
- If UART TX busy, **drop** that telem frame (never block > existing CLI timeout pattern)
- PC tool root: `tools/telem_viewer/`
- Auto-diagnosis suggests params only; never writes Flash from PC
- Commits only when the user explicitly asks (skip plan commit steps unless requested)

---

## File structure

| Path | Responsibility |
|------|----------------|
| `App/app.h`, `App/app.c` | Expose `settled`, `last_dir` on status; call `cli_telem_tick` after control update |
| `App/cli.h`, `App/cli.c` | `telem` commands + `cli_telem_tick` emit |
| `tests/host/test_cli.c` | Host tests for telem on/off and line format |
| `tools/telem_viewer/requirements.txt` | Dependencies |
| `tools/telem_viewer/telem_viewer/parser.py` | Parse `T,` lines |
| `tools/telem_viewer/telem_viewer/model.py` | Sample dataclass + ring buffer |
| `tools/telem_viewer/telem_viewer/diagnostics.py` | Rule engine |
| `tools/telem_viewer/telem_viewer/serial_worker.py` | QThread serial I/O |
| `tools/telem_viewer/telem_viewer/capture.py` | Record/replay files |
| `tools/telem_viewer/telem_viewer/main_window.py` | GUI |
| `tools/telem_viewer/telem_viewer/__main__.py` | Entry |
| `tools/telem_viewer/tests/test_parser.py` | Parser unit tests |
| `tools/telem_viewer/tests/test_diagnostics.py` | Diagnostics unit tests |
| `tools/telem_viewer/captures/.gitkeep` | Capture dir |
| `docs/PROTOCOL_NOTES.md`, `README.md` | Protocol + usage |

---

### Task 1: Firmware status fields + telem CLI emit

**Files:**
- Modify: `App/app.h` — extend `app_status_t`
- Modify: `App/app.c` — fill new fields; call `cli_telem_tick(now_ms)` after `control_update` / speed apply path (end of successful tick before LED, and also on early returns only if telem must stay quiet during fault — **emit only when not in servo-fault early-return**, after control has run)
- Modify: `App/cli.h` — declare `void cli_telem_tick(uint32_t now_ms);`
- Modify: `App/cli.c` — telem state, commands, emit
- Modify: `tests/host/test_cli.c` — fake `app_get_status` already exists; extend stub fields; add telem tests
- Modify: `tests/host/test_app.c` — stub `cli_telem_tick` if linked

**Interfaces:**
- Consumes: `app_get_status`, `servo_bus_get_output_speed` (already used), `s_control.settled`, `s_control.last_dir`, `s_control.current` (or `current_filt` if `filt_valid`)
- Produces: `cli_telem_tick(uint32_t now_ms)`; CLI `telem` / `telem on` / `telem off`

- [ ] **Step 1: Extend `app_status_t` in `app.h`**

Add after `speed_out`:

```c
    bool settled;
    int8_t last_dir;
```

- [ ] **Step 2: Fill fields in `app_get_status`**

In `app.c` `app_get_status`, after assigning `speed_out`:

```c
    out->settled = s_control.settled;
    out->last_dir = s_control.last_dir;
    /* Prefer filtered count when valid so telem matches closed loop. */
    out->current = s_control.filt_valid ? s_control.current_filt
                                        : s_control.current;
```

(Keep existing `out->current = s_control.current` replaced by the ternary above.)

- [ ] **Step 3: Update host `app_get_status` fakes**

In `tests/host/test_cli.c` (and any other stub), zero-init new fields / set defaults so compiles:

```c
    out->settled = true;
    out->last_dir = 0;
```

- [ ] **Step 4: Add CLI telem API and state in `cli.c` / `cli.h`**

`cli.h`:

```c
void cli_telem_tick(uint32_t now_ms);
```

In `cli.c`:

```c
static bool s_telem_on;
static uint32_t s_telem_last_ms;

static bool uart_tx_ready(void)
{
    return (s_huart != NULL)
        && (s_huart->gState == HAL_UART_STATE_READY);
}

static void emit_telem_line(uint32_t now_ms)
{
    char output[160];
    app_status_t st;
    int n;

    if (!uart_tx_ready()) {
        return; /* drop frame */
    }
    app_get_status(&st);
    n = snprintf(output, sizeof(output),
                 "T,%lu,%u,%ld,%ld,%ld,%d,%d,%d,%d,%d,%d\r\n",
                 (unsigned long)now_ms,
                 (unsigned int)st.pwm_us,
                 (long)st.current,
                 (long)st.target,
                 (long)(st.target - st.current),
                 (int)st.speed_cmd,
                 (int)st.speed_out,
                 st.hold ? 1 : 0,
                 st.settled ? 1 : 0,
                 (int)st.last_dir,
                 st.servo_fault ? 1 : 0);
    if (n > 0) {
        write_text(output);
    }
}

void cli_telem_tick(uint32_t now_ms)
{
    if (!s_telem_on) {
        return;
    }
    if ((uint32_t)(now_ms - s_telem_last_ms) < 10U) {
        return;
    }
    s_telem_last_ms = now_ms;
    emit_telem_line(now_ms);
}
```

In `execute_line`, before unknown-command:

```c
    } else if (strcmp(line, "telem on") == 0) {
        s_telem_on = true;
        s_telem_last_ms = 0U; /* force next tick */
        write_text("OK telem on\r\n");
    } else if (strcmp(line, "telem off") == 0) {
        s_telem_on = false;
        write_text("OK telem off\r\n");
    } else if (strcmp(line, "telem") == 0) {
        write_text(s_telem_on ? "telem=on\r\n" : "telem=off\r\n");
```

Update help string to mention telem.

Init: `s_telem_on = false` in `cli_init`.

- [ ] **Step 5: Call `cli_telem_tick` from `app_tick`**

After `control_update` and servo speed path (after ramp update is fine so `speed_out` is current), before final LED section:

```c
    cli_telem_tick(now_ms);
```

Also call it on the encoder-fail early path **only if** you still want visibility during fault — **spec:** emit when control has run; skip pure servo-fault early return at top (no update). On encoder fail streak path, optional: still emit once with fault — prefer skip to keep motor stop fast.

- [ ] **Step 6: Stub `cli_telem_tick` in `test_app.c`**

```c
void cli_telem_tick(uint32_t now_ms) { (void)now_ms; }
```

- [ ] **Step 7: Write failing CLI telem tests in `test_cli.c`**

Extend stub `app_get_status` with known values. After `cli_init`:

```c
static void test_telem_off_by_default_and_emits_when_on(void)
{
    tx_length = 0;
    tx_text[0] = '\0';
    cli_telem_tick(100);
    assert(tx_length == 0); /* off */

    issue("telem on\n");
    assert(strstr(tx_text, "OK telem on") != NULL);
    tx_length = 0;
    tx_text[0] = '\0';
    cli_telem_tick(100);
    assert(strncmp(tx_text, "T,", 2) == 0);
    assert(strstr(tx_text, "\r\n") != NULL);

    issue("telem off\n");
    tx_length = 0;
    tx_text[0] = '\0';
    cli_telem_tick(200);
    assert(tx_length == 0);
}
```

Host fake: `huart->gState` must be `HAL_UART_STATE_READY` — set in fake UART handle if needed (`cli_uart.gState = HAL_UART_STATE_READY`).

- [ ] **Step 8: Build and run host tests**

From `firmware/wing_fold_actuator/tests/host` (use existing CMake/build script the repo already uses for CLI tests). Expected: all OK including new telem test.

- [ ] **Step 9: Rebuild firmware hex** (CubeIDE / existing Debug build). Note hex path for user flash.

---

### Task 2: Python parser + sample model (TDD)

**Files:**
- Create: `tools/telem_viewer/requirements.txt`
- Create: `tools/telem_viewer/telem_viewer/__init__.py`
- Create: `tools/telem_viewer/telem_viewer/parser.py`
- Create: `tools/telem_viewer/telem_viewer/model.py`
- Create: `tools/telem_viewer/tests/test_parser.py`
- Create: `tools/telem_viewer/captures/.gitkeep`
- Create: `tools/telem_viewer/.gitignore` with `captures/*/` except `.gitkeep`

**Interfaces:**
- Produces:
  - `@dataclass Sample` with fields matching telem columns + optional `rpm_est: float | None`
  - `parse_telem_line(line: str) -> Sample | None`
  - `class SampleBuffer` with `append(s)`, `clear()`, `as_lists()` for plotting, max length 6000 (60 s @ 100 Hz)

- [ ] **Step 1: Write `requirements.txt`**

```text
pyside6>=6.6
pyqtgraph>=0.13
pyserial>=3.5
pytest>=7.0
```

- [ ] **Step 2: Write failing parser tests**

```python
# tools/telem_viewer/tests/test_parser.py
from telem_viewer.parser import parse_telem_line

def test_parse_valid_t_line():
    s = parse_telem_line("T,1234,1500,100,200,-100,500,480,0,0,1,0\r\n")
    assert s is not None
    assert s.ms == 1234
    assert s.pwm == 1500
    assert s.count == 100
    assert s.tgt == 200
    assert s.err == -100
    assert s.spd_cmd == 500
    assert s.spd_out == 480
    assert s.hold == 0
    assert s.settled == 0
    assert s.last_dir == 1
    assert s.fault == 0

def test_ignore_status_and_noise():
    assert parse_telem_line("count=1 tgt=2\r\n") is None
    assert parse_telem_line("OK telem on\r\n") is None
    assert parse_telem_line("") is None
```

- [ ] **Step 3: Run pytest — expect fail**

```bash
cd tools/telem_viewer
pip install -r requirements.txt
python -m pytest tests/test_parser.py -v
```

Expected: import/collection failure or FAIL.

- [ ] **Step 4: Implement `parser.py` and `model.py`**

```python
# model.py
from dataclasses import dataclass

@dataclass
class Sample:
    ms: int
    pwm: int
    count: int
    tgt: int
    err: int
    spd_cmd: int
    spd_out: int
    hold: int
    settled: int
    last_dir: int
    fault: int
    rpm_est: float | None = None

class SampleBuffer:
    def __init__(self, maxlen: int = 6000):
        self.maxlen = maxlen
        self.samples: list[Sample] = []

    def append(self, s: Sample) -> None:
        self.samples.append(s)
        if len(self.samples) > self.maxlen:
            self.samples = self.samples[-self.maxlen:]

    def clear(self) -> None:
        self.samples.clear()
```

```python
# parser.py
from .model import Sample

def parse_telem_line(line: str) -> Sample | None:
    line = line.strip()
    if not line.startswith("T,"):
        return None
    parts = line.split(",")
    if len(parts) != 12:
        return None
    try:
        return Sample(
            ms=int(parts[1]),
            pwm=int(parts[2]),
            count=int(parts[3]),
            tgt=int(parts[4]),
            err=int(parts[5]),
            spd_cmd=int(parts[6]),
            spd_out=int(parts[7]),
            hold=int(parts[8]),
            settled=int(parts[9]),
            last_dir=int(parts[10]),
            fault=int(parts[11]),
        )
    except ValueError:
        return None
```

- [ ] **Step 5: Run pytest — expect PASS**

```bash
python -m pytest tests/test_parser.py -v
```

---

### Task 3: Diagnostics engine (TDD)

**Files:**
- Create: `tools/telem_viewer/telem_viewer/diagnostics.py`
- Create: `tools/telem_viewer/tests/test_diagnostics.py`

**Interfaces:**
- Produces: `@dataclass Event` with `t_ms: int`, `type: str`, `message: str`, `hint: str`
- Produces: `analyze(samples: list[Sample]) -> list[Event]`
- Default thresholds (module constants, overridable later):  
  `HUNT_FLIP_N=6` in 1000 ms; `LAG_ERR=800`, `LAG_MS=500`; `STALL_CMD=200`, `STALL_DCOUNT=5`, `STALL_MS=300`; `SETTLE_CHATTER_EDGES=4` in 2000 ms

- [ ] **Step 1: Write failing tests with synthetic hunt**

```python
from telem_viewer.model import Sample
from telem_viewer.diagnostics import analyze

def _s(ms, err=500, spd_out=0, last_dir=0, settled=0, pwm=1500, count=0, tgt=500):
    return Sample(ms, pwm, count, tgt, err, spd_out, spd_out, 0, settled, last_dir, 0)

def test_hunt_detects_rapid_dir_flips():
    samples = []
    for i in range(20):
        d = 1 if (i % 2 == 0) else -1
        samples.append(_s(ms=i * 50, last_dir=d, spd_out=400 * d, err=600))
    events = analyze(samples)
    types = [e.type for e in events]
    assert "HUNT" in types
```

Add at least one test each for `STALL` (cmd high, count flat) and `SETTLE_CHATTER` (settled toggles).

- [ ] **Step 2: pytest fail, then implement `analyze`**

Implement sliding-window checks over `samples` sorted by `ms`. Emit at most one event of each type per analysis pass (or rate-limit duplicates) to avoid spam.

Hints:
- HUNT → `Increase CONTROL_REVERSE_LOCK or reduce vmax`
- STALL → `Check load/servo bus; try higher |motor| speed`
- SETTLE_CHATTER → `Widen deadzone / DZ_EXIT_MUL`
- LAG / OVERSHOOT / RAMP_LAG / PWM_GLITCH as in spec

- [ ] **Step 3: pytest pass**

```bash
python -m pytest tests/test_diagnostics.py tests/test_parser.py -v
```

---

### Task 4: Capture + rpm_est helper

**Files:**
- Create: `tools/telem_viewer/telem_viewer/capture.py`
- Create: `tools/telem_viewer/tests/test_capture.py`

**Interfaces:**
- `enrich_rpm(samples: list[Sample]) -> None` mutates `rpm_est` using Δcount/Δms (counts per second; label as count/s in UI, not true RPM unless documented)
- `CaptureSession.start(base_dir) -> path`
- `write_sample(sample)`, `write_raw(line)`, `write_events(events)`, `close()`
- `load_capture(dir) -> tuple[list[Sample], list[Event]]`

CSV header:

```text
ms,pwm,count,tgt,err,spd_cmd,spd_out,hold,settled,last_dir,fault,rpm_est
```

- [ ] **Step 1: Tests for round-trip CSV write/read**
- [ ] **Step 2: Implement capture.py**
- [ ] **Step 3: pytest pass**

---

### Task 5: Serial worker + main window GUI

**Files:**
- Create: `tools/telem_viewer/telem_viewer/serial_worker.py`
- Create: `tools/telem_viewer/telem_viewer/main_window.py`
- Create: `tools/telem_viewer/telem_viewer/__main__.py`
- Create: `tools/telem_viewer/README.md`

**Interfaces:**
- `SerialWorker(QObject)` signals: `line_received(str)`, `error(str)`, `connected(bool)`
- Methods: `open(port, baud=115200)`, `close()`, `send(str)` (append `\n` if needed)
- Optional timer in UI: every 500 ms send `status` while connected and “poll status” checkbox checked (scheme C)
- Main window: port combo (`serial.tools.list_ports`), Connect, Telem On/Off, Record, Clear, Open capture; 4 pyqtgraph PlotWidgets; event QTextEdit; raw QTextEdit

- [ ] **Step 1: Implement `serial_worker.py`** — read thread loop, decode utf-8 with replace, emit complete lines on `\n`

- [ ] **Step 2: Implement `main_window.py`**

On each `T` line: parse → enrich rpm using last sample → buffer.append → if recording write → every N ms refresh plots (throttle UI to ~20 Hz) → periodically `analyze(buffer.samples[-500:])` and append new events to text pane.

Plot curves:
1. count, tgt vs ms
2. err vs ms
3. spd_cmd, spd_out, rpm_est vs ms
4. pwm vs ms

- [ ] **Step 3: `__main__.py`**

```python
from PySide6.QtWidgets import QApplication
from telem_viewer.main_window import MainWindow
import sys

def main():
    app = QApplication(sys.argv)
    w = MainWindow()
    w.show()
    sys.exit(app.exec())

if __name__ == "__main__":
    main()
```

- [ ] **Step 4: README with install/run and bench recipe**

```bash
cd tools/telem_viewer
pip install -r requirements.txt
python -m telem_viewer
```

Bench: flash firmware → connect COM → Telem On → Record → slow PWM 1000→2000 → stop → check HUNT events.

- [ ] **Step 5: Smoke-run GUI on Windows** (opens window; Connect may wait for hardware)

---

### Task 6: Docs sync

**Files:**
- Modify: `docs/PROTOCOL_NOTES.md` — document `telem` commands and `T,` line
- Modify: `README.md` — link to `tools/telem_viewer/README.md` and mention telem for hunt debug

- [ ] **Step 1: Add protocol section** (copy format table from spec)
- [ ] **Step 2: Add README pointer**

---

## Spec coverage checklist

| Spec item | Task |
|-----------|------|
| `telem on/off` + 10 ms CSV | 1 |
| Fields incl. settled/last_dir | 1 |
| Drop if TX busy | 1 |
| PySide6 + pyqtgraph tool | 5 |
| Charts 1–4 | 5 |
| Diagnostics HUNT…SETTLE | 3 |
| raw.log + telem.csv + events.json | 4 |
| status poll option (C) | 5 |
| PROTOCOL_NOTES / README | 6 |
| Host/unit tests | 1, 2, 3, 4 |

## Placeholder / consistency self-review

- Line field order matches firmware `snprintf` and Python `Sample` / CSV.
- `cli_telem_tick` name used in app and tests.
- `rpm_est` is count/s derived on PC only (not in firmware line) — documented in UI label as `dcount/s`.

---

**Plan complete and saved to** `docs/superpowers/plans/2026-08-25-telem-viewer.md`.

**Two execution options:**

1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks  
2. **Inline Execution** — implement in this session with checkpoints  

Which approach?
