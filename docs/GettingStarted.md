# Getting Started

This guide takes a fresh checkout to a verified local Kéire build. Repository launchers are the supported interface;
they resolve tools, verify locked dependencies, generate build files, and select compatible compiler settings.

## Clone

Clone all pinned submodules with the repository:

```sh
git clone --recurse-submodules https://github.com/hcfgod/KeireEngine.git
cd KeireEngine
```

If the repository was cloned without submodules, run:

```sh
git submodule update --init --recursive
```

Normal bootstrap restores and verifies the commits in `Config/Dependencies.lock`. It does not advance dependency
pointers or stage Git changes.

## First Windows Build

Open PowerShell at the repository root:

```powershell
./Scripts/project.ps1 bootstrap -Generator ninja -Toolset msc
./Scripts/project.ps1 generate -Generator ninja -Toolset msc
./Scripts/project.ps1 test -Generator ninja -Configuration Debug -Toolset msc
./Scripts/project.ps1 run -Generator ninja -Configuration Debug -Toolset msc
```

Visual Studio users may select `vs2022` instead. Ninja is useful for fast local verification and is the most direct
cross-platform workflow.

## First Linux Build

```sh
bash Scripts/project.sh bootstrap --generator ninja --toolset gcc
bash Scripts/project.sh generate --generator ninja --toolset gcc
bash Scripts/project.sh test --generator ninja --configuration Debug --toolset gcc
bash Scripts/project.sh run --generator ninja --configuration Debug --toolset gcc
```

Use `--toolset clang` when Clang is the intended compiler.

## First macOS Build

```sh
bash Scripts/project.sh bootstrap --generator ninja --toolset clang
bash Scripts/project.sh generate --generator ninja --toolset clang
bash Scripts/project.sh test --generator ninja --configuration Debug --toolset clang
bash Scripts/project.sh run --generator ninja --configuration Debug --toolset clang
```

Xcode generation is also supported, but command-line validation should still use a repository launcher.

## Daily Workflow

Generation is required after adding source files, changing Premake policy, switching generator or architecture, or
removing generated outputs. Ordinary C++ edits only require build or test:

```powershell
./Scripts/project.ps1 build -Generator ninja -Configuration Debug -Toolset msc
./Scripts/project.ps1 test -Generator ninja -Configuration Debug -Toolset msc
```

```sh
bash Scripts/project.sh build --generator ninja --configuration Debug --toolset clang
bash Scripts/project.sh test --generator ninja --configuration Debug --toolset clang
```

Run the bounded rendered UI smoke when changing the editor or UI renderer:

```powershell
./Scripts/project.ps1 run -Generator ninja -Configuration Debug -Toolset msc -SmokeUi
```

```sh
bash Scripts/project.sh run --generator ninja --configuration Debug --toolset clang --smoke-ui
```

The smoke requires a graphics-capable session. Headless CI and package validation use the window-only smoke where
appropriate.

## Configurations

| Configuration | Intended use |
| --- | --- |
| `Debug` | Normal development, assertions, symbols, and debug dependency variants |
| `Release` | Optimized validation with `NDEBUG` and symbols |
| `Dist` | Fully optimized, link-time-optimized, stripped distribution behavior |
| `DebugASan` | Address and lifetime error detection |
| `DebugUBSan` | Undefined-behavior detection with supported GCC/Clang toolchains |
| `DebugTSan` | Data-race detection with supported GCC/Clang toolchains |
| `Coverage` | Clang instrumentation and the repository coverage threshold |

Release and Dist disable assertions without evaluating assertion arguments. Sanitizer support depends on the selected
platform and compiler; unsupported combinations are rejected before generation.

## Project Configuration

`Config/Client.json` configures the sample client window. An implicit missing default file is allowed; an explicitly
requested missing `--config` path is an error. Configuration parsing is strict and rejects unknown or duplicate fields.

Project identity belongs in `Config/Project.conf`. Use the repository rename command for an intentional template-wide
rename rather than editing duplicated names manually.

## Cleaning Disposable Outputs

Generated build products are ignored and must not be committed:

```powershell
./Scripts/project.ps1 clean -CleanScope build
./Scripts/project.ps1 clean -CleanScope generated
```

```sh
bash Scripts/project.sh clean --clean-scope build
bash Scripts/project.sh clean --clean-scope generated
```

`build` removes compiled outputs, coverage, and package artifacts. `generated` removes generated project files and build
identity. `full` removes both groups. Do not manually delete vendor submodules or dependency inputs.

## Diagnose A Workstation

```powershell
./Scripts/project.ps1 doctor -Generator ninja -Toolset msc
```

```sh
bash Scripts/project.sh doctor --generator ninja --toolset clang
```

The doctor reports concrete tool resolution, compiler selection, architecture, and dependency state. If generation
reports a stamp mismatch, regenerate with the intended generator, architecture, and toolset; use `-Force` or `--force`
only when deliberately replacing the previous generated configuration.

## Common Problems

### A dependency submodule is missing or at the wrong commit

Run bootstrap again. If local vendor edits exist, preserve or discard them deliberately before bootstrap; never patch
or format vendored sources as part of unrelated engine work.

### A build cannot find a newly added source file

Regenerate the selected build system. Premake source globs are evaluated during generation, not during compilation.

### The UI smoke cannot create a renderer

Run it from an interactive graphics-capable desktop. Use the headless doctest UI mode for deterministic automated
coverage rather than weakening rendered startup checks.

### A command behaves differently from an IDE build button

Use the launcher result as authoritative. It refreshes build identity and selects the locked dependency variant before
compilation; an IDE project may be stale until regenerated.
