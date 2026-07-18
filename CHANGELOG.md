# Changelog

All notable template changes are documented here. The format follows Keep a Changelog, and releases use semantic version tags.

## Unreleased

### Added

- A project-first editor workflow with transactional Empty/Starter creation, versioned descriptors, canonical roots,
  OS-exclusive editor locks, project-local service paths, a recoverable recent-project registry, and packaged Sandbox.
- A dedicated KeireHub with searchable/pinnable recent projects, async native folder browsing, reveal/open actions,
  detached editor launch, project status diagnostics, and Hub/project smoke modes.
- Typed `.keirescene` assets, owner-thread mutable scenes with weak object handles and validated hierarchy mutation,
  plus asynchronous single/additive SceneSystem loading and frame-boundary activation events.
- Scene authoring across Project, Scene, Hierarchy, Inspector, and Console with atomic save, bounded undo/redo,
  Save/Discard/Cancel transitions, transform editing, subtree operations, and crash-recovery snapshots.
- A dockable Input Debugger with scoped UI-capture bypass, device/user inspection, transactional map subscriptions, and
  bounded searchable Console logging of action phase, processed value, scheme, device/user, duration, and timestamp.

- A reference-counted asynchronous asset runtime with typed fallbacks, priority loading, integrity-checked Zstandard packs, last-good hot reload, mount overrides, owner-thread completion events, and bounded eviction.
- A Unity-style source database with stable `.keiremeta` identities, content-addressed import cache, transactional file operations, deterministic cooker/validator, dedicated `KeireAssetTool`, and Project/Inspector editor integration.
- An application-owned action input system with typed `.keireinput` assets, keyboard/mouse/gamepad devices, local users and pairing, control schemes, frame snapshots, interactions/processors/composites, hot reload, UI capture, cursor modes, interactive rebinding, and atomic profile overrides.
- A dockable Input Actions editor with four creation templates, master-detail authoring, bounded undo/redo, canonical Save/Revert/Validate, searchable bindings, conflict-aware Listen capture, and live device/action monitoring.

- A Unity-style editor workspace with stable panel registration, factory docking, named and portable layouts, per-user atomic autosave, semantic Kéire Dark/Light themes, custom theme editing, and a polished eight-panel client shell.
- Static-first `KEIRE_API` annotations for same-toolchain shared-library preparation and a configurable assertion foundation.
- Thread-safe factory-only `Ref`/`WeakRef` ownership with polymorphic conversion and race-safe weak locking.
- Generated runtime build identity and dependency-free KeireClient help/version commands.
- A canonical SDK consumer example and validated package-only CMake imported target.
- A managed SDK consumer that links the KeireCore-owned entrypoint and validates the client factory contract.
- Identity and dependency lock manifests.
- Transactional full-template rename support.
- Doctor, LLVM coverage, SDK package, and script regression commands.
- Native x64/ARM64 selection and concrete compiler resolution.
- CI coverage, compatibility, security, dependency, and release-package automation.
- SDL 3.4.10 multi-window platform API with typed polling events, high-DPI state, owner-thread enforcement, deferred worker destruction, and inert post-shutdown handles.
- Strict nlohmann/json 3.12.0-backed `WindowSpecification` loading and a tracked `Config/Client.json`.
- Bounded `--smoke-window` and explicit `--config` client options with a real interactive event loop.
- A single-run `Application` runtime with deterministic layer/overlay lifecycle, deferred structural mutation, cancelable close handling, and exception-safe service teardown.
- A standalone typed `EventBus` with prioritized handled propagation, RAII subscriptions, allocation-free immediate dispatch, and bounded cross-thread queued delivery.
- Unity-style application-owned `Time` with scaled/unscaled clocks, smoothing, pause, fixed-step accumulation, interpolation, and backlog diagnostics.
- A Kéire-owned immediate UI API with frame-scoped RAII widgets, application/layer lifecycle integration, headless testing, docking, layout persistence, SDL3 input, and cross-platform SDL_GPU rendering backed privately by Dear ImGui 1.92.8-docking.

### Changed

- Normal repository `run` launches KeireHub; direct editor launches require an explicit project. SDKs now carry the Hub,
  project/scene headers, and a complete validated sample project instead of a root-level standalone input asset.
- Generation stamps include Premake/config content and the first-party source inventory, automatically regenerating when
  translation units are added or removed. Windows Premake version checks now work from Unicode repository paths.

- First-party headers now live exclusively beneath each project's `Include` directory. Asset public headers and implementation sources are grouped under `Keire/Assets` and `Source/Assets`, while non-SDK Core headers remain isolated under `KeireInternal`.
- `Application` can own an opt-in `AssetSystem`; SDKs now include the asset APIs, asset CLI, private `KeireZstd` archive, Zstandard attribution, and transitive Core → ImGui → Zstd → SDL link closure.
- `Application` can own Input after Assets/Windowing and before UI. Windowing routes native events to multiple private sinks, and SDKs include the public Input API plus a validated Default Input source asset without exposing SDL or JSON.

- Dear ImGui now builds as the dedicated private `DearImGui` static-library project under the generated solution's
  `Dependencies` group. SDKs carry its separate archive transitively through `Keire::Core` without exposing or
  redistributing ImGui headers and sources.
- Public mutex-backed snapshot observers now accurately permit synchronization failures instead of declaring `noexcept`, and ignored return-value diagnostics cover the remaining query-style build and logging APIs.
- SDK packages now include the first-party `Keire/UiWorkspace.h` contract while keeping all Dear ImGui and JSON implementation headers private.
- Dear ImGui remains private to KeireCore's UI implementation with SDL3 and SDL_GPU backends; KeireClient uses the
  public `Keire::UiFrame` facade, and SDK packages expose `Keire/Ui.h` without redistributing Dear ImGui headers or
  sources.
- SDL dependency builds now enable SDL_GPU while keeping SDL_Renderer disabled. Docking is enabled for rendered UI; multi-viewports remain disabled pending explicit multi-window renderer ownership.
- Extracted layer ownership, overlay ordering, deferred mutations, traversal, and teardown into a dedicated public `LayerStack`; `Application` now delegates layer operations while orchestrating frame boundaries.
- Moved the executable entrypoint, informational command handling, exception boundary, application lifetime, and `Run` invocation into KeireCore; KeireClient now supplies `CreateApplication`.
- Moved client-specific help text into a static client command-line descriptor while retaining core-owned help/version handling.
- Public KeireCore headers now use the `Keire/` include prefix and the clean `KEIRE_*` macro family.
- Logging owns reference-counted private asynchronous state, supports console suppression, and makes detached handles safely inert after shutdown.
- Build identity refreshes immediately before KeireCore compilation and includes tracked and untracked dirty state.
- Dist builds use link-time optimization and CI treats template warnings as errors.
- Release SDKs include complete dependency licenses and separate platform symbols; Dist remains stripped.
- CodeQL and Dependency Review are explicitly opt-in and strict when enabled.
- SDL is built through a compiler-keyed dependency-only CMake cache while Kéire remains Premake-driven; SDK CMake consumers receive SDL transitively.

### Fixed

- Project Hub recent-project persistence now encodes filesystem paths as UTF-8, allowing projects beneath Unicode
  directories such as `KéireEngine` to open on Windows.
- Project generation can no longer silently omit a newly added implementation file and surface as unresolved editor
  symbols at link time.

- `CommandLineError` now participates in the `KEIRE_API` boundary, script regressions cover every exported class and free function, and mandatory event, UI, and window teardown paths contain synchronization failures.
- Workspace dock splits now preserve their proportions across fullscreen, maximized, and windowed host sizes instead
  of retaining fullscreen side-panel widths and collapsing the central region.
- Dirty theme preset changes now defer their confirmation modal until the preset combo or menu has closed, so Save, Discard, and Cancel can complete the requested switch reliably.
- Release packaging now compiles and runs a standalone consumer from the extracted SDK archive.
- Nested layer traversal now defers structural mutation until the outermost callback returns, and layer operations enforce the application construction thread.
- Detaching layers cannot create automatic subscriptions, and native window registration rolls back partial resource acquisition.
- Ninja's transient lock file no longer contaminates runtime build identity or package manifests.
- Windows packaging supports Git repositories without a first commit, and vendor probes suppress expected native Git errors.
- Linux coverage resolves `llvm-profdata` and `llvm-cov` from the selected Clang major version.
- Logger handles no longer expose spdlog or standard-library ownership and lock types in the public API.
- Shutdown no longer waits for live logger-handle values, preventing same-thread handle/shutdown deadlocks.
- Security workflows expose an always-running activation check so disabled advanced-security jobs cannot look silently successful.
- Linux Premake bootstrap now accepts release archives whose executable bit is not preserved.
- ARM64 Premake source builds install the platform UUID development headers before compilation.
- Vendor bootstrap verifies the committed submodule pointer and restores detached working trees to that exact hash.
- macOS tool version checks consume complete command output, avoiding `xcodebuild` broken-pipe crashes.
- Linux Clang bootstrap installs the LLVM profiling and coverage utilities required by coverage reports.
- Lazy and explicit logger initialization now serialize under one lifecycle lock.
- macOS bootstrap accepts Command Line Tools without full Xcode for non-Xcode generators.
- Arch installation performs a full package upgrade instead of a partial database synchronization.
- Windows detects containing Git worktrees and rename updates public-header include guards.
