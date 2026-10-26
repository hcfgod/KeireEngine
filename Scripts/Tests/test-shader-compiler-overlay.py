#!/usr/bin/env python3
"""Configure the real pinned-input host overlay without rebuilding compiler dependencies."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
OVERLAY = ROOT / "Scripts/Dependencies/ShaderCompilerOverlay.cmake"


@unittest.skipUnless(shutil.which("cmake") and shutil.which("ninja"), "CMake and Ninja are required")
class ShaderCompilerOverlayTests(unittest.TestCase):
    def configure(self, root: Path, *, drift: bool = False, duplicate: bool = False) -> subprocess.CompletedProcess:
        source = root / "source"
        (source / "src").mkdir(parents=True)
        for filename in ("cli.c", "SDL_shadercross.c"):
            original = (ROOT / "Vendor/SDL_shadercross/src" / filename).read_bytes()
            if drift and filename == "cli.c":
                original = original.replace(b"char *includeDir = NULL;", b"char *changedInclude = NULL;")
            if duplicate and filename == "cli.c":
                original += b"\n    char *includeDir = NULL;\n"
            (source / "src" / filename).write_bytes(original)
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.22)\n"
            "project(SDL3_shadercross LANGUAGES NONE)\n"
            "add_custom_target(shadercross SOURCES src/cli.c)\n"
            "add_custom_target(SDL3_shadercross-static SOURCES src/SDL_shadercross.c)\n",
            encoding="utf-8",
        )
        return subprocess.run(
            ["cmake", "-S", str(source), "-B", str(root / "build"), "-G", "Ninja",
             f"-DCMAKE_PROJECT_INCLUDE={OVERLAY.as_posix()}"],
            capture_output=True, text=True, check=False,
        )

    def test_generates_targeted_copies_without_touching_pinned_sources(self):
        with tempfile.TemporaryDirectory(prefix="Keire-ShaderCompilerOverlay-") as directory:
            root = Path(directory)
            result = self.configure(root)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            cli = (root / "build/KeireHostOverlay/cli.c").read_text(encoding="utf-8")
            library = (root / "build/KeireHostOverlay/SDL_shadercross.c").read_text(encoding="utf-8")
            self.assertIn("includeDirectoryCount >= 17", cli)
            self.assertIn("includeDirectories[includeDirectoryCount++] = argv[i]", cli)
            self.assertIn("#define MAX_DEFINES 129", library)
            self.assertIn("numDefineStrings + 13 + includeDirectoryCount * 2", library)
            self.assertIn("args[argCount++] = includeDirectoriesUtf16[i]", library)
            self.assertIn("includeDirectoryCount > 17", library)
            for filename in ("cli.c", "SDL_shadercross.c"):
                self.assertEqual(
                    (root / "source/src" / filename).read_bytes(),
                    (ROOT / "Vendor/SDL_shadercross/src" / filename).read_bytes(),
                )

    def test_pinned_source_drift_fails_before_installing_an_unpatched_tool(self):
        with tempfile.TemporaryDirectory(prefix="Keire-ShaderCompilerOverlay-") as directory:
            result = self.configure(Path(directory), drift=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("overlay no longer matches", result.stdout + result.stderr)

    def test_ambiguous_upstream_anchor_fails_instead_of_patching_multiple_sites(self):
        with tempfile.TemporaryDirectory(prefix="Keire-ShaderCompilerOverlay-") as directory:
            result = self.configure(Path(directory), duplicate=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("ambiguous upstream input", result.stdout + result.stderr)

    def test_both_platform_launchers_key_overlay_changes_without_discarding_dependency_objects(self):
        windows = (ROOT / "Scripts/Windows/shader-compiler.ps1").read_text(encoding="utf-8")
        unix = (ROOT / "Scripts/Unix/shader-compiler.sh").read_text(encoding="utf-8")
        self.assertIn("Get-FileHash -LiteralPath $overlay -Algorithm SHA256", windows)
        self.assertIn("$configuredKey -ne $dependencyKey", windows)
        self.assertIn('cmake -E sha256sum "$overlay"', unix)
        self.assertIn('"$configured_key" != "$dependency_key"', unix)
        for script in (windows, unix):
            self.assertIn("-DCMAKE_PROJECT_INCLUDE=", script)
            self.assertIn("host-overlay:", script)


if __name__ == "__main__":
    unittest.main()
