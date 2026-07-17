# Changelog

All notable template changes are documented here. The format follows Keep a Changelog, and releases use semantic version tags.

## Unreleased

### Added

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

- Dear ImGui now compiles only into KeireCore with SDL3 and SDL_GPU backends; KeireClient uses the public `Keire::UiFrame` facade, and SDK packages expose `Keire/Ui.h` without redistributing Dear ImGui headers or sources.
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
