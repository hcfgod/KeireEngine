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
