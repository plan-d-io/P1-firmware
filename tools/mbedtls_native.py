"""Compile mbedtls AES and SHA-256 into the native tests.

The native environment downloads Mbed TLS v3.6.5, the same version string the
ESP32 framework ships. Only the three source files those two algorithms need
are compiled. Firmware targets link the framework copy and do not run this.
"""

from __future__ import annotations

import os
import sys

from SCons.Script import DefaultEnvironment

Import("env")  # type: ignore[name-defined]


def find_root(libdeps: str) -> str | None:
    if not os.path.isdir(libdeps):
        return None
    for name in sorted(os.listdir(libdeps)):
        root = os.path.join(libdeps, name)
        aes = os.path.join(root, "library", "aes.c")
        header = os.path.join(root, "include", "mbedtls", "aes.h")
        if os.path.isfile(aes) and os.path.isfile(header):
            return root
    return None


project = env["PROJECT_DIR"]  # type: ignore[name-defined]
root = find_root(os.path.join(project, ".pio", "libdeps", "native"))
if root is None:
    sys.stderr.write(
        "mbedtls 3.6.5 was not found under .pio/libdeps/native. "
        "The native environment should download Mbed-TLS/mbedtls tag v3.6.5.\n"
    )
    env.Exit(1)  # type: ignore[name-defined]

include = os.path.join(root, "include")
library = os.path.join(root, "library")
# tools/ must come before mbedtls's include directory so the host config
# (tools/mbedtls/mbedtls_config.h) is the one build_info.h picks up.
tools = os.path.join(project, "tools")
env.Prepend(CPPPATH=[tools])  # type: ignore[name-defined]
env.Append(CPPPATH=[include, library])  # type: ignore[name-defined]

ccflags = []
for flag in env.get("CCFLAGS", []):  # type: ignore[name-defined]
    if "-std=" in str(flag):
        continue
    ccflags.append(flag)
ccflags.append("-std=c11")

objects = []
for name in ("aes.c", "sha256.c", "platform.c", "platform_util.c"):
    objects.append(
        env.Object(  # type: ignore[name-defined]
            os.path.join("$BUILD_DIR", "mbedtls", name + ".o"),
            os.path.join(library, name),
            CCFLAGS=ccflags,
        )
    )
DefaultEnvironment().Append(PIOBUILDFILES=objects)
