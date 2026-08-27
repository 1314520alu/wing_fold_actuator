from telem_viewer.diagnostics import Event, analyze
from telem_viewer.model import Sample


def _s(
    ms,
    err=500,
    spd_out=0,
    last_dir=0,
    settled=0,
    pwm=1500,
    count=0,
    tgt=500,
    spd_cmd=None,
    hold=0,
    fault=0,
):
    cmd = spd_out if spd_cmd is None else spd_cmd
    return Sample(ms, pwm, count, tgt, err, cmd, spd_out, hold, settled, last_dir, fault)


def test_hunt_detects_rapid_dir_flips():
    samples = []
    for i in range(20):
        d = 1 if (i % 2 == 0) else -1
        samples.append(_s(ms=i * 50, last_dir=d, spd_out=400 * d, err=600))
    events = analyze(samples)
    types = [e.type for e in events]
    assert "HUNT" in types


def test_stall_detects_cmd_high_count_flat():
    samples = []
    for i in range(40):
        samples.append(_s(ms=i * 10, spd_out=500, count=100, tgt=500, err=400))
    events = analyze(samples)
    assert "STALL" in [e.type for e in events]


def test_settle_chatter_detects_toggles():
    samples = []
    for i in range(10):
        settled = i % 2
        samples.append(_s(ms=i * 50, settled=settled, err=10, tgt=500, count=500))
    events = analyze(samples)
    assert "SETTLE_CHATTER" in [e.type for e in events]


def test_lag_detects_persistent_error_while_pwm_moving():
    samples = []
    for i in range(60):
        pwm = 1500 + i * 2
        samples.append(_s(ms=i * 10, pwm=pwm, err=900, tgt=500, count=100))
    events = analyze(samples)
    assert "LAG" in [e.type for e in events]


def test_overshoot_detects_err_cross_and_reverse():
    samples = [
        _s(ms=0, err=100, last_dir=1, spd_out=300, count=400, tgt=500),
        _s(ms=10, err=20, last_dir=1, spd_out=200, count=480, tgt=500),
        _s(ms=20, err=-30, last_dir=1, spd_out=150, count=520, tgt=500),
        _s(ms=30, err=-80, last_dir=-1, spd_out=-200, count=530, tgt=500),
        _s(ms=40, err=-150, last_dir=-1, spd_out=-250, count=540, tgt=500),
    ]
    events = analyze(samples)
    assert "OVERSHOOT" in [e.type for e in events]


def test_ramp_lag_detects_spd_mismatch():
    samples = []
    for i in range(40):
        samples.append(
            Sample(i * 10, 1500, 100 + i, 500, 400, 500, 100, 0, 0, 1, 0)
        )
    events = analyze(samples)
    assert "RAMP_LAG" in [e.type for e in events]


def test_pwm_glitch_detects_pwm_jump_without_tgt_change():
    samples = [
        _s(ms=0, pwm=1500, tgt=500),
        _s(ms=10, pwm=1500, tgt=500),
        _s(ms=20, pwm=1700, tgt=500),
    ]
    events = analyze(samples)
    assert "PWM_GLITCH" in [e.type for e in events]


def test_analyze_empty_returns_empty():
    assert analyze([]) == []


def test_at_most_one_event_per_type():
    samples = []
    for i in range(20):
        d = 1 if (i % 2 == 0) else -1
        samples.append(_s(ms=i * 50, last_dir=d, spd_out=400 * d, err=600))
    events = analyze(samples)
    hunt_events = [e for e in events if e.type == "HUNT"]
    assert len(hunt_events) == 1


def test_event_fields():
    samples = []
    for i in range(20):
        d = 1 if (i % 2 == 0) else -1
        samples.append(_s(ms=i * 50, last_dir=d, spd_out=400 * d, err=600))
    events = analyze(samples)
    hunt = next(e for e in events if e.type == "HUNT")
    assert isinstance(hunt, Event)
    assert hunt.t_ms >= 0
    assert hunt.message
    assert hunt.hint
