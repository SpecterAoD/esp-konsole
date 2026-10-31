#!/usr/bin/env python3
"""Gibt die Firmware-Version als Build-Flag aus (für platformio.ini: build_flags = !python3 werkzeuge/version.py).

Version = `git describe --tags --always --dirty` (z. B. v2.0.0, v2.0.0-3-gabc1234-dirty), sonst "dev".
In GitHub Actions kann FW_VERSION von außen gesetzt werden.
"""
import os
import subprocess

v = os.environ.get("FW_VERSION")
if not v:
    try:
        v = subprocess.run(["git", "describe", "--tags", "--always", "--dirty"], capture_output=True,
                           text=True, check=True, timeout=10).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        v = ""
v = "".join(c for c in (v or "dev") if c.isalnum() or c in ".-_+")[:40]
print(f'-DFW_VERSION=\\"{v}\\"')
