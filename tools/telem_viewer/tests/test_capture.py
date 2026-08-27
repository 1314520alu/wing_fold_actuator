import json
from pathlib import Path

import pytest

from telem_viewer.capture import (
    CSV_HEADER,
    CaptureSession,
    enrich_rpm,
    load_capture,
)
from telem_viewer.diagnostics import Event
from telem_viewer.model import Sample


def _sample(ms, count=0, rpm_est=None, **kwargs):
    defaults = dict(
        pwm=1500,
        tgt=500,
        err=100,
        spd_cmd=300,
        spd_out=280,
        hold=0,
        settled=0,
        last_dir=1,
        fault=0,
    )
    defaults.update(kwargs)
    return Sample(ms, count=count, rpm_est=rpm_est, **defaults)


def test_enrich_rpm_computes_counts_per_second():
    samples = [
        _sample(0, count=100),
        _sample(10, count=110),
        _sample(20, count=90),
    ]
    enrich_rpm(samples)
    assert samples[0].rpm_est is None
    assert samples[1].rpm_est == pytest.approx(1000.0)
    assert samples[2].rpm_est == pytest.approx(-2000.0)


def test_enrich_rpm_skips_zero_dt():
    samples = [_sample(0, count=100), _sample(0, count=110)]
    enrich_rpm(samples)
    assert samples[1].rpm_est is None


def test_capture_round_trip_csv_and_events(tmp_path):
    base = tmp_path / "captures"
    capture_dir = CaptureSession.start(base)
    session = CaptureSession.active()
    assert capture_dir.is_dir()
    assert capture_dir.parent == base
    assert session.path == capture_dir

    samples = [
        _sample(0, count=100),
        _sample(10, count=110),
        _sample(20, count=125),
    ]
    enrich_rpm(samples)

    for s in samples:
        session.write_sample(s)

    session.write_raw("T,0,1500,100,500,100,300,280,0,0,1,0\r\n")
    session.write_raw("status reply\n")

    events = [
        Event(500, "HUNT", "flipping", "Increase CONTROL_REVERSE_LOCK or reduce vmax"),
        Event(1200, "STALL", "stuck", "Check load/servo bus; try higher |motor| speed"),
    ]
    session.write_events(events)
    session.close()

    csv_text = (capture_dir / "telem.csv").read_text(encoding="utf-8")
    assert csv_text.splitlines()[0] == CSV_HEADER
    assert len(csv_text.strip().splitlines()) == 1 + len(samples)

    raw_text = (capture_dir / "raw.log").read_text(encoding="utf-8")
    assert "T,0,1500" in raw_text
    assert "status reply" in raw_text

    loaded_samples, loaded_events = load_capture(capture_dir)
    assert len(loaded_samples) == 3
    assert loaded_samples[0].ms == 0
    assert loaded_samples[1].count == 110
    assert loaded_samples[1].rpm_est == pytest.approx(1000.0)
    assert loaded_samples[2].rpm_est == pytest.approx(1500.0)

    assert len(loaded_events) == 2
    assert loaded_events[0].type == "HUNT"
    assert loaded_events[1].hint == "Check load/servo bus; try higher |motor| speed"

    events_json = json.loads((capture_dir / "events.json").read_text(encoding="utf-8"))
    assert events_json[0]["t_ms"] == 500


def test_load_capture_without_events_file(tmp_path):
    capture_dir = tmp_path / "cap"
    capture_dir.mkdir()
    (capture_dir / "telem.csv").write_text(
        CSV_HEADER + "\n0,1500,100,500,100,300,280,0,0,1,0,\n",
        encoding="utf-8",
    )

    samples, events = load_capture(capture_dir)
    assert len(samples) == 1
    assert samples[0].rpm_est is None
    assert events == []


def test_write_sample_requires_active_session():
    with pytest.raises(RuntimeError):
        CaptureSession.active().write_sample(_sample(0))


def test_capture_start_avoids_timestamp_collisions(tmp_path, monkeypatch):
    import telem_viewer.capture as capture_module

    class FixedDateTime:
        @classmethod
        def now(cls):
            return cls()

        def strftime(self, _format):
            return "20260825_154800_123000"

    monkeypatch.setattr(capture_module, "datetime", FixedDateTime)
    first = CaptureSession.start(tmp_path)
    CaptureSession.active().close()
    second = CaptureSession.start(tmp_path)
    CaptureSession.active().close()

    assert first.name == "20260825_154800_123"
    assert second.name == "20260825_154800_123_2"


def test_load_capture_accepts_telem_csv_path(tmp_path):
    capture_dir = tmp_path / "cap"
    capture_dir.mkdir()
    csv_path = capture_dir / "telem.csv"
    csv_path.write_text(
        CSV_HEADER + "\n0,1500,100,500,100,300,280,0,0,1,0,\n",
        encoding="utf-8",
    )

    samples, events = load_capture(csv_path)

    assert len(samples) == 1
    assert events == []
