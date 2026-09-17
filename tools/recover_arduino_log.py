#!/usr/bin/env python3
"""Recover raw DSMR telegrams from an Arduino IDE serial-monitor transcript.

The Arduino IDE 2.x monitor, when its output is copied out and saved, mangles a
P1 stream in three ways at once:

    18:43:26.994 -> /FLU5\\000000000_A\\r\r\n\\n\r\n
    ^^^^^^^^^^^^^^^^                  ^^^^    ^^^^
    timestamp prefix                  literal escapes, then real CRLFs

That is: a per-line timestamp prefix, the telegram's own CR and LF rewritten as
the two-character sequences "\\r" and "\\n", and real CRLFs inserted around
them. The DSMR CRC16 is computed over the exact wire bytes, so a mangled
transcript fails validation and is useless as a test fixture.

This reverses all three. The CRC is the oracle: if every telegram validates
after recovery, the reconstruction is byte-exact rather than merely plausible.

Usage:
    python tools/recover_arduino_log.py INPUT.txt -o fixtures/name.p1
    python tools/recover_arduino_log.py INPUT.txt --check      (report only)
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

TIMESTAMP = re.compile(rb"^\d{2}:\d{2}:\d{2}\.\d{3} -> ")
# One to four hex characters, not exactly four: a sender that omits the leading
# zero would otherwise cause this to swallow two telegrams as one. See
# tools/p1crc.py for the full explanation.
TELEGRAM = re.compile(rb"/.*?![0-9A-Fa-f]{1,4}(?=\r\n|\n|$)", re.S)


def crc16_arc(data: bytes) -> int:
    """CRC-16/ARC over the telegram from '/' through '!' inclusive, as DSMR specifies."""
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def recover(raw: bytes) -> bytes:
    out = bytearray()
    for line in raw.split(b"\r\n"):
        if not line:
            continue
        line = TIMESTAMP.sub(b"", line)
        line = line.replace(b"\\r", b"\r").replace(b"\\n", b"\n")
        out += line
    return bytes(out)


def verify(data: bytes) -> tuple[int, int]:
    """Return (valid, total) telegram counts."""
    telegrams = TELEGRAM.findall(data)
    valid = 0
    for t in telegrams:
        body, _, claimed = t.rpartition(b"!")
        if int(claimed, 16) == crc16_arc(body + b"!"):
            valid += 1
    return valid, len(telegrams)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", type=pathlib.Path)
    ap.add_argument("-o", "--output", type=pathlib.Path,
                    help="Where to write the recovered stream.")
    ap.add_argument("--check", action="store_true",
                    help="Report CRC results without writing anything.")
    args = ap.parse_args()

    raw = args.input.read_bytes()
    before_valid, before_total = verify(raw)
    data = recover(raw)
    after_valid, after_total = verify(data)

    print(f"input:     {len(raw):>6} bytes, {before_total} telegram(s), "
          f"{before_valid} with valid CRC")
    print(f"recovered: {len(data):>6} bytes, {after_total} telegram(s), "
          f"{after_valid} with valid CRC")

    if after_total == 0:
        print("\nNo telegrams found. Is this really a P1 transcript?", file=sys.stderr)
        return 1
    if after_valid != after_total:
        print(f"\n{after_total - after_valid} telegram(s) still fail CRC. The "
              f"recovery is not byte-exact; do not use this as a fixture.",
              file=sys.stderr)
        return 1

    if args.check:
        print("\nAll CRCs valid. Recovery is byte-exact.")
        return 0

    if not args.output:
        print("\nAll CRCs valid. Pass -o to write the result.", file=sys.stderr)
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f"\nAll CRCs valid. Written to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
