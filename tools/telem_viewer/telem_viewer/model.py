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
    _PLOT_FIELDS = (
        "ms",
        "pwm",
        "count",
        "tgt",
        "err",
        "spd_cmd",
        "spd_out",
        "hold",
        "settled",
        "last_dir",
        "fault",
        "rpm_est",
    )

    def __init__(self, maxlen: int = 6000):
        self.maxlen = maxlen
        self.samples: list[Sample] = []

    def append(self, s: Sample) -> None:
        self.samples.append(s)
        if len(self.samples) > self.maxlen:
            self.samples = self.samples[-self.maxlen :]

    def clear(self) -> None:
        self.samples.clear()

    def as_lists(self) -> dict[str, list]:
        out: dict[str, list] = {field: [] for field in self._PLOT_FIELDS}
        for s in self.samples:
            for field in self._PLOT_FIELDS:
                out[field].append(getattr(s, field))
        return out
