# Changelog

All notable template changes are documented here. The format follows Keep a Changelog, and releases use semantic version tags.

## Unreleased

### Added

- Identity and dependency lock manifests.
- Transactional full-template rename support.
- Doctor, LLVM coverage, SDK package, and script regression commands.
- Native x64/ARM64 selection and concrete compiler resolution.
- CI coverage, compatibility, security, dependency, and release-package automation.

### Changed

- Public Core headers now use the `Core/` include prefix.
- Logging owns private asynchronous spdlog state and supports console suppression.
- Dist builds use link-time optimization and CI treats template warnings as errors.

### Fixed

- Linux Premake bootstrap now accepts release archives whose executable bit is not preserved.
- ARM64 Premake source builds install the platform UUID development headers before compilation.
- Vendor bootstrap verifies the committed submodule pointer and restores detached working trees to that exact hash.
- macOS tool version checks consume complete command output, avoiding `xcodebuild` broken-pipe crashes.
- Dependency review reports a warning instead of failing repositories where GitHub dependency analysis is unavailable.
