# Material/shader revamp benchmark and visual-fixture evidence

This document is the benchmark/fixture lane record for the material and shader replacement. It does not mark a
production gate complete. Windows editor interaction and GPU capture are reserved for the validation lane; this lane
does not run either activity.

## Prepared fixture identity

The canonical visual workload is the existing `Samples/KeireSandbox` `SandboxShowcase` scene. It places the twelve
Material Lab examples in a fixed three-by-four layout and adds the curated VFX gallery. The source fixture includes:

- `Assets/Examples/MaterialLab`, containing the twelve Shader Graph and twelve material source examples.
- `Assets/Scenes/SandboxShowcase.keirescene` and its metadata, which fix camera, light, mesh, material assignment, and
  VFX placement.
- `Assets/Textures` and `Assets/Vfx`, which supply material and VFX source inputs used by the scene.
- `ProjectSettings/BuildScenes.keiresettings`, which controls the cook's scene closure.

`Scripts/Windows/render-benchmark.ps1` now records a schema-1 `fixture` entry in the durable render matrix. The entry
contains the roots above, every relative file path, byte count, SHA-256, and a SHA-256 over that sorted list. It is
computed from the copied benchmark project immediately before cooking, so the recorded identity is the exact fixture
submitted to the player rather than a later working-tree snapshot. The list uses ordinal path order. The benchmark
rehashes the copied source after cooking and fails if it changed during the cook. A fixture mismatch is evidence of
changed visual inputs, not a renderer regression or improvement.

The existing two-mode run remains unchanged: Release, 300 warm-up frames and 2,000 measured frames for both VSync and
immediate presentation. The matrix already records build and hardware identity, frame timelines, CPU submission and
GPU-retirement timings. It still does not provide shader cold/warm compilation percentiles, allocation/memory
attribution, image comparison, or a GPU timestamp proof for material-specific passes.

The reported timeline summaries are owner update, capture, admission wait, queue delay, render CPU, GPU retirement,
and submit-to-present latency. GPU retirement is fence completion latency, not a GPU timestamp. The report also omits
per-material or per-variant counts, shader compiler invocations/cache state, camera and resolution, and process or
renderer memory, so those facts need separately recorded profiler/capture evidence.

## Controlled Release measurement commands (not executed here)

The supported acceptance command always rebuilds Release targets and cooks a fresh content copy; the wrapper has no
`-SkipBuild` switch. Run it only after the canonical rebuild is complete, while no editor, GPU capture, or other native
build is active, and hold the shared native-build lock for the entire command:

```powershell
$lockPath = "D:\Projects\C++\KéireEngine\Temp\revamp-parallel-native-build.lock"
$lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
try {
    Set-Location "D:\Projects\C++\KéireEngine"
    .\Scripts\Windows\render-benchmark.ps1 -Generator ninja -Toolset msc
}
finally {
    $lock.Dispose()
}
```

This produces `Build\Benchmarks\render-matrix.json` after both visible runtime passes complete. Before archiving it,
verify the matrix status, two distinct presentation modes, Release build identity, hardware identity, 2,000 timeline
entries per mode, and the top-level `fixture.identitySha256`/file list. Copy that matrix, not a manually transcribed
percentile summary, into acceptance evidence.

For a diagnostic rerun with previously validated cooked content and Release binary, the runtime can be launched
directly once per mode. This does not rebuild, cook, validate the content origin, or append the fixture manifest, so it
is not a replacement for the supported command above:

```powershell
& .\Build\Bin\Release-windows-x86_64\KeireRuntime\KeireRuntime.exe `
    --content .\Build\Benchmarks\Content\KeireSandbox `
    --render-benchmark .\Build\Benchmarks\diagnostic-vsync.json --present-mode vsync
```

At a 60 Hz VSync display, the 2,300 collected frames take about 38 seconds before startup and shutdown; the immediate
pass has no fixed wall-clock duration. The full command additionally includes Release builds, shader-compiler build,
and cooking, so total elapsed time is hardware/cache dependent and cannot be inferred from the frame-count contract.

## Required visual scenarios (not executed here)

- Capture the isolated Game view at a fixed 16:9 resolution after warm-up for the complete SandboxShowcase layout.
  Record the fixture identity, backend, GPU/driver, build identity, camera, resolution, exposure, and output color
  configuration beside each image.
- Capture individual representative materials: Studio Paint (PBR controls), Tiled Ceramic (texture/UV), Neon Pulse
  (time-driven emission), Procedural Cutout (opacity), Automotive Clear Coat, Frosted Glass (transmission), Energy
  Dissolve, Vertex Wave, and Iridescent Shield (transparency). Freeze or record deterministic time for animated
  examples before image comparison.
- In the disposable acceptance project, create Material, Shader Graph, Shader Code, and each supported graph target;
  assign by picker and drag/drop; edit a value during Play; Undo/Redo; save and reopen; intentionally break and repair
  a shader; then cook and run the same content. Capture visible expected and observed state at every recovery boundary.
- Record a separate control image for fullscreen/UI/VFX/custom-graphics previews. The current generic mesh previews do
  not demonstrate VFX sample simulation or a custom-pass scene and must not be filed as equivalent evidence.
- Repeat the cooked-player capture on Windows/D3D12, Linux/Vulkan, and macOS/Metal. Use backend-specific tolerances
  only after validating equivalent color management and feature support; retain raw captures where a tolerance rejects
  an image.

## Required performance scenarios (not executed here)

- Cold and warm shader compilation: clear only the declared shader cache between cold samples, then run at least five
  cold and five warm imports of the same fixture. Record median, P95, P99, source/compiler/include hashes, variant
  count, cancellation behavior, and process/environment state.
- Value-edit latency: edit dynamic material values repeatedly after shader warm-up and prove zero shader compilations
  with importer/compiler telemetry. Record editor and player timings separately.
- Render workload: run `Scripts/Windows/render-benchmark.ps1` on a quiet reference machine with no concurrent native
  build or GPU capture. Preserve its `render-matrix.json`, including its new `fixture` object, rather than copying only
  percentile summaries.
- Memory and CPU/GPU frame timing: capture a fixed camera after warm-up with timestamp support confirmed. Report
  process memory, renderer/material allocations when instrumentation exists, frame CPU and GPU percentiles, and the
  number of material variants. Fence latency is not a substitute for GPU execution time.

## Results placeholders

- Fixture identity SHA-256: _not yet recorded by a benchmark run_.
- Windows/D3D12 editor visual capture: _not run by this lane_.
- Windows/D3D12 cooked-player capture: _not run_.
- Linux/Vulkan visual and performance capture: _not run_.
- macOS/Metal visual and performance capture: _not run_.
- Cold/warm compile, value-edit, memory, CPU, and GPU measurements: _not run_.

## Validation record

The fixture helper has an isolated behavior test at `Scripts/Performance/test-render-benchmark-fixture.ps1`. It extracts
only `Get-MaterialShaderFixture` from the benchmark script, then checks the schema, declared roots, sorted unique
entries, SHA-256 shape, and repeated-call determinism against the canonical Sandbox source. It neither bootstraps
dependencies nor builds, cooks, opens an editor, or creates a graphics surface.

The test also rejects a missing fixture root and copies the fixture to a system temporary directory, verifies an
unchanged copy hashes identically, then changes only the copied Material Lab README and requires a different identity.
The temporary copy is deleted in a `finally` block; source assets are never written.

On September 12, 2026, that isolated test passed in Windows PowerShell and PowerShell 7 with 89 input files and source
fixture identity `c0b847dbc2e609999aadf3fef85ad654880acadaccf89663a60e032e467fb341`. This is a source-manifest check,
not a copied-project/cooked-player benchmark result.

An earlier attempt to use the repository-wide Windows script harness was interrupted because that harness begins
dependency/submodule setup outside this lane's scope. Its incomplete Vendor/SDL state was not transferred to the
canonical checkout. Treat that harness as interrupted, not passed; use the isolated fixture test above until a separately
coordinated full-harness window is available.

## Integration notes

- The owner of Windows UI validation should copy the canonical disposable acceptance project into this worktree's
  `Temp` directory before interaction; do not change the canonical project in place.
- Do not run a heavy native build while the canonical rebuild is active. After it finishes, hold the global native-build
  lock for the full build and use Ninja with two jobs. GPU/UI recording must not overlap a build.
- The render benchmark creates its own `Build/Benchmarks` content copy and is intentionally Release-only. It is a
  player performance capture, not an editor UI scenario and not a replacement for the interactive acceptance matrix.
- The lightweight script assertions pin the fixture-provenance contract text. A full Windows/Unix harness run and a real
  Release benchmark remain integration validation, not results claimed by this document.
