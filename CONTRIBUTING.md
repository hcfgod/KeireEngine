# Contributing

## Development Workflow

1. Bootstrap and generate with the platform launcher.
2. Build and run tests in the configurations affected by the change.
3. Run the Client smoke test.
4. Keep `git status` free of generated files and downloaded tools.

Do not commit generated Visual Studio, Xcode, Ninja, Make, compile-database, build-output, log, or downloaded-tool files.

## Quality Requirements

C++ uses the repository `.clang-format` and `.clang-tidy` configurations. Shell scripts must pass ShellCheck, PowerShell scripts must pass PSScriptAnalyzer, and workflow files must pass actionlint. Local builds enable extra warnings; CI treats template-code warnings as errors.

Every behavioral fix requires a focused doctest. Logger tests must use isolated temporary directories and must not depend on test registration order. Changes to platform scripts should preserve explicit nonzero exits and safe clean-path containment.

## Dependencies

Normal bootstrap must not move dependency commits or stage files. Use `vendor-update` only when intentionally evaluating a new upstream tag. Review the upstream change, update every platform's pinned commit, run the full required matrix, and then stage the submodule pointer explicitly.

GitHub Actions must be pinned to full commit SHAs. Executables and installer scripts downloaded by bootstrap must have a reviewed SHA-256 value committed alongside their version.

## Compatibility

Required CI must remain green before merging. Changes affecting a non-default generator, compiler, or ARM64 should also run the Extended Compatibility workflow manually. Unsupported combinations should fail with a clear message rather than falling back silently.
