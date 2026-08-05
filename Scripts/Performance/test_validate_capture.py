#!/usr/bin/env python3
"""Regression tests for reference-hardware performance gates."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = REPOSITORY_ROOT / "Scripts/Performance/validate_capture.py"


class PerformanceGateTests(unittest.TestCase):
    def evaluate(self, snapshot: str) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            snapshot_path = root / "snapshot.txt"
            history_path = root / "history.csv"
            metadata_path = root / "metadata.json"
            snapshot_path.write_text(snapshot, encoding="utf-8", newline="\n")
            history_path.write_text(
                "sequence,duration_us\n"
                + "".join(f"{index},5000\n" for index in range(300)),
                encoding="utf-8",
                newline="\n",
            )
            metadata_path.write_text(
                json.dumps(
                    {
                        "hardwareId": "keire-win-rtx3060-i7-12700f",
                        "gpuBackend": "direct3d12",
                        "gpuName": "NVIDIA GeForce RTX 3060",
                        "gpuDriver": "32.0.15.9597",
                        "cpuName": "12th Gen Intel(R) Core(TM) i7-12700F",
                        "buildConfiguration": "Release",
                        "resolution": "3440x1377",
                        "workload": "sandbox-vfx-reference",
                        "engineCommit": "0" * 40,
                    }
                ),
                encoding="utf-8",
                newline="\n",
            )
            return subprocess.run(
                [
                    sys.executable,
                    str(VALIDATOR),
                    "--snapshot",
                    str(snapshot_path),
                    "--history",
                    str(history_path),
                    "--metadata",
                    str(metadata_path),
                ],
                cwd=REPOSITORY_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )

    def test_reference_vfx_capture_passes(self) -> None:
        result = self.evaluate(
            """Keire Profiler Capture 300
Frame: 5.00 ms (200 FPS)
Average: 5.00 ms (200 FPS)
P95: 5.00 ms
P99: 5.00 ms
Stutters: 0
COUNTER,Rendering,GPU timing supported,1
COUNTER,Rendering,GPU frame (ms),4.5
COUNTER,Rendering,GPU fence wait (ms),0.1
COUNTER,Rendering,Renderer latency (ms),5.0
COUNTER,Rendering,VFX preparation (ms),0.2
COUNTER,Rendering,VFX pipeline warmup pending,0
COUNTER,Rendering,VFX pipeline warmup (ms),80.0
COUNTER,Rendering,VFX GPU completion latency (ms),8.0
COUNTER,Rendering,VFX GPU worlds,1
COUNTER,Rendering,Dropped VFX particles,0
"""
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_capture_without_true_gpu_timestamps_fails(self) -> None:
        result = self.evaluate(
            """Keire Profiler Capture 300
Frame: 5.00 ms (200 FPS)
Average: 5.00 ms (200 FPS)
P95: 5.00 ms
P99: 5.00 ms
Stutters: 0
COUNTER,Rendering,GPU timing supported,0
COUNTER,Rendering,GPU fence wait (ms),0.1
COUNTER,Rendering,Renderer latency (ms),5.0
COUNTER,Rendering,VFX preparation (ms),0.2
COUNTER,Rendering,VFX pipeline warmup pending,0
COUNTER,Rendering,VFX pipeline warmup (ms),80.0
COUNTER,Rendering,VFX GPU completion latency (ms),8.0
COUNTER,Rendering,VFX GPU worlds,1
COUNTER,Rendering,Dropped VFX particles,0
"""
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("GPU timing supported", result.stderr)
        self.assertIn("GPU frame (ms)", result.stderr)

    def test_excessive_cold_vfx_warmup_fails(self) -> None:
        result = self.evaluate(
            """Keire Profiler Capture 300
Frame: 5.00 ms (200 FPS)
Average: 5.00 ms (200 FPS)
P95: 5.00 ms
P99: 5.00 ms
Stutters: 0
COUNTER,Rendering,GPU timing supported,1
COUNTER,Rendering,GPU frame (ms),4.5
COUNTER,Rendering,GPU fence wait (ms),0.1
COUNTER,Rendering,Renderer latency (ms),5.0
COUNTER,Rendering,VFX preparation (ms),0.2
COUNTER,Rendering,VFX pipeline warmup pending,0
COUNTER,Rendering,VFX pipeline warmup (ms),17122.0
COUNTER,Rendering,VFX GPU completion latency (ms),8.0
COUNTER,Rendering,VFX GPU worlds,1
COUNTER,Rendering,Dropped VFX particles,0
"""
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("VFX pipeline warmup (ms)", result.stderr)


if __name__ == "__main__":
    unittest.main()
