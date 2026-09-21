#!/usr/bin/env python3
"""Gzip web/ assets and emit C++ blobs for lib/P1Web.

Runs as a PlatformIO pre-script and can be invoked directly:

    python tools/embed_web.py
"""

from __future__ import annotations

import gzip
import pathlib
import sys


def c_bytes(data: bytes, name: str) -> str:
    lines = [f"const std::uint8_t {name}[] = {{"]
    row: list[str] = []
    for b in data:
        row.append(f"0x{b:02x}")
        if len(row) == 12:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ",")
    lines.append("};")
    return "\n".join(lines)


def c_raw_string(text: str, name: str) -> str:
    if ")p1web\"" in text:
        sys.exit(f"template contains forbidden delimiter for {name}")
    return f'const char {name}[] = R"p1web({text})p1web";'


def generate(root: pathlib.Path) -> int:
    web = root / "web"
    out_h = root / "lib" / "P1Web" / "src" / "assets_gen.h"
    out_cpp = root / "lib" / "P1Web" / "src" / "assets_gen.cpp"

    files = {
        "style.css": web / "style.css",
        "setup.html": web / "setup.html",
        "status.html": web / "status.html",
        "restarting.html": web / "restarting.html",
        "rebooting.html": web / "rebooting.html",
        "config.html": web / "config.html",
        "log.html": web / "log.html",
    }
    for path in files.values():
        if not path.is_file():
            sys.exit(f"missing asset: {path}")

    style_raw = files["style.css"].read_bytes()
    style_gz = gzip.compress(style_raw, mtime=0, compresslevel=9)
    restart_raw = files["restarting.html"].read_bytes()
    restart_gz = gzip.compress(restart_raw, mtime=0, compresslevel=9)
    reboot_raw = files["rebooting.html"].read_bytes()
    reboot_gz = gzip.compress(reboot_raw, mtime=0, compresslevel=9)
    config_raw = files["config.html"].read_bytes()
    config_gz = gzip.compress(config_raw, mtime=0, compresslevel=9)
    log_raw = files["log.html"].read_bytes()
    log_gz = gzip.compress(log_raw, mtime=0, compresslevel=9)
    setup_tpl = files["setup.html"].read_text(encoding="utf-8")
    status_tpl = files["status.html"].read_text(encoding="utf-8")

    report = root / ".pio" / "asset-sizes.txt"
    report.parent.mkdir(parents=True, exist_ok=True)
    with report.open("w", encoding="utf-8") as fh:
        fh.write("asset raw_bytes gzip_bytes\n")
        fh.write(f"style.css {len(style_raw)} {len(style_gz)}\n")
        fh.write(f"restarting.html {len(restart_raw)} {len(restart_gz)}\n")
        fh.write(f"rebooting.html {len(reboot_raw)} {len(reboot_gz)}\n")
        fh.write(f"config.html {len(config_raw)} {len(config_gz)}\n")
        fh.write(f"log.html {len(log_raw)} {len(log_gz)}\n")
        fh.write(f"setup.html {len(setup_tpl.encode('utf-8'))} -\n")
        fh.write(f"status.html {len(status_tpl.encode('utf-8'))} -\n")

    out_h.parent.mkdir(parents=True, exist_ok=True)
    out_h.write_text(
        """#pragma once
#include <cstddef>
#include <cstdint>

namespace p1web {
namespace assets {

extern const std::uint8_t kStyleCssGz[];
extern const std::size_t kStyleCssGzLen;
extern const std::size_t kStyleCssRawLen;

extern const std::uint8_t kRestartingHtmlGz[];
extern const std::size_t kRestartingHtmlGzLen;
extern const std::size_t kRestartingHtmlRawLen;

extern const std::uint8_t kRebootingHtmlGz[];
extern const std::size_t kRebootingHtmlGzLen;
extern const std::size_t kRebootingHtmlRawLen;

extern const std::uint8_t kConfigHtmlGz[];
extern const std::size_t kConfigHtmlGzLen;
extern const std::size_t kConfigHtmlRawLen;

extern const std::uint8_t kLogHtmlGz[];
extern const std::size_t kLogHtmlGzLen;
extern const std::size_t kLogHtmlRawLen;

extern const char kSetupHtmlTpl[];
extern const char kStatusHtmlTpl[];

}  // namespace assets
}  // namespace p1web
""",
        encoding="utf-8",
    )

    cpp = [
        '#include "assets_gen.h"',
        "",
        "namespace p1web {",
        "namespace assets {",
        "",
        c_bytes(style_gz, "kStyleCssGz"),
        "const std::size_t kStyleCssGzLen = sizeof(kStyleCssGz);",
        f"const std::size_t kStyleCssRawLen = {len(style_raw)};",
        "",
        c_bytes(restart_gz, "kRestartingHtmlGz"),
        "const std::size_t kRestartingHtmlGzLen = sizeof(kRestartingHtmlGz);",
        f"const std::size_t kRestartingHtmlRawLen = {len(restart_raw)};",
        "",
        c_bytes(reboot_gz, "kRebootingHtmlGz"),
        "const std::size_t kRebootingHtmlGzLen = sizeof(kRebootingHtmlGz);",
        f"const std::size_t kRebootingHtmlRawLen = {len(reboot_raw)};",
        "",
        c_bytes(config_gz, "kConfigHtmlGz"),
        "const std::size_t kConfigHtmlGzLen = sizeof(kConfigHtmlGz);",
        f"const std::size_t kConfigHtmlRawLen = {len(config_raw)};",
        "",
        c_bytes(log_gz, "kLogHtmlGz"),
        "const std::size_t kLogHtmlGzLen = sizeof(kLogHtmlGz);",
        f"const std::size_t kLogHtmlRawLen = {len(log_raw)};",
        "",
        c_raw_string(setup_tpl, "kSetupHtmlTpl"),
        "",
        c_raw_string(status_tpl, "kStatusHtmlTpl"),
        "",
        "}  // namespace assets",
        "}  // namespace p1web",
        "",
    ]
    out_cpp.write_text("\n".join(cpp), encoding="utf-8")
    print(
        f"embed_web: style {len(style_raw)}->{len(style_gz)}  "
        f"restarting {len(restart_raw)}->{len(restart_gz)}  "
        f"rebooting {len(reboot_raw)}->{len(reboot_gz)}  "
        f"config {len(config_raw)}->{len(config_gz)}  "
        f"log {len(log_raw)}->{len(log_gz)}"
    )
    return 0


try:
    Import("env")  # type: ignore[name-defined]
    generate(pathlib.Path(env["PROJECT_DIR"]))  # type: ignore[name-defined]
except NameError:
    if __name__ == "__main__":
        raise SystemExit(generate(pathlib.Path(__file__).resolve().parents[1]))
