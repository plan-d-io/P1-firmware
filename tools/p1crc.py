#!/usr/bin/env python3
"""Validate -- or recompute -- the CRC16 of DSMR telegrams in a .p1 file.

DSMR uses CRC-16/ARC over the telegram from '/' through '!' inclusive, written
as four uppercase hex digits immediately after the '!'.

Two uses:

  Validate (default). Confirms a fixture is byte-exact. Any editor that
  "helpfully" rewrites line endings will break this, which is the point.

      python tools/p1crc.py fixtures/belgian-3phase-fluvius.p1

  Recompute (--fix). Needed when a telegram has deliberately been edited --
  anonymising a meter ID before publishing, or filling in a spec example that
  ships with a placeholder CRC. Only ever use this when you *intend* the bytes
  to differ from what a meter sent; never to make a failing capture pass.

      python tools/p1crc.py fixtures/anonymised.p1 --fix
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

TELEGRAM = re.compile(rb"/.*?![0-9A-Fa-f]{4}", re.S)


def crc16_arc(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", type=pathlib.Path)
    ap.add_argument("--fix", action="store_true",
                    help="Rewrite each CRC to match the current bytes.")
    args = ap.parse_args()

    data = args.file.read_bytes()
    telegrams = TELEGRAM.findall(data)
    if not telegrams:
        print(f"No telegrams found in {args.file}.", file=sys.stderr)
        return 1

    bad = 0
    for i, t in enumerate(telegrams):
        claimed = t[-4:].decode()
        calc = crc16_arc(t[:-4])
        ident = t.split(b"\r\n")[0].decode(errors="replace")
        if int(claimed, 16) == calc:
            print(f"  [{i}] {claimed}  ok        {ident[:44]}")
        else:
            bad += 1
            print(f"  [{i}] {claimed}  MISMATCH (computed {calc:04X})  {ident[:30]}")
            if args.fix:
                data = data.replace(t, t[:-4] + ("%04X" % calc).encode(), 1)

    print(f"\n{len(telegrams)} telegram(s), {len(telegrams) - bad} valid")

    if bad and args.fix:
        args.file.write_bytes(data)
        print(f"Rewrote {bad} CRC(s) in {args.file}.")
        return 0
    if bad:
        print("Run with --fix only if you intended these bytes to change.",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
