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

## Platform Support Status

| Platform | Current evidence | Public preview |
| --- | --- | --- |
| Windows x86-64 | Debug, Release, AddressSanitizer, complete Core/editor/Hub suites, SDK/package consumers, Direct3D 12, and Vulkan | Unsigned native Hub installer |
| Linux x86-64 (glibc) | Ubuntu 22.04/24.04, Debian 12, Fedora, Arch, openSUSE Tumbleweed, and Rocky Linux 9; Ubuntu 26.04 setup/matrix target pending native validation; GCC warnings-as-errors, Debug/Release, sanitizers, packages, and WSLg Vulkan | Unsigned Hub DEB for Debian/Ubuntu; source builds on the other validated families |
| Linux ARM64 or Alpine/musl | Build contracts retained but not validated in this audit | Not published |
| macOS x86-64/ARM64 | Build and packaging contracts retained; native macOS and Metal evidence remains outstanding | Not published |

The [Downloads page](https://keireengine.duckdns.org/downloads/) shows the exact Hub and editor version, architecture,
signing state, byte size, publication date, and SHA-256 for every preview. Its
[previous-versions archive](https://keireengine.duckdns.org/downloads/previous/) retains immutable earlier uploads.
Unsigned development previews are not stable releases and remain outside the signed catalog trust path.

## First Windows Build

Open PowerShell at the repository root:

```powershell
./Scripts/project.ps1 bootstrap -Generator ninja -Toolset msc
./Scripts/project.ps1 generate -Generator ninja -Toolset msc
./Scripts/project.ps1 test -Generator ninja -Configuration Debug -Toolset msc
./Scripts/project.ps1 run -Generator ninja -Configuration Debug -Toolset msc
```

Visual Studio users may select `vs2022` instead. Generated Visual Studio projects stage each executable's native
runtime dependencies after linking, and the editor project also stages its complete managed host, so **Start Debugging**
works after a clean bootstrap and build. Ninja is useful for fast local verification and is the most direct
cross-platform workflow.

## First Linux Build

Kéire's clean Linux path is validated on Ubuntu 22.04/24.04, Debian 12, current Fedora and Arch, openSUSE
Tumbleweed, and Rocky Linux 9. Ubuntu 26.04 is included as a setup and container-matrix target, but it remains a new
native compatibility observation until the full test and graphics gates pass on a real host. The bootstrap recognizes
`apt`, `dnf`, `pacman`, and `zypper`; installs the required X11/Wayland, compiler, CMake, .NET, shader, and media
prerequisites; and keeps pinned fallback tools beneath
`Tools/Linux` or the user's Kéire toolchain cache. It does not replace the distribution's system compiler or .NET
installation.

### Fresh-distribution preparation

Bring a fresh operating-system installation current using that distribution's supported update workflow and reboot
when its kernel or core runtime changes. Kéire does not automate system upgrades or reboots. Install only Git and the
system CA bundle before cloning; the repository setup installs the remaining build prerequisites:

| Distribution family | Initial command |
| --- | --- |
| Ubuntu or Debian (`apt`) | `sudo apt-get update && sudo apt-get install -y git ca-certificates` |
| Fedora or Rocky Linux (`dnf`) | `sudo dnf install -y git ca-certificates` |
| Arch Linux (`pacman`) | `sudo pacman -Syu --needed git ca-certificates` |
| openSUSE Tumbleweed (`zypper`) | `sudo zypper --non-interactive install git ca-certificates` |

VM guest additions, shared folders, GPU passthrough, DNS overrides, and firewall policy belong to the VM or host
administrator. The project setup never changes them. Use a native Linux filesystem for the checkout, including inside
a VM; shared folders are appropriate for transferring final artifacts but not for dependency-heavy builds.

Clone, set up, and run the complete Debug gate:

```sh
mkdir -p ~/src
cd ~/src
git clone --recurse-submodules https://github.com/hcfgod/KeireEngine.git
cd KeireEngine
bash Scripts/setup-linux.sh --generator ninja --toolset gcc --test
```

`Scripts/setup-linux.sh` reports the detected distribution and package-manager family, invokes the authoritative
`project.sh bootstrap`, runs `project.sh doctor`, and optionally runs one complete test configuration. It is safe to
rerun. Use `--jobs <count>` only when the machine can support more or fewer than the default maximum of four compiler
workers. `--configuration Release --test` selects the Release gate; `--update` refreshes installed prerequisites; and
`--force` intentionally restores project-private pinned tools.

The equivalent explicit launcher sequence remains supported:

```sh
bash Scripts/project.sh bootstrap --generator ninja --toolset gcc
bash Scripts/project.sh generate --generator ninja --toolset gcc
bash Scripts/project.sh test --generator ninja --configuration Debug --toolset gcc
bash Scripts/project.sh run --generator ninja --configuration Debug --toolset gcc
```

Use `--toolset clang` when Clang is the intended compiler.

For WSL2, clone into the WSL ext4 filesystem rather than a Windows-mounted path:

```sh
mkdir -p ~/src
cd ~/src
git clone --recurse-submodules https://github.com/hcfgod/KeireEngine.git
cd KeireEngine
bash Scripts/project.sh test --generator ninja --configuration Debug --toolset gcc
```

The first clean build can be lengthy because it compiles locked native dependencies and the shader compiler. Builds
use at most four parallel jobs by default to avoid memory exhaustion. Override that only when appropriate, for example
`KEIRE_BUILD_JOBS=8 bash Scripts/project.sh test --generator ninja --configuration Debug --toolset gcc`.

### Linux Hub and editor validation

After the Debug test passes, run the optimized and sanitizer gates independently so a failure identifies its exact
configuration:

```sh
bash Scripts/setup-linux.sh --generator ninja --toolset gcc --test --configuration Release
bash Scripts/setup-linux.sh --generator ninja --toolset gcc --test --configuration DebugASan
bash Scripts/Tests/test-unix.sh
```

Run these graphical checks from an interactive desktop session:

```sh
bash Scripts/project.sh run --generator ninja --configuration Debug --toolset gcc --smoke-ui
bash Scripts/project.sh run --generator ninja --configuration Debug --toolset gcc --smoke-project
```

Normal `run` opens the Hub. Direct editor launch requires a project:

```sh
bash Scripts/project.sh run --generator ninja --configuration Debug --toolset gcc
bash Scripts/project.sh run --generator ninja --configuration Debug --toolset gcc \
  --editor --project "$PWD/Samples/KeireSandbox"
```

### Linux Hub and editor packages

Packaging requires a clean checkout and always builds the `Dist` configuration. The distribution commands create
portable archives, while the installer commands create Debian packages under `Artifacts/`:

```sh
git status --short
bash Scripts/project.sh package-hub --generator ninja --toolset gcc
bash Scripts/project.sh package-editor --generator ninja --toolset gcc
bash Scripts/project.sh package-hub-installer --generator ninja --toolset gcc
bash Scripts/project.sh package-installer --generator ninja --toolset gcc
ls -lh Artifacts/
```

Verify the Hub installer from inside the artifact directory because its checksum file records the immutable basename:

```sh
cd Artifacts
project_version="$(sed -n 's/^PROJECT_VERSION=//p' ../Config/Project.conf)"
deb_architecture="$(dpkg --print-architecture)"
hub_installer="keire-hub_${project_version}_${deb_architecture}.deb"
sha256sum --check "${hub_installer}.sha256"
dpkg-deb --info "$hub_installer"
dpkg-deb --contents "$hub_installer"
cd ..
```

A source build on a current Fedora, Arch, openSUSE, or new Ubuntu host validates that host; it does not make its linked
binaries universal. Build a public Debian/Ubuntu preview on the declared Debian/Ubuntu release baseline, and use the
oldest claimed Ubuntu baseline when the same artifact is intended to run on every newer supported Ubuntu version.
Never relabel a binary built against a newer glibc as an older-baseline or distribution-independent artifact.

## First macOS Build

```sh
bash Scripts/project.sh bootstrap --generator ninja --toolset clang
bash Scripts/project.sh generate --generator ninja --toolset clang
bash Scripts/project.sh test --generator ninja --configuration Debug --toolset clang
bash Scripts/project.sh run --generator ninja --configuration Debug --toolset clang
```

Xcode generation is also supported, but command-line validation should still use a repository launcher.

## Daily Workflow

Build/test automatically regenerate after source files are added/removed, Premake/config inputs change, generator or
architecture changes, or generated outputs disappear. The generation fingerprint prevents the stale-project linker
failure that otherwise follows a new translation unit. Ordinary C++ content edits only rebuild affected targets:

Keep new headers in the owning project’s `Include/` tree and new implementation files in `Source/`. Directory names
are case-sensitive repository contracts even on Windows: use `Docs`, `Include`, and `Source`, never `docs` or `src`
for first-party source trees. `python Scripts/Tests/check-repository-layout.py` checks the complete layout quickly.

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

Normal `run` opens KeireHub. To exercise a real project lifecycle or bypass the Hub deliberately:

```powershell
./Scripts/project.ps1 run -SmokeProject
./Scripts/project.ps1 run -Editor -ProjectPath C:\Projects\MyGame
```

```sh
bash Scripts/project.sh run --smoke-project
bash Scripts/project.sh run --editor --project /projects/MyGame
```

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

`Config/Client.json` configures the editor window. An implicit missing default file is allowed; an explicitly requested
missing `--config` path is an error. Interactive KeireClient startup also requires `--project` naming a directory with
`ProjectSettings/Project.keireproject`; use KeireHub for the normal create/open workflow.

Project identity belongs in `Config/Project.conf`. Use the repository rename command for an intentional template-wide
rename rather than editing duplicated names manually.

## Cleaning Disposable Outputs

Generated build products are ignored and must not be committed:

```powershell
./Scripts/project.ps1 clean
./Scripts/project.ps1 clean -CleanScope build
./Scripts/project.ps1 clean -CleanScope generated
```

```sh
bash Scripts/project.sh clean
bash Scripts/project.sh clean --clean-scope build
bash Scripts/project.sh clean --clean-scope generated
```

The default `full` scope removes the complete `Build` directory, package artifacts, and generated project files.
`build` removes all build outputs and package artifacts while retaining the dependency cache and generated project
state. `generated` removes generated project files and build identity. Do not manually delete vendor submodules or
dependency inputs.

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

The generated workspace contains the native product, worker, tool, test, managed-build, and private dependency targets
declared by `premake5.lua`. `DearImGui` appears under the `Dependencies` solution folder and writes its generated
metadata below `Build/Projects/DearImGui`; its reviewed Premake definition remains at
`Scripts/Premake/DearImGui.lua`. If that project or its sources are missing after a vendor restore, rerun bootstrap and
regenerate rather than adding vendor files to KeireCore manually.

### The UI smoke cannot create a renderer

Run it from an interactive graphics-capable desktop. Use the headless doctest UI mode for deterministic automated
coverage rather than weakening rendered startup checks.

### A command behaves differently from an IDE build button

Use the launcher result as authoritative. It refreshes build identity and selects the locked dependency variant before
compilation; an IDE project may be stale until regenerated.
