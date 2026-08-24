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

    windows_project = fixture / "Windows.ninja"
    with windows_project.open("w", encoding="utf-8", newline="") as output:
        output.write("rule cxx_msc\r\n  command = cl $cxxflags -c $in\r\n")
    subprocess.run([sys.executable, str(script), str(fixture), "--cache", "sccache"], check=True)
    with windows_project.open("r", encoding="utf-8", newline="") as input_file:
        windows_result = input_file.read()
    assert windows_result == "rule cxx_msc\n  command = sccache cl $cxxflags -c $in\n"

print("Ninja compiler-cache tests passed")
