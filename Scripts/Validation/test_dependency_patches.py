#!/usr/bin/env python3
"""Exercise real dependency patch helpers in disposable repositories, without building dependencies.

PowerShell functions are extracted through its parser AST; Bash helpers/digest branches are extracted without
executing dependency-script top-level setup. Unsupported shells are explicit skips. On Windows, optionally set
KEIRE_TEST_BASH to Git Bash's bash.exe to cover Unix helpers without accidentally selecting WSL's launcher.
"""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
POWERSHELL = shutil.which("pwsh") or (shutil.which("powershell") if os.name == "nt" else None)
BASH = os.environ.get("KEIRE_TEST_BASH") or (shutil.which("bash") if os.name != "nt" else None)
GIT = shutil.which("git")


POWERSHELL_RUNNER = r"""
param([string]$DependencyScript, [string]$Action, [string]$Repository,
      [string]$Commit, [string]$Digest, [string]$PatchDirectory)
$ErrorActionPreference = 'Stop'
$tokens = $null
$parseErrors = $null
$tree = [Management.Automation.Language.Parser]::ParseFile($DependencyScript, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
foreach ($functionName in @('Get-DependencyPatchDigest', 'Assert-PatchedDependencySource')) {
    $definitions = @($tree.FindAll({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $functionName
    }, $true))
    if ($definitions.Count -ne 1) { throw "Expected exactly one dependency helper: $functionName" }
    . ([scriptblock]::Create($definitions[0].Extent.Text))
}
$patches = @(Get-ChildItem -LiteralPath $PatchDirectory -Filter '*.patch' -File | Sort-Object Name)
if ($Action -eq 'digest') {
    Get-DependencyPatchDigest -PatchFiles $patches
} elseif ($Action -eq 'validate') {
    Assert-PatchedDependencySource -Path $Repository -Commit $Commit -Digest $Digest -PatchFiles $patches -Name SDL
} else {
    throw "Unknown fixture action: $Action"
}
exit 0
"""


def extract_bash_function(source: str, name: str) -> str:
    match = re.search(rf"^{re.escape(name)}\(\) \{{\n.*?^\}}\n", source, re.MULTILINE | re.DOTALL)
    if not match:
        raise AssertionError(f"Could not extract dependency function {name}; update this isolated test adapter.")
    return match.group(0)


def extract_digest_branch(source: str, dependency: str) -> str:
    for match in re.finditer(r"^if command -v sha256sum\b.*?^fi\n", source, re.MULTILINE | re.DOTALL):
        if f"{dependency}_patch_digest=" in match.group(0):
            return match.group(0)
    raise AssertionError(f"Could not extract {dependency} digest branch; update this isolated test adapter.")


class PatchFixture:
    """All Git writes are confined to this temporary repository, never the engine checkout."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="Keire dependency patches ")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.repository = self.directory / "fixture source"
        self.repository.mkdir()
        self.patches = self.directory / "patch inputs"
        self.patches.mkdir()
        self.environment = dict(os.environ)
        # Avoid inherited repository selectors and user hooks/signing in disposable fixtures.
        for key in list(self.environment):
            if key.startswith("GIT_"):
                del self.environment[key]
        self.environment.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull)
        self.git("init", "--quiet")
        self.git("config", "core.autocrlf", "false")
        self.git("config", "core.hooksPath", str(self.directory / "absent hooks"))
        self.git("config", "user.name", "Dependency regression fixture")
        self.git("config", "user.email", "fixture@example.invalid")
        self.source = self.repository / "source.cpp"
        self.original = "".join(f"line {index}\n" for index in range(50))
        self.source.write_text(self.original, encoding="utf-8", newline="\n")
        (self.repository / "second.cpp").write_text("second original\n", encoding="utf-8", newline="\n")
        (self.repository / "unrelated.cpp").write_text("unchanged\n", encoding="utf-8", newline="\n")
        self.git("add", "source.cpp", "second.cpp", "unrelated.cpp")
        self.git("-c", "commit.gpgsign=false", "commit", "--quiet", "-m", "Fixture baseline")
        self.commit = self.git("rev-parse", "HEAD").decode().strip()
        self.source.write_text(self.original.replace("line 10\n", "patched line 10\n"), encoding="utf-8", newline="\n")
        (self.patches / "001-first.patch").write_bytes(self.git("diff", "--full-index", "--binary", "--", "source.cpp"))
        (self.repository / "second.cpp").write_text("second patched\n", encoding="utf-8", newline="\n")
        (self.patches / "002-second.patch").write_bytes(self.git("diff", "--full-index", "--binary", "--", "second.cpp"))
        self.digest = self.canonical_digest()
        self.stamp = self.repository / "keire-sdl-patch.stamp"
        self.write_stamp()
        self.prepare_runner()

    def git(self, *arguments: str) -> bytes:
        return subprocess.check_output([GIT, "-C", str(self.repository), *arguments], env=self.environment,
                                       stderr=subprocess.STDOUT, timeout=30)

    def canonical_digest(self) -> str:
        value = hashlib.sha256()
        for patch in sorted(self.patches.glob("*.patch")):
            value.update(patch.name.encode("utf-8") + b"\n" + patch.read_bytes())
        return value.hexdigest()

    def write_stamp(self):
        self.stamp.write_text(f"{self.commit}|{self.digest}\n", encoding="utf-8", newline="\n")

    def assert_valid(self):
        result = self.run_helper("validate")
        self.assertEqual(result.returncode, 0, result.stdout)

    def assert_rejected(self):
        result = self.run_helper("validate")
        self.assertNotEqual(result.returncode, 0, "Modified dependency cache was accepted:\n" + result.stdout)

    def test_exact_filename_one_lf_and_bytes_digest(self):
        for action in self.digest_actions:
            with self.subTest(action=action):
                result = self.run_helper(action)
                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertEqual(result.stdout.strip(), self.digest)

    def test_pristine_patch_cache_is_accepted(self):
        self.assert_valid()

    def test_extra_same_file_edit_outside_patch_hunk_is_rejected(self):
        self.assert_valid()
        text = self.source.read_text(encoding="utf-8").replace("line 45\n", "unexpected line 45\n")
        self.source.write_text(text, encoding="utf-8", newline="\n")
        # Reverse-apply alone still succeeds; the production blob comparison must detect this change.
        for patch in sorted(self.patches.glob("*.patch")):
            self.git("apply", "--reverse", "--check", str(patch))
        self.assert_rejected()

    def test_staged_unrelated_edit_is_rejected(self):
        self.assert_valid()
        (self.repository / "unrelated.cpp").write_text("unexpected staged edit\n", encoding="utf-8", newline="\n")
        self.git("add", "unrelated.cpp")
        self.assert_rejected()

    def test_corrupt_patch_stamp_is_rejected(self):
        self.assert_valid()
        self.stamp.write_text(f"{self.commit}|{'0' * 64}\n", encoding="utf-8", newline="\n")
        self.assert_rejected()

    def test_missing_blob_identity_is_rejected(self):
        self.assert_valid()
        patch = self.patches / "001-first.patch"
        text = re.sub(r"^index .*\n", "", patch.read_text(encoding="utf-8"), flags=re.MULTILINE)
        patch.write_text(text, encoding="utf-8", newline="\n")
        self.digest = self.canonical_digest()
        self.write_stamp()
        self.assert_rejected()


@unittest.skipUnless(GIT and POWERSHELL, "PowerShell or Git unavailable; run on a host with both installed")
class PowerShellDependencyPatchTests(PatchFixture, unittest.TestCase):
    digest_actions = ("digest",)

    def prepare_runner(self):
        self.runner = self.directory / "isolated helpers.ps1"
        self.runner.write_text(POWERSHELL_RUNNER, encoding="utf-8", newline="\n")

    def run_helper(self, action):
        return subprocess.run([POWERSHELL, "-NoProfile", "-NonInteractive", "-File", str(self.runner),
                               "-DependencyScript", str(ROOT / "Scripts/Windows/dependencies.ps1"),
                               "-Action", action, "-Repository", str(self.repository), "-Commit", self.commit,
                               "-Digest", self.digest, "-PatchDirectory", str(self.patches)],
                              env=self.environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, encoding="utf-8", errors="replace", timeout=60)


@unittest.skipUnless(GIT and BASH, "Native Bash unavailable; Windows may explicitly set KEIRE_TEST_BASH to Git Bash")
class BashDependencyPatchTests(PatchFixture, unittest.TestCase):
    digest_actions = ("digest-assimp", "digest-sdl")

    def prepare_runner(self):
        source = (ROOT / "Scripts/Unix/dependencies.sh").read_text(encoding="utf-8")
        validator = extract_bash_function(source, "validate_patched_dependency_source")
        branches = {name: extract_digest_branch(source, name) for name in ("assimp", "sdl")}
        text = "set -euo pipefail\n" + validator + "\n"
        text += 'action="$1"; repository="$2"; commit="$3"; digest="$4"; patches="$5"\n'
        text += 'assimp_patches=("$patches"/*.patch); sdl_patches=("$patches"/*.patch)\n'
        text += 'case "$action" in\n'
        for name, branch in branches.items():
            text += f'digest-{name})\n{branch}\nprintf \'%s\\n\' "${{{name}_patch_digest}}"\n;;\n'
        text += 'validate) validate_patched_dependency_source "$repository" SDL "$commit" "$digest" "${sdl_patches[@]}" ;;\n'
        text += '*) printf "Unknown fixture action\\n" >&2; exit 2 ;;\nesac\n'
        self.runner = self.directory / "isolated helpers.sh"
        self.runner.write_text(text, encoding="utf-8", newline="\n")

    def run_helper(self, action):
        return subprocess.run([BASH, "--noprofile", "--norc", self.runner.as_posix(), action,
                               self.repository.as_posix(), self.commit, self.digest, self.patches.as_posix()],
                              env=self.environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, encoding="utf-8", errors="replace", timeout=60)


if __name__ == "__main__":
    unittest.main()
