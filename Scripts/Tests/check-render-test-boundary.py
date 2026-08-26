#!/usr/bin/env python3
"""Verify renderer fault-injection code is compiled only into test-capable configurations."""

from __future__ import annotations

import pathlib
import re
import sys
import argparse


ROOT = pathlib.Path(__file__).resolve().parents[2]
MARKERS = (
    "InjectDeviceLoss",
    "SaturateRendererQueue",
    "InjectDeviceLossAtNextFrame",
    "InjectDeviceLossAtRetirement",
    "InjectDeviceLossAtNextRetirement",
    "test fence retirement injection",
    "InjectDeviceLossWithActiveResources",
    "InjectDeviceLossWithActiveResourcesAtNextFrame",
    "InjectCaptureFailure",
    "InjectCaptureFailureAtNextFrame",
    "InjectRecoveryAtAdmissionBarrier",
    "InjectPostSubmitFailure",
    "InjectPostSubmitFailureAtNextFrame",
    "InjectTerminalFailureAtNextAcceptedFrame",
    "Injected accepted-frame terminal failure.",
    "InjectRecoveryCandidateFailure",
    "RenderRecoveryCandidateFault",
    "InjectHealthyRecoveryCandidateFailures",
    "InjectLostRecoveryCandidateFailures",
    "HealthyRecoveryCandidateCleanupCount",
    "LastRetriedVfxSnapshotCount",
    "RecoveryAttemptCountForTest",
    "LastRecoveryBackoffMillisecondsForTest",
    "SatisfyRecoveryStabilityWindowForTest",
    "ValidateDeviceLoss",
    "DeviceLossInjectedDuringLoading",
    "DeviceLossInjectedDuringPlay",
    "--validate-device-loss",
    "--smoke-play-device-loss",
    "Injected healthy recovery-candidate failure.",
    "Injected candidate device lost.",
    "Injected post-submit publication failure.",
    "SetDeviceRecoveryStateForTest",
    "DelayNextAcceptedFrame",
    "DelayNextAcceptedFrameMilliseconds",
    "StartThreadedHeadlessForTest",
    "ThreadedHeadlessForTest",
    "BlockNextAcceptedFrame",
    "AcceptedFrameBlocked",
    "WaitForAcceptedFrameBlock",
    "WaitForFrameAdmissionWaiter",
    "ReleaseAcceptedFrameBlock",
    "ReleaseAcceptedFrame",
    "FrameAdmissionWaiters",
    "SceneCaptureEnumerationCount",
    "RuntimeUiCaptureEnumerationCount",
    "LastCapturedDirectionalLightEntity",
    "LastCapturedAdditiveScene",
    "LostGenerationAbandonedHandleCount",
    "LostGenerationGpuCleanupCallCount",
    "test active-resource frame injection",
    "CompleteFrameTwiceForTest",
    "ClassifyDeviceFailureForTest",
    "Injected GPU device loss.",
    "test frame injection",
)
PRODUCTION_FILES = (
    ROOT / "KeireCore/Include/KeireInternal/RenderInternal.h",
    ROOT / "KeireCore/Include/KeireInternal/Rendering/RenderPipelineStateInternal.h",
    ROOT / "KeireCore/Source/Rendering/RenderFrameExecution.cpp",
    ROOT / "KeireCore/Source/Rendering/RenderDeviceRecovery.cpp",
    ROOT / "KeireCore/Source/Rendering/RenderDeviceShutdown.cpp",
    ROOT / "KeireCore/Source/Rendering/RenderDeviceFrame.cpp",
    ROOT / "KeireCore/Source/Rendering/RenderPipelineLifecycle.cpp",
    ROOT / "KeireCore/Source/Rendering/RenderResourceCaches.cpp",
    ROOT / "KeireCore/Source/Rendering/RenderSystem.cpp",
    ROOT / "KeireRuntime/Include/KeireRuntimeInternal/RuntimeAdditiveValidation.h",
    ROOT / "KeireRuntime/Include/KeireRuntimeInternal/RuntimeCommandLine.h",
    ROOT / "KeireRuntime/Source/RuntimeAdditiveValidation.cpp",
    ROOT / "KeireRuntime/Source/RuntimeApplication.cpp",
    ROOT / "KeireRuntime/Source/RuntimeCommandLine.cpp",
    ROOT / "KeireRuntime/Source/RuntimeCommandLineDescription.cpp",
    ROOT / "KeireClient/Include/KeireClient/Editor/EditorSmokePlayValidation.h",
    ROOT / "KeireClient/Include/KeireClient/EditorWorkspaceLayer.h",
    ROOT / "KeireClient/Source/ClientApplication.cpp",
    ROOT / "KeireClient/Source/Editor/EditorSmokePlayValidation.cpp",
    ROOT / "KeireClient/Source/EditorWorkspaceLayer.cpp",
)


def guarded_marker_errors(path: pathlib.Path) -> list[str]:
    errors: list[str] = []
    stack: list[bool] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        stripped = line.strip()
        if re.match(r"#\s*(if|ifdef|ifndef)\b", stripped):
            is_positive_test_guard = (
                "KEIRE_ENABLE_TEST_HOOKS" in stripped
                and not re.match(r"#\s*ifndef\b", stripped)
                and "!defined" not in stripped
            )
            stack.append(is_positive_test_guard)
        elif re.match(r"#\s*else\b", stripped) and stack:
            stack[-1] = False
        elif re.match(r"#\s*endif\b", stripped) and stack:
            stack.pop()

        if any(marker in line for marker in MARKERS) and not any(stack):
            errors.append(f"{path.relative_to(ROOT)}:{line_number}: renderer test hook is not explicitly guarded")
    return errors


def premake_errors() -> list[str]:
    path = ROOT / "Scripts/Premake/Common.lua"
    current_filter = ""
    errors: list[str] = []
    observed = 0
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        stripped = line.strip()
        if stripped.startswith("filter "):
            current_filter = stripped
        if "KEIRE_ENABLE_TEST_HOOKS" not in line:
            continue
        observed += 1
        if current_filter != 'filter "configurations:Debug or DebugASan"':
            errors.append(
                f"{path.relative_to(ROOT)}:{line_number}: test-hook define escapes Debug/DebugASan"
            )
    if observed != 1:
        errors.append(f"{path.relative_to(ROOT)}: expected exactly one Debug/DebugASan test-hook define")
    return errors


def binary_errors(paths: list[pathlib.Path]) -> list[str]:
    errors: list[str] = []
    for path in paths:
        if not path.is_file():
            errors.append(f"{path}: release/package binary is missing")
            continue
        data = path.read_bytes()
        for marker in MARKERS:
            if marker.encode("utf-8") in data or marker.encode("utf-16-le") in data:
                errors.append(f"{path}: release/package binary contains renderer test-hook marker {marker!r}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", action="append", default=[], type=pathlib.Path)
    arguments = parser.parse_args()
    errors = premake_errors()
    for path in PRODUCTION_FILES:
        errors.extend(guarded_marker_errors(path))
    errors.extend(binary_errors(arguments.binary))
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("Renderer fault-injection code is restricted to Debug and DebugASan configurations.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
