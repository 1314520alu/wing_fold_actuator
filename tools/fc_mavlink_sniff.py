#!/usr/bin/env python3
"""Sniff MAVLink NAMED_VALUE_FLOAT from wing-fold USART3 (FC_MAVLINK build).

Example:
  python tools/fc_mavlink_sniff.py COM5
  python tools/fc_mavlink_sniff.py /dev/ttyUSB0 --baud 115200
"""

from __future__ import annotations

import argparse
import sys
import time


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="Serial port (e.g. COM5)")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    try:
        from pymavlink import mavutil
    except ImportError:
        print("Need pymavlink: pip install pymavlink", file=sys.stderr)
        return 1

    m = mavutil.mavlink_connection(args.port, baud=args.baud, dialect="common")
    print(f"Listening on {args.port} @ {args.baud} for NAMED_VALUE_FLOAT…")
    print("Expected names: fold_pct fold_cnt fold_flt fold_pwm fold_hld")
    t0 = time.time()
    while True:
        msg = m.recv_match(type="NAMED_VALUE_FLOAT", blocking=True, timeout=2.0)
        if msg is None:
            print(f"[{time.time() - t0:6.1f}s] (no message)")
            continue
        name = msg.name
        if isinstance(name, bytes):
            name = name.split(b"\0", 1)[0].decode("ascii", errors="replace")
        else:
            name = str(name).split("\0", 1)[0]
        print(f"{name:10s} = {msg.value:g}")


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nbye")
        raise SystemExit(0)
