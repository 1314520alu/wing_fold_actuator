from dataclasses import dataclass

from telem_viewer.model import Sample

HUNT_FLIP_N = 6
HUNT_WINDOW_MS = 1000
HUNT_ERR_MIN = 200

LAG_ERR = 800
LAG_MS = 500
LAG_PWM_DELTA = 5

STALL_CMD = 200
STALL_DCOUNT = 5
STALL_MS = 300

SETTLE_CHATTER_EDGES = 4
SETTLE_CHATTER_WINDOW_MS = 2000

OVERSHOOT_ERR_MIN = 50
RAMP_LAG_DELTA = 100
RAMP_LAG_MS = 300

PWM_GLITCH_DELTA = 80


@dataclass
class Event:
    t_ms: int
    type: str
    message: str
    hint: str


def analyze(samples: list[Sample]) -> list[Event]:
    if not samples:
        return []

    ordered = sorted(samples, key=lambda s: s.ms)
    events: list[Event] = []
    seen: set[str] = set()

    for detector in (
        _detect_hunt,
        _detect_lag,
        _detect_overshoot,
        _detect_stall,
        _detect_ramp_lag,
        _detect_pwm_glitch,
        _detect_settle_chatter,
    ):
        event = detector(ordered)
        if event is not None and event.type not in seen:
            events.append(event)
            seen.add(event.type)

    events.sort(key=lambda e: e.t_ms)
    return events


def _window(samples: list[Sample], end_ms: int, span_ms: int) -> list[Sample]:
    start_ms = end_ms - span_ms
    return [s for s in samples if start_ms <= s.ms <= end_ms]


def _dir_sign(s: Sample) -> int:
    if s.last_dir > 0:
        return 1
    if s.last_dir < 0:
        return -1
    if s.spd_out > 0:
        return 1
    if s.spd_out < 0:
        return -1
    return 0


def _count_dir_flips(window: list[Sample]) -> int:
    flips = 0
    prev = 0
    for s in window:
        if abs(s.err) <= HUNT_ERR_MIN:
            continue
        sign = _dir_sign(s)
        if sign == 0:
            continue
        if prev != 0 and sign != prev:
            flips += 1
        prev = sign
    return flips


def _detect_hunt(samples: list[Sample]) -> Event | None:
    for s in samples:
        flips = _count_dir_flips(_window(samples, s.ms, HUNT_WINDOW_MS))
        if flips >= HUNT_FLIP_N:
            return Event(
                t_ms=s.ms,
                type="HUNT",
                message=f"{HUNT_WINDOW_MS} ms 内方向翻转 {flips} 次，且 |err|>{HUNT_ERR_MIN}",
                hint="增大 CONTROL_REVERSE_LOCK，或降低 vmax",
            )
    return None


def _detect_lag(samples: list[Sample]) -> Event | None:
    for i, s in enumerate(samples):
        end_ms = s.ms + LAG_MS
        start_pwm = s.pwm
        end_pwm = s.pwm
        last_ms = s.ms
        ok = True

        for j in range(i, len(samples)):
            cur = samples[j]
            if cur.ms > end_ms:
                break
            if abs(cur.err) <= LAG_ERR:
                ok = False
                break
            end_pwm = cur.pwm
            last_ms = cur.ms

        pwm_moved = abs(end_pwm - start_pwm) >= LAG_PWM_DELTA

        if ok and pwm_moved and last_ms - s.ms >= LAG_MS - 1:
            return Event(
                t_ms=last_ms,
                type="LAG",
                message=f"PWM 变化期间 |err|>{LAG_ERR} 持续 {LAG_MS} ms",
                hint="检查 cruise 窗口 / vmax / 映射",
            )
    return None


def _detect_overshoot(samples: list[Sample]) -> Event | None:
    for i in range(1, len(samples) - 2):
        before = samples[i - 1].err
        mid = samples[i].err
        after = samples[i + 1].err
        later = samples[i + 2].err

        if before == 0 or after == 0:
            continue
        if before * after >= 0:
            continue
        if abs(after) < OVERSHOOT_ERR_MIN:
            continue
        if abs(later) <= abs(after):
            continue
        if (after > 0 and later <= after) or (after < 0 and later >= after):
            continue

        s = samples[i + 1]
        reverse = (before > 0 and (s.last_dir < 0 or s.spd_out < 0)) or (
            before < 0 and (s.last_dir > 0 or s.spd_out > 0)
        )
        if not reverse:
            continue

        return Event(
            t_ms=samples[i + 2].ms,
            type="OVERSHOOT",
            message="误差过零后反向增大，并出现反向指令",
            hint="增大反向锁定 / 制动区",
        )
    return None


def _detect_stall(samples: list[Sample]) -> Event | None:
    for i, s in enumerate(samples):
        if abs(s.spd_cmd) <= STALL_CMD:
            continue

        end_ms = s.ms + STALL_MS
        count_min = s.count
        count_max = s.count
        last_ms = s.ms
        ok = True

        for j in range(i, len(samples)):
            cur = samples[j]
            if cur.ms > end_ms:
                break
            if abs(cur.spd_cmd) <= STALL_CMD:
                ok = False
                break
            count_min = min(count_min, cur.count)
            count_max = max(count_max, cur.count)
            last_ms = cur.ms

        if not ok:
            continue
        if last_ms - s.ms < STALL_MS - 1:
            continue
        if count_max - count_min > STALL_DCOUNT:
            continue

        return Event(
            t_ms=last_ms,
            type="STALL",
            message=f"|spd_cmd|>{STALL_CMD} 但 |Δcount|<={STALL_DCOUNT}，持续 {STALL_MS} ms",
            hint="检查负载/舵机总线；可尝试提高 |motor| 速度",
        )
    return None


def _detect_ramp_lag(samples: list[Sample]) -> Event | None:
    for i, s in enumerate(samples):
        end_ms = s.ms + RAMP_LAG_MS
        last_ms = s.ms
        ok = True

        for j in range(i, len(samples)):
            cur = samples[j]
            if cur.ms > end_ms:
                break
            if abs(cur.spd_out - cur.spd_cmd) <= RAMP_LAG_DELTA:
                ok = False
                break
            last_ms = cur.ms

        if ok and last_ms - s.ms >= RAMP_LAG_MS - 1:
            return Event(
                t_ms=last_ms,
                type="RAMP_LAG",
                message=f"|spd_out-spd_cmd|>{RAMP_LAG_DELTA} 持续 {RAMP_LAG_MS} ms",
                hint="提高斜坡速率",
            )
    return None


def _detect_pwm_glitch(samples: list[Sample]) -> Event | None:
    for i in range(1, len(samples)):
        prev = samples[i - 1]
        cur = samples[i]
        if cur.tgt != prev.tgt:
            continue
        if abs(cur.pwm - prev.pwm) >= PWM_GLITCH_DELTA:
            return Event(
                t_ms=cur.ms,
                type="PWM_GLITCH",
                message=f"PWM 跳变 {cur.pwm - prev.pwm}，tgt 未变（tgt={cur.tgt}）",
                hint="检查 PWM 捕获 / 飞控映射",
            )
    return None


def _count_settled_edges(window: list[Sample]) -> int:
    edges = 0
    prev = window[0].settled
    for s in window[1:]:
        if s.settled != prev:
            edges += 1
        prev = s.settled
    return edges


def _detect_settle_chatter(samples: list[Sample]) -> Event | None:
    for s in samples:
        window = _window(samples, s.ms, SETTLE_CHATTER_WINDOW_MS)
        if len(window) < 2:
            continue
        edges = _count_settled_edges(window)
        if edges >= SETTLE_CHATTER_EDGES:
            return Event(
                t_ms=s.ms,
                type="SETTLE_CHATTER",
                message=f"{SETTLE_CHATTER_WINDOW_MS} ms 内 settled 边沿 {edges} 次",
                hint="加大死区 / DZ_EXIT_MUL",
            )
    return None
