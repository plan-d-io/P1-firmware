#!/usr/bin/env python3
"""Emit EnergieID provisioning constants into the PlatformIO build directory.

Reads _local/eid_secret.txt (key on line 1, secret on line 2). Never prints the
values. They are written only as C string constants in a generated header, never
on a compiler command line.
"""
from __future__ import annotations

from pathlib import Path


def c_string(value: str) -> str:
    out: list[str] = []
    for b in value.encode("utf-8"):
        if b == ord("\\"):
            out.append("\\\\")
        elif b == ord('"'):
            out.append('\\"')
        elif 32 <= b < 127:
            out.append(chr(b))
        else:
            out.append(f"\\x{b:02x}")
    return '"' + "".join(out) + '"'


def load_lines(path: Path) -> tuple[str, str] | None:
    if not path.is_file():
        return None
    lines = path.read_text(encoding="utf-8").splitlines()
    key = lines[0].strip() if len(lines) >= 1 else ""
    secret = lines[1].strip() if len(lines) >= 2 else ""
    if not key or not secret:
        return None
    return key, secret


def write_header(out: Path, pair: tuple[str, str] | None) -> bool:
    out.parent.mkdir(parents=True, exist_ok=True)
    if pair is None:
        out.write_text("#pragma once\n#define P1_EID_KEYS 0\n", encoding="utf-8")
        return False
    key, secret = pair
    text = (
        "#pragma once\n"
        "#define P1_EID_KEYS 1\n"
        "namespace p1eidkeys {\n"
        f"constexpr char kKey[] = {c_string(key)};\n"
        f"constexpr char kSecret[] = {c_string(secret)};\n"
        "}\n"
    )
    out.write_text(text, encoding="utf-8")
    return True


try:
    Import("env")  # type: ignore[name-defined]
    project = Path(env["PROJECT_DIR"])  # type: ignore[name-defined]
    build_dir = Path(env.subst("$BUILD_DIR"))  # type: ignore[name-defined]
    header = build_dir / "eid_keys_gen.h"
    pair = load_lines(project / "_local" / "eid_secret.txt")
    enabled = write_header(header, pair)
    env.Append(CPPPATH=[str(build_dir)])  # type: ignore[name-defined]
    if not enabled:
        print("WARNING: EnergieID keys missing; firmware builds without EnergieID")
except NameError:
    if __name__ == "__main__":
        raise SystemExit("run as a PlatformIO extra_script")
