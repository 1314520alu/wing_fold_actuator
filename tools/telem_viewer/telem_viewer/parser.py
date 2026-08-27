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
