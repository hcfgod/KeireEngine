# Testing And Release

## Asset Validation

Asset changes require the focused `AssetTests.cpp` coverage plus Debug, DebugASan, and Release runs. The tests exercise
stable metadata identity, cache hits, pack validation, typed fallbacks, async completion, failure diagnostics, and
last-good reload behavior. For a manual content check, run `KeireAssetTool import`, then `KeireAssetTool validate` on
the generated development catalog. Never format or modify `Vendor/zstd`.

SDK validation requires `KeireAssetTool`, the platform `KeireZstd` archive, the Zstandard license, and its locked commit
in `build-manifest.json`. Direct consumers link Core, ImGui, Zstd, then SDL; CMake consumers continue naming only
`Keire::Core`.

Rendering/shader changes additionally run canonical shader/material tests, target-variant stripping, component
serialization, project-aware rendered smoke, and real compilation of the sample HLSL through the pinned host compiler.
Conditional native GPU checks cover D3D12, Vulkan, or Metal where available; deterministic logic remains testable in
headless mode. Packages require `KeireShaderCompiler`, its runtime libraries, every recursive lock identity, and all
SDL_shadercross/DXC/SPIR-V license and notice files.
Asset-backed rendered tests additionally cover custom indexed geometry and a real material/shader/Texture2D pipeline;
packaging cooks the Sandbox dependency graph and runs `KeireRuntime` against the packaged catalog.

Input changes additionally run canonical schema/import tests, an SDL-dummy outer-frame keyboard action test, cursor
focus restoration, public dependency isolation/`KEIRE_API` assertions, and headless UI facade tests. Packages must
contain the complete `samples/KeireSandbox` project; the staged asset tool imports its input and scene assets before
archive publication and the temporary project cache is removed afterward.

Project/scene changes run descriptor, template rollback, lock, registry recovery, canonical scene, hierarchy, weak-handle,
thread rejection, single/additive activation, failed-load preservation, and project-aware rendered smoke coverage. SDKs
must include KeireHub, project/scene public headers, and the sample project.

ECS changes additionally cover stale entity/component handles, registration rejection, required and single-instance
components, typed queries, hierarchy world transforms, cycle rollback, activation, exact lifecycle order, deferred
structural changes, Play/Pause/Step/Stop isolation, callback faults, v1 migration, and Missing Component round trips.
Asset Browser changes cover bounded thumbnail requests, deterministic cache keys, cancellation, provider invalidation,
owner-thread image upload, and project-local List/Grid preferences. Tray behavior is tested through backend-independent
lifecycle checks where the native platform cannot expose a deterministic tray.

Undo changes cover execution and record-applied paths, redo invalidation, merge behavior, isolated contexts, nested
transaction rollback, bounded eviction, stale targets, shutdown inertness, and rejected worker calls. Scene-camera
changes use the backend-independent controller tests for every gesture, framing, projection, snapping, and invalid state;
rendered smoke remains responsible for cursor capture and viewport integration.

Kéire validation is proportional to risk. The repository launchers select the correct dependency variant, refresh
build identity, build the requested target, and run the executable with the expected environment.
Noninteractive repository launcher commands preserve child-script failures as their process exit code. Automation must
invoke the repository launcher directly and treat every nonzero result as a failed build, test, run, or package step.
The interactive menu reports failures and remains open for another command.
Direct test executables and top-level launcher commands therefore have identical pass/fail semantics. Both script
regression harnesses create an isolated child that exits with a known nonzero code and require the launcher to return
that exact code.

## Baseline Validation

Every change should finish with the narrowest relevant test plus formatting and repository hygiene:

```powershell
./Scripts/project.ps1 test -Generator ninja -Configuration Debug -Toolset msc
```

```sh
bash Scripts/project.sh test --generator ninja --configuration Debug --toolset clang
```

Then run clang-format on changed first-party C++ files, its dry-run check, `git diff --check`, and `git status --short`.
Never format or patch vendored code during ordinary engine work.

## Risk-Based Matrix

| Change type | Required additional validation |
| --- | --- |
| Documentation only | Local-link/reference check and `git diff --check` |
| Public header or SDK API | Standalone header/package consumers and Release |
| Ownership or lifecycle | Focused failure tests and DebugASan |
| Thread affinity | Rejection from a worker plus proof that state is unchanged |
| `NDEBUG` or assertion behavior | Release |
| Window or application behavior | SDL dummy-driver tests and explicit shutdown |
| Rendered UI | Headless focused tests plus graphics-capable `--smoke-ui` |
| Project/scene/ECS lifecycle | Focused component/play tests plus graphics-capable `--smoke-project` |
| Asset Browser or thumbnails | Focused queue/cache tests plus rendered project smoke |
| Hub tray behavior | Injected lifecycle tests plus conditional native smoke |
| Script changes | Matching Windows and Unix regression harnesses |
| Packaging | Release package with direct and CMake low-level/managed consumers |
| Rendered pixels | Hidden-window `KeireRenderTests` on D3D12/Vulkan/Metal where supported |

UBSan and TSan should also run when a supported GCC/Clang platform is available and the change affects undefined
behavior or concurrency. A missing host capability must be reported; it is not equivalent to a passing check.

## Focused Tests

KeireTests uses doctest and remains independent of KeireClient. Tests should cover the successful contract, invalid
input, failure rollback, lifecycle edges, and retained-object behavior where relevant.

Window and application tests use SDL's dummy video driver, drain events, and shut services down explicitly. UI tests
use `UiMode::Headless` unless renderer integration is the subject under test. Temporary layout and theme directories
must be isolated and removed by the test.

Avoid tests that depend on wall-clock scheduling, global execution order, a developer's preference directory, or a
graphics adapter unless the test is explicitly a rendered smoke.

## Sanitizers

Windows AddressSanitizer:

```powershell
./Scripts/project.ps1 test -Generator ninja -Configuration DebugASan -Toolset msc
```

Linux AddressSanitizer:

```sh
bash Scripts/project.sh test --generator ninja --configuration DebugASan --toolset clang
```

On supported Unix toolchains, select `DebugUBSan` or `DebugTSan` with the same command shape. Sanitizer failures must be
fixed at their ownership or synchronization boundary; do not suppress first-party diagnostics to make a run green.

## Smoke Tests

The window smoke creates the client, pumps a bounded loop, and exits without UI. Platform packaging invokes it to
validate runtime startup without requiring a graphics session.

The rendered UI smoke creates the SDL_GPU device, initializes UI backends, submits several editor frames, and performs
a clean shutdown:

```powershell
./Scripts/project.ps1 run -Generator ninja -Configuration Debug -Toolset msc -SmokeUi
```

```sh
bash Scripts/project.sh run --generator ninja --configuration Debug --toolset clang --smoke-ui
```

The UI workspace is ephemeral in this mode, so smoke validation does not read or write the user's layouts and themes.

The project smoke opens `Samples/KeireSandbox`, imports project-local assets, activates the startup scene, attaches the
default input context, renders the editor, and releases its lock after a bounded run:

```powershell
./Scripts/project.ps1 run -Generator ninja -Configuration Debug -Toolset msc -SmokeProject
```

```sh
bash Scripts/project.sh run --generator ninja --configuration Debug --toolset clang --smoke-project
```

## Script Regression Harnesses

```powershell
./Scripts/Tests/test-windows.ps1
```

```sh
bash Scripts/Tests/test-unix.sh
```

The harnesses exercise argument validation, launcher exit propagation, dependency mappings, generated identity,
package-stage requirements, public dependency isolation, and safe cleanup behavior without performing a full release
build. Editor behavior is asserted by compiled controller/document tests rather than source-string matching.

Asset filesystem tests inject rename outcomes and backoff callbacks. Transient sharing/permission failures receive five
bounded attempts with 10/20/40/80/160 ms delays; nontransient errors fail immediately and identify both resolved paths.
Asset publication callers use the same helper for forward and rollback moves.

Normal Debug and Release test commands also build `KeireRenderTests`. Windows attempts D3D12 and Vulkan, Linux attempts
Vulkan, and macOS attempts Metal. An unavailable local driver is reported as a skip; set `KEIRE_REQUIRE_GPU_TESTS=all`
or a comma-separated backend list to turn a missing configured backend into a failure. Pixel tests use central image
regions and tolerant color/behavior deltas rather than exact screenshots.

## Coverage

Coverage requires Ninja and Clang. The launcher generates instrumentation, executes tests, writes LCOV and HTML output,
and enforces the repository line threshold:

```powershell
./Scripts/project.ps1 coverage -Generator ninja -Toolset clang
```

```sh
bash Scripts/project.sh coverage --generator ninja --toolset clang
```

Coverage output is disposable and belongs under `Build/Coverage`. Coverage does not replace sanitizer, Release, or
failure-path validation.

## Release Package

Packages are produced only from Release or Dist:

```powershell
./Scripts/project.ps1 package -Generator ninja -Configuration Release -Toolset msc
```

```sh
bash Scripts/project.sh package --generator ninja --configuration Release --toolset clang
```

Packaging performs tests and a runtime smoke before staging the SDK. It then validates:

- KeireClient plus the KeireCore, private KeireImGui/KeireZstd static libraries, and host shader compiler;
- every supported public `Keire/` header;
- the required SDL static SDK inputs and private-backend license notices;
- complete third-party license attribution, including Dear ImGui and shader compiler dependencies;
- a machine-readable build manifest with locked dependency commits;
- the low-level consumer with its own `main`;
- the managed consumer using the KeireCore-owned entrypoint;
- both consumers through direct compiler commands and the generated CMake package.

The sandbox source is staged from Git's tracked-file list, not by recursively copying the developer workspace. Package
validation fails before publication, while inspecting the archive, and after extraction if it finds a `Library`, `Logs`,
`Build`, `Temp`, `SceneRecovery`, or `Recovery` directory, a recovery file, or a `.tmp` file. Package-only asset imports
may create `Library` transiently, but the directory is removed and the stage is revalidated before compression.

Direct compiler validation links the static archives in `KeireCore`, `KeireImGui`, `KeireZstd`, SDL order. The package CMake target
encodes that private closure transitively, so supported consumers continue linking only `Keire::Core`. Package validation
also rejects a missing platform-specific ImGui archive or missing MIT attribution; Dear ImGui headers and sources remain
outside the SDK.

Archives and symbol packages are written under `Artifacts/` with SHA-256 files. They are generated outputs and must not
be committed.

## Final Handoff

Before handing work to another maintainer:

1. Confirm every requested behavior and boundary is implemented.
2. Run the required matrix and record exact commands and results.
3. Run clang-format only on changed first-party C++ files and verify dry-run output.
4. Run `git diff --check`.
5. Run `git status --short` and inspect every modified or untracked path.
6. Confirm Vendor has no accidental edits or pointer changes.
7. Report unavailable platform checks explicitly.

Do not claim a check passed unless it actually ran. Do not stage, commit, push, or create a pull request unless the user
explicitly requests that Git operation.
