#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


root = Path(__file__).resolve().parents[2]
script = root / "Scripts" / "patch-ninja-compiler-cache.py"
with tempfile.TemporaryDirectory() as temporary:
    fixture = Path(temporary)
    project = fixture / "Example.ninja"
    project.write_text(
        "rule cxx_msc\n"
        "  command = cl $cxxflags -c $in\n"
        "rule pch_clang\n"
        "  command = clang++ $cxxflags -c $in\n"
        "rule link_msc\n"
        "  command = cl $in /link\n",
        encoding="utf-8",
    )
    subprocess.run([sys.executable, str(script), str(fixture), "--cache", "sccache"], check=True)
    result = project.read_text(encoding="utf-8")
    assert "command = sccache cl $cxxflags" in result
    assert "command = sccache clang++ $cxxflags" in result
    assert "rule link_msc\n  command = cl $in /link" in result
    subprocess.run([sys.executable, str(script), str(fixture), "--cache", "sccache"], check=True)
    assert project.read_text(encoding="utf-8") == result

print("Ninja compiler-cache tests passed")
