#!/usr/bin/env python3
"""Regression tests for generated Ninja dependency-file repair."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "Scripts" / "patch-ninja-depfiles.py"


class NinjaDepfileTests(unittest.TestCase):
    def test_repairs_gcc_style_rule_and_is_idempotent(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "build.ninja").write_text(
                "subninja project.ninja\n", encoding="utf-8"
            )
            project = root / "project.ninja"
            project.write_text(
                "rule cxx_clang\n"
                "  command = clang++ $cxxflags -c $in -o $out\n"
                "  deps = gcc\n"
                "  depfile = $out.d\n",
                encoding="utf-8",
            )
            for _ in range(2):
                subprocess.run(
                    [sys.executable, str(SCRIPT), str(root)],
                    check=True,
                    capture_output=True,
                    text=True,
                )
            command = project.read_text(encoding="utf-8")
            self.assertEqual(command.count("-MD -MF $out.d"), 1)

    def test_rejects_unrecognized_gcc_style_rule(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "build.ninja").write_text(
                "subninja project.ninja\n", encoding="utf-8"
            )
            (root / "project.ninja").write_text(
                "rule cxx_clang\n"
                "  command = unexpected $in\n"
                "  deps = gcc\n"
                "  depfile = $out.d\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [sys.executable, str(SCRIPT), str(root)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("cannot add dependency output", result.stderr)


if __name__ == "__main__":
    unittest.main()
