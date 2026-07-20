# Changelog

All notable template changes are documented here. The format follows Keep a Changelog, and releases use semantic version tags.

## Unreleased

### Added

- A metallic-roughness material surface with shader-declared semantic slots, neutral base/normal/ORM/emissive
  fallbacks, ranged numeric/color authoring, strict cook validation, and glTF +Y normal-map handling.
- Version-two mesh vertices with stored tangent handedness, deterministic v1 tangent reconstruction, and cataloged
  local bounds used by hitch-free scene picking.
- Cold-process rendered-output repeat harnesses and balanced render-frame cancellation/completion during shutdown.
- Correct SPIR-V entrypoint binding for Vulkan, Windows catalogs containing both DXIL and SPIR-V, and required
  GPU-runner diagnostics for D3D12, Vulkan, and Metal.
- Persisted Scene-camera ownership and typed viewport asset-drop routing, including direct mesh-to-entity creation.
- Shader-driven material texture authoring in code and Inspector, with filtered Texture2D pickers for every declared
  base-color, normal, emissive, mask, or custom texture slot and source-safe material serialization.
- Scene-view ray picking for active transform-only and rendered entities, while retaining Camera and Directional Light
  overlay selection and nearest-hit behavior.
- A renderer-owned asset resource layer for revisioned mesh, material, shader, texture, sampler, and pipeline GPU
  resources, with indexed asset geometry, fixed shader bindings, transactional fence retirement, checker/error
  fallbacks, and last-good hot reload behavior.
- MSVC AddressSanitizer builds retain first-party instrumentation while matching the unannotated STL ABI used by
  private CMake dependency archives.
- A transitive startup-scene cooker, textured Sandbox pyramid, cooked runtime manifest, and managed `KeireRuntime`
  player with finite-frame package smoke validation.
- Pinned private Assimp and stb dependencies, a strict versioned `.keiremesh` format, deterministic OBJ/FBX/glTF/GLB
  static-mesh import, RGBA8 texture import with normalized sampler settings and deterministic mip generation, and an
  `AssetTool convert-mesh` command.
- Headless editor document and command-router tests, with scene and Input Actions state moved behind dedicated document
  owners and workspace panels composed through narrow controller interfaces.
- A component-driven Inspector fallback with transactional property edits, generic drawers for every registered property
  kind, component/property overrides, filtered asset and entity pickers, and engine-owned scalar/vector UI controls.
- Kéire-owned logging levels and formatting for placeholders, integer hex/width, and floating precision; public headers
  and SDK include paths no longer expose spdlog or fmt.
- Backend-conditional D3D12, Vulkan, and Metal rendered-output tests with synchronized offscreen RGBA8 readback and
  tolerant pixel assertions for lighting, transforms, material/shader/texture revision swaps, and last-good output.

- Project-owned ambient color, intensity, and exposure settings with atomic persistence, a dockable Project Settings
  editor, and matching Scene/Game lighting submissions.
- Unity-style Scene transform gizmos with `Q/W/E/R` tool shortcuts, Local/Global space, independent position/rotation/
  scale snapping, persistent tool preferences, Camera icons/frustums, and Directional Light icons/rays.

- Advanced Input Actions authoring for action/value types, control schemes and device requirements, composites, binding
  groups, control browsing, every built-in interaction/processor, and conflict-aware interactive rebinding.
- Typed Scene-view asset drops: Material assignment uses object picking and shared scene undo, while Scene and Input
  Actions assets open through their existing guarded workflows.
- A built-in Directional Light Lambert path with transformed cube normals, linear color/intensity, optional color
  temperature, and an unlit editor grid.
- Fixed ambient and Directional Light delivery across SDL_GPU backends by using the proven per-draw vertex uniform
  block, and stopped Project Settings drags from issuing repeated atomic writes while Windows still retires a file.

- An application-owned bounded undo/redo service with isolated document contexts, owner-thread enforcement, mergeable
  commands, nested rollback-safe transactions, contextual Edit-menu shortcuts, and scene/input/theme/asset integration.
- Unity-style Project asset creation and management with current-folder templates, immediate browser synchronization,
  per-asset best-effort import diagnostics, multi-selection file commands, extension-free labels, metadata hover cards,
  drag/drop moves, and persistent recoverable trash.
- A first-party generated unlit shader path that applies each Mesh Renderer linear tint in Scene and Game views, plus a
  testable editor camera controller with framing, view locking, orthographic navigation, fly-speed control, and axis snaps.

- An application-owned SDL_GPU rendering system with coordinated Scene/Game/UI presentation, resizable sRGB/depth/MSAA
  targets, fence-based retirement, editor grid/cube rendering, Camera and Mesh Renderer components, and persistent
  Unity-style Scene camera navigation.
- A contextual shader import pipeline with pinned SDL_shadercross and recursive compiler dependencies, reproducible
  DXIL/SPIR-V/MSL compilation and reflection, immutable shader/material/mesh assets, target-platform cooking, editor
  creation commands, and packaged host compiler tooling.

- A Kéire-owned Unity-style ECS surface with stable weak entities, reference-counted components, application-owned
  registration, typed queries, deterministic lifecycle callbacks, private EnTT storage, and private GLM-backed math.
- Mandatory hierarchical Transform and authorable Directional Light components, canonical scene schema v2, schema v1
  migration, Missing Component preservation, and isolated Play/Pause/Step/Stop runtime cloning.
- A dedicated Asset Browser with folder navigation, persistent List/Grid modes, multi-selection, transactional rename,
  duplicate/trash operations, opaque UI images, and bounded asynchronous digest/version-cached thumbnails.
- Asynchronous Save As, global scene shortcuts, hierarchy context commands, component inspectors, scene play controls,
  and system-tray Hub backgrounding with a recoverable no-tray fallback.

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

- Scene view now previews the active scene Camera's clear color while retaining the nonserialized editor camera, and
  built-in Directional Light plus ambient illumination is evaluated through validated fragment-stage uniforms.

- Input Debugger action events now stay in a bounded local history, suppress idle/reset noise, coalesce meaningful
  analog changes, and forward to Console only through an explicit opt-in.
- EnTT 3.16.0 and GLM 1.0.3 are pinned private dependencies with IDE utility projects, external warning isolation,
  package attribution, and build-manifest identities without public header or source redistribution.

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

- Restored the declared Zstandard, EnTT, GLM, and SDL_shadercross submodule gitlinks so recursive clones reproduce the
  locked vendor tree.
- SDK packaging now stages only tracked sandbox sources and rejects generated `Library`, `Logs`, `Build`, `Temp`, and
  recovery data in the staging tree, archive, and extracted validation copy.

- Project thumbnails and labels now initiate the same asset drag, component cards collapse without losing their bordered
  presentation, horizontal Scene-camera motion follows pointer direction, and file-manager reveal opens the canonical
  project asset path instead of the process default folder.
- The Game view resolves Camera components from the active runtime scene rather than editor-camera state, and focused
  Inspector/Hierarchy edits route Undo/Redo to the scene context.
- Hub-launched editors now resolve the pinned shader compiler independently of the project working directory; failed
  imports show full Inspector diagnostics, mirror errors to the editor Console and rotating logs, and no longer trigger
  Dear ImGui's null-ID drag-source assertion when the Project panel displays an error badge.
- Asset creation now remains visible when a later best-effort import fails, blank-space Project context creation works,
  and Mesh Renderer tint changes reach GPU draw constants instead of leaving the cube at its test-shader color.

- Project Hub tray callbacks now defer native window mutations until polling completes; one Show action restores,
  raises, and focuses the Hub, minimizing hides it, and closing or choosing tray Quit performs a full process shutdown.

- Minimizing the editor during event polling no longer abandons pending fixed ticks and terminates the process on the
  following frame.
- Minimizing a restored Project Hub now hides it back to the system tray, and Show Hub reliably makes it visible,
  restores it, and raises it again.
- Asset Browser grids no longer restore invalid Dear ImGui stretch weights when their responsive column count changes,
  and narrow panels collapse the folder tree instead of crushing both browser panes together.
- Transform inspectors now use compact responsive X/Y/Z drag fields with direct numeric entry instead of nine
  full-width sliders.
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
