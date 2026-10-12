#!/usr/bin/env python3
"""Search files and URLs for the EnergieID key and secret. Prints only found/not found."""
from __future__ import annotations

import gzip
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SECRET_PATH = ROOT / "_local" / "eid_secret.txt"


def load_pair() -> tuple[bytes, bytes]:
    if not SECRET_PATH.is_file():
        print("missing _local/eid_secret.txt", file=sys.stderr)
        raise SystemExit(1)
    lines = SECRET_PATH.read_text(encoding="utf-8").splitlines()
    key = lines[0].strip().encode("utf-8") if len(lines) >= 1 else b""
    secret = lines[1].strip().encode("utf-8") if len(lines) >= 2 else b""
    if not key or not secret:
        print("eid_secret.txt must have two non-empty lines", file=sys.stderr)
        raise SystemExit(1)
    return key, secret


def read_target(spec: str) -> bytes | None:
    if spec.startswith("http://") or spec.startswith("https://"):
        req = urllib.request.Request(spec, headers={"Accept-Encoding": "gzip"})
        try:
            with urllib.request.urlopen(req, timeout=15) as resp:
                raw = resp.read()
                enc = (resp.headers.get("Content-Encoding") or "").lower()
                if enc == "gzip" or raw[:2] == b"\x1f\x8b":
                    try:
                        raw = gzip.decompress(raw)
                    except OSError:
                        pass
                return raw
        except Exception as e:
            print(f"{spec} error: {e}", file=sys.stderr)
            return None
    path = Path(spec)
    if not path.is_file():
        print(f"{spec} missing")
        return None
    return path.read_bytes()


def check_one(label: str, data: bytes, key: bytes, secret: bytes) -> None:
    k = "found" if key in data else "not found"
    s = "found" if secret in data else "not found"
    print(f"{label} key: {k}")
    print(f"{label} secret: {s}")


def main() -> None:
    key, secret = load_pair()
    args = sys.argv[1:]
    if not args:
        print("usage: secret_check.py [--git] <file-or-url>...", file=sys.stderr)
        raise SystemExit(2)
    targets: list[str] = []
    if "--git" in args:
        out = subprocess.check_output(["git", "ls-files"], cwd=ROOT, text=True)
        targets.extend([line.strip() for line in out.splitlines() if line.strip()])
        args = [a for a in args if a != "--git"]
    targets.extend(args)
    for spec in targets:
        data = read_target(str(ROOT / spec) if "://" not in spec else spec)
        if data is None:
            continue
        check_one(spec, data, key, secret)


if __name__ == "__main__":
    main()
