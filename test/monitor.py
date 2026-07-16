#!/usr/bin/env python3
"""
Live BME680 monitor — reads the firmware's [METRIC] UART stream and redraws a
boxed dashboard of current sensor values on every reading. Read-only: it does
not build, flash, or write anything. Ctrl-C to quit.

Usage:
    uv run --with pyserial python test/monitor.py
    uv run --with pyserial python test/monitor.py --port /dev/cu.usbmodem4402
    uv run --with pyserial python test/monitor.py --selftest   # render one demo frame, no serial
"""

import argparse
import re
import sys
import time

METRIC_RE = re.compile(
    r"\[METRIC\]\s+read_ok=1\s+temp=(-?[\d.]+)\s+hum=(-?[\d.]+)\s+"
    r"press=(-?[\d.]+)\s+gas=(-?\d+)\s+gas_valid=(\d+)\s+"
    r"iaq=(-?[\d.]+)\s+iaq_baseline=(\d+)\s+warming_up=(\d+)\s+ts_ms=(\d+)"
)
FAIL_RE = re.compile(r"\[METRIC\]\s+read_fail=1\s+err=(\w+)")

W = 46  # inner width of the box


def iaq_label(iaq):
    for hi, lbl in ((50, "excellent"), (100, "good"), (150, "lightly polluted"),
                    (200, "moderately polluted"), (300, "heavily polluted")):
        if iaq <= hi:
            return lbl
    return "severely polluted"


def _row(label, value, unit):
    inner = f"  {label:<13}{value:>12}  {unit:<15}"
    return "│" + inner[:W].ljust(W) + "│"


def _rule():
    return "├" + "─" * W + "┤"


def frame(port, reads, fails, m):
    temp, hum, press, gas, gas_valid, iaq, baseline, warming, ts = m
    total = reads + fails
    fail_rate = (fails / total * 100.0) if total else 0.0
    lines = []
    lines.append("┌" + "─" * W + "┐")
    title = "  BME680 — live"
    lines.append("│" + title.ljust(W) + "│")
    lines.append(_rule())
    lines.append(_row("Temperature", f"{temp:.1f}", "degC"))
    lines.append(_row("Humidity", f"{hum:.1f}", "%RH"))
    lines.append(_row("Pressure", f"{press:.0f}", "hPa"))
    lines.append(_row("Gas resist.", f"{gas/1e6:.3f}", "MOhm"))
    lines.append(_row("IAQ index", f"{iaq:.1f}", iaq_label(iaq)))
    lines.append(_rule())
    status = ("gas_valid" if gas_valid else "gas_INVALID") + "  " + ("warming" if warming else "warm")
    lines.append("│" + f"  {status}".ljust(W) + "│")
    meta = f"  reads {reads}  fails {fails}  {fail_rate:.2f}%  up {ts/1000:.0f}s"
    lines.append("│" + meta[:W].ljust(W) + "│")
    lines.append("└" + "─" * W + "┘")
    return "\n".join(lines)


def draw(port, reads, fails, m):
    print("\033[2J\033[H", end="")  # clear screen + cursor home
    print(frame(port, reads, fails, m))
    print("  (Ctrl-C to quit)", flush=True)


def main():
    ap = argparse.ArgumentParser(description="Live BME680 boxed monitor")
    ap.add_argument("--port", default="/dev/cu.usbmodem4402")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--selftest", action="store_true", help="render one demo frame and exit")
    args = ap.parse_args()

    if args.selftest:
        demo = (27.2, 53.5, 962, 12917167, 1, 28.1, 12917167, 0, 42000)
        print(frame(args.port, 5, 0, demo))
        return 0

    import serial
    reads = fails = 0
    last = None
    ser = None
    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
        ser.reset_input_buffer()
        print(f"Listening on {args.port} @ {args.baud} ... waiting for first reading")
        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode("utf-8", errors="replace").strip()
                m = METRIC_RE.search(line)
                if m:
                    reads += 1
                    last = (float(m.group(1)), float(m.group(2)), float(m.group(3)),
                            int(m.group(4)), int(m.group(5)), float(m.group(6)),
                            int(m.group(7)), int(m.group(8)), int(m.group(9)))
                    draw(args.port, reads, fails, last)
                elif FAIL_RE.search(line):
                    fails += 1
                    if last:
                        draw(args.port, reads, fails, last)
            else:
                time.sleep(0.05)
    except serial.SerialException as e:
        print(f"SERIAL ERROR: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nbye")
        return 0
    finally:
        if ser is not None and ser.is_open:
            ser.close()


if __name__ == "__main__":
    sys.exit(main())
