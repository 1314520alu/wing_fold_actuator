from __future__ import annotations

import csv
import json
from datetime import datetime
from pathlib import Path

from telem_viewer.diagnostics import Event
from telem_viewer.model import Sample

CSV_HEADER = (
    "ms,pwm,count,tgt,err,spd_cmd,spd_out,hold,settled,last_dir,fault,rpm_est"
)
CSV_FIELDS = CSV_HEADER.split(",")


def enrich_rpm(samples: list[Sample]) -> None:
    """Set rpm_est on each sample from delta count / delta ms (counts per second)."""
    if not samples:
        return

    samples[0].rpm_est = None
    for i in range(1, len(samples)):
        dt_ms = samples[i].ms - samples[i - 1].ms
        if dt_ms <= 0:
            samples[i].rpm_est = None
            continue
        dcount = samples[i].count - samples[i - 1].count
        samples[i].rpm_est = dcount / dt_ms * 1000.0


def _rpm_cell(value: float | None) -> str:
    if value is None:
        return ""
    return f"{value:.6g}"


def _parse_rpm_cell(text: str) -> float | None:
    text = text.strip()
    if not text:
        return None
    return float(text)


class CaptureSession:
    def __init__(self, capture_dir: Path):
        self.path = capture_dir
        self._csv_path = capture_dir / "telem.csv"
        self._raw_path = capture_dir / "raw.log"
        self._events_path = capture_dir / "events.json"
        self._csv_file = self._csv_path.open("w", newline="", encoding="utf-8")
        self._raw_file = self._raw_path.open("w", encoding="utf-8")
        self._csv = csv.writer(self._csv_file)
        self._csv.writerow(CSV_FIELDS)
        self._closed = False

    @classmethod
    def start(cls, base_dir: str | Path) -> Path:
        base = Path(base_dir)
        base.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")[:-3]
        capture_dir = base / stamp
        suffix = 2
        while capture_dir.exists():
            capture_dir = base / f"{stamp}_{suffix}"
            suffix += 1
        capture_dir.mkdir(parents=True, exist_ok=False)
        cls._active = cls(capture_dir)
        return capture_dir

    def write_sample(self, sample: Sample) -> None:
        self._csv.writerow(
            [
                sample.ms,
                sample.pwm,
                sample.count,
                sample.tgt,
                sample.err,
                sample.spd_cmd,
                sample.spd_out,
                sample.hold,
                sample.settled,
                sample.last_dir,
                sample.fault,
                _rpm_cell(sample.rpm_est),
            ]
        )
        self._csv_file.flush()

    def write_raw(self, line: str) -> None:
        stamp = datetime.now().isoformat(timespec="milliseconds")
        self._raw_file.write(f"{stamp} {line}")
        if not line.endswith("\n"):
            self._raw_file.write("\n")
        self._raw_file.flush()

    def write_events(self, events: list[Event]) -> None:
        payload = [
            {
                "t_ms": e.t_ms,
                "type": e.type,
                "message": e.message,
                "hint": e.hint,
            }
            for e in events
        ]
        self._events_path.write_text(
            json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )

    def close(self) -> None:
        if self._closed:
            return
        self._csv_file.close()
        self._raw_file.close()
        self._closed = True
        if getattr(type(self), "_active", None) is self:
            type(self)._active = None

    @classmethod
    def active(cls) -> CaptureSession:
        session = getattr(cls, "_active", None)
        if session is None or session._closed:
            raise RuntimeError("No active capture session; call CaptureSession.start first")
        return session


def load_capture(capture_dir: str | Path) -> tuple[list[Sample], list[Event]]:
    root = Path(capture_dir)
    if root.is_file():
        if root.name.lower() != "telem.csv":
            raise ValueError(f"Expected telem.csv, got {root.name}")
        root = root.parent
    samples: list[Sample] = []

    with (root / "telem.csv").open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            samples.append(
                Sample(
                    ms=int(row["ms"]),
                    pwm=int(row["pwm"]),
                    count=int(row["count"]),
                    tgt=int(row["tgt"]),
                    err=int(row["err"]),
                    spd_cmd=int(row["spd_cmd"]),
                    spd_out=int(row["spd_out"]),
                    hold=int(row["hold"]),
                    settled=int(row["settled"]),
                    last_dir=int(row["last_dir"]),
                    fault=int(row["fault"]),
                    rpm_est=_parse_rpm_cell(row.get("rpm_est", "")),
                )
            )

    events: list[Event] = []
    events_path = root / "events.json"
    if events_path.is_file():
        raw = json.loads(events_path.read_text(encoding="utf-8"))
        events = [
            Event(
                t_ms=int(item["t_ms"]),
                type=str(item["type"]),
                message=str(item["message"]),
                hint=str(item["hint"]),
            )
            for item in raw
        ]

    return samples, events
