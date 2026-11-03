"""Validate sustained runtime intervals without treating renderer latency as frame time."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


def validate_gpu_timing(timing: dict, measured_frames: int) -> None:
    if timing["scope"] != "sampled_completed_gpu_frames_not_complete_timeline":
        raise ValueError("Unknown GPU timing scope.")
    samples = timing["samples"]
    if (type(timing["count"]) is not int or timing["count"] != len(samples)
            or len(samples) > measured_frames):
        raise ValueError("Invalid GPU sample count.")
    if type(timing["available"]) is not bool or timing["available"] != bool(samples):
        raise ValueError("GPU availability does not match samples.")
    previous_frame = 0
    values = []
    for sample in samples:
        frame = sample["frame"]
        value = sample["milliseconds"]
        if type(frame) is not int or frame <= previous_frame:
            raise ValueError("GPU sample frames must be positive and strictly increasing.")
        if (isinstance(value, bool) or not isinstance(value, (int, float))
                or not math.isfinite(value) or value < 0):
            raise ValueError("GPU durations must be finite and nonnegative.")
        previous_frame = frame
        values.append(value)
    names = ("averageMilliseconds", "medianMilliseconds", "p95Milliseconds",
             "p99Milliseconds", "maximumMilliseconds")
    if not values:
        if any(name in timing for name in names):
            raise ValueError("Unavailable GPU timing cannot declare duration summaries.")
        return
    ordered = sorted(values)
    summaries = (math.fsum(values) / len(values),
                 ordered[math.ceil(len(values) * 0.50) - 1],
                 ordered[math.ceil(len(values) * 0.95) - 1],
                 ordered[math.ceil(len(values) * 0.99) - 1], ordered[-1])
    for name, actual in zip(names, summaries):
        declared = timing[name]
        if (isinstance(declared, bool) or not isinstance(declared, (int, float))
                or not math.isclose(declared, actual, rel_tol=1e-6, abs_tol=1e-6)):
            raise ValueError(f"GPU summary does not match raw samples: {name}.")


def validate(report: dict) -> dict:
    if report.get("schemaVersion") != 1 or report.get("status") != "passed":
        raise ValueError("Runtime capture did not complete successfully.")
    timing = report["applicationTiming"]
    if timing["scope"] != "application_render_submission_intervals_not_displayed_fps":
        raise ValueError("Unknown application timing scope.")
    values = timing["intervalsMilliseconds"]
    count = report["workload"]["measuredFrames"]
    if isinstance(count, bool) or not isinstance(count, int) or count < 1:
        raise ValueError("Invalid measured interval count.")
    if "gpuExecutionTiming" in report:
        validate_gpu_timing(report["gpuExecutionTiming"], count)
    if isinstance(timing["count"], bool) or timing["count"] != count or len(values) != count:
        raise ValueError("Incomplete application interval capture.")
    if any(isinstance(value, bool) or not isinstance(value, (int, float))
           or not math.isfinite(value) or value <= 0 for value in values):
        raise ValueError("Intervals must be finite positive numbers.")
    ordered = sorted(values)
    actual = {
        "elapsedMilliseconds": math.fsum(values),
        "averageMilliseconds": math.fsum(values) / count,
        "medianMilliseconds": ordered[math.ceil(count * 0.50) - 1],
        "p95Milliseconds": ordered[math.ceil(count * 0.95) - 1],
        "p99Milliseconds": ordered[math.ceil(count * 0.99) - 1],
        "maximumMilliseconds": ordered[-1],
    }
    for name, value in actual.items():
        declared = timing[name]
        if isinstance(declared, bool) or not isinstance(declared, (int, float)):
            raise ValueError(f"Invalid {name}.")
        if not math.isclose(declared, value, rel_tol=1e-6, abs_tol=1e-6):
            raise ValueError(f"Summary does not match raw intervals: {name}.")
    configuration = report["build"]["configuration"]
    if configuration not in ("Release", "DevDist", "Dist"):
        raise ValueError("Unsupported measured build configuration.")
    target = 10.0 if configuration == "DevDist" else 8.0
    return {
        "configuration": configuration,
        "targetMilliseconds": target,
        "meanTargetMet": actual["averageMilliseconds"] <= target,
        "p95TargetMet": actual["p95Milliseconds"] <= target,
        **actual,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    options = parser.parse_args()
    try:
        result = validate(json.loads(options.capture.read_text(encoding="utf-8-sig")))
    except (KeyError, TypeError, ValueError) as error:
        parser.exit(2, f"Invalid capture: {error}\n")
    print(json.dumps(result, indent=2))
    return 0 if result["meanTargetMet"] and result["p95TargetMet"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
