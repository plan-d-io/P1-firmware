#!/usr/bin/env python3
"""Send console commands to the dongle and capture a bounded reply.

`pio device monitor` never exits, so a caller that waits for it hangs. This sends
one or more lines, reads for a fixed window (or until a sentinel), prints what
it got, and returns a status code.

Output goes to stdout *and* to a timestamped file under _local/serial/, same
as capture_serial.py.

Examples
--------
    python tools/serial_cmd.py --send "status" --seconds 5
    python tools/serial_cmd.py --send-file _local/provision.txt --until "saved"

Requires pyserial, which ships with PlatformIO. If you get an ImportError, run
it with the PlatformIO interpreter instead:

    python tools/serial_cmd.py
"""

from __future__ import annotations

import argparse
import datetime as _dt
import pathlib
import sys
import time

import capture_serial as cap

DEFAULT_BAUD = cap.DEFAULT_BAUD
DEFAULT_SECONDS = 5.0
DEFAULT_DELAY_MS = 200
LOG_DIR = cap.LOG_DIR


def collect_lines(ops: list[tuple[str, str]]) -> list[str]:
    """Expand --send / --send-file operations in argv order."""
    lines: list[str] = []
    for kind, value in ops:
        if kind == "line":
            lines.append(value)
            continue
        path = pathlib.Path(value)
        if not path.is_file():
            sys.exit(f"send-file not found: {path}")
        text = path.read_text(encoding="utf-8")
        for raw in text.splitlines():
            s = raw.strip()
            if not s or s.startswith("#"):
                continue
            lines.append(s)
    return lines


def capture_window(ser, seconds: float, until: str | None, sink) -> tuple[bool, bool]:
    """Read until deadline or sentinel. Returns (sentinel_hit, any_output)."""
    deadline = time.monotonic() + seconds
    found_sentinel = False
    any_output = False
    partial = b""

    while time.monotonic() < deadline:
        chunk = ser.read(4096)
        if not chunk:
            continue
        partial += chunk
        *complete, partial = partial.split(b"\n")
        for raw in complete:
            line = raw.decode("utf-8", errors="replace").rstrip("\r")
            any_output = True
            stamp = _dt.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            sink(f"[{stamp}] {line}")
            if until and until in line:
                return True, True

    if partial:
        line = partial.decode("utf-8", errors="replace").rstrip("\r")
        if line:
            any_output = True
            stamp = _dt.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            sink(f"[{stamp}] {line}")
            if until and until in line:
                found_sentinel = True

    return found_sentinel, any_output


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Send serial console commands and capture a bounded reply.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--port", help="Serial port, e.g. COM3. Auto-detected if omitted.")
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                    help=f"Baud rate (default {DEFAULT_BAUD}).")
    ap.add_argument("--send", action="append", default=[], metavar="TEXT",
                    help="One line to send; repeatable, sent in order.")
    ap.add_argument("--send-file", action="append", default=[], metavar="F",
                    dest="send_file",
                    help="Lines from a file, sent in order.")
    ap.add_argument("--delay", type=int, default=DEFAULT_DELAY_MS, metavar="MS",
                    help=f"Pause between lines in milliseconds (default {DEFAULT_DELAY_MS}).")
    ap.add_argument("--seconds", type=float, default=DEFAULT_SECONDS,
                    help=f"Capture window after the last line (default {DEFAULT_SECONDS:g}).")
    ap.add_argument("--until", metavar="TEXT",
                    help="Stop early when this appears in the output.")
    ap.add_argument("--reset", action="store_true",
                    help="Pulse DTR/RTS first, so you capture the boot banner.")
    ap.add_argument("--quiet", action="store_true",
                    help="Print only the captured output.")
    args = ap.parse_args()

    # Preserve argv order of --send / --send-file rather than grouping by flag.
    ops: list[tuple[str, str]] = []
    argv = sys.argv[1:]
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--send" and i + 1 < len(argv):
            ops.append(("line", argv[i + 1]))
            i += 2
            continue
        if a.startswith("--send=") and a != "--send=":
            ops.append(("line", a.split("=", 1)[1]))
            i += 1
            continue
        if a == "--send-file" and i + 1 < len(argv):
            ops.append(("file", argv[i + 1]))
            i += 2
            continue
        if a.startswith("--send-file=") and a != "--send-file=":
            ops.append(("file", a.split("=", 1)[1]))
            i += 1
            continue
        i += 1

    # argparse still validates unknown flags; if the user only used the
    # grouped options, fall back to those lists (same order within each).
    if not ops:
        ops.extend(("line", s) for s in args.send)
        ops.extend(("file", s) for s in args.send_file)

    lines = collect_lines(ops)
    port = cap.resolve_port(args.port)
    ser = cap.open_port(port, args.baud)

    LOG_DIR.mkdir(parents=True, exist_ok=True)
    stamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    log_path = LOG_DIR / f"{stamp}.log"
    log_file = log_path.open("w", encoding="utf-8")

    def sink(line: str) -> None:
        if not args.quiet or not line.startswith("--- "):
            print(line, flush=True)
        log_file.write(line + "\n")
        log_file.flush()

    def reset_board() -> None:
        ser.dtr = False
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
        time.sleep(0.05)
        ser.reset_input_buffer()

    header = (
        f"--- {port} @ {args.baud} baud, {len(lines)} line(s), "
        f"up to {args.seconds:g}s after last"
        + (f', until "{args.until}"' if args.until else "")
        + " ---"
    )
    sink(header)

    hit = False
    any_output = False
    try:
        if args.reset:
            reset_board()
        for idx, text in enumerate(lines):
            ser.write((text + "\n").encode("utf-8"))
            ser.flush()
            last = idx == len(lines) - 1
            window = args.seconds if last else max(args.delay, 0) / 1000.0
            if window <= 0:
                continue
            found, got = capture_window(ser, window, args.until, sink)
            any_output = any_output or got
            if found:
                hit = True
                break
        if not lines:
            found, got = capture_window(ser, args.seconds, args.until, sink)
            any_output = any_output or got
            hit = found
        elif not hit:
            # Last line already used the post-send window. Nothing more to do.
            pass
    except KeyboardInterrupt:
        sink("--- interrupted ---")
    finally:
        ser.close()

    if args.until and not hit:
        sink(f'--- ended: timed out after {args.seconds:g}s without seeing "{args.until}" ---')
    else:
        sink("--- ended ---")

    log_file.close()
    if not args.quiet:
        print(f"\nSaved to {log_path}", file=sys.stderr)

    if args.until and not hit:
        return 1
    if not any_output:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
