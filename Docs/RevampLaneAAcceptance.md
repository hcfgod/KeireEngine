# Lane A acceptance — September 15, 2026

## Scope and source

All changes in this continuation use `C:/Users/keith/Desktop/KéireEngine`. Existing staged migration changes remain
untouched. No build, SDK package, player, or GPU result is claimed by preparation alone.

`MaterialCookParityTests.cpp` now repeats four material value edits before cooking. Separate subcases cover the edited
override, reset to the shader default, and a stale property identity with the same display name. Each checks the cooked
runtime value and that value imports and cooking never reinvoke the counting shader fixture importer. The fixture uses
synthetic binary bytes: this is an import dependency regression, not measurement of a real shader compiler or rendering.

## Commands prepared for the exclusive build lease

Run from the canonical checkout. The coordinator must limit Ninja to two jobs; the current Windows build launcher
does not expose a jobs parameter and does not consume `CMAKE_BUILD_PARALLEL_LEVEL` for its own Ninja invocation.

```powershell
./Scripts/project.ps1 build -Generator ninja -Configuration Debug -Toolset msc -Target KeireTests
$env:SDL_VIDEODRIVER = 'dummy'
& ./Build/Bin/Debug-windows-x86_64/KeireTests/KeireTests.exe '--test-case=*property-only materials cook*' --no-colors
```

Repeat the build and focused invocation with `Release` and `DebugASan` as available. The ASan process needs the runtime
directory resolved by `Scripts/Windows/common.ps1`. Restore the previous SDL driver environment before visible UI runs.

```powershell
./Scripts/project.ps1 package -Generator ninja -Configuration Release -Toolset msc -AllowDirty
./Scripts/Windows/render-benchmark.ps1 -Generator ninja -Toolset msc
```

The package launcher runs the Release suite and editor/additive Play smoke checks before producing and extracting the
SDK. Its extracted validation compiles and runs the low-level and managed-entrypoint C++ consumers directly and through
CMake, and validates the source-module consumer. The managed C# API example is compiled; that compilation must not be
reported as execution. `-AllowDirty` identifies the artifact as a development package and preserves uncommitted work.

The benchmark launcher copies the tracked Sandbox fixture into `Build/Benchmarks/Sample`, cooks that copy, verifies its
source identity did not change, and runs the Release player in both presentation modes. It owns its output directories;
those paths must be verified before execution. It does not exercise the disposable interactive acceptance project.

## Validation so far

Prepared disposable project `Temp/MaterialAcceptance/MaterialAcceptance0915LaneA` from the September 11 acceptance
project. Only Assets, ProjectSettings, and README were copied: 94 files / 512,242 bytes. Library and Logs were excluded.
The copied project retains its original identity and startup scene. The before-interaction manifest is
`Temp/RevampLaneA0915Tools/fixture-before.json`, SHA-256
`38d8baf90dc48b26ab496eac5da6da2551b1bff613163b0ffeb5a88b4b295d00`.

The ignored `Temp/RevampLaneA0915Tools/ninja.cmd` wrapper invokes the verified installed Ninja with `-j 2`.
After initializing MSVC, prepend that directory to PATH. Repeating `Enter-WindowsToolEnvironment ninja msc x86_64`
still resolved the wrapper, and `ninja --version` returned 1.13.2. No compilation was performed by this check.

After the AssetTool and Runtime targets are built and the compiler runtime is staged, the disposable project commands are:

```powershell
& ./Build/Bin/Release-windows-x86_64/KeireAssetTool/KeireAssetTool.exe cook `
    --project ./Temp/MaterialAcceptance/MaterialAcceptance0915LaneA `
    --output ./Temp/MaterialAcceptance/Cooked0915LaneA --profile Dist --target windows
& ./Build/Bin/Release-windows-x86_64/KeireRuntime/KeireRuntime.exe `
    --content ./Temp/MaterialAcceptance/Cooked0915LaneA --frames 120
```

This finite rendered player run requires visual/source corroboration for parity; exit success alone does not prove
the material appearance. UI execution is not yet started.

- `clang-format --dry-run --Werror KeireTests/Source/Assets/MaterialCookParityTests.cpp`: passed.
- `git diff --check`: passed (existing documentation line-ending warnings only).
- Native build and focused test: pending the coordinated build lease.
- SDK, cooked player, real compiler invocation measurements, and UI interaction: pending.
- Linux/Vulkan and macOS/Metal: unavailable on this Windows host.

No Vendor changes, staging, commits, CI, or billing changes were made by this lane.
