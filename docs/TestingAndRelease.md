# Testing And Release

Kéire validation is proportional to risk. The repository launchers select the correct dependency variant, refresh
build identity, build the requested target, and run the executable with the expected environment.

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
| Script changes | Matching Windows and Unix regression harnesses |
| Packaging | Release package with direct and CMake low-level/managed consumers |

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

## Script Regression Harnesses

```powershell
./Scripts/Tests/test-windows.ps1
```

```sh
bash Scripts/Tests/test-unix.sh
```

The harnesses exercise argument validation, dependency mappings, generated identity, package-stage requirements,
public dependency isolation, and safe cleanup behavior without performing a full release build.

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

- KeireClient plus the KeireCore and private KeireImGui static libraries;
- every supported public `Keire/` header;
- the required spdlog and SDL static SDK inputs;
- complete third-party license attribution, including Dear ImGui;
- a machine-readable build manifest with locked dependency commits;
- the low-level consumer with its own `main`;
- the managed consumer using the KeireCore-owned entrypoint;
- both consumers through direct compiler commands and the generated CMake package.

Direct compiler validation links the static archives in `KeireCore`, `KeireImGui`, SDL order. The package CMake target
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
