#!/usr/bin/env python3
"""Verify the Editor and Hub diagnostic-bundle product integration contract."""

from __future__ import annotations

import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require_all(relative: str, markers: tuple[str, ...], errors: list[str]) -> None:
    source = read(relative)
    for marker in markers:
        if marker not in source:
            errors.append(f"{relative}: missing diagnostic integration marker {marker!r}")


def main() -> int:
    errors: list[str] = []
    require_all(
        "KeireClient/Source/Editor/EditorWorkspaceTheme.cpp",
        ('BeginMenu("Help")', 'MenuItem("Collect Diagnostics...")', "OpenDiagnosticBundle();"),
        errors,
    )
    require_all(
        "KeireClient/Source/Editor/EditorDiagnosticBundle.cpp",
        (
            "DeviceIdentity()",
            "Capabilities()",
            "Statistics()",
            "RecentFrameTimelines()",
            "LastDeviceLoss()",
            "InstalledPackages()",
            "DiagnosticReports()",
            "CreateProductDiagnosticBundleRequest(snapshot)",
            '"Keire-Editor-Diagnostics.zip"',
            "m_DiagnosticBundle->Draw",
        ),
        errors,
    )
    require_all(
        "KeireClient/Source/EditorWorkspaceLayer.cpp",
        ("DrawDiagnosticBundle(ui);", "DiagnosticBundleDialogController"),
        errors,
    )
    require_all(
        "KeireClient/Source/EditorWorkspaceLifecycle.cpp",
        ("case Phase::DiagnosticBundle:", "m_DiagnosticBundle->Shutdown();"),
        errors,
    )

    require_all(
        "KeireHub/Source/HubProductUi.cpp",
        (
            'IconButton("HubHelp"',
            'BeginPopup("HubHelpPopover")',
            'MenuItem("Collect Diagnostics...")',
            "command.Type = HubUiCommandType::CopyDiagnostics;",
        ),
        errors,
    )
    require_all(
        "KeireHub/Source/HubApplication.cpp",
        (
            "m_DiagnosticBundle.Shutdown();",
            "m_DiagnosticBundle.Draw(ui",
            "case KeireHub::HubUiCommandType::CopyDiagnostics:",
            "m_DiagnosticBundle.Open(",
            "CreateHubDiagnosticBundleSummary(",
        ),
        errors,
    )
    require_all(
        "KeireHub/Source/HubDiagnosticBundleController.cpp",
        (
            "DeviceIdentity()",
            "Capabilities()",
            "Statistics()",
            "RecentFrameTimelines()",
            "LastDeviceLoss()",
            "CreateProductDiagnosticBundleRequest(snapshot)",
            '"Keire-Hub-Diagnostics.zip"',
            "m_Dialog.Draw",
            "m_Dialog.Shutdown",
        ),
        errors,
    )

    diagnostic_sources = tuple((ROOT / "KeireCore/Source/Diagnostics").glob("DiagnosticBundle*.cpp"))
    diagnostic_sources += (
        ROOT / "KeireClient/Source/Editor/EditorDiagnosticBundle.cpp",
        ROOT / "KeireHub/Source/HubDiagnosticBundleController.cpp",
    )
    for path in diagnostic_sources:
        source = path.read_text(encoding="utf-8")
        if "recursive_directory_iterator" in source or "directory_iterator" in source:
            errors.append(f"{path.relative_to(ROOT)}: diagnostic collection must not scan directories")
        if len(source.splitlines()) >= 1500:
            errors.append(f"{path.relative_to(ROOT)}: diagnostic production source is not below 1,500 lines")

    sanitizer = read("KeireCore/Source/Diagnostics/DiagnosticBundleSanitizer.cpp")
    for marker in ("#include <regex>", "std::regex", "regex_replace"):
        if marker in sanitizer:
            errors.append(
                "KeireCore/Source/Diagnostics/DiagnosticBundleSanitizer.cpp: "
                f"whole-buffer regular-expression sanitizer marker remains: {marker!r}"
            )

    for path in (ROOT / "KeireCore/Include/Keire").rglob("*.h"):
        if "DiagnosticBundle" in path.read_text(encoding="utf-8"):
            errors.append(f"{path.relative_to(ROOT)}: internal diagnostic bundle leaked into the supported Core API")

    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("Editor and Hub diagnostic bundle UI, lifecycle, privacy, and internal-boundary wiring is intact.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
