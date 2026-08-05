#!/usr/bin/env python3
"""Validate a Kéire profiler capture against a reference-hardware gate."""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import sys
from pathlib import Path


SUMMARY_PATTERN = re.compile(
    r"^(Frame|Average|P95|P99):\s+([0-9]+(?:\.[0-9]+)?)\s+ms(?:\s|$)"
)
STUTTER_PATTERN = re.compile(r"^Stutters:\s+([0-9]+)\s*$")


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--snapshot", type=Path, required=True, help="Copy Full Snapshot profiler text."
    )
    parser.add_argument(
        "--history", type=Path, required=True, help="Copy Frame CSV profiler history."
    )
    parser.add_argument(
        "--metadata", type=Path, required=True, help="Capture hardware metadata JSON."
    )
    parser.add_argument(
        "--config", type=Path, default=Path("Config/PerformanceGates.json")
    )
    parser.add_argument("--profile", default="sandbox-vfx-reference")
    return parser.parse_args()


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        raise ValueError("Profiler history contains no frames.")
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]


def parse_snapshot(path: Path) -> tuple[dict[str, float], int, dict[str, float]]:
    summary: dict[str, float] = {}
    counters: dict[str, float] = {}
    stutters: int | None = None
    for raw_line in path.read_text(encoding="utf-8-sig").splitlines():
        line = raw_line.strip()
        if match := SUMMARY_PATTERN.match(line):
            summary[match.group(1)] = float(match.group(2))
        elif match := STUTTER_PATTERN.match(line):
            stutters = int(match.group(1))
        elif line.startswith("COUNTER,"):
            parts = line.split(",", 3)
            if len(parts) != 4:
                raise ValueError(f"Malformed profiler counter: {line}")
            name = parts[2]
            if name in counters:
                raise ValueError(
                    f"Profiler snapshot contains duplicate counter {name!r}."
                )
            counters[name] = float(parts[3])
    missing = sorted({"Average", "P95", "P99"}.difference(summary))
    if missing or stutters is None:
        raise ValueError(
            f"Profiler snapshot is missing summary values: {missing or ['Stutters']}"
        )
    return summary, stutters, counters


def parse_history(path: Path) -> list[float]:
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames is None or "duration_us" not in reader.fieldnames:
            raise ValueError("Profiler history is missing its duration_us column.")
        values = [float(row["duration_us"]) / 1000.0 for row in reader]
    if any(not math.isfinite(value) or value < 0.0 for value in values):
        raise ValueError("Profiler history contains an invalid frame duration.")
    return values


def validate(options: argparse.Namespace) -> list[str]:
    configuration = json.loads(options.config.read_text(encoding="utf-8"))
    if configuration.get("schemaVersion") != 1:
        raise ValueError("Performance gate configuration schemaVersion must be 1.")
    profiles = configuration.get("profiles", {})
    if options.profile not in profiles:
        raise ValueError(f"Unknown performance gate profile {options.profile!r}.")
    profile = profiles[options.profile]
    metadata = json.loads(options.metadata.read_text(encoding="utf-8"))
    summary, stutters, counters = parse_snapshot(options.snapshot)
    history = parse_history(options.history)
    failures: list[str] = []

    expected_hardware = profile["hardwareId"]
    if metadata.get("hardwareId") != expected_hardware:
        failures.append(
            f"hardwareId is {metadata.get('hardwareId')!r}; expected {expected_hardware!r}"
        )
    for field, pattern_key in (
        ("gpuBackend", "gpuBackendPattern"),
        ("gpuName", "gpuNamePattern"),
        ("gpuDriver", "gpuDriverPattern"),
        ("cpuName", "cpuNamePattern"),
    ):
        value = str(metadata.get(field, ""))
        if re.search(profile[pattern_key], value) is None:
            failures.append(
                f"{field} {value!r} does not match {profile[pattern_key]!r}"
            )
    for field in ("resolution", "workload"):
        if metadata.get(field) != profile[field]:
            failures.append(
                f"{field} is {metadata.get(field)!r}; expected {profile[field]!r}"
            )
    allowed_configurations = set(profile["allowedBuildConfigurations"])
    if metadata.get("buildConfiguration") not in allowed_configurations:
        failures.append(
            f"buildConfiguration must be one of {sorted(allowed_configurations)}"
        )
    if (
        re.fullmatch(r"[0-9a-fA-F]{40,64}", str(metadata.get("engineCommit", "")))
        is None
    ):
        failures.append(
            "engineCommit must be a full 40-64 digit hexadecimal source revision"
        )

    minimum_frames = int(profile["minimumHistoryFrames"])
    if len(history) < minimum_frames:
        failures.append(
            f"history has {len(history)} frames; at least {minimum_frames} are required"
        )
    history_summary = {
        "Average": sum(history) / len(history) if history else math.inf,
        "P95": percentile(history, 0.95) if history else math.inf,
        "P99": percentile(history, 0.99) if history else math.inf,
    }
    for name, maximum in profile["maximumSummaryMilliseconds"].items():
        if summary[name] > maximum:
            failures.append(
                f"snapshot {name} is {summary[name]:.4f} ms; maximum is {maximum:.4f} ms"
            )
        tolerance = max(0.05, history_summary[name] * 0.05)
        if abs(summary[name] - history_summary[name]) > tolerance:
            failures.append(
                f"snapshot {name} ({summary[name]:.4f} ms) disagrees with history ({history_summary[name]:.4f} ms)"
            )
    if stutters > profile["maximumStutters"]:
        failures.append(
            f"snapshot reports {stutters} stutters; maximum is {profile['maximumStutters']}"
        )

    for name, minimum in profile.get("minimumCounters", {}).items():
        if name not in counters:
            failures.append(f"required counter {name!r} is missing")
        elif counters[name] < minimum:
            failures.append(
                f"counter {name!r} is {counters[name]:.4f}; minimum is {minimum:.4f}"
            )
    for name, maximum in profile.get("maximumCounters", {}).items():
        if name not in counters:
            failures.append(f"required counter {name!r} is missing")
        elif counters[name] > maximum:
            failures.append(
                f"counter {name!r} is {counters[name]:.4f}; maximum is {maximum:.4f}"
            )
    return failures


def main() -> int:
    options = arguments()
    try:
        failures = validate(options)
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        print(
            f"Performance gate could not evaluate the capture: {error}", file=sys.stderr
        )
        return 2
    if failures:
        print(f"Performance gate {options.profile!r} failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    print(f"Performance gate {options.profile!r} passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
