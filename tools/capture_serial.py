#!/usr/bin/env python3
"""Capture serial output from the board for a bounded time, then exit.

Written so a script can run it and exit: `pio device monitor` streams forever, which
hangs anything that waits for a command to finish. This reads for a fixed
number of seconds (or until a sentinel line appears), prints what it got, and
returns.

Output goes to stdout *and* to a timestamped file under _local/serial/, which
***REMOVED***
so the log sits next to the project.

Examples
--------
    python tools/capture_serial.py
    python tools/capture_serial.py --seconds 30
    python tools/capture_serial.py --port COM3 --until "Setup done"
    python tools/capture_serial.py --list

Requires pyserial, which ships with PlatformIO. If you get an ImportError, run
it with the PlatformIO interpreter instead:

    python tools/capture_serial.py
"""

from __future__ import annotations

import argparse
import datetime as _dt
import pathlib
import re
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit(
        "pyserial is not available to this Python interpreter.\n"
        "Run the script with the PlatformIO interpreter instead, e.g.\n"
        "  python tools/capture_serial.py"
    )

DEFAULT_BAUD = 115200
DEFAULT_SECONDS = 15.0
LOG_DIR = pathlib.Path(__file__).resolve().parent.parent / "_local" / "serial"


def describe(port) -> str:
    """One readable line per candidate port."""
    bits = [port.device]
    if port.description and port.description != "n/a":
        bits.append(port.description)
    if port.vid is not None and port.pid is not None:
        bits.append(f"VID:PID={port.vid:04X}:{port.pid:04X}")
    return "  ".join(bits)


def candidate_ports() -> list:
    """USB serial ports, i.e. those reporting a USB vendor ID.

    Filters out motherboard COM ports and Bluetooth virtual ports, which have
    no VID and are never the board.
    """
    return [p for p in list_ports.comports() if p.vid is not None]


def resolve_port(requested: str | None) -> str:
    if requested:
        return requested

    found = candidate_ports()
    if not found:
        sys.exit(
            "No USB serial port found.\n"
            "Is the board plugged in? Is its driver installed?\n"
            "Run with --list to see every port Windows reports."
        )
    if len(found) > 1:
        lines = "\n".join("  " + describe(p) for p in found)
        sys.exit(
            f"More than one USB serial port found:\n{lines}\n"
            "Pick one explicitly with --port COMx."
        )
    return found[0].device


def open_port(port: str, baud: int) -> "serial.Serial":
    try:
        return serial.Serial(port, baud, timeout=0.2)
    except serial.SerialException as exc:
        msg = str(exc)
        if "access is denied" in msg.lower() or "permission" in msg.lower():
            sys.exit(
                f"Could not open {port}: the port is already in use.\n"
                "Close any other serial monitor holding the port (or any other terminal\n"
                "holding the port) and try again. Only one program can hold a\n"
                "COM port at a time."
            )
        sys.exit(f"Could not open {port}: {exc}")


def capture(ser, seconds: float, until: str | None, sink) -> bool:
    """Stream lines until the deadline or the sentinel. True if sentinel seen."""
    deadline = time.monotonic() + seconds
    found_sentinel = False
    partial = b""

    while time.monotonic() < deadline:
        chunk = ser.read(4096)
        if not chunk:
            continue
        partial += chunk
        *complete, partial = partial.split(b"\n")
        for raw in complete:
            line = raw.decode("utf-8", errors="replace").rstrip("\r")
            stamp = _dt.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            sink(f"[{stamp}] {line}")
            if until and until in line:
                found_sentinel = True
                return found_sentinel

    if partial:  # whatever arrived without a trailing newline
        line = partial.decode("utf-8", errors="replace").rstrip("\r")
        if line:
            stamp = _dt.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            sink(f"[{stamp}] {line}")

    return found_sentinel


TELEGRAM_END = re.compile(rb"^![0-9A-Fa-f]{4}\r?$", re.MULTILINE)


def capture_raw(ser, seconds: float, telegrams: int | None,
                log_file, echo: bool) -> int:
    """Byte-exact passthrough. Returns the number of complete telegrams seen.

    Used to capture DSMR telegrams as test fixtures: no timestamps, no added
    or removed bytes, because the CRC16 is computed over the exact wire bytes.
    A telegram ends with a line of "!" followed by four hex digits.
    """
    deadline = time.monotonic() + seconds
    seen = 0
    tail = b""

    out = sys.stdout.buffer if echo else None

    while time.monotonic() < deadline:
        chunk = ser.read(4096)
        if not chunk:
            continue
        if log_file:
            log_file.write(chunk)
            log_file.flush()
        if out:
            out.write(chunk)
            out.flush()

        # Count terminators, keeping a small tail in case one straddles chunks.
        window = tail + chunk
        seen += len(TELEGRAM_END.findall(window))
        tail = window[-8:]

        if telegrams is not None and seen >= telegrams:
            return seen

    return seen


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Capture bounded serial output from the board.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--port", help="Serial port, e.g. COM3. Auto-detected if omitted.")
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                    help=f"Baud rate (default {DEFAULT_BAUD}).")
    ap.add_argument("--seconds", type=float, default=DEFAULT_SECONDS,
                    help=f"How long to listen (default {DEFAULT_SECONDS:g}).")
    ap.add_argument("--until", metavar="TEXT",
                    help="Stop early when a line contains this text.")
    ap.add_argument("--reset", action="store_true",
                    help="Pulse DTR/RTS to reset the board before capturing, "
                         "so the boot messages are included.")
    ap.add_argument("--raw", action="store_true",
                    help="Byte-exact passthrough: no timestamps, no header in "
                         "the log. Use this to capture DSMR telegrams as "
                         "fixtures, where the CRC is computed over the exact "
                         "wire bytes.")
    ap.add_argument("--telegrams", type=int, metavar="N",
                    help="With --raw, stop once at least N complete telegrams "
                         "(a line of '!' plus four hex digits).")
    ap.add_argument("--no-file", action="store_true",
                    help="Print only; do not write a log file.")
    ap.add_argument("--list", action="store_true",
                    help="List serial ports and exit.")
    args = ap.parse_args()

    if args.list:
        ports = list_ports.comports()
        if not ports:
            print("No serial ports found.")
        for p in ports:
            marker = "*" if p.vid is not None else " "
            print(f"{marker} {describe(p)}")
        print("\n* = USB device, most likely the board.")
        return 0

    port = resolve_port(args.port)
    ser = open_port(port, args.baud)

    log_path = None
    log_file = None
    if not args.no_file:
        LOG_DIR.mkdir(parents=True, exist_ok=True)
        stamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
        suffix = "p1" if args.raw else "log"
        log_path = LOG_DIR / f"{stamp}.{suffix}"
        # Raw mode writes bytes verbatim; line mode writes decoded text.
        log_file = log_path.open("wb" if args.raw else "w",
                                 **({} if args.raw else {"encoding": "utf-8"}))

    def reset_board() -> None:
        ser.dtr = False
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
        time.sleep(0.05)
        ser.reset_input_buffer()

    header = (f"--- {port} @ {args.baud} baud, up to {args.seconds:g}s"
              + (f", {args.telegrams} telegrams" if args.raw and args.telegrams else "")
              + (f', until "{args.until}"' if args.until else "")
              + (" [raw]" if args.raw else "")
              + " ---")

    if args.raw:
        # Header to stderr only: stdout must stay byte-exact.
        print(header, file=sys.stderr, flush=True)
        try:
            if args.reset:
                reset_board()
            seen = capture_raw(ser, args.seconds, args.telegrams, log_file, echo=True)
        except KeyboardInterrupt:
            seen = 0
        finally:
            ser.close()
            if log_file:
                log_file.close()

        print(f"\n--- {seen} complete telegram(s) ---", file=sys.stderr)
        if log_path:
            print(f"Saved to {log_path}", file=sys.stderr)
        if args.telegrams and seen < args.telegrams:
            print(f"Expected {args.telegrams}, got {seen}.", file=sys.stderr)
            return 1
        return 0 if seen else 1

    def sink(line: str) -> None:
        print(line, flush=True)
        if log_file:
            log_file.write(line + "\n")
            log_file.flush()

    sink(header)

    try:
        if args.reset:
            reset_board()
        hit = capture(ser, args.seconds, args.until, sink)
    except KeyboardInterrupt:
        sink("--- interrupted ---")
        hit = False
    finally:
        ser.close()

    if args.until and not hit:
        sink(f'--- ended: timed out after {args.seconds:g}s without seeing "{args.until}" ---')
    else:
        sink("--- ended ---")

    if log_path:
        log_file.close()
        print(f"\nSaved to {log_path}", file=sys.stderr)

    # Non-zero when an expected sentinel never arrived, so a caller can tell.
    return 1 if (args.until and not hit) else 0


if __name__ == "__main__":
    raise SystemExit(main())
