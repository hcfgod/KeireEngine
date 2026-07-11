# C++ Cross-Platform Core-Client Template

[![CI](https://github.com/hcfgod/C-Cross-Platform-Core-Client-Template/actions/workflows/ci.yml/badge.svg)](https://github.com/hcfgod/C-Cross-Platform-Core-Client-Template/actions/workflows/ci.yml)
[![Security](https://github.com/hcfgod/C-Cross-Platform-Core-Client-Template/actions/workflows/security.yml/badge.svg)](https://github.com/hcfgod/C-Cross-Platform-Core-Client-Template/actions/workflows/security.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE.txt)

A reproducible C++20 starter with a static Core library, Client application, doctest suite, private asynchronous spdlog integration, Premake generation, sanitizers, LLVM coverage, SDK packaging, and Windows/Linux/macOS automation.

## Quick Start

Clone submodules with the repository:

```sh
git clone --recurse-submodules https://github.com/hcfgod/C-Cross-Platform-Core-Client-Template.git
cd C-Cross-Platform-Core-Client-Template
```

Windows PowerShell:

```powershell
./Scripts/project.ps1 bootstrap -Generator vs2022
./Scripts/project.ps1 test -Generator vs2022 -Configuration Debug
./Scripts/project.ps1 run -Generator vs2022 -Configuration Debug
```

Linux:

```sh
bash Scripts/project.sh bootstrap --generator ninja --toolset gcc
bash Scripts/project.sh test --generator ninja --configuration Debug --toolset gcc
bash Scripts/project.sh run --generator ninja --configuration Debug --toolset gcc
```

macOS:

```sh
bash Scripts/project.sh bootstrap --generator ninja --toolset clang
bash Scripts/project.sh test --generator ninja --configuration Debug --toolset clang
bash Scripts/project.sh run --generator ninja --configuration Debug --toolset clang
```

Run `Scripts/project.bat` on Windows or `Scripts/project.sh` on Unix without a command for an interactive menu. Commands work from the repository root or from `Scripts`.

## Commands

| Command | Purpose |
| --- | --- |
| `bootstrap` | Verify/install tools and restore locked vendor dependencies |
| `generate` | Generate IDE, Ninja, Make, or compile-database files |
| `build` | Build a selected target and configuration |
| `test` | Build and run the doctest executable |
| `run` | Build and smoke-run Client |
| `coverage` | Run Clang source coverage and enforce 80% line coverage |
| `package` | Test, smoke-run, and create runtime/SDK archives and checksums |
| `doctor` | Report detected tools, versions, identity, and environment |
| `clean` | Remove build, generated, or all disposable outputs |
| `vendor-update` | Intentionally update one dependency lock and working tree |
| `rename` | Transactionally rename the complete template |
| `help` | Show launcher syntax and supported values |

Common examples:

```powershell
./Scripts/project.ps1 generate -Generator ninja -Architecture ARM64 -Toolset clang -Force
./Scripts/project.ps1 clean -CleanScope generated
./Scripts/project.ps1 package -Generator vs2022 -Configuration Dist
```

```sh
bash Scripts/project.sh generate --generator ninja --architecture ARM64 --toolset clang --force
bash Scripts/project.sh clean --clean-scope generated
bash Scripts/project.sh package --generator ninja --configuration Dist --toolset clang
```

`default` resolves before Premake runs: MSVC for Visual Studio and Windows Ninja, GCC for Windows GNU Make and Linux, and Clang for macOS. Generation stamps record the concrete toolset.

## Build Matrix

Configurations are `Debug`, `Release`, `Dist`, `DebugASan`, `DebugUBSan`, `DebugTSan`, and `Coverage`. Release and Dist define `NDEBUG`; Dist also enables link-time optimization. CI adds fatal warnings.

| Platform | Generators | Toolsets | Architectures |
| --- | --- | --- | --- |
| Windows | VS2019, VS2022, VS2026, Ninja, GNU Make, compile commands | MSVC, GCC, Clang where compatible | x86_64, ARM64 |
| Linux | Ninja, GNU Make, compile commands | GCC, Clang | x86_64, ARM64 |
| macOS | Ninja, Xcode, GNU Make, compile commands | Clang | x86_64, ARM64 |

ASan is supported on all three operating systems. UBSan and TSan are supported with GCC/Clang on Linux and Clang on macOS, but rejected with MSVC. Coverage requires Ninja and Clang and writes LCOV plus HTML under `Build/Coverage/<system-architecture>`.

## Project Layout

```text
Config/                 Project identity and immutable dependency lock
Core/                   Static library and public Core/<header> API
Client/                 Console application
Tests/                  Independent doctest cases
Vendor/                 Pinned spdlog and doctest submodules
Scripts/Premake/        Shared Premake policy
Scripts/Unix/           Shared macOS/Linux implementation
Scripts/<platform>/     Platform bootstrap and wrappers
Tools/<platform>/       Ignored, checksum-verified local tools
.github/workflows/      CI, compatibility, security, and packaging
```

The root Premake file owns the workspace. Each target owns its project definition. Public consumers include `Core/Core.h` or `Core/Log.h`.

## Logging

`Core::Log` owns a private spdlog thread pool and never changes spdlog's global registry or default logger. Core and Client use separate asynchronous rotating-file loggers. `LogConfig::EnableConsole` controls the color console sink and defaults to true.

Logs default to `Logs` relative to the process working directory. IDE debug directories and scripts use the repository root. Initialization is idempotent only for identical configuration; conflicting configuration throws. `LoggerHandle` coordinates active calls with exclusive shutdown, and disabled macro levels evaluate neither logger acquisition nor message arguments.

## Dependencies

`Config/Dependencies.lock` is the source of truth for tool URLs, archive hashes, installer pins, and submodule commits. Normal bootstrap verifies immutable state and never stages files or advances dependency pointers.

Intentional updates are explicit:

```powershell
./Scripts/project.ps1 vendor-update -Dependency spdlog -Tag v1.18.0
```

```sh
bash Scripts/project.sh vendor-update --dependency spdlog --tag v1.18.0
```

Review the diff and upstream changes before running the Git commands printed by the updater.

## Rename The Template

Rename must run in a clean Git worktree; non-Git copies are also supported. The identifier must be PascalCase. Changes are transactional and are never staged or committed.

```powershell
./Scripts/project.ps1 rename -Name MyGame -DisplayName "My Game" -Repository owner/my-game
```

```sh
bash Scripts/project.sh rename --name MyGame --display-name "My Game" --repository owner/my-game
```

This derives `MyGameCore`, `MyGameClient`, `MyGameTests`, namespace `MyGame`, matching folders, package names, workflow URLs, and identity manifest values. Build the renamed copy before committing it.

## CI And Packages

Required CI covers Windows/VS2022, Linux/GCC, macOS/Clang, x64 and ARM64 smoke builds, sanitizers, Client execution, script regression tests, 80% LLVM line coverage, formatting, clang-tidy, ShellCheck, PSScriptAnalyzer, actionlint, yamllint, and Python validation. Extended Compatibility runs weekly and manually. Dependabot checks Actions and submodules weekly.

CodeQL and Dependency Review are an explicit repository opt-in. Enable Dependency Graph and code scanning in GitHub, create the repository variable `ENABLE_ADVANCED_SECURITY=true`, and require the resulting checks in the `master` branch protection rules. Once enabled, workflow and upload failures are fatal; CodeQL findings are governed by the repository's code-scanning rules. Privileged CodeQL uploads are skipped for pull requests from forks.

Version tags and manual release workflow runs create SDK archives without publishing a GitHub Release. Archives contain Client, Core static library, public headers, spdlog headers, complete dependency license texts, notices, documentation, and a validated JSON build manifest. SHA-256 files and separate Client/Core symbol archives are included where available; Dist packages are intentionally stripped.

## Troubleshooting

- Run `doctor` first when a compiler, SDK, or package manager is not found.
- Use `--update` or `-Update` only when installed system tools should be upgraded.
- Run forced generation after changing a compiler installation or toolchain: `--force` or `-Force`.
- Do not bypass a checksum failure. Verify the lock entry against the upstream release.
- Windows MSVC builds require the Desktop development with C++ workload for the requested Visual Studio major version.
- Apple's ASan runtime does not support leak detection; the macOS scripts disable that check.

See [Architecture](docs/Architecture.md), [Contributing](CONTRIBUTING.md), [Security](SECURITY.md), and [Changelog](CHANGELOG.md).
