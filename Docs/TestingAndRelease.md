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

Renderer lifecycle acceptance uses real rendered paths in addition to focused contracts. On a graphics-capable Windows
host, `Scripts/Windows/test-render-device-loss.ps1` stages the Debug runtime as an isolated package, cooks all Sandbox
build scenes, injects loss while the second scene loads, then runs the real Editor `--smoke-play` window/update/input
loop through recovery. Both JSON reports are removed before execution and must match schema, build commit,
configuration, recovery generation, exactly-one retry, and no-touch-lost-generation assertions. Release and Dist never
contain these injection options. `Scripts/Windows/render-benchmark.ps1` runs the Release runtime with VSync on and off,
using 300 warm-up and 2,000 measured frames; it rejects stale/missing artifacts, wrong identity or presentation mode,
and non-monotonic frame IDs before publishing median/p95/p99 results beneath ignored `Build/Benchmarks` output.

### Render benchmark evidence schema

The benchmark publishes schema-1 JSON for each presentation mode and a schema-1 `render-matrix.json` that contains both
runs. Each run must identify the workload (`warmupFrames=300`, `measuredFrames=2000`), presentation mode, and these
evidence groups:

- `build`: version, Git commit, dirty-worktree flag, Release configuration, compiler, platform, and architecture;
- `hardware`: operating-system description/version, CPU model, logical processor count, physical memory, renderer
  backend, GPU adapter, driver name/version/information, and device generation;
- `pipeline`: accepted frame bound, frames-in-flight high-water mark, and renderer-queue high-water mark;
- `summary`: median, p95, and p99 for owner update, capture, admission wait, queue delay, render CPU, GPU retirement,
  and submit-to-present latency; and
- `timelines`: exactly 2,000 consecutive frame IDs with those same finite, non-negative measurements plus the
  outstanding-frame count observed at admission.

The launcher compares the recorded build identity and dirty flag with the invoking checkout, requires both VSync and
immediate runs, and rejects a frame or high-water count outside the configured 1–3 frame bound. A JSON file merely
existing under `Build/Benchmarks` is not evidence: only a launcher-completed matrix from named graphics hardware is a
passing characterization.

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
owner-thread image upload, revision-keyed visible views across a 50,000-record catalog, and project-local List/Grid
preferences. Tray behavior is tested through backend-independent lifecycle checks where the native platform cannot
expose a deterministic tray.

Asset-package parser changes run the canonical archive round trips, path and bound rejection cases, cancellation and
cleanup tests, plus a deterministic mutation corpus spanning truncation, distributed bit flips, and trailing data.
Rejected mutations must never leave a partial staging tree.

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

Run `python Scripts/Tests/check-repository-layout.py` whenever files or directories move. It enforces the exact
`Docs`, `Include`, and `Source` casing used by Linux and macOS, verifies every first-party header and implementation
unit is in its owning tree, and prevents a Windows-only case-insensitive checkout from masking a broken package or
include path. The fast Windows and Unix harnesses run the same check automatically.

The fast script harnesses additionally parse every workflow with duplicate-key rejection, scan tracked and untracked
first-party text as strict UTF-8 for common mojibake, and enforce ratcheting limits in
`Config/SourceFileBudgets.json`. Their repository-wide scans prefer `rg` and use `git grep` when it is unavailable.
CI runs `actionlint`, `yamllint`, Ruff, ShellCheck, PSScriptAnalyzer, recursive first-party clang-format 22.1.8, and
recursive compile-database-backed clang-tidy. A
compile-database coverage gate rejects any omitted Premake-owned translation unit; SDK examples remain covered by their
direct and CMake package-consumer builds. A hosted workflow that did not start because Actions quota was exhausted is
unavailable evidence, not a product failure and not a passing lane.

## Local Linux Distribution Matrix

Run the containerized Linux matrix from a standalone Linux or WSL2 clone with initialized submodules and Podman:

```sh
bash Scripts/Tests/test-linux-distros.sh --suite test --distro all
```

The matrix covers Ubuntu 22.04/24.04, Debian 12, Fedora, Arch, openSUSE Tumbleweed, and Rocky Linux 9, with Ubuntu 26.04
included as a new setup and compatibility target pending complete native evidence. Each distro uses its native package
manager, an isolated source overlay, and separate persistent build/tool/cache volumes. The `bootstrap` suite exercises
the same distro-aware `Scripts/setup-linux.sh` entry point documented for real workstations. The default `test` suite
performs the same complete warnings-as-errors core, editor, Hub, managed, and client compile validation as
the hosted Linux launcher, then starts the built client through SDL's dummy window backend. Use `--suite bootstrap`
for a faster clean-machine prerequisite check, `--distro <name>` for a focused run, or `--refresh-images` to pull
current base images. `--jobs <count>` controls compiler concurrency inside each container.

This is the locally repeatable substitute when hosted Actions execution is unavailable; it does not convert an
unobserved hosted check into a pass. Linux `.tar.gz` distributions are the common archive format produced on each
intended Linux release baseline; a binary built against a newer glibc is not relabeled as universal. Native DEB
installers target Debian/Ubuntu and native RPM installers target Rocky/Fedora/openSUSE. Each is a separate artifact
built and tested on its declared baseline rather than simulated by the container matrix.

Stable Linux editor catalog packages are produced from a clean detached release commit inside the Rocky Linux 9
baseline container (glibc 2.34 and GCC Toolset 12). Headless release validation uses Xvfb with Mesa Vulkan for the
packaged runtime GPU smoke. Do not relabel an artifact built against a newer glibc as a general Linux release.

Kéire 0.4.2 targets signed catalog sequence 17 and is not active until newly built Windows and Linux packages pass
their native gates. The active 0.4.1 sequence-16 stable catalog publishes Windows and Linux x86-64. Its Linux catalog contains the Editor
archive plus distinct DEB and RPM Hub records. The exact release Editor/package gate ran on the Rocky Linux 9 baseline;
the compatibility-baseline DEB was installed on Ubuntu 22.04 and Debian 13, the RPM on Rocky Linux 9, Fedora 44, and
openSUSE Tumbleweed, and a packaged Vulkan/WSLg Play Mode smoke completed. Linux ARM64, Alpine/musl, native macOS, and
Metal remain unobserved and must not be presented as tested download targets.

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
| Startup-scene Play Mode | Graphics-capable `--project <path> --smoke-play` with native audio and scripting enabled |
| Asset Browser or thumbnails | Focused queue/cache tests plus rendered project smoke |
| Hub tray behavior | Injected lifecycle tests plus conditional native smoke |
| Script changes | Matching Windows and Unix regression harnesses |
| Packaging | Release package with direct and CMake low-level/managed consumers |
| Renderer/VFX performance | Named reference hardware, Release/Dist capture metadata, full frame-history export, and a passing configured performance profile |
| Rendered pixels | Hidden-window `KeireRenderTests` on D3D12/Vulkan/Metal where supported |

The production validation launchers accept `-IncludeGraphicsSmokes` on Windows or `--include-graphics-smokes` on Unix
to run the sample-project Release smoke on a graphics-capable worker. `-IncludePackage` or `--include-package` adds the
Release SDK build plus direct and CMake validation for both packaged consumers.

UBSan and TSan should also run when a supported GCC/Clang platform is available and the change affects undefined
behavior or concurrency. A missing host capability must be reported; it is not equivalent to a passing check.

## Focused Tests

KeireTests uses doctest and remains independent of KeireClient. `KeireEditorTests` owns editor document and worker
coverage, while `KeireHubTests` owns the private Hub runtime and Hub-product integration boundary, including
secondary-process activation. The normal test launcher builds and runs those three headless suites, then conditionally
runs `KeireRenderTests` for each available platform backend. Tests should cover the
successful contract, invalid input, failure rollback, lifecycle edges, and retained-object behavior where relevant.

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
validate runtime startup without requiring a graphics session. On Windows, managed executables enter through the
wide-character CRT boundary and convert arguments to UTF-8 before engine parsing; run smoke validation from the actual
checkout path so non-ASCII repository and project paths remain covered.

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
./Scripts/Tests/test-windows.ps1 -Suite Fast
./Scripts/Tests/test-windows.ps1 -Suite Integration
```

```sh
bash Scripts/Tests/test-unix.sh
bash Scripts/Tests/test-unix.sh --suite fast
bash Scripts/Tests/test-unix.sh --suite integration
```

The umbrella command runs both suites. Fast checks batch repository scans through `rg` and print elapsed time;
integration keeps the Unicode, rename, rollback, contamination, and launcher fixtures isolated and reports its own
timing. The harnesses exercise argument validation, launcher exit propagation, dependency mappings, generated identity,
package-stage requirements, public dependency isolation, and safe cleanup behavior without performing a full release
build. Editor behavior is asserted by compiled controller/document tests rather than source-string matching.

Asset filesystem tests inject rename outcomes and backoff callbacks. Transient sharing/permission failures receive five
bounded attempts with 10/20/40/80/160 ms delays; nontransient errors fail immediately and identify both resolved paths.
Asset publication callers use the same helper for forward and rollback moves.

Normal Debug and Release test commands also build `KeireRenderTests`. Windows attempts D3D12 and Vulkan, Linux attempts
Vulkan, and macOS attempts Metal. An unavailable local driver is reported as a skip; set `KEIRE_REQUIRE_GPU_TESTS=all`
or a comma-separated backend list to turn a missing configured backend into a failure. Pixel tests use central image
regions and tolerant color/behavior deltas rather than exact screenshots.

SDL's canonical Windows backend identifier is `direct3d12`. The render-test executable also accepts the common
`d3d12` shorthand and normalizes the environment before any case recreates SDL, so probes and full runs select the same
driver. Release scripts use the canonical identifier.

The required self-hosted GPU workflow runs Windows D3D12/Vulkan and Linux Vulkan/macOS Metal in Debug and Release.
Release Windows requires 25 consecutive D3D12 runs, five D3D12 validation-layer runs, and five Vulkan runs. Release
lanes also create a clean package and run its runtime and SDK-consumer validation; failures upload render diagnostics,
readbacks, crash dumps, and asset-worker protocol/journal artifacts.

## Coverage

Coverage requires Ninja and Clang. The launcher generates instrumentation, executes the core, editor, Hub, and client
targets, writes aggregate LCOV and HTML output, and enforces two line-coverage non-regression floors: 74.5% for the core
test binary and 63.0% across the complete instrumented product set. These are measured baseline floors, not aspirational
quality targets; raise them as focused coverage closes the remaining editor, Hub, and client gaps.

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

- KeireClient plus the private KeireAssetWorker, KeireCore, private KeireImGui/KeireZstd static libraries, and host
  shader compiler;
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

Release and Dist packaging reject tracked or untracked worktree changes before building. A developer may explicitly
pass `-AllowDirty` on Windows or `--allow-dirty` on Unix for a local diagnostic package; its manifest records both
`dirty: true` and `developmentArtifact: true`. CI rejects the override, and publication packages require both flags to
be false.

## Editor Distribution Package

The editor distribution command is separate from the SDK package and always builds Dist:

```powershell
./Scripts/project.ps1 package-editor -Generator vs2022 -Toolset msc
```

```sh
bash Scripts/project.sh package-editor --generator ninja --toolset clang
```

Run the command on each release OS. Windows produces a `.zip`, while Linux and macOS produce `.tar.gz` archives;
macOS also receives an Editor `.app` launcher. Editor artifacts contain no Hub executable, HubWorker, Hub content,
Hub-owned native launcher, desktop file, or icon; those belong exclusively to the standalone Hub package. Native editor
binaries and platform SDK/framework dependencies are not cross-packaged from a different host.

The command first runs the normal Dist test, editor smoke, asset-cook, runtime-smoke, manifest, and license staging
gate. It then publishes an end-user layout containing the editor, AssetTool, private AssetWorker, runtime, shader
compiler, FFmpeg runtime, managed host, complete .NET 10 SDK, tracked sandbox, documentation, and third-party notices.
Hub-owned templates, content catalogs, fonts, and their license records are deliberately omitted. SDK-only headers,
static libraries, CMake metadata, and consumer examples are also omitted. Validation checks
the stage, the compressed archive, and an extracted copy; it also executes the staged and extracted .NET SDK. The
archive has a separate `editor-package.json` referencing the locked `build-manifest.json`, and its checksum is written
beside it under `Artifacts/`. The validated, unpacked distribution remains available under `Build/Distributions/` for
immediate local use through `launch-editor.sh --project <path>` on Unix or its platform launcher.

`editor-package.json` is schema 2. Its machine-readable contract includes `entrypoints.editor` plus editor-specific
companion-tool entrypoints, `projectSchema.minimum`/`maximum`, `moduleFingerprint`, `manifestFingerprint`, optional
`packagedTemplates`, `bundledToolchains`, `licenseReferences`, `releaseNotes`, `files`, and `installedSizeBytes`. Every
inventory entry has a normalized relative path, byte count, and SHA-256 digest. The canonical manifest fingerprint
covers metadata and the file inventory without introducing a self-hash cycle; installed size includes the manifest
itself. Packaging validates the staged and extracted bytes against that inventory. Schema-1 consumers remain
supported because the original
top-level identity, worktree, platform, launcher, `bundledDotnetSdk`, and `buildManifest` fields are retained and
declared by `compatibility.legacyTopLevelFields`.

Windows Dist editor packages stage the editor as a GUI-subsystem executable with `mainCRTStartup`; the independently
packaged Hub remains outside the editor artifact. Normal packaged launches therefore create no terminal window; native
Core and Client records remain visible through the editor Console and rotating log files. Development configurations
retain their terminal sink, and dependency-free help/version commands preserve command-line output for validation and
diagnostics.

The same clean-worktree policy as SDK packaging applies. `-AllowDirty` or `--allow-dirty` remains a local diagnostic
escape hatch, is rejected in CI, and is recorded in both manifests.

## Standalone Hub Distribution Package

The Hub lifecycle can be built and published independently of every versioned editor:

```powershell
./Scripts/project.ps1 package-hub -Generator vs2022 -Toolset msc
```

```sh
bash Scripts/project.sh package-hub --generator ninja --toolset clang
```

This command always builds Dist and copies only the Hub executable, its private HubWorker task process and load-time
runtime, branding,
licensed Hub fonts, tracked documentation and sandbox learning content, validated template catalog/payloads, notices,
and licenses. It deliberately rejects a staged editor or editor-specific AssetWorker
executable and SDK-only `include`, `lib`, or `examples` trees. It does not invoke the SDK or editor packager. Windows
publishes a `.zip`; Linux and macOS publish `.tar.gz` files, with a native Hub `.app` launcher included on macOS. The
unpacked stage remains in `Build/Distributions/` and the archive plus SHA-256 file is written to `Artifacts/`.

`hub-package.json` uses the same schema-2 identity, fingerprints, exact file inventory, installed-size, license, and
release-note contracts as editor packages. Hub packages have `hub` and private `worker` entrypoints and no editor
entrypoint. A bundled
.NET runtime is represented as a read-only `bundledToolchains` entry only when it exists in the built Hub output. The
stage and an extracted archive copy are both validated before publication. The native editor installer consumes the
backward-compatible but editor-only package; the standalone Hub installer consumes only this independent Hub stage.

A Dist Hub archive is not accepted merely because `package-hub` emitted a file. The target-platform release sequence
first runs the normal Debug test launcher (including `KeireHubTests` and the platform's package/installer regression
harnesses), then `package-hub`, and finally `package-hub-installer` where a native installer is supported. Retain the
validated stage, extracted-archive check, `hub-package.json`, archive checksum, native-installer checksum, hidden
`--verify-installation` result, and exact command output as one evidence set. A Windows result does not satisfy Linux or
macOS Hub package and native-installer gates; each artifact is built and validated on its target operating system.

## Windows Installer Release Gate

Keep transaction testing separate from architecture-specific Dist packaging. The Debug worker and actual-NSIS
fixtures exercise both products with custom roots, interruptions, drift, unsafe links, and unowned nested
`Config`, `Docs`, and `Samples` content. The normal Windows test launcher runs the Editor and Hub worker/NSIS matrices
first and stops on the first failure; only then does it run the editor and Hub distribution-package contract gates:

```powershell
./Scripts/project.ps1 test -Generator ninja -Configuration Debug -Architecture x86_64 -Toolset msc
```

After that gate passes, build both native installers. Each command independently builds its product's Dist stage,
rejects a worker containing the test-only interruption seam, executes the staged real executable's hidden
`--verify-installation` path, validates the exact package inventory, and only then compiles the NSIS shell:

```powershell
./Scripts/project.ps1 package-installer -Generator ninja -Architecture x86_64 -Toolset msc
./Scripts/project.ps1 package-hub-installer -Generator ninja -Architecture x86_64 -Toolset msc
```

Do not make an ARM64 or other Dist package recursively build the x86_64 Debug fixtures. For a focused transaction
rerun after those Debug targets already exist, use `Scripts/Tests/test-installer-windows.ps1` followed by
`Scripts/Tests/test-hub-installer-windows.ps1` before either package command.

The shared worker durably journals `staged`, `backupMoved`, `payloadActivated`, `registrationWritten`, `verified`, and
`committed` phases. Recovery accepts only a product- and transaction-bound locator whose digest matches that journal;
cleanup authority is limited to the journal-verified staging, backup, and transaction entries. A failure before commit
restores both the prior payload and prior registration, and replaying recovery or shutdown is idempotent. Optional
shortcuts and protocol registration remain outside payload ownership and must either complete before commit or roll
back to their exact prior values.

## Standalone Hub Native Installers

Create a native installer without bundling an editor:

```powershell
winget install NSIS.NSIS
./Scripts/project.ps1 package-hub-installer -Generator vs2022 -Toolset msc
```

```sh
bash Scripts/project.sh package-hub-installer --generator ninja --toolset clang
```

Windows compiles `Installer/Windows/KeireHub.nsi` into a per-user Hub setup executable. macOS wraps the exact Hub stage
inside a self-contained Hub `.app` and drag-to-Applications DMG. Linux installs that stage beneath `/opt/keire-hub`
with a `keire-hub` command, desktop entry, and icon. Its `/usr/bin` wrapper executes the explicit `/opt` Hub binary so
the package-relative launcher cannot resolve beside `/usr/bin`. On Linux, `auto` selects DEB for Ubuntu/Debian and RPM
for Rocky/Fedora/openSUSE; release automation can pass `--linux-installer-format deb` or
`--linux-installer-format rpm`.
The RPM path requires `rpm-build`, `rpm`, and `cpio`, records the native runtime dependencies, validates its identity and
file inventory after extraction, and uses the source commit timestamp for deterministic payload metadata. All three
platform workflows update or remove only the application
payload; Hub preferences, package caches, project metadata, and managed or externally located editor roots remain
untouched by default.

Set `KEIRE_WINDOWS_SIGNING_CERT_SHA1` to the Authenticode certificate thumbprint on Windows;
`KEIRE_WINDOWS_TIMESTAMP_URL` optionally overrides the RFC 3161 timestamp service. On macOS,
`KEIRE_MACOS_SIGNING_IDENTITY` enables hardened-runtime signing and `KEIRE_MACOS_NOTARY_PROFILE` names the
`xcrun notarytool` keychain profile used for submission and stapling. A notarization profile requires a signing
identity. Hub code is signed explicitly from inner Mach-O files and nested bundles outward; the packager never uses
blanket `--deep` signing and gives the native Hub no managed-runtime entitlements. These values are release secrets
and must not be committed.

Only publish a Hub installer catalog record when its package key, channel, platform, architecture, native format, and
disclosed trust level exactly match the catalog endpoint and download surface. The Ed25519 catalog signature and bound
SHA-256 authenticate an artifact but do not replace Authenticode, RPM GPG signing, or Apple notarization. A
catalog-verified Windows installer is rehashed and inspected before automatic handoff. A genuinely absent
Authenticode signature is accepted only under the disclosed preview policy; a present invalid, revoked, or untrusted
signature fails closed. Existing 0.4.1 Hubs still carry the older unconditional policy, so crossing that boundary
  requires either an Authenticode-signed bridge/target installer or a documented one-time manual install. The macOS
drag-to-Applications DMG remains a
manual install: the Hub reveals the verified artifact and does not exit as though mounting it had replaced the app. The
in-Hub flow downloads through the persistent task worker, verifies the catalog size and SHA-256, and waits for a second
explicit install action only where a transactional native handoff exists. The Windows NSIS update mode accepts only the
Hub-generated install root, resume token, and process-wait arguments, waits for that process to close, and verifies the
  registered root and ownership marker. Production Windows packages include `KeireInstallWorker`; NSIS is the UI shell,
  while that first-party worker owns canonical path and reparse validation, staging, exact hash validation, registration,
  verification, activation, recovery, and removal. A fresh install accepts only an absent or empty ordinary directory. A
  non-empty destination must have a product-bound JSON receipt and marker whose product ID, random installation ID,
  version, package-manifest fingerprint, receipt SHA-256, canonical registry root, and exact file inventory agree. A
  bounded legacy NSIS path migrates only complete marker-and-registration-bound installations created before receipts.
  The active payload and prior registration remain recoverable until optional shell work succeeds and NSIS asks the
  worker to commit. Interrupted work is rolled back idempotently; uninstall removes only unchanged receipt entries,
  preserves drift and unknown neighbors at every depth, prunes empty owned directories, and never recursively removes
  the selected root. Before commit, NSIS runs `bin/KeireHub.exe --verify-installation` with a 30-second bound. This hidden
  real-executable path validates the exact Hub package manifest and every inventoried resource before application
  construction, without preferences, projects, windows, singleton activation, or network. Run
  `Scripts/Tests/test-hub-installer-windows.ps1` after modifying this contract.

### Development preview retention

Unsigned website previews remain separate from stable catalogs. Publishing a preview copies the validated installer
under an immutable digest-suffixed filename and appends a schema-2 entry to
`Website/assets/preview-downloads.json`; it never reuses a filename or silently replaces an earlier record. Each entry
binds a unique release ID, Hub version, editor version, UTC publication time, platform, architecture, native package
format, byte size, and SHA-256. The current Downloads page selects every verified native format for the newest
available preview version per platform, while
`/downloads/archive/` lists the retained release metadata while the primary download surface stays focused on the
current supported release.

When the native release builder and distribution host are different machines, transfer the installer and its checksum
through a private authenticated channel or a hypervisor shared folder into a non-served incoming directory. Do not add
an upload endpoint to the public GET/HEAD-only website. Recompute the artifact size and SHA-256 on the distribution
host, require them to match the builder's checksum, then copy the verified bytes under a new digest-suffixed name and
append the metadata record with the actual UTC publication time. A transfer never overwrites an existing preview,
changes a signed catalog, or removes an older retained artifact. Public HEAD/download checks and a second digest
comparison complete the handoff.

## Native Editor Installers

The native installer command always runs the complete editor-distribution gate first:

```powershell
winget install NSIS.NSIS
./Scripts/project.ps1 package-installer -Generator vs2022 -Toolset msc
```

```sh
bash Scripts/project.sh package-installer --generator ninja --toolset clang
```

Run it on every release OS; installers are never cross-produced. Windows compiles
`Installer/Windows/KeireEditor.nsi` with NSIS 3 and emits a per-user setup executable. Its wizard presents the license,
  optional shortcuts, an editable destination, and launch-on-finish. The stable uninstall registration supports upgrades.
  As with Hub, production NSIS delegates path validation and mutation to `KeireInstallWorker`. A fresh install accepts only
  an absent or empty ordinary directory; a non-empty destination requires an exact Editor receipt, marker, canonical
  product/uninstall registration, and reparse-free inventory. Replacements are staged beside the destination, validated
  by size and SHA-256, journaled, and retained with the previous registration until shell shortcuts succeed and the worker
  commits. Recovery is idempotent after interruption at any durable phase. Uninstall revalidates each receipt entry at its
  mutation boundary, preserves modified and unrelated files at any depth, prunes only empty owned directories, and never
  recursively removes a custom root. Before commit, NSIS runs `bin/KeireClient.exe --verify-installation` with a
  30-second bound. This hidden real-executable path validates the exact Editor package manifest and every inventoried
  dependency before application construction, without configuration/project loading, windows, or network. The finish
  action, uninstall-registration icon,
Start Menu shortcut, and optional desktop
shortcut all target the editor executable; the installer contains and owns no Hub files or shortcuts. Set
  `KEIRE_WINDOWS_SIGNING_CERT_SHA1` to an Authenticode certificate thumbprint to sign and
verify the final setup executable. `KEIRE_WINDOWS_TIMESTAMP_URL` overrides the default RFC 3161 timestamp service.

The package scripts reject a Dist worker containing the Debug/DebugASan-only interruption seam. Never enable
`KEIRE_INSTALL_WORKER_FAULT_INJECTION` in Release or Dist output. Installer runtime tests use only test-capable builds;
production packages must not contain the `KEIRE_INSTALL_WORKER_INTERRUPT_AFTER` environment-variable string.

macOS uses the platform `hdiutil`, `sips`, and `iconutil` tools to create a self-contained Editor application in a
drag-to-Applications DMG. Set `KEIRE_MACOS_SIGNING_IDENTITY` to Developer ID Application identity text to enable hardened
runtime signing. Set `KEIRE_MACOS_NOTARY_PROFILE` to an `xcrun notarytool` keychain profile to submit, wait for, and
staple notarization. Production macOS publication requires both signing and notarization. The reviewed
`Config/Signing/KeireManagedHost.entitlements` follows Microsoft's
[macOS notarization guidance](https://learn.microsoft.com/en-us/dotnet/core/install/macos-notarization-issues) and is
applied only to the native managed editor host. It enables JIT, unsigned executable memory, dyld environment variables,
and disabled library validation; it never enables `get-task-allow`. The signer works inside-out, verifies the existing
Microsoft .NET Mach-O signatures before and after signing, and rejects any byte change beneath the bundled .NET tree.

`Config/Dependencies.lock` pins `MACOS_DEPLOYMENT_TARGET=12.0`. Premake, CMake dependency/bootstrap builds, FFmpeg,
libsodium, Coral, shader compiler builds, and packaged SDK consumer validation all consume that value. macOS package
gates inspect every non-.NET Mach-O slice with `vtool` or `otool` and reject a minimum-version value other than the
pinned target. The `.app` launchers publish the same value as `LSMinimumSystemVersion`.

Linux uses `dpkg-deb` to create a Debian/Ubuntu package under `/opt`, with a `keire-editor` `/usr/bin` launcher,
freedesktop desktop entry, and hicolor application icon. It does not own `keire-hub` or any Hub desktop integration.
The regular-file launcher executes the explicit editor binary below `/opt`, and the package declares its baseline C,
C++, compiler-runtime, and curl dependencies. Other distributions continue using the validated `.tar.gz` editor
distribution; RPM and repository metadata are separate publication work. Every installer receives a neighboring
SHA-256 file under `Artifacts/`. Signing and publication credentials remain external release secrets and are never
stored in the tree.

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
