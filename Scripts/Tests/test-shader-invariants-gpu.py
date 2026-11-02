#!/usr/bin/env python3
"""Compile and dispatch water/planetary numerical probes on explicitly selected GPU backends."""

import argparse
from pathlib import Path
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[2]
SUITES = {
    "cloud-view-footprint": ("CloudViewFootprintProbe.hlsl", 3),
    "planet-weather": ("PlanetWeatherTransportProbe.hlsl", 6),
    "planet-volume": ("PlanetVolumeTransportProbe.hlsl", 5),
    "planet-volume-candidates": ("PlanetVolumeCandidateProbe.hlsl", 3),
    "planet-volume-cloud-sampling": ("PlanetVolumeCloudSamplingProbe.hlsl", 6),
    "water-wave": ("WaterWaveProbe.hlsl", 2),
    "water-optics": ("WaterOpticsProbe.hlsl", 3),
}


def run_probe(host: Path, backend: str, shader: Path, records: int, expected_success: bool, cloud_noise: bool = False) -> None:
    started = time.perf_counter()
    try:
        result = subprocess.run(
            [str(host), backend, str(shader), str(records)] + (["--cloud-noise"] if cloud_noise else []),
            cwd=ROOT,
            text=True,
            capture_output=True,
            timeout=180,
        )
    except subprocess.TimeoutExpired as error:
        # Timeout output may be bytes even with text=True; preserve stage markers for diagnosis.
        for output in (error.stdout, error.stderr):
            if output:
                print(output.decode(errors="replace") if isinstance(output, bytes) else output, end="")
        raise
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    # Includes device/pipeline creation and readback; this is not a GPU kernel timing.
    print(f"Probe host wall time: {time.perf_counter() - started:.3f} seconds.", flush=True)
    if expected_success:
        result.check_returncode()
        if f"records={records} finite=1 maximum_error=" not in result.stdout:
            raise RuntimeError("Probe did not report complete finite GPU output.")
    elif result.returncode == 0 or f"records={records} finite=0" not in result.stdout:
        # A driver/startup failure is not evidence that unwritten output was detected.
        raise RuntimeError("Negative control did not reject unwritten output through the finite-value check.")


def main() -> None:
    suffix = ".exe" if sys.platform == "win32" else ""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("direct3d12", "vulkan"), action="append", required=True)
    parser.add_argument("--suite", choices=tuple(SUITES), action="append")
    parser.add_argument("--compiler", type=Path,
                        default=ROOT / f"Build/Tools/ShaderCompiler/KeireShaderCompiler{suffix}")
    parser.add_argument("--probe", type=Path,
                        default=ROOT / f"Build/Validation/shader-invariant-gpu{suffix}")
    parser.add_argument("--output", type=Path, default=Path("Build/Validation/ShaderInvariants"))
    args = parser.parse_args()
    output = args.output if args.output.is_absolute() else ROOT / args.output
    output.mkdir(parents=True, exist_ok=True)

    for suite in dict.fromkeys(args.suite or SUITES):
        source, records = SUITES[suite]
        for backend in dict.fromkeys(args.backend):
            shader_format = "DXIL" if backend == "direct3d12" else "SPIRV"
            compiled = output / f"{suite}-{shader_format}.bin"
            # Relative paths avoid passing the repository's Unicode parent to narrow-argv tools.
            compiled_argument = compiled.relative_to(ROOT) if compiled.is_relative_to(ROOT) else compiled
            compile_started = time.perf_counter()
            subprocess.run(
                [str(args.compiler.resolve()), f"Scripts/Tests/{source}", "-s", "HLSL", "-d", shader_format,
                 "-t", "compute", "-e", "main", "-I", "KeireCore/Shaders", "-o", str(compiled_argument)],
                cwd=ROOT,
                check=True,
                timeout=180,
            )
            print(f"{suite}: {shader_format} compile wall time: "
                  f"{time.perf_counter() - compile_started:.3f} seconds.", flush=True)
            print(f"{suite}: {backend}", flush=True)
            run_probe(args.probe.resolve(), backend, compiled_argument, records, True, suite.startswith("planet-volume"))
            run_probe(args.probe.resolve(), backend, compiled_argument, records + 1, False, suite.startswith("planet-volume"))
            print(f"{suite}: numerical output and missing-write rejection passed on {backend}.", flush=True)


if __name__ == "__main__":
    main()
