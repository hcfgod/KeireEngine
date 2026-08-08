# Kéire Engine Repository Rules

These instructions apply to the entire repository unless a more specific `AGENTS.md` exists below the file being
changed. Treat them as requirements for every coding task, review, and generated patch.

## Project Direction

- Build a production-quality, cross-platform C++20 engine. Prefer clear ownership, deterministic behavior,
  explicit lifecycle management, and boring reliability over cleverness.
- Keep changes within the requested milestone. Do not introduce rendering, input, ECS, jobs, serialization, or other
  engine systems unless the task explicitly includes them.
- Preserve compatibility across Windows, Linux, and macOS. Do not solve a cross-platform problem with an unguarded
  platform-specific API.
- Keep `KeireCore` reusable, `KeireClient` client-specific, and `KeireTests` independent. Premake is the engine build
  authority; CMake in SDK examples exists to validate packaged consumers.

## C++ Formatting And Style

- `.clang-format` is authoritative. Run clang-format on every changed first-party C++ file before finishing.
- Use four spaces, never tabs, Allman braces, and a 120-column limit.
- Always indent everything inside namespaces. Namespace contents must never sit at the same indentation level as the
  `namespace` declaration. `NamespaceIndentation: All` must remain enabled in `.clang-format`.
- Use `#pragma once` in first-party headers. Do not add traditional `#ifndef`/`#define` include guards.
- Keep includes explicit and minimal. Public headers must compile on their own and may not depend on transitive includes.
- Follow existing naming: `PascalCase` types and public functions, `m_` member fields, and the ASCII C++ namespace
  `Keire`. Use the accented display name `Kéire` only in user-facing prose and metadata.
- Prefer standard-library types, RAII, scoped enums, `std::span`, `std::string_view`, and value types where appropriate.
  Do not add macros when a typed C++ construct works.
- Use `[[nodiscard]]`, `const`, and `noexcept` where they express a real contract. Do not mark potentially throwing code
  `noexcept` merely for style.
- Comments should explain constraints, ownership, or non-obvious reasoning. Do not narrate straightforward code.

## Public API And Dependency Boundaries

- Public APIs live under `KeireCore/Include/Keire` and are exported through `Keire/Core.h` when they are part of the
  supported umbrella API.
- Public headers must not expose SDL, nlohmann/json, spdlog implementation ownership, platform handles, mutexes, or
  other private implementation types. Use opaque IDs, value types, abstract interfaces, or PImpl boundaries.
- SDL calls, raw SDL events, and native window ownership remain in the window implementation boundary. JSON remains a
  configuration parsing detail.
- Avoid mutable process-global state. `Time` is application-owned; event and window services have explicit owners and
  lifetimes.
- Do not break source compatibility casually. When changing a public contract, update consumers, tests, documentation,
  package validation, and the changelog together.

## Ownership, Lifetime, And Exceptions

- Express ownership in types. Application layers use `std::unique_ptr`; engine reference-counted objects derive from
  `RefCounted` and are created with `CreateRef`. Use `WeakRef` to break every reference cycle.
- Never expose owning raw pointers. Non-owning pointers and views must have an obvious, documented lifetime.
- Acquire native resources into RAII guards immediately. Multi-step registration must behave as a transaction and roll
  back partial state if any later step fails.
- Destruction and shutdown paths must be safe, deterministic, and idempotent where the public contract requires it.
  Objects intentionally allowed to outlive a service must become inert safely after that service closes.
- Preserve the original exception when cleanup runs during failure. Do not swallow callback failures unless the API is
  explicitly `noexcept`; `noexcept` cleanup must contain or prevent secondary failures.

## Application, Layers, Events, And Time

- `Application` orchestrates services and frame order. `LayerStack` owns layer storage, overlay partitioning, traversal,
  deferred structural changes, and reverse teardown. Do not move layer-stack responsibilities back into `Application`.
- The application construction thread owns `Run`, immediate event dispatch/subscription, window operations, and all
  layer mutations. `RequestExit` and queued event enqueue are the supported cross-thread operations.
- Layer traversal depth is RAII-managed. Structural mutations requested during nested attach, detach, event, fixed-update,
  or update callbacks remain deferred until the next application safe boundary.
- Layer updates run bottom-to-top. Events run overlays/layers top-to-bottom. Preserve handled propagation and stable
  ordering.
- Automatic layer event subscriptions must disconnect before detachment and may not be created after detachment begins.
  `OnDetach` remains `noexcept`.
- Immediate events are synchronous and owner-thread-affine. Worker producers use the bounded owned queue. Do not let a
  drain process events enqueued during that same drain.
- Scaled time drives fixed simulation. Pause, clamping, smoothing, tick caps, interpolation remainder, and dropped-time
  accounting are observable contracts and require deterministic tests when changed.

## Entrypoint And SDK Contract

- KeireCore owns `main`, generic help/version handling, the exception boundary, application lifetime, and `Run` for
  managed clients.
- Managed clients define only static-lifetime `GetApplicationCommandLineDescription()` data and `CreateApplication()`.
  Help and version must not initialize runtime services.
- Preserve both SDK examples: the low-level consumer owns its own `main`; the managed consumer obtains `main` from
  KeireCore. Packaging changes must compile and run both directly and through CMake.

## Build And Repository Hygiene

- Use `Config/Project.conf` for project identity and `Config/Dependencies.lock` for immutable dependency inputs. Do not
  duplicate those values in scripts.
- Use repository launchers under `Scripts/` for normal generation, builds, tests, smoke runs, and packaging.
- Do not commit generated build files, binaries, logs, packages, downloaded tools, coverage output, `.ninja_lock`, or
  dependency build caches.
- Do not edit files under `Vendor/` or advance submodule pointers unless dependency work is explicitly requested. Follow
  any nested vendor `AGENTS.md` when vendor changes are required.
- Treat GitHub Actions, hosted security, clang-tidy policy, and billing-dependent CI as separate scope. Do not edit or
  investigate them unless the user explicitly asks.
- Preserve unrelated working-tree changes. Never stage, commit, amend, reset, force-push, or create a pull request unless
  the user explicitly requests that Git operation.

## Tests And Validation

- Every behavioral fix or public feature needs focused doctest coverage, including failure and lifecycle edges.
- Window and application tests use SDL's dummy video driver, drain events, and shut systems down explicitly.
- Thread-affinity changes need both rejection tests and proof that rejected operations leave state unchanged.
- Script changes require the matching Windows and Unix regression harnesses when practical. Packaging changes require
  both SDK consumers to be validated.
- At minimum, run the narrowest relevant build/tests plus clang-format dry-run and `git diff --check`. For ownership,
  threading, or lifecycle changes, also run AddressSanitizer. For `NDEBUG` behavior, run Release.
- Useful Windows commands:
  - `./Scripts/project.ps1 test -Generator ninja -Configuration Debug -Toolset msc`
  - `./Scripts/project.ps1 test -Generator ninja -Configuration DebugASan -Toolset msc`
  - `./Scripts/Tests/test-windows.ps1`
- Useful Unix commands:
  - `bash Scripts/project.sh test --generator ninja --configuration Debug --toolset clang`
  - `bash Scripts/Tests/test-unix.sh`
- Before handing off, run `git status --short` and confirm no generated or accidental files were introduced. Report the
  exact validations run and any validation that could not be run.

## Documentation And Completion

- Update `README.md` for user-facing workflows and `Docs/Architecture.md` for ownership or boundary changes.
- Add a concise `CHANGELOG.md` entry for behavior or API changes relevant to engine or SDK users.
- Keep examples compilable. Documentation snippets must follow the same formatting and namespace-indentation rules as
  production code.
- A task is complete only when the implementation, focused tests, formatting, documentation, package/consumer impact,
  and repository hygiene have all been considered. Do not claim checks passed unless they were actually run.
