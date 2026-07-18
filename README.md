# Kéire

[![CI](https://github.com/hcfgod/KeireEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/hcfgod/KeireEngine/actions/workflows/ci.yml)
[![Security](https://github.com/hcfgod/KeireEngine/actions/workflows/security.yml/badge.svg)](https://github.com/hcfgod/KeireEngine/actions/workflows/security.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE.txt)

A reproducible C++20 foundation with a static KeireCore library, a production application/layer runtime, typed immediate and queued events, Unity-style frame timing and UI, an SDL3 multi-window and SDL_GPU platform layer, strict typed JSON configuration, reusable strong/weak references, private asynchronous logging, Premake generation, sanitizers, SDK packaging, and Windows/Linux/macOS automation.

KeireCore is static by default. Its export annotations prepare for a possible same-toolchain C++ shared-library build; they are not a stable cross-compiler ABI. Generated build identity, development assertions, and a core-owned entrypoint keep startup policy consistent while the client supplies only its application factory.

## Quick Start

Clone submodules with the repository:

```sh
git clone --recurse-submodules https://github.com/hcfgod/KeireEngine.git
cd KeireEngine
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
| `run` | Build and run the interactive KeireClient window loop |
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
Config/                 Project identity, client JSON, and immutable dependency lock
KeireCore/              Static library and public Keire/<header> API
KeireClient/            Console application
KeireTests/             Independent doctest cases
Vendor/                 Pinned spdlog, doctest, SDL3, nlohmann/json, and Dear ImGui submodules
Scripts/Premake/        Shared Premake policy
Scripts/Unix/           Shared macOS/Linux implementation
Scripts/<platform>/     Platform bootstrap and wrappers
Tools/<platform>/       Ignored, checksum-verified local tools
docs/                   Architecture and focused subsystem guides
.github/workflows/      CI, compatibility, security, and packaging
```

The root Premake file owns the workspace. Each target owns its project definition. Public consumers include `Keire/Core.h` or `Keire/Log.h`.

## Documentation

The [documentation index](docs/README.md) links focused guides for getting started, architecture, runtime lifecycle,
the UI workspace, and testing/release workflows.

## Windowing And Configuration

`Keire/Window.h` exposes SDL-free `WindowSystem`, `Window`, opaque `WindowId`, logical/pixel extents, and a typed ordered event variant. One system is active per process and any number of windows may be created. SDL video initialization, window creation, mutation, polling, and shutdown are creating-thread-affine; releasing the final `Ref<Window>` from a worker is safe because native destruction is deferred to the owner thread. Shutdown destroys all native windows and makes surviving handles inert.

Logical sizes describe UI coordinates while pixel sizes describe the high-DPI drawable density. `DisplayScale()` bridges the two; pixel-size and scale changes arrive independently through events. The platform boundary deliberately creates no graphics context and translates no input; rendering and input remain separate concerns.

`LoadWindowSpecification` parses `Config/Client.json` without exposing nlohmann/json. The root `window` object accepts `title`, `width`, `height`, `resizable`, `highPixelDensity`, `visible`, `maximized`, and `mode` (`windowed` or `borderlessFullscreen`). Missing optional fields retain API defaults; unknown/duplicate keys, malformed UTF-8, wrong types, invalid dimensions, oversized titles/files, and incompatible fullscreen/maximized state are errors with file and JSON-pointer-style locations.

KeireClient accepts `--config <path>`, `--smoke-window`, and `--smoke-ui`. The default `Config/Client.json` is optional when implicit, while an explicitly named missing file is an error. `--smoke-window` is bounded, disables UI, and is used with `SDL_VIDEODRIVER=dummy` by CI and package validation. `--smoke-ui` renders a bounded set of frames and requires a graphics-capable environment.

## Application, Layers, Events, And Time

`Keire::Application` owns logging, a standalone `EventBus`, `Time`, `WindowSystem`, the primary window, and a dedicated `LayerStack`. KeireCore supplies `main`, handles dependency-free help/version commands, owns the top-level exception boundary and application lifetime, and calls `Run()`. A managed client supplies a static command-line description plus `CreateApplication(const ApplicationCommandLineArguments&)`; custom help remains client-owned without initializing engine services. The stack owns layer lifetimes, overlay partitioning, attachment, detachment, deferred structural changes, and traversal. Access it through `Application::Layers()`; the `PushLayer`, `PushOverlay`, and `RemoveLayer` application helpers remain as convenient delegates. The application construction thread owns `Run` and all layer mutations, while `RequestExit` remains safe from workers. Layers update bottom-to-top, receive events top-to-bottom, and may safely request structural changes during nested callbacks; those changes apply at the next frame boundary. Automatic layer subscriptions cannot be created during `OnDetach` and never survive detachment.

`Keire::UiFrame` is a first-party, frame-scoped immediate UI facade. Set `ApplicationSpecification::Ui.Mode` to `UiMode::Rendered` for SDL_GPU output or `UiMode::Headless` for deterministic tests and SDK validation. `Layer::OnUi` runs after variable update, bottom-to-top with overlays last. Window, menu, tab, tree, disabled, child, and ID scopes are move-only RAII values, so callback exceptions cannot leave the backend stack unbalanced. Calls outside `OnUi` or from another thread are rejected. Docking is enabled by default; detachable native viewports, images, renderer handles, and custom draw lists are intentionally unavailable.

Enable `ApplicationSpecification::Ui.Workspace` for the editor-grade layout system. `Application::GetUiWorkspace()` provides named layouts, immutable Default reset, registered panels, Kéire Dark/Light/Classic themes, editable semantic theme tokens, and portable `.keirelayout`/`.keiretheme` import and export. Live layout changes autosave atomically to SDL's per-user preference directory; `DirectoryOverride` supports deterministic tools and tests, while `Ephemeral` keeps smoke runs off disk. A backend-independent `BuildFactoryLayout` callback declares the default dock recipe using stable panel IDs. The legacy single-file `LayoutPath` remains available for simple clients, but it is mutually exclusive with the workspace.

The [UI Workspace Guide](docs/UiWorkspace.md) documents configuration, panel ownership, factory docking, layout and theme workflows, persistence, recovery, and troubleshooting.

```cpp
void EditorLayer::OnUi(Keire::UiFrame& ui)
{
    if (auto window = ui.BeginWindow("Inspector"); window)
    {
        ui.Text("Kéire owns the UI lifecycle and renderer.");
        (void)ui.Checkbox("Visible", m_Visible);
    }
}
```

```cpp
constexpr Keire::ApplicationCommandLineOption Options[]{
    {"--profile <name>", "Select a runtime profile."},
};

Keire::ApplicationCommandLineDescription
Keire::GetApplicationCommandLineDescription() noexcept
{
    return {"[--profile <name>]", Options};
}

std::unique_ptr<Keire::Application>
Keire::CreateApplication(const Keire::ApplicationCommandLineArguments& arguments)
{
    return std::make_unique<ClientApplication>(BuildSpecification(arguments));
}
```

Events are ordinary C++ value types. `Subscribe<T>` and `SubscribeAny` return move-only RAII tokens, while `Dispatch` delivers synchronously on the bus owner thread. Worker threads use bounded `TryEnqueue`; the application drains one stable snapshot per frame. Higher priorities run first, equal priorities retain registration order, and `EventFlow::Handled` stops propagation. An unhandled quit or primary-window close exits cleanly, so overlays can veto close requests when necessary.

```cpp
struct AssetReady { std::uint64_t Id; };

auto events = application.Events();
auto subscription = events->Subscribe<AssetReady>([](const AssetReady& event) {
    UseAsset(event.Id);
    return Keire::EventFlow::Continue;
});

events->Dispatch(AssetReady{42});  // application thread
events->TryEnqueue(AssetReady{43}); // any thread
```

`Keire::Time` provides `RawDeltaTime`, scaled and unscaled delta/time, smoothed delta, fixed time, frame/tick counts, interpolation alpha, pause, time scale, and dropped-backlog diagnostics. Fixed simulation defaults to 60 Hz, clamps a frame to 250 ms, and runs at most eight ticks per frame. The application-owned service is passed read-only to layer hooks; code that intentionally changes pause or scale uses `Application::GetTime()` on the application thread.

## Logging

`Keire::Log` owns a private spdlog thread pool and never changes spdlog's global registry or default logger. KeireCore and KeireClient use separate asynchronous rotating-file loggers. `LogConfig::EnableConsole` controls the color console sink and defaults to true. The default files are `Logs/Core.log` and `Logs/Client.log`.

Logs default to `Logs` relative to the process working directory. IDE debug directories and scripts use the repository root. Initialization is idempotent only for identical configuration; conflicting configuration throws. `LoggerHandle` is a copyable value backed by `Ref`; each call takes a short operation lock. Shutdown may wait for an active write, but never for a handle's lifetime, and old handles safely become inert. Disabled macro levels evaluate neither logger acquisition nor message arguments.

## References

`Keire/Ref.h` provides thread-safe `Ref<T>` strong ownership, `WeakRef<T>` observation, and the factory-only `CreateRef<T>(...)` API for types derived from `RefCounted`. The external control block keeps weak locking safe while the last strong owner is released, supports polymorphic and incomplete object types, and exposes `UseCount()` for inspection. Reference-counted graphs must use at least one `WeakRef` in every cycle.

## Build Identity And Assertions

`Keire::GetBuildInfo()` reports the project version, Git commit and tracked/untracked dirty state, configuration, compiler, platform, and architecture compiled into the binary. Identity is regenerated immediately before KeireCore compiles and only rewrites its header when values change. `KeireClient --version` prints that identity; `KeireClient --help` documents the intentionally small command line. Informational commands do not initialize logging.

`KEIRE_ASSERT(condition)` and `KEIRE_ASSERT(condition, "message")` diagnose and abort in Debug and sanitizer configurations. They compile out without evaluating arguments in Release, Dist, and Coverage. Logging uses `KEIRE_CORE_*` and `KEIRE_CLIENT_*` for every severity.

## Dependencies

`Config/Dependencies.lock` is the source of truth for tool URLs, archive hashes, installer pins, and submodule commits. Normal bootstrap verifies immutable state and never stages files or advances dependency pointers. SDL 3.4.10 is built as cached Debug and Release static archives by a dependency-only CMake step; Kéire itself remains Premake-driven. Debug, sanitizer, and Coverage configurations select Debug SDL, while Release and Dist select Release SDL. nlohmann/json 3.12.0 remains a private header-only implementation dependency.

Dear ImGui 1.92.8 is pinned to the released `v1.92.8-docking` tag and compiled privately into KeireCore with its SDL3 platform, SDL_GPU renderer, and standard-string adapters. Kéire owns context, event forwarding, frame, docking, layout, GPU, swapchain, and shutdown lifecycles; clients use only `Keire::UiFrame`. SDL is built with GPU support and without SDL_Renderer. Dear ImGui types and headers never cross the public API, and its headers and sources are not redistributed. Multi-viewports remain disabled until Kéire has explicit multi-window renderer ownership.

KeireClient demonstrates the workspace as a Unity-style editor shell with Scene, Game, Hierarchy, Inspector, Project, Console, Diagnostics, and Theme Editor panels. These panels are deliberate polished empty states around the UI foundation; this milestone does not introduce scene, renderer, asset, or serialization systems.

Intentional updates are explicit:

```powershell
./Scripts/project.ps1 vendor-update -Dependency spdlog -Tag v1.18.0
./Scripts/project.ps1 vendor-update -Dependency imgui -Tag v1.92.8-docking
```

```sh
bash Scripts/project.sh vendor-update --dependency spdlog --tag v1.18.0
bash Scripts/project.sh vendor-update --dependency imgui --tag v1.92.8-docking
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

Required CI covers Windows/VS2022, Linux/GCC, macOS/Clang, x64 and ARM64 smoke builds, sanitizers, KeireClient execution, script regression tests, 80% LLVM line coverage, formatting, clang-tidy, ShellCheck, PSScriptAnalyzer, actionlint, yamllint, and Python validation. Extended Compatibility runs weekly and manually. Dependabot checks Actions and submodules weekly.

CodeQL and Dependency Review are an explicit repository opt-in. Enable Dependency Graph and code scanning in GitHub, create the repository variable `ENABLE_ADVANCED_SECURITY=true`, and require `Security activation status` plus the resulting checks in the `master` branch protection rules. Once enabled, the status job fails if any eligible security job is skipped or unsuccessful; CodeQL findings are governed by the repository's code-scanning rules. Privileged CodeQL uploads are skipped for pull requests from forks.

Version tags and manual release workflow runs create SDK archives without publishing a GitHub Release. Archives contain KeireClient, KeireCore, public headers including `Keire/Ui.h`, spdlog headers, SDL's static archive/headers/official CMake configuration, complete licenses, two consumers, and a validated JSON build manifest containing SDL, JSON, and Dear ImGui commits. Every package extracts and validates both a low-level consumer with its own `main` and a managed headless-UI consumer whose `main` comes from KeireCore, using direct compiler invocation and `find_package(Keire CONFIG REQUIRED)`. `Keire::Core` transitively links `SDL3::SDL3-static`. nlohmann/json and Dear ImGui headers are intentionally absent because neither dependency crosses the public API. SHA-256 files and separate symbol archives are included where available; Dist packages are stripped.

## Troubleshooting

- Run `doctor` first when a compiler, SDK, or package manager is not found.
- Use `--update` or `-Update` only when installed system tools should be upgraded.
- Run forced generation after changing a compiler installation or toolchain: `--force` or `-Force`.
- Do not bypass a checksum failure. Verify the lock entry against the upstream release.
- Windows MSVC builds require the Desktop development with C++ workload for the requested Visual Studio major version.
- Apple's ASan runtime does not support leak detection; the macOS scripts disable that check.

See [Architecture](docs/Architecture.md), [Contributing](CONTRIBUTING.md), [Security](SECURITY.md), and [Changelog](CHANGELOG.md).
