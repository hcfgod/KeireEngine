#!/usr/bin/env python3
"""Run atmosphere or cloud-density invariants through the offscreen GPU probe host."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]


def repository_path(path: Path) -> str:
    # Keep the repository's Unicode parent out of narrow-argv native tools on Windows.
    try:
        return os.path.relpath(path.resolve(), ROOT)
    except ValueError:
        return str(path.resolve())


def main() -> None:
    suffix = ".exe" if sys.platform == "win32" else ""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path,
                        default=ROOT / f"Build/Tools/ShaderCompiler/KeireShaderCompiler{suffix}")
    parser.add_argument("--probe", type=Path, default=ROOT / f"Build/Validation/cloud-appearance-gpu{suffix}")
    parser.add_argument("--backend", choices=("direct3d12", "vulkan"), action="append", required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "Build/Validation/AtmosphereMedium")
    parser.add_argument("--suite", choices=("atmosphere", "cloud-density", "cloud-ambient"), default="atmosphere")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    shader = {
        "atmosphere": "AtmosphereMediumProbe.hlsl",
        "cloud-density": "CloudAppearanceProbe.hlsl",
        "cloud-ambient": "CloudAmbientProbe.hlsl",
    }[args.suite]
    for shader_format in ("DXIL", "SPIRV", "MSL"):
        subprocess.run([
            str(args.compiler.resolve()), f"Scripts/Tests/{shader}",
            "-s", "HLSL", "-d", shader_format, "-t", "compute", "-e", "CSMain",
            "-I", "KeireCore/Shaders", "-I", "Scripts/Tests",
            "-o", repository_path(args.output / f"{shader_format}.bin"),
        ], check=True, cwd=ROOT)

    for backend in dict.fromkeys(args.backend):
        shader_format = "DXIL" if backend == "direct3d12" else "SPIRV"
        densities = (0, 1, 30) if args.suite != "atmosphere" else (None,)
        for density in densities:
            case = args.suite if density is None else f"density-{density}"
            parameters = [] if density is None else [
                str(density), ".7", "4", ".7", ".06", "1", "2", ".65", "48", "6",
                "density-check", ".65",
            ]
            result = subprocess.run([
                str(args.probe.resolve()), backend, repository_path(args.output / f"{shader_format}.bin"),
                repository_path(args.output / f"{backend}-{case}.bmp"), *parameters,
            ], check=True, text=True, stdout=subprocess.PIPE, cwd=ROOT)
            print(result.stdout, end="")
            report = re.search(r"^pixels=(\d+) mean_opacity=(\S+)$", result.stdout, re.MULTILINE)
            # The shader encodes any invariant failure as alpha 1.
            if report is None or int(report[1]) == 0 or float(report[2]) != 0.0:
                raise RuntimeError(f"{case} invariant failure on {backend}")
            print(f"{case} invariants passed on {backend} ({report[1]} samples).")



if __name__ == "__main__":
    main()
