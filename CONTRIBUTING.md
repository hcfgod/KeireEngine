# Contributing

## Development Workflow

1. Bootstrap through the platform launcher.
2. Run the script regression harness for script changes.
3. Build and test every affected configuration and toolset.
4. Run Client as a smoke test.
5. Run coverage for logger or Core behavior changes.
6. Confirm `git status --porcelain` contains no generated output.

Use `./Scripts/Tests/test-windows.ps1` on Windows and `bash Scripts/Tests/test-unix.sh` on Unix. Do not commit generated IDE/Ninja/Make files, compile databases, logs, build output, coverage output, packages, or downloaded tools.

## Quality Gates

C++ must match `.clang-format`, pass `.clang-tidy`, and compile warning-free under the affected toolchains. CI treats template warnings as errors. Shell scripts must pass ShellCheck, PowerShell must pass PSScriptAnalyzer, Python must pass syntax and Ruff checks, workflows must pass actionlint, and YAML must pass yamllint.

Behavioral fixes require focused doctest coverage. Tests must isolate logger directories and lifecycle state. Script changes need regression coverage for parsing, resolution, containment, installer failure, lock behavior, or rename transactions as applicable.

## Compatibility

Required CI must be green before merge. Run Extended Compatibility manually for changes affecting non-default generators, compiler discovery, ARM64, sanitizers, packaging, or dependency installation. Unsupported combinations must fail before generation with a specific diagnostic.

## Dependencies

Normal bootstrap must not fetch mutable tags, alter submodule pointers, or stage files. Use `vendor-update` only for an intentional dependency review. Verify the resulting commit, update notices when needed, run the complete matrix, then stage the lock file and submodule pointer yourself.

GitHub Actions must use full commit SHAs. Downloaded executables and installer scripts require a reviewed SHA-256 in `Config/Dependencies.lock`.

## Pull Requests

Keep changes scoped and explain user-visible behavior, tested combinations, and residual platform limitations. Include release-note text in `CHANGELOG.md` for changes that affect template users.
