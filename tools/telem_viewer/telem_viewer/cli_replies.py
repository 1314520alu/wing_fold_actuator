from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Any


@dataclass(frozen=True)
class StatusReply:
    count: int | None
    motor: int
    cal: str
    enc_fail: int
    hold: int
    tgt: int
    spd_cmd: int
    spd_out: int
    fault: int
    pwm: int | None = None
    backend: str | None = None


@dataclass(frozen=True)
class CalShowReply:
    count_a: int
    count_b: int
    saved: bool
    pwm_min: int | None = None
    pwm_max: int | None = None
    deadzone: int | None = None
    kp: int | None = None
    vmax: int | None = None
    cruise: int | None = None


@dataclass(frozen=True)
class CalCaptureReply:
    endpoint: str
    count: int


@dataclass(frozen=True)
class ErrReply:
    message: str


@dataclass(frozen=True)
class OkReply:
    message: str


_STATUS_RE = re.compile(
    r"^count=(?P<count>-?\d+|ERR)\s+"
    r"motor=(?P<motor>-?\d+)\s+"
    r"cal=(?P<cal>\S+)\s+"
    r"enc_fail=(?P<enc_fail>\d+)\s+"
    r"pwm=(?P<pwm>\d+)\s+"
    r"raw=(?P<raw>\d+)\s+"
    r"irq=(?P<irq>\d+)\s+"
    r"age=(?P<age>\d+)ms\s+"
    r"hold=(?P<hold>\d+)\s+"
    r"tgt=(?P<tgt>-?\d+)\s+"
    r"spd=(?P<spd_cmd>-?\d+)/(?P<spd_out>-?\d+)\s+"
    r"fault=(?P<fault>\d+)"
    r"(?:\s+backend=(?P<backend>\S+))?\s*$"
)

_CAL_SHOW_RE = re.compile(
    r"^a=(?P<a>-?\d+)\s+b=(?P<b>-?\d+)\s+"
    r"pwm=(?P<pwm_min>\d+)\.\.(?P<pwm_max>\d+)\s+"
    r"dz=(?P<dz>-?\d+)\s+kp=(?P<kp>-?\d+)\s+"
    r"vmax=(?P<vmax>-?\d+)\s+cruise=(?P<cruise>-?\d+)\s+"
    r"(?P<saved>calibrated|NOT SAVED)\s*$"
)

_OK_CAL_RE = re.compile(r"^OK cal (?P<endpoint>[ab])=(?P<count>-?\d+)\s*$")
_OK_RE = re.compile(r"^OK(?:\s+(?P<msg>.+))?\s*$")
_ERR_RE = re.compile(r"^ERR(?:\s+(?P<msg>.+))?\s*$")

# USB-UART local echo often glues the typed command onto the reply with no newline:
#   "cal show" + "a=0 b=…" → "cal showa=0 b=…"
#   "telem off" + "OK telem off" → "telem offOK telem off"
_REPLY_MARKERS = ("count=", "a=", "OK ", "ERR ", "telem=")


def normalize_cli_line(line: str) -> str:
    """Strip command echo / garbage before the first recognizable reply marker."""
    text = line.strip()
    if not text:
        return text
    earliest = -1
    for marker in _REPLY_MARKERS:
        idx = text.find(marker)
        if idx < 0:
            continue
        if earliest < 0 or idx < earliest:
            earliest = idx
    # Also handle "OK" / "ERR" with no trailing space (rare).
    for bare in ("OK", "ERR"):
        if text.startswith(bare) and (len(text) == len(bare) or text[len(bare)] in " \t"):
            return text
        idx = text.find(bare)
        if idx > 0 and (idx + len(bare) == len(text) or text[idx + len(bare)] in " \t"):
            if earliest < 0 or idx < earliest:
                earliest = idx
    if earliest > 0:
        return text[earliest:].strip()
    return text


def parse_cli_reply(
    line: str,
) -> StatusReply | CalShowReply | CalCaptureReply | ErrReply | OkReply | None:
    text = normalize_cli_line(line)
    if not text or text.startswith("T,"):
        return None

    match = _STATUS_RE.match(text)
    if match is not None:
        count_raw = match.group("count")
        return StatusReply(
            count=None if count_raw == "ERR" else int(count_raw),
            motor=int(match.group("motor")),
            cal=match.group("cal"),
            enc_fail=int(match.group("enc_fail")),
            hold=int(match.group("hold")),
            tgt=int(match.group("tgt")),
            spd_cmd=int(match.group("spd_cmd")),
            spd_out=int(match.group("spd_out")),
            fault=int(match.group("fault")),
            pwm=int(match.group("pwm")),
            backend=match.group("backend"),
        )

    match = _CAL_SHOW_RE.match(text)
    if match is not None:
        return CalShowReply(
            count_a=int(match.group("a")),
            count_b=int(match.group("b")),
            saved=match.group("saved") == "calibrated",
            pwm_min=int(match.group("pwm_min")),
            pwm_max=int(match.group("pwm_max")),
            deadzone=int(match.group("dz")),
            kp=int(match.group("kp")),
            vmax=int(match.group("vmax")),
            cruise=int(match.group("cruise")),
        )

    match = _OK_CAL_RE.match(text)
    if match is not None:
        return CalCaptureReply(
            endpoint=match.group("endpoint"),
            count=int(match.group("count")),
        )

    match = _ERR_RE.match(text)
    if match is not None:
        return ErrReply(message=(match.group("msg") or "").strip() or "error")

    match = _OK_RE.match(text)
    if match is not None:
        return OkReply(message=(match.group("msg") or "").strip() or "ok")

    return None


def reply_as_dict(reply: Any) -> dict[str, Any]:
    """Test helper: flatten dataclass replies to plain dicts."""
    if hasattr(reply, "__dataclass_fields__"):
        return {key: getattr(reply, key) for key in reply.__dataclass_fields__}
    return {}
