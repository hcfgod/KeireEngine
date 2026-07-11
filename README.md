# C++ Cross-Platform Core-Client Template

A C++20 starter repository with a reusable static `Core` library, a `Client` console application, doctest coverage, asynchronous spdlog logging, Premake project generation, sanitizers, and GitHub Actions for Windows, Linux, and macOS.

## Quick Start

Clone with submodules:

```sh
git clone --recurse-submodules https://github.com/hcfgod/C-Cross-Platform-Core-Client-Template.git
cd C-Cross-Platform-Core-Client-Template
```

Windows users can double-click `Scripts/project.bat` for the menu or use PowerShell:

```powershell
.\Scripts\project.ps1 bootstrap -Generator vs2022
.\Scripts\project.ps1 generate -Generator vs2022
.\Scripts\project.ps1 test -Generator vs2022 -Configuration Debug
.\Scripts\project.ps1 run -Generator vs2022 -Configuration Debug
```

Linux users can run:

```sh
bash Scripts/project.sh bootstrap --generator ninja --toolset gcc
bash Scripts/project.sh generate --generator ninja --toolset gcc
bash Scripts/project.sh test --generator ninja --configuration Debug --toolset gcc
bash Scripts/project.sh run --generator ninja --configuration Debug --toolset gcc
```

macOS users can use Ninja or Xcode:

```sh
bash Scripts/project.sh bootstrap --generator ninja --toolset clang
bash Scripts/project.sh generate --generator ninja --toolset clang
bash Scripts/project.sh test --generator ninja --configuration Debug --toolset clang
```

Missing prerequisites are installed automatically when the platform package manager supports it. Existing system tools are upgraded only when `-Update` or `--update` is supplied.

## Commands

| Command | Purpose |
|---|---|
| `bootstrap` | Verify/install tools and restore pinned vendor dependencies |
| `generate` | Generate IDE projects, Ninja files, Makefiles, or a compile database |
| `build` | Build a selected project target and configuration |
| `test` | Build and run the doctest executable |
| `run` | Build and run the Client from the repository working directory |
| `clean` | Remove build output and generated project files |
| `vendor-update` | Explicitly check out a new dependency tag without staging it |

Windows uses `-Architecture`, `-Toolset`, and `-Configuration`. Linux/macOS use `--architecture`, `--toolset`, and `--configuration`. Run either launcher without a command to open its interactive menu.

## Build Options

Architectures default to the native host and may be overridden with `x86_64` or `ARM64`. Output is written to:

```text
Build/Bin/<Configuration>-<system>-<architecture>/<project>/
Build/Intermediates/<Configuration>-<system>-<architecture>/<project>/
```

| Platform | Generators | Toolsets |
|---|---|---|
| Windows | `vs2019`, `vs2022`, `vs2026`, `ninja`, `gmake`, `compilecommands` | `default`, `msc`, `gcc`, `clang` where compatible |
| Linux | `ninja`, `gmake`, `compilecommands` | `default`, `gcc`, `clang` |
| macOS | `xcode4`, `ninja`, `gmake`, `compilecommands` | `default`, `clang` |

| Configuration | Purpose | Windows MSVC | Linux/macOS |
|---|---|---:|---:|
| `Debug` | Symbols, trace logging | Yes | Yes |
| `Release` | Optimized, info logging, `NDEBUG` | Yes | Yes |
| `Dist` | Fully optimized distribution build | Yes | Yes |
| `DebugASan` | AddressSanitizer | Yes | Yes |
| `DebugUBSan` | UndefinedBehaviorSanitizer | No | Yes |
| `DebugTSan` | ThreadSanitizer | No | Yes |

Unsupported generator, toolset, architecture, and sanitizer combinations fail before compilation instead of silently producing an unsanitized binary.

## Repository Layout

```text
Core/                 Static library and public headers
Client/               Example application linking Core
Tests/                Doctest executable linking Core
Vendor/               Pinned Git submodules
Scripts/              Central and platform-specific automation
Scripts/Premake/      Shared Premake settings
Tools/<platform>/     Ignored, checksum-verified local tools
.github/workflows/    Required CI, compatibility, security, and artifacts
```

The root `premake5.lua` owns the workspace. Each project owns its own `premake5.lua`, so adding or removing a project does not turn the workspace file into a monolith.

## Logging

`Core::Log` provides asynchronous Core and Client loggers with a color console sink and separate rotating files. Logging is lazily initialized, but applications should call `Core::Log::Initialize()` before starting worker threads and `Core::Log::Shutdown()` after those threads stop.

The default directory is `Logs` relative to the process working directory. Generated IDE projects and repository scripts launch from the repository root for consistency. Applications may provide an absolute or application-specific path through `Core::LogConfig`.

Repeated initialization with the same configuration is safe. Reinitializing with a different configuration without shutting down first throws `std::logic_error`. Logger handles coordinate with shutdown so active log expressions finish before the asynchronous thread pool is destroyed.

## Dependencies And Tools

spdlog and doctest are Git submodules pinned by commit. Normal bootstrap restores and verifies those exact commits without fetching mutable tags or modifying the Git index.

To inspect a newer dependency explicitly:

```powershell
.\Scripts\project.ps1 vendor-update -Dependency spdlog -Tag v1.18.0
```

```sh
bash Scripts/project.sh vendor-update --dependency spdlog --tag v1.18.0
```

Review the dependency before updating the pinned commit values and staging the submodule pointer. Premake archives and the optional Homebrew installer are pinned and SHA-256 verified before execution.

## CI And Releases

Required CI runs on pushes to `master`, pull requests, and manual dispatch. It covers VS2022 on Windows x64, GCC on Linux x64, Clang on macOS Intel, ARM64 smoke builds, all supported sanitizer configurations, Client execution, formatting, shell analysis, PowerShell analysis, workflow validation, and clang-tidy.

The weekly/manual compatibility workflow exercises the additional advertised generators and toolsets. Security workflow jobs run CodeQL and dependency review. Version tags matching `v*` build, test, run, package, checksum, and upload release artifacts without creating a GitHub Release.

## Troubleshooting

- If a downloaded tool fails checksum validation, do not bypass the check. Confirm the pinned version and hash against the upstream release.
- If Visual Studio cannot be found, install the Desktop development with C++ workload for the requested major version and rerun bootstrap.
- If a generated build uses stale architecture/toolset settings, run `clean` and `generate`; scripts also regenerate automatically when their settings stamp changes.
- Windows UBSan and TSan are intentionally rejected because MSVC does not provide them.
- macOS LeakSanitizer is disabled because Apple's AddressSanitizer runtime does not support leak detection.

## Using This As A Template

Rename the workspace in the root Premake file and update `CrossPlatformCoreClientTemplate` references in scripts and workflows. Rename `Core`, `Client`, and their namespaces only if those names do not fit the new project. Replace the README introduction, repository URL, and MIT copyright holder before publishing a derived repository.

See [CONTRIBUTING.md](CONTRIBUTING.md) for the required quality checks and generated-file policy.
