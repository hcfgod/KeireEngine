# Architecture

## Ownership

`KeireCore` is a static C++20 library and owns reusable application behavior, including reference-counted ownership and logging. `KeireClient` is the runnable application and depends on KeireCore. `KeireTests` is a separate executable that depends on KeireCore and doctest. Each project owns a local `premake5.lua`; the root file only defines workspace identity and loads projects.

`Config/Project.conf` defines names and folders. `Config/Dependencies.lock` defines immutable external inputs. Premake and launchers read these files so renaming and dependency verification have one source of truth.

## Automation Flow

```mermaid
flowchart LR
    Launcher["Platform launcher"] --> Identity["Identity and dependency locks"]
    Launcher --> Bootstrap["Tool and vendor verification"]
    Launcher --> Generate["Premake generation"]
    Generate --> Build["Compiler or IDE build"]
    Build --> KeireTests["doctest and KeireClient smoke run"]
    KeireTests --> Coverage["LLVM coverage"]
    KeireTests --> Package["Runtime and SDK archive"]
```

The scripts resolve `default` to a concrete compiler before generation. Architecture defaults to the native host and may be overridden with x86_64 or ARM64. A generation stamp prevents reuse with mismatched generator, architecture, toolset, or CI warning policy.

## Reference Ownership

Project-owned shared objects derive from `RefCounted` and are constructed with `CreateRef`. `Ref` and `WeakRef` point at an external atomic control block containing a type-erased deleter. The last strong release destroys the object; the implicit weak owner then releases the control block when no explicit weak references remain. `WeakRef::Lock` uses atomic increment-if-nonzero, so it cannot resurrect an object or race its destruction. Cyclic graphs must contain at least one weak edge.

## Logging Lifecycle

KeireCore owns a private spdlog thread pool and two asynchronous loggers inside a reference-counted `LogState`. The implementation does not register global names, replace the spdlog default, or shut down unrelated state. Handles keep the state storage valid but take operation locks only while making calls. Shutdown detaches the global state, takes its exclusive operation lock, flushes pending work, and closes it. It may wait for an active call but not for a handle's lifetime; detached handles observe the closed state and become safe no-ops.

File paths are intentionally relative to the process working directory. Scripts and generated IDE targets set that directory to the repository root, producing consistent `Logs/Core.log` and `Logs/Client.log` paths.

## Window Platform Boundary

Public headers contain only Kéire value types. SDL pointers, flags, identifiers, headers, and raw events live in `Window.cpp`. `WindowSystem` owns SDL video state and is unique while active; each abstract `Window` delegates through a reference-counted private implementation. The creating thread owns SDL calls. A final window release on another thread adds its opaque ID to a destruction queue, which event polling and shutdown drain on the owner thread.

The polling translator updates cached state before returning each `WindowEvent`, preserving SDL ordering without callbacks or re-entrancy. Logical dimensions, pixel dimensions, and display scale remain distinct so high-DPI changes are observable. Unknown input events are ignored until a dedicated input subsystem extends this central translator.

Configuration is a separate typed boundary. nlohmann/json parses implementation-side input into `WindowSpecification`; callers never depend on JSON types. Strict keys and bounds make configuration mistakes deterministic rather than silently accepting typos.

## Application And Layer Runtime

`Application` is an explicit, single-run orchestrator for logging, event/time services, the SDL window system, the primary window, and an application-bound `LayerStack`. Construction is side-effect free; `Run` initializes services in dependency order and unwinds them in reverse order even when client callbacks throw. KeireCore owns the executable entrypoint, dependency-free help/version handling, the top-level exception boundary, application lifetime, and the `Run` call. The client defines `CreateApplication` and performs client-specific argument and configuration validation before returning its application instance. Because the entrypoint is an unreferenced member of the static library, tests and SDK consumers that define their own `main` do not pull it into their executables.

`LayerStack` owns layer records and lifetimes, the overlay partition, pending structural operations, activation, traversal, and reverse-order teardown. Fixed and variable updates traverse bottom-to-top, while events traverse top-to-bottom. Push/remove operations requested during a traversal are deferred until a safe boundary. Ownership is unique, attachment is exactly once, subscription tokens disconnect before `OnDetach`, and shutdown detaches in reverse order before client shutdown and service teardown. `Application` coordinates safe boundaries and delegates its convenience layer methods to the stack.

## Event And Time Runtime

`EventBus` uses exact C++ payload types without a base-event hierarchy. Typed and generic listeners share one priority/registration order; inactive tombstones allow safe unsubscribe and nested dispatch without allocating on the immediate path. Owner-thread dispatch and subscription keep callback mutation deterministic. A bounded mutex-protected queue accepts owned events from any thread, rejects overflow without blocking, and drains a fixed snapshot so producers cannot starve a frame. Closing a bus makes retained references and subscription tokens safely inert.

`Time` is application-owned rather than process-global. A monotonic frame sample feeds raw, clamped unscaled, scaled, smoothed, and elapsed clocks. Scaled time feeds a 60 Hz accumulator with a fixed per-frame tick cap; excess whole ticks are recorded as dropped simulation time while the fractional interpolation remainder is retained. Pause and minimized suspension stop scaled simulation without losing real/unscaled time.

## Dependency Build Boundary

Premake remains the Kéire build authority. A dependency-only CMake invocation builds and installs pinned SDL3 Debug and Release variants into ignored, compiler-keyed caches. A generated Lua manifest supplies Premake with the selected include/archive paths and platform requirements. SDK packages preserve SDL's official CMake target and make `Keire::Core` transitively depend on `SDL3::SDL3-static`.

## Release Shape

Packages include the KeireClient runtime, KeireCore static library, public `Keire/<header>` headers, required spdlog headers, SDL static SDK, complete license texts, notices, README, and a validated machine-readable build manifest. Packaging extracts the archive and compiles, links, and runs the checked-in C++20 consumer both directly and through the generated CMake package. CMake builds SDL and serves consumers; Premake builds Kéire. Release debug symbols are uploaded separately where a platform toolchain emits them; Dist is intentionally stripped. Export annotations describe same-toolchain shared-library preparation only, not a compiler-independent C++ ABI.

A KeireCore prebuild step refreshes version and source-control identity under `Build/Generated` immediately before compilation, including tracked and untracked dirty state. The generator C-escapes configured strings and only rewrites the header when its content changes. The compiler supplies configuration, compiler, platform, and architecture identity. Packaging regenerates identity and verifies the staged binary's commit prefix and dirty marker against its manifest. The resulting `Keire::BuildInfo` describes the binary itself rather than the machine inspecting it.
