# Security Policy

## Supported Versions

Security fixes are applied to the current `master` branch. This template does not maintain parallel release branches.

## Reporting A Vulnerability

Do not open a public issue for a suspected vulnerability. Use GitHub's private vulnerability reporting for this repository. Include affected commit, platform/toolchain, reproduction steps, expected impact, and any suggested mitigation.

Dependency and workflow changes are locked by commit or checksum. Do not bypass failed integrity checks while investigating a report.

## Automated checks

CodeQL and Dependency Review require repository features that are not available in every copied template. Enable Dependency Graph and code scanning, set the repository variable `ENABLE_ADVANCED_SECURITY=true`, and require the resulting checks in branch protection. With that variable enabled, action and upload failures are not suppressed.
