# Kéire

[![CI](https://github.com/hcfgod/KeireEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/hcfgod/KeireEngine/actions/workflows/ci.yml)
[![Security](https://github.com/hcfgod/KeireEngine/actions/workflows/security.yml/badge.svg)](https://github.com/hcfgod/KeireEngine/actions/workflows/security.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE.txt)

A reproducible C++20 foundation with a static KeireCore library, a project-first editor Hub, typed scene and input assets,
asynchronous reference-counted loading, deterministic cooked packs, a production application/layer runtime, typed events,
Unity-style frame timing, application-owned transactional undo/redo, an SDL3 multi-window and SDL_GPU platform layer,
private Dear ImGui UI, reusable strong/weak
references, private asynchronous logging, Premake generation, sanitizers, SDK packaging, and Windows/Linux/macOS
automation.

The editor now renders resizable Scene and Game views through an application-owned SDL_GPU pipeline. The starter scene
contains a primary Camera, a tintable cube, and a Directional Light; Scene view adds a depth-tested grid plus Unity-style framing, locking,
orbit, camera-local pan, dolly, fly, orthographic, and axis-snap navigation. The Project panel provides extension-free
List/Grid views, contextual creation, multi-item file operations, delayed metadata cards, and persistent recoverable trash.
Pinned host-side shader tooling imports HLSL into DXIL, SPIR-V, and MSL while runtime/public APIs remain backend-free.

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
| `run` | Build and run the Project Hub; optional flags open a project editor directly |
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
KeireHub/               Project discovery, creation, and editor launcher
AssetTool/              Source scan, import, cook, and package validation CLI
KeireTests/             Independent doctest cases
Samples/KeireSandbox/   Validated starter project packaged with the SDK
Vendor/                 Pinned runtime, test, UI, compression, ECS, and math submodules
Scripts/Premake/        Shared policy and tracked private dependency project definitions
Scripts/Unix/           Shared macOS/Linux implementation
Scripts/<platform>/     Platform bootstrap and wrappers
Tools/<platform>/       Ignored, checksum-verified local tools
docs/                   Architecture and focused subsystem guides
.github/workflows/      CI, compatibility, security, and packaging
```

The root Premake file owns first-party targets plus dependency projects. `KeireCore`, `KeireClient`, `KeireHub`,
`KeireAssetTool`, and `KeireTests` own first-party definitions; private `DearImGui` and `Zstd` static libraries plus
header-only EnTT and GLM utility projects are grouped under `Dependencies`.
Generated dependency metadata remains below ignored `Build/Projects`. Public consumers include `Keire/Core.h` and link
only the supported `Keire::Core` package target.

## Documentation

The [documentation index](docs/README.md) links focused guides for projects, Project Settings, the Hub, scene runtime/authoring, rendering,
shaders/materials, undo/redo, input debugging, architecture, runtime lifecycle, the UI workspace, and testing/release
workflows.

The Scene view uses the selected runtime Camera's clear color while retaining its independent editor viewpoint. Its
`Q/W/E/R` View/Move/Rotate/Scale tools support Local/Global handles, configurable position/rotation/scale snapping, and
camera/light gizmos. Project ambient color, intensity, and exposure are edited through **Edit > Project Settings...**
and light both Scene and Game views together.

## Windowing And Configuration

`Keire/Window.h` exposes SDL-free `WindowSystem`, `Window`, opaque `WindowId`, logical/pixel extents, and a typed ordered event variant. One system is active per process and any number of windows may be created. SDL video initialization, window creation, mutation, polling, and shutdown are creating-thread-affine; releasing the final `Ref<Window>` from a worker is safe because native destruction is deferred to the owner thread. Shutdown destroys all native windows and makes surviving handles inert.

Logical sizes describe UI coordinates while pixel sizes describe the high-DPI drawable density. `DisplayScale()` bridges the two; pixel-size and scale changes arrive independently through events. The platform boundary deliberately creates no graphics context and translates no input; rendering and input remain separate concerns.

`LoadWindowSpecification` parses `Config/Client.json` without exposing nlohmann/json. The root `window` object accepts `title`, `width`, `height`, `resizable`, `highPixelDensity`, `visible`, `maximized`, and `mode` (`windowed` or `borderlessFullscreen`). Missing optional fields retain API defaults; unknown/duplicate keys, malformed UTF-8, wrong types, invalid dimensions, oversized titles/files, and incompatible fullscreen/maximized state are errors with file and JSON-pointer-style locations.

KeireClient accepts `--project <path>`, `--config <path>`, `--smoke-window`, `--smoke-ui`, and `--smoke-project`.
Interactive editor startup requires a validated project; the normal launcher opens KeireHub first. The default
`Config/Client.json` is optional when implicit, while an explicitly named missing file is an error. Window, Hub UI, and
full project editor smoke modes are bounded; the latter exercises project locks, assets, scenes, input, workspace, and
clean shutdown.

## Application, Layers, Events, And Time

`Keire::Application` owns logging, a standalone `EventBus`, `Time`, `WindowSystem`, the primary window, and a dedicated `LayerStack`. KeireCore supplies `main`, handles dependency-free help/version commands, owns the top-level exception boundary and application lifetime, and calls `Run()`. A managed client supplies a static command-line description plus `CreateApplication(const ApplicationCommandLineArguments&)`; custom help remains client-owned without initializing engine services. The stack owns layer lifetimes, overlay partitioning, attachment, detachment, deferred structural changes, and traversal. Access it through `Application::Layers()`; the `PushLayer`, `PushOverlay`, and `RemoveLayer` application helpers remain as convenient delegates. The application construction thread owns `Run` and all layer mutations, while `RequestExit` remains safe from workers. Layers update bottom-to-top, receive events top-to-bottom, and may safely request structural changes during nested callbacks; those changes apply at the next frame boundary. Automatic layer subscriptions cannot be created during `OnDetach` and never survive detachment.

`Keire::UiFrame` is a first-party, frame-scoped immediate UI facade. Set `ApplicationSpecification::Ui.Mode` to `UiMode::Rendered` for SDL_GPU output or `UiMode::Headless` for deterministic tests and SDK validation. `Layer::OnUi` runs after variable update, bottom-to-top with overlays last. Window, menu, tab, tree, disabled, child, and ID scopes are move-only RAII values, so callback exceptions cannot leave the backend stack unbalanced. Calls outside `OnUi` or from another thread are rejected. Docking is enabled by default; opaque ref-counted RGBA images are supported, while detachable native viewports, renderer handles, raw textures, and custom draw lists remain intentionally unavailable.

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

`Keire::Log` owns a private asynchronous backend and never changes its backend's global registry or default logger.
KeireCore and KeireClient use separate rotating-file loggers. Public code formats through Kéire-owned `{}` placeholders,
including integer hex/width and floating precision specifications; spdlog and fmt remain implementation details.
`LogConfig::EnableConsole` controls the color console sink and defaults to true. The default files are `Logs/Core.log`
and `Logs/Client.log`.

Logs default to `Logs` relative to the process working directory. IDE debug directories and scripts use the repository root. Initialization is idempotent only for identical configuration; conflicting configuration throws. `LoggerHandle` is a copyable value backed by `Ref`; each call takes a short operation lock. Shutdown may wait for an active write, but never for a handle's lifetime, and old handles safely become inert. Disabled macro levels evaluate neither logger acquisition nor message arguments.

Editor Console entries are mirrored to the Client terminal/file sinks. Asset import and runtime-load failures are also
written to the Core terminal/file sinks with stable asset ID, importer/type, source path, line/column when available,
and the original diagnostic. Selecting a failed shader shows the same compiler diagnostics in Inspector; successful
hot-reload data remains active until a corrected import completes.

## References

`Keire/Ref.h` provides thread-safe `Ref<T>` strong ownership, `WeakRef<T>` observation, and the factory-only `CreateRef<T>(...)` API for types derived from `RefCounted`. The external control block keeps weak locking safe while the last strong owner is released, supports polymorphic and incomplete object types, and exposes `UseCount()` for inspection. Reference-counted graphs must use at least one `WeakRef` in every cycle.

## Build Identity And Assertions

`Keire::GetBuildInfo()` reports the project version, Git commit and tracked/untracked dirty state, configuration, compiler, platform, and architecture compiled into the binary. Identity is regenerated immediately before KeireCore compiles and only rewrites its header when values change. `KeireClient --version` prints that identity; `KeireClient --help` documents the intentionally small command line. Informational commands do not initialize logging.

`KEIRE_ASSERT(condition)` and `KEIRE_ASSERT(condition, "message")` diagnose and abort in Debug and sanitizer configurations. They compile out without evaluating arguments in Release, Dist, and Coverage. Logging uses `KEIRE_CORE_*` and `KEIRE_CLIENT_*` for every severity.

`KEIRE_API` marks KeireCore-owned public symbols and cross-boundary exception types for same-toolchain shared-library preparation. Header-only values and templates remain unannotated, while the managed-client factory hooks remain executable-defined reverse imports. The SDK is still distributed as a static library and does not promise a compiler-independent C++ ABI.

## Asset Workflow

Set `ApplicationSpecification::Assets.Mode` to `Development` for editor catalogs or `Cooked` with explicit mounts for a
distribution runtime. `AssetHandle<T>::Get()` immediately returns a typed fallback, workers load and validate content,
and frame-boundary completion swaps in immutable data. Initial failure is explicit; failed reload keeps the last
known-good revision. Built-in `BinaryAsset` and UTF-8 `TextAsset` types establish the lifecycle without prematurely
introducing renderer, audio, model, or scene ownership.

The editor discovers the opened project's `Assets/`, creates stable `.keiremeta` sidecars, caches immutable imports under
`Library/AssetCache`, and exposes Project/Inspector browsing plus import and Dist cook actions. The same workflow is
available without the editor:

```powershell
./Build/Bin/Debug-windows-x86_64/KeireAssetTool/KeireAssetTool.exe import --project Samples/KeireSandbox
./Build/Bin/Debug-windows-x86_64/KeireAssetTool/KeireAssetTool.exe cook --project Samples/KeireSandbox --output Build/Assets --profile Dist
```

See [Asset Runtime](docs/AssetRuntime.md) and [Asset Pipeline](docs/AssetPipeline.md) for threading, mount, metadata,
integrity, file-operation, cook, and packaging contracts.

Asset APIs are organized beneath `Keire/Assets` (for example, `#include "Keire/Assets/AssetSystem.h"`). The umbrella
`Keire/Core.h` continues to include the supported asset surface for consumers that prefer the aggregate header.

Static meshes can be imported from OBJ, FBX, glTF, or GLB and converted explicitly with `KeireAssetTool convert-mesh
--input <model>`. PNG, JPEG, TGA, and BMP textures import as validated RGBA8 assets with deterministic mip generation
and sampler settings stored in source metadata. Assimp and stb remain private implementation dependencies; their
headers are not required by engine or SDK consumers.

## Projects And Scenes

`ProjectSettings/Project.keireproject` is the fixed marker for an isolated Kéire project. `Project::Create` produces
transactional Empty or Starter roots; `Project::Open` validates schema/version and the editor holds an OS-exclusive lock
for its lifetime. Assets, import caches, input profiles, workspace state, recovery, logs, and cooked output are rebased
under that root. The Project Hub manages a per-user recent registry without deleting projects and launches each editor as
an independent process.

`.keirescene` is the first scene asset. `SceneSystem` loads it asynchronously and commits single/additive activation only
at application frame boundaries; failures preserve the last-good scene set. Mutable `Scene` instances support stable
weak object handles, hierarchy edits, transforms, subtree duplication/deletion, cycle-safe reparenting, and dirty state.
The editor adds atomic Save, bounded undo/redo, Save/Discard/Cancel transitions, and project-local crash recovery.

Schema v2 scenes use stable `Entity` handles and application-registered, reference-counted Components. Transform is
mandatory; Directional Light supplies the built-in Lambert path while its shadow fields remain future-facing. Play clones authored state, Pause freezes component
updates, Step advances one fixed tick, and Stop discards runtime changes. EnTT and GLM implement ECS/math privately;
SDK code sees only Kéire IDs, components, queries, vectors, quaternions, matrices, and colors.

See [Project System](docs/ProjectSystem.md), [Project Hub](docs/ProjectHub.md), [Scene System](docs/SceneSystem.md), and
[Scene Authoring](docs/SceneAuthoring.md), plus [ECS And Components](docs/ECSAndComponents.md).

## Input Actions

Input is disabled by default. Enable `ApplicationSpecification::Assets` and set
`ApplicationSpecification::Input.Mode = InputMode::Enabled`; Kéire rejects Input without Assets. The application then
publishes `Application::Input()` after window events and asset completions once per outer frame. Action contexts expose
stable map/action lookup, polling, RAII phase callbacks, users and exclusive/shared pairing, control schemes, hot
reload, interactive rebinding, and atomic profile overrides without exposing SDL.

`Samples/KeireSandbox/Assets/Input/DefaultInput.keireinput` provides Player Move/Look/Fire and UI
Navigate/Submit/Cancel/Point/Click/Scroll maps
for keyboard/mouse and gamepad. KeireClient enables Development Assets and Input for its editor. Create or select a
`.keireinput` asset, then open the dockable Input Actions panel from Inspector, Project double-click/context menu, or
Window. The editor provides four templates, searchable master-detail authoring, bounded undo/redo, Save/Revert,
validation, conflict-aware Listen rebinding, and a live monitor entirely through `Keire::UiFrame`.

The Input Debugger can enable the project maps with a scoped UI-capture override and records filtered phase/value,
user/device, scheme, duration, and timestamp data in its bounded local history. Idle values and synthetic resets are
discarded; optional Console forwarding is off by default. See [Input System](docs/InputSystem.md),
[Input Actions Editor](docs/InputActionsEditor.md), and [Input Debugger](docs/InputDebugger.md).

## Dependencies

`Config/Dependencies.lock` is the source of truth for tool URLs, archive hashes, installer pins, and submodule commits. Normal bootstrap verifies immutable state and never stages files or advances dependency pointers. SDL 3.4.10 is built as cached Debug and Release static archives by a dependency-only CMake step; Kéire itself remains Premake-driven. Debug, sanitizer, and Coverage configurations select Debug SDL, while Release and Dist select Release SDL. nlohmann/json 3.12.0 remains a private header-only implementation dependency.

Dear ImGui 1.92.8 is pinned to the released `v1.92.8-docking` tag. Its core, demo, SDL3 platform, SDL_GPU renderer,
and standard-string adapter translation units build in the private `DearImGui` project as `KeireImGui.lib` on Windows
or `libKeireImGui.a` on Unix. Kéire owns context, event forwarding, frame, docking, layout, GPU, swapchain, and shutdown
lifecycles; clients use only `Keire::UiFrame`. SDL is built with GPU support and without SDL_Renderer. Dear ImGui types
and headers never cross the public API, and its headers and sources are not redistributed. Multi-viewports remain
disabled until Kéire has explicit multi-window renderer ownership.

Zstandard 1.5.7 is pinned and compiled as the private `KeireZstd` archive. Asset packs use it with deterministic build
profiles and SHA-256 payload verification. Zstandard headers never cross the public API and are not redistributed.

EnTT 3.16.0 and GLM 1.0.3 are pinned header-only implementation dependencies for entities/components and math. Their
headers never cross Kéire's API, third-party warnings are isolated, SDKs contain their attribution and manifest commits,
and their source trees are not redistributed.

KeireClient demonstrates the workspace as a Unity-style project editor. Project and Inspector are backed by the asset
database; Scene and Hierarchy author typed scene assets, Console captures editor/input diagnostics, and Game remains a
deliberate renderer-owned preview boundary.

Intentional updates are explicit:

```powershell
./Scripts/project.ps1 vendor-update -Dependency spdlog -Tag v1.18.0
./Scripts/project.ps1 vendor-update -Dependency imgui -Tag v1.92.8-docking
./Scripts/project.ps1 vendor-update -Dependency zstd -Tag v1.5.7
./Scripts/project.ps1 vendor-update -Dependency entt -Tag v3.16.0
./Scripts/project.ps1 vendor-update -Dependency glm -Tag 1.0.3
```

```sh
bash Scripts/project.sh vendor-update --dependency spdlog --tag v1.18.0
bash Scripts/project.sh vendor-update --dependency imgui --tag v1.92.8-docking
bash Scripts/project.sh vendor-update --dependency entt --tag v3.16.0
bash Scripts/project.sh vendor-update --dependency glm --tag 1.0.3
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

Version tags and manual release workflow runs create SDK archives without publishing a GitHub Release. Archives contain
KeireClient, KeireCore, the private KeireImGui archive, public headers including `Keire/Ui.h`, SDL's
static archive/headers/official CMake configuration, complete licenses, two consumers, and a validated JSON build
manifest containing SDL, JSON, Dear ImGui, and Zstandard commits. Packages also include the canonical Default Input
source/metadata pair and validate it with the packaged asset tool. Every package extracts and validates both a low-level consumer
with its own `main` and a managed headless-UI consumer whose `main` comes from KeireCore, using direct compiler invocation
and `find_package(Keire CONFIG REQUIRED)`. `Keire::Core` transitively links Core → ImGui → Zstd → SDL, so CMake
consumers still name only `Keire::Core`. nlohmann/json, Dear ImGui, and Zstandard headers are intentionally absent
because those dependencies do not cross the public API. The asset CLI is included under `bin`. SHA-256 files and separate symbol archives are
included where available; Dist packages are stripped.

## Troubleshooting

- Run `doctor` first when a compiler, SDK, or package manager is not found.
- Use `--update` or `-Update` only when installed system tools should be upgraded.
- Run forced generation after changing a compiler installation or toolchain: `--force` or `-Force`.
- Do not bypass a checksum failure. Verify the lock entry against the upstream release.
- Windows MSVC builds require the Desktop development with C++ workload for the requested Visual Studio major version.
- Apple's ASan runtime does not support leak detection; the macOS scripts disable that check.

See [Architecture](docs/Architecture.md), [Contributing](CONTRIBUTING.md), [Security](SECURITY.md), and [Changelog](CHANGELOG.md).
