# Editor workflow optimization

Optimization is measured per operation; a quick median must not hide a multi-second stall.

## Baseline (2026-09-25)

Historical KeireGame044 Client.log samples, most recent 20 per operation where available:

| Operation | Samples | Median ms | Maximum ms |
| --- | ---: | ---: | ---: |
| Asset creation | 8 | 96.21 | 2557.23 |
| Targeted import | 20 | 75.11 | 1827.28 |
| Full import | 20 | 105.87 | 198.83 |
| Script build | 5 | 3437 | 6765 |

These are historical worker/build timings, not a controlled before/after benchmark or complete UI latency measurement.
The script lifecycle test (successful build followed by invalid source rejection) took 14.626 seconds before changes.

## First change

Generated script project files now use the existing atomic write-if-changed utility. Identical content preserves its
modification time. Changed content still publishes atomically; write failures remain errors. Focused coverage checks
unchanged timestamps, changed bytes, and an invalid destination. No public API or build-server ownership change.

## Remaining measurements

- Separate script generation, SDK startup/restore, compilation, publication and runtime reload.
- Compare cold, warm unchanged, one-file edit, add/delete source and failed build recovery.
- Reproduce asset-creation outliers and measure worker startup, import, publication and UI availability separately.
- Measure project open and Play transition on representative scenes.
- Repeat correctness gates after each optimization. Preserve last-good script generations and asset transactions.

Supporting logs are under Build/Validation/editor-opt-*. No general near-instant workflow claim is made yet.

Validation: Debug KeireTests build passed; two focused cases / 17 assertions passed; clang-format dry-run and git diff --check passed. The lifecycle test took 14.163 seconds after the change. A single before/after sample is insufficient to attribute that difference to this optimization; the timestamp behavior is the verified improvement. Dist editor packaging has not been repeated for this iteration.

## Stable compiler workspace (second iteration)

The compiler previously received a new API reference path and output directory for every generation, forcing warm
builds to recompile unchanged assemblies. Compiler reference bytes and output paths now remain stable. Output paths
are partitioned by generated project graph and configuration. Successful compiler output is copied to the independent
runtime generation; runtime reload never opens the mutable compiler cache.

An instrumented warm build with only stable references took 3961 ms. After also stabilizing output paths it took
2046 ms, and 2400 ms in the final expanded test run. These few samples suggest roughly 39–48% lower warm-build time,
but are not a controlled statistical benchmark. The stronger evidence is that the compiled DLL timestamp remains
unchanged during a no-change build. Actual edited-source compilation still took 4434 ms and remains a priority.
Initial fixture builds (including engine API compilation) took 10.8–13.0 seconds; these are not packaged-editor startup
timings. Debug compilation, three focused tests / 359 assertions, formatting, and diff whitespace checks passed.
Tests include new independent generations, changed source bytes, removed assembly exclusion, last-good publication
after errors, and transactional runtime reload. Dist packaging and interactive timing validation remain pending.

The historical 2557 ms material-creation outlier was operation c230a0c0-1a6f-4255-8de4-0bd52be4eccf. Its retained
request identifies Materials/Ground-Mat.keirematerial, and its result records both the material and shader dependency
88a528e6-5d5c-8730-2782-05e8406f6bfe being imported. Worker startup loaded the source index without a full scan.
This implicates dependency preparation but does not isolate shader compilation time; fresh phase measurements remain
necessary before making another asset-path change.

## Asset operation phase baseline (third iteration)

The existing skipped asset-worker benchmark now reports worker setup and operation durations. Production worker logs
also emit the same phase line, without changing request/result schema. The fixture contains 200 small script assets,
then creates five scenes and imports five batches of 20 scripts. Current Debug results: 586.5 ms creation median and
1325.1 ms import-batch median on the first run; 649.7 ms and 1405.8 ms on the phase-reporting run. These are Debug
worker measurements, not Dist UI response times. Indexed worker setup was approximately 100–140 ms; scene operations
were approximately 400–460 ms. Worker operation time includes source-index publication. Source-index loading succeeded
throughout. Next isolate import/catalog publication and measure the same fixture with an optimized worker.

Validation: current Debug editor-test build passed; isolated-worker regression plus benchmark passed 113 assertions.

A disposable MSBuild traversal experiment was also run under Build/Validation/editor-traversal-benchmark. Corrected
alternating edited-source samples measured approximately 2966–3262 ms for traversal versus 3247–3458 ms for the SDK
wrapper. An initial experiment omitted DefaultTargets=Build and only restored: its apparently fast values were invalid
and are excluded. The production wrapper was not changed because this small benefit does not address most latency.

## Detailed asset phases and optimized-worker comparison

Worker timing now includes time between progress-phase transitions and source-index publication separately. Phase totals
include all work until the next transition; they are not profiler samples of individual functions. Indexed Debug scene
creation measured 606.9 ms median, with approximately 66–76 ms scanning, 68–79 ms importing, 119–139 ms cooking,
71–81 ms publishing, and 35–42 ms index publication in the later samples. The 20-script batch median was 1358.1 ms.
The cooker already reuses existing packs during incremental publication; it does not copy the old pack collection.

The skipped `Asset worker timing benchmark` accepts `KEIRE_BENCHMARK_ASSET_WORKER` pointing to a worker in the normal
`Build/Bin/<configuration>-windows-<architecture>/KeireAssetWorker` layout. Its runtime fixture chooses the matching
FFmpeg configuration from that worker, allowing Debug tests to measure an optimized Dist worker. Without the variable,
it uses the worker matching the test executable as before. This switch affects only the benchmark, not editor behavior.

Optimized-worker result (same fixture, Debug test driver): Dist scene creation median **201.2 ms**, 20-script import
median **530.1 ms**; 2 tests / 136 assertions passed, including the ordinary Debug isolated-worker regression. Dist
worker setup was 31–45 ms, scene operation 64–79 ms, and source-index publication 5–6 ms. End-to-end timings include
worker process startup and the Debug operation-service driver, so they are not pure Dist-editor measurements. This is
configuration comparison, not a speedup from the timing instrumentation. Batch staging (128–133 ms) and publication
(88–104 ms) are larger than cooking (19–22 ms); investigate those transactional writes before optimizing compression.
The Dist worker and Debug editor tests built successfully; formatting and whitespace checks passed. No native UI
performance claim or completed editor-wide optimization is implied by this fixture.

## Cold shader compilation

The benchmark now creates a default surface Shader Graph, then five materials referencing it. Its Windows fixture
stages the pinned shader compiler and DXC runtime beside the isolated worker, matching packaged compiler discovery.
Before format parallelism, the Dist worker measured 4535.8 ms cold graph creation versus 218.5 ms median material
creation. Approximately 4255 ms was spent before import publication in graph validation/compilation. Two fixture cases
passed 172 assertions. This measures a new generated graph, not a cached shared-library material or shader edit.

Graphics shader import previously compiled one binary format at a time (two stages concurrently). It now schedules
all supported formats within the current pass, at most six compiler stage jobs. Futures remain scoped inside the pass,
so exception unwinding joins every task before releasing defines or temporary files. Output collection retains the
specified format order; reflection and ABI validation remain unchanged. The production-import regression now injects
an actual shader compilation failure and verifies a successful subsequent import with matching variant identities.

After format parallelism, the same Dist-worker fixture measured **2313.8 ms** cold graph creation (versus 4535.8 ms
before), **233.5 ms** warm material median, **202.0 ms** scene creation median, and **522.9 ms** per 20-script batch.
This single before/after comparison indicates approximately 49% lower cold-graph latency; it is not a statistical
performance guarantee. The optimized fixture passed 92 assertions. Three production-import cases passed 157 assertions
in both Debug and DebugASan, covering surface graphs, Starter 3D, fullscreen presets, actual compiler failure, and
subsequent recovery. The sanitizer was run with the MSVC ASan runtime directory from the repository launcher on PATH;
an initial direct launch lacked that DLL and did not execute tests. Dist worker, Debug, and DebugASan builds passed.

These measurements do not include the editor's synchronous pre-queue material refresh, project opening, Play startup,
or an interactive packaged-editor session. Those remain explicit next measurements; the goal is still active.

## Startup warmup scan removal

Editor startup opened the cached source index, then `QueueDefaultLitWarmup` immediately called a full `Refresh`,
published the index, and refreshed browser records synchronously. Warmup now queues the target directly. The existing
worker path validates indexed targets and rebuilds its database when the requested target is missing; completion
reloads the published source index and browser records. Shared-library installation still occurs before queuing.
The separate material-create pre-queue refresh is unchanged pending its own measurements and dependency checks.

Three existing source-index tests passed 29 assertions (missing target fallback, offline source edits, and avoiding
unrelated scans during single-asset mutation). The real-worker regression now restores an older index before importing
a newly created target and requires the worker's rescan diagnostic. The benchmark also times indexed open followed by
the redundant full refresh/publication. The rebuilt editor-test checks passed as recorded below.
No end-to-end startup speedup is claimed. Windows testing remains pending:
Rust was open during desktop inventory and the user has not yet confirmed desktop availability.

## Morning test checklist

Use the freshly staged **Dist** Hub and Editor together. In Hub's **Installs** area, locate the development Editor
folder under `Build/Distributions/keire-editor-windows-x86_64-Dist` if it is not already registered.
Open a disposable project first, then the showcase project.

1. Time opening an existing project until the scene and asset browser respond. Repeat once with its cache warm.
2. Create several materials after default-shader warmup; select and edit each immediately. Check that values persist.
3. Create a fresh Shader Graph. Its first compilation still costs seconds; the editor should remain responsive.
4. Import a small batch, rename an asset, undo, and reopen it. Check that thumbnails/catalog entries update correctly.
5. Edit one C# method and save. Check compilation, hot reload, and the new behavior in Play. Introduce one syntax
   error, then fix it; the last good generation must remain usable and recovery must succeed.
6. Enter/stop Play repeatedly, including after a script change. Check input ownership, scene state, and UI rendering.
7. Close/reopen through Hub and repeat a material edit and Play entry. Report any long pause with the matching
   asset-worker timing line and Managed Build diagnostic from the logs.

Local development staging validates package manifests, executable versions, and the bundled SDK/runtime. It is not
release/archive validation, and worker benchmarks do not prove interactive editor responsiveness.

Startup follow-up validation: the rebuilt Debug editor-test binary passed 128 assertions across the real-worker
stale-index recovery case and warmup queue/coalescing lifecycle checks. Core source-index checks passed 29 assertions.
The local morning Dist editor/Hub staging job completed successfully with the latest source. User requested stopping
near 30% remaining usage, reserving packaging
and handoff time; the account showed 45% remaining when that request was received.

The expanded startup benchmark passed 93 assertions. With the Dist editor linker active, the **Debug test driver**
measured 1940.9 ms opening the published index and 6277.3 ms for the subsequent redundant full refresh/publication
on the approximately 300-asset fixture. Worker times were also inflated by concurrent build activity. These samples
show why repeating a synchronous scan is undesirable, but are not isolated Dist editor startup timings or a measured
end-to-end startup speedup. Retake startup measurements after staging and with no build running.

Hidden-window startup smoke: the first fresh-fixture run exited successfully but logged a missing input asset before
its first import completed, so it is not treated as a clean project-open result. After the Dist asset tool successfully
imported all 81 fixture assets, a second `--smoke-project` run exited 0 with no new core errors. It created D3D12 scene
pipelines and queued Default Lit warmup. Total process lifetime was 4023.9 ms while the Hub linker was active; this
includes startup, eight smoke frames, and shutdown, and does not measure sustained interactive readiness or Play.
The fixture is under `Build/Validation/editor-perf-morning`, with logs and JSON reports in `Build/Validation`.

Final morning staging completed for both `keire-editor-windows-x86_64-Dist` and
`keire-hub-windows-x86_64-Dist` under `Build/Distributions`. The packaged editor repeated the hidden-window smoke
successfully: exit 0, no new core errors, 3904.1 ms total process lifetime. This is a startup/shutdown check, not
interactive workflow or Play validation. The player-support bundle passed its integrity verification.

Packaging follow-up: the current player-support script uses a Debug host asset tool to pack and verify approximately
1.9 GB across 664 payload files at compression level 9. Both steps remained CPU-active but took several minutes.
Evaluate an optimized host tool separately; do not remove integrity validation to shorten packaging.

## Two follow-up optimizations (2026-09-25)

Material creation no longer repeats `Refresh` and source-index publication on the editor thread after successful
Default Lit warmup. Warmup completion already reloads the published index. The worker still reads current shader
source and rejects destinations created externally after publication. A new real-worker regression passed 15 assertions
covering successful indexed material creation, preserved external content on collision, and rejection of a shader
invalidated after warmup. Existing material assets survive that failure, and unrelated new files remain unscanned.

The repeated Dist-worker benchmark passed 93 assertions: warm material median 228.1 ms; Debug-driver indexed open
262.8 ms; the removed refresh/publication cost 723.3 ms on the approximately 300-asset fixture. The warm-material
measurement excludes the former editor pre-queue scan, so it is not itself a before/after UI latency result.

Windows and Unix player-support scripts now build/use a Dist host asset tool instead of Debug. Player configurations,
compression level 9, transaction handling, and mandatory archive verification are unchanged. Verifying the exact same
399,038,487-byte support archive took 36.06 seconds with Dist; the earlier Debug process consumed several minutes.
This is a single configuration comparison, not a controlled statistical benchmark.

The updated Windows support script rebuilt/packed/verified the bundle in 294.8 seconds, including launcher overhead
and contention from concurrent editor linking. Its output is byte-identical to the preceding bundle, SHA-256
`a1939c18b83bfdd652de39608384fcef17ed28778c09e08aebb4ec3bd51a6580`.

The focused Windows player-support runtime harness passed. The broader Windows fast suite reached website validation
and failed because this guide was missing from the fallback document inventory. That inventory was corrected; the
website and documentation-source checks then passed independently. The entire fast suite was not rerun afterward.
Unix validation limitations: Git Bash's full fast suite failed the SDK path-alias fixture; its focused support runtime
suite could not find a usable `python3`. WSL Ubuntu could not start because virtualization is disabled. Bash syntax
validation passed; native Linux/macOS runtime validation remains outstanding. SDK consumers were not rebuilt for this
host-tool configuration change; unchanged support bytes and local development-stage checks are not full release gates.

Usage after implementation and focused validation: 58% consumed, 42% remaining. Final Dist staging was still running
when that snapshot was taken.

Final follow-up packaging: both Dist development staging commands exited 0. Both package manifests validated, and
staged Editor, AssetWorker, and Hub SHA-256 hashes matched their Dist build outputs. The packaged editor's hidden
startup smoke exited 0 without new core errors or stderr (5003.8 ms process lifetime, including shutdown). This is
not a material-interaction timing or interactive QA result. Formatting dry-run and `git diff --check` passed.

## Asset reconciliation and targeted refresh (2026-09-26)

Two additional changes reuse the existing database ID index, without adding another cache or changing public APIs:

1. Worker status validation and stale-status pruning after index reload/full refresh use indexed membership checks.
   A rejected batch still leaves every old status unchanged: updates are prepared separately before the final swap.
2. Indexed targeted refresh snapshots only selected source owners under the existing records mutex. Primary IDs use
   direct lookup; generated sub-assets retain their owner-search fallback. A set deduplicates parent/sub-asset targets.
   All owners are validated before any source refresh begins; filesystem work runs outside the records lock.

The focused Debug run passed six cases / 2,100 assertions, including a skipped opt-in benchmark enabled for this run.
Five rounds on 2,000 real indexed files averaged 11.82 ms for the previous linear status-reconciliation strategy and
5.94 ms for the production indexed method. Both paths copy/merge status maps; the reference strategy runs locally
without the production mutex. This is a Debug microbenchmark, not an end-to-end Dist editor latency guarantee.
Targeted-refresh coverage includes invalid identities, duplicate primary/generated identities, stable generated
identity, and avoiding unrelated rescans. No separate targeted-refresh wall-clock speedup is claimed. Later import
stages still take their own full-record snapshots, so this removes one redundant snapshot rather than every full copy.

DebugASan passed five focused correctness cases / 98 assertions (excluding the timing benchmark). Clang-format
dry-run, whitespace checks, website inventory validation, and documentation-source validation passed. The new code
uses the existing record lock and index; it adds no worker threads, long-lived resources, or cache invalidation rules.
The existing blocked-import snapshot-responsiveness regression also passed under DebugASan (one case / six assertions).

Both Dist development stages completed successfully. The refreshed player-support archive passed integrity verification;
both package manifests validated; staged Editor, AssetWorker, and Hub hashes matched the Dist outputs. The packaged
editor's hidden startup smoke exited 0, with no new core errors or stderr, in 5128.2 ms including shutdown. This is
not interactive UI or end-to-end asset-operation validation. The final usage snapshot showed 60% consumed / 40% remaining.

## Dependency traversal and native workflow check (2026-09-26)

Targeted imports build a temporary reverse source-dependency lookup and visit affected source paths once. This replaces
repeated full-list scans on long dependency chains; cycles and diamonds are deduplicated, and selected records retain
their original database order. Targeted imports also avoid copying all records only to clear that copy. Per-record
import result updates use the existing ID index under the existing mutex. No public API or persistent cache is added.

Debug and DebugASan each passed six focused cases / 90 assertions. Coverage includes cyclic and diamond dependencies,
duplicate requests, stable processing order, valid cache hits, unrelated-source exclusion, generated sub-assets,
status updates, and snapshot responsiveness. No wall-clock speedup is claimed for this traversal change.

Native Windows testing created `D:/Projects/KeireProjects/EditorWorkflowQA0926` from the Hub's 3D Starter template
using the preceding Dist development package. Material creation returned to an editable inspector; changing Roughness
to 0.23 survived Play/Stop and was confirmed in the saved asset. The editor closed normally. Logs recorded:

| Operation | Single observed duration |
| --- | ---: |
| Fresh-project script build | 6360 ms |
| Initial full import | 8603.45 ms |
| Default Lit warmup | 139.66 ms |
| Material creation worker execution | 148.48 ms |
| Material edit refresh worker execution | 143.00 ms |
| Play startup | 112.21 ms |

These are logged operation durations, not click-to-ready latency measurements. A Dist rebuild ran concurrently;
cold setup and warm material operations are different workloads. The fresh project's core log contained no errors.

After intentionally rebuilding an external editor development stage, Hub may report that its package manifest differs
from the registered fingerprint. Use **Installs → Refresh registration** and wait for **Verified**. Native testing
confirmed this recovery before project creation. Do not bypass integrity checks or use Remove from Hub as a repair.

Final Dist staging succeeded for Editor and Hub. Both manifests validated; staged Editor, AssetWorker, and Hub
SHA-256 hashes matched their build outputs. The 401,483,076-byte Player Build Support archive passed verification.
The first Hub staging attempt hit a file lock because the Hub hides when launching an editor; reopening it and exiting
normally released the lock, and the retry succeeded. Close the Hub itself before staging, even when no Hub window is visible.

The final package was opened through the verified Hub registration. Asset search found the material, and its inspector
retained Roughness 0.23 after restart. Native testing exercised a script edit, automatic rebuild/reload, an explicit
Build Scripts command, Play/Stop, and clicking the starter button. Its text changed to `QA Ready`. An intentional
`#error` produced the expected Console diagnostic; correcting the source automatically rebuilt and reloaded it, and
a subsequent Play interaction displayed `QA Recovered`. The fixture is left with valid source.

Final-package log samples: warm startup script build 1399 ms; edited-source build 3246 ms; explicit unchanged build
1469 ms; corrected-source recovery build 3333 ms; Play startup 104.97 and 56.94 ms. These are single-operation samples,
not statistical before/after measurements. No unexpected core errors occurred; the intentional compiler error was
reported twice per failed build, a remaining cosmetic diagnostic duplication. Last-good execution during the failed
build was covered by earlier automated tests, not separately exercised in this native session.

This is a tested optimization checkpoint, not complete editor certification. Cold imports and edited-source builds
still take seconds. Small Game views make the template's scaled secondary text difficult to read; the maximized view
was readable. Shader/VFX graph editing, gamepad workflows, broad UI Builder authoring, and a long-session soak were not
repeated in this pass. Final checks include formatting, diff whitespace, website inventory, and documentation sources;
full release archives, SDK consumer gates, and native Linux/macOS validation remain outside this development-stage run.

## Owned compiler reuse and coalesced edits (2026-09-26)

ScriptSystem now owns a Roslyn compiler with a unique private pipe for its build session. It resolves the compiler
through the selected SDK, reuses the process between edits, and stops it after joining the worker during SDK changes
or shutdown. Cancellation and exceptional build failure also stop it; ordinary compiler diagnostics retain the warm
process and preserve the last good generation. Ancestor SDK/build configuration changes restart the compiler.
Unavailable discovery falls back to non-shared compilation. MSBuild node reuse remains disabled, and the engine never
requests a global compiler-server shutdown. Restore and complete-generation validation remain enabled.

A plain MSBuild traversal now restores and builds the real assembly projects without compiling an empty wrapper DLL.
Edits received during compilation are coalesced into one follow-up build instead of synchronously cancelling the active
worker on the UI thread. Intermediate success is not reloaded while a newer request is pending, and queued Play waits
for the newest requested build and reload.

Focused Debug tests passed four cases / 381 assertions; DebugASan passed three cases / 378 assertions. These cover
compiler reuse, SDK/build configuration invalidation, cancellation, owned-process cleanup, stable incremental outputs,
changed/removed assembly inputs, failed-build recovery, and transactional reload. Editor coordinator and Play readiness
checks passed six cases / 60 assertions in both Debug and DebugASan, including repeated edits while a build is active.

Production integration samples with the new compiler and traversal: initial build 7684 ms, unchanged build 1545 ms,
edited build 1637 ms (Debug). DebugASan samples were 7721 / 1777 / 1769 ms. A separate small fixture measured warm edits
at 2949–2993 ms without shared compilation and 1773–1787 ms with sharing; removing the empty wrapper reduced its warm
samples from 1729–1779 ms to 1547–1599 ms. These are local samples, not a statistical guarantee for larger projects.
First-build setup, MSBuild startup, restore, compilation, and runtime reload still have real costs. This is faster
iteration with a responsive pending-build path, not a claim that every editor action is instantaneous.

Native testing of the first refreshed Dist stage reused compiler PID 29744 across multiple builds and confirmed that
it exited after normal editor shutdown. The starter button displayed both changed script labels during Play, and a
new material was selected with an editable inspector immediately after the creation operation completed. Logged
samples: first edited build 2607 ms, subsequent edited build 1374 ms, unchanged build 1264 ms, material creation worker
135.48 ms (queue 5.63 ms), and Play startup 107.99 / 47.09 ms. The first compiler process startup was followed by a
coalesced startup scan/build; its final reported build was 1175 ms, not total project-open latency.

This native check exposed an existing readiness ordering issue: an active last-good runtime bypassed a replacement
build when starting Play. The readiness check now waits for Generating, Compiling, and Publishing even if a runtime
is already active, and also waits between build publication and the matching reload request. Failed builds continue
to permit the last-good runtime. Rapid-edit coalescing and the readiness race have focused automated coverage; the
initial native manual-build attempt completed before the next Play click, so it was not evidence of queued entry.
Asynchronous file detection still means a Play click made before a save is detected can run last-good code and then
hot-reload. Material creation also caused a redundant unchanged script rebuild after source-record reconciliation;
that remaining invalidation inefficiency is recorded rather than hidden by the faster compiler.

The final readiness/coordinator suite passed seven cases / 67 assertions in both Debug and DebugASan. The corresponding
filters were `managed-runtime coordinator*,extracted runtime coordinators*,*Play Mode*` in `KeireEditorTests` after
building that target with `Scripts/Windows/build.ps1`. The compiler/runtime filters were `Managed compiler sessions*,
Managed builds publish*,Managed runtime reload is transactional*`, plus `Third-person sandbox gameplay*` in Debug,
in `KeireTests`. No public scripting API changed. Full release archive/SDK consumer gates, native Linux/macOS runs,
and a long-session soak were not repeated for this pass.

## Script notification and dependency traversal follow-up (2026-09-26)

The editor now deduplicates automatic C# notifications by source contents for its session. Reconciliation after asset
creation therefore does not repeatedly compile the same observed script. Script creation seeds the observation before
requesting its build. Different bytes rebuild even when size and timestamp are unchanged; deletion and recreation are
distinct states. Unreadable sources invalidate the remembered digest and request a normal build. Reads are capped at
16 MiB for this notification optimization; larger files still use the ordinary compiler path. Explicit Build Scripts
and assembly-definition changes remain unconditional. This is notification deduplication, not a compiled-output cache.

The generated MSBuild coordinator now starts from assembly graph roots. SDK project references restore/build their
transitive dependencies, while independent roots remain included. Full restore is still enabled, with no new restore
cache or skipped dependency validation. Diamond selection and real multi-assembly publication/reload have regression
coverage. A small two-project benchmark showed less repeated work but variable timings: final paired samples were
2142 / 2080 ms and 2191 / 2175 ms for all-entry / root-entry traversal. Do not treat these samples as a guaranteed
speedup or compare them directly to earlier native QA timings.

The Core log now separates preparation, compiler setup, MSBuild, and immutable-generation publication. One Debug
edited-build sample was 53.95 / 0.44 / 2116.50 / 8.20 ms (2179 ms total). This identifies MSBuild as the dominant
remaining cost for that fixture. The first build includes API compilation and SDK discovery and is a separate workload.
A tested static-graph restore flag was rejected: warm samples were about 2827–2937 ms versus 2049–2218 ms without it.
A direct-single-project experiment was confounded by concurrent native compilation and was not shipped.

Validation for this follow-up: Debug and DebugASan each passed three core cases / 362 assertions (graph traversal,
successful/failed generation publication, incremental source changes, and transactional reload) and seven editor
cases / 59 assertions (notification deduplication, coordinator behavior, and Play readiness). Clang-format dry-run,
`git diff --check`, website validation, and documentation-source validation passed. Native UI testing was not resumed
after the user stopped Windows control. No subagents or Git mutations were used, and unrelated working-tree changes
were preserved. This pass does not establish sub-second compilation; MSBuild/restore remains the main measured cost.

The follow-up Dist development editor stage completed successfully with updated Debug/Release/Dist player support.
Its player-support archive passed verification. This is a local development stage, not a published release; the full
release archive/SDK consumer gates and native UI follow-up were not repeated.

## Headless follow-up (2026-09-26)

No desktop automation was used. Benchmarks used disposable fixtures; no user project or shared compiler process was
modified. Native builds were kept out of the timed CLI and asset-worker runs. Other applications can still affect
these wall-clock samples, so they are not hard performance guarantees.

Managed API fingerprinting now derives relative identities lexically from paths already produced by its directory
traversal, avoiding repeated filesystem resolution. Source enumeration, sorting, file sizes, and modification-time
checks remain in place. On the same engine source tree, the median of 21 Debug samples fell from 12.8446 ms to
3.9254 ms (69.4% less time for this step). This is a small preparation improvement, not a comparable reduction in
total compilation time. The focused build integration measured 2005 ms unchanged and 1816 ms after an edit; its
first build, including API compilation and discovery, took 10613 ms. These are individual integration samples.

Two MSBuild alternatives were rejected: limiting workers to one and using direct `dotnet msbuild` arguments instead
of `dotnet build`. Six warm alternating pairs per experiment showed no convincing improvement over the existing
path. Restore, analyzers, compiler diagnostics, and transactional publication were not disabled.

The headless asset benchmark used the existing Dist asset worker and Debug editor-test driver. It starts with 200
source files and adds five scenes, 100 imported source files, a shader graph, and five materials. Five-sample medians:

| Operation | Time |
| --- | ---: |
| Scene creation, including worker round trip | 300.524 ms |
| Import 20 files, including worker round trip | 1032.74 ms |
| Warm material creation, including worker round trip | 307.846 ms |
| First shader graph (one sample) | 3097.3 ms |

The separate diagnostic index-open sample was 172.847 ms; an intentionally redundant full refresh/publication took
656.845 ms. These latter operations measure diagnostic driver work, not a claim that the editor repeats that refresh.
Asset-worker coverage passed 93 assertions; editor coordinator, Play readiness, material conflict, and asset-operation
coverage passed 11 cases / 214 assertions. Debug scripting coverage passed four cases / 408 assertions, including
the new API freshness test, owned compiler lifecycle, failed replacement builds, and transactional reload.
The same four scripting cases / 408 assertions also passed under DebugASan. Clang-format dry-run,
`git diff --check`, website validation, and documentation-source validation passed. Windows and Unix script harnesses,
native Linux/macOS execution, and release packaging were not rerun; no launcher scripts or public APIs changed.

To repeat from the repository root after building `KeireTests` and `KeireEditorTests` with the repository launcher:

```powershell
& Build/Bin/Debug-windows-x86_64/KeireTests/KeireTests.exe '--test-case=Managed API fingerprints*,Managed builds publish*'
$env:KEIRE_BENCHMARK_ASSET_WORKER = (Resolve-Path Build/Bin/Dist-windows-x86_64/KeireAssetWorker/KeireAssetWorker.exe).Path
$env:KEIRE_SHADER_COMPILER = (Resolve-Path Build/Tools/ShaderCompiler/KeireShaderCompiler.exe).Path
& Build/Bin/Debug-windows-x86_64/KeireEditorTests/KeireEditorTests.exe '--test-case=Asset worker timing benchmark' '--no-skip'
```

Use a dedicated PowerShell session for the benchmark environment variables. These commands do not launch editor
windows. They do not replace visual UI checks, packaged-game testing, or long-session testing. The existing Dist
stage predates the small fingerprint optimization; this follow-up does not claim a newly packaged build.

## Asset import optimization loop (2026-09-26)

This pass stayed headless and kept all edits on master without Git publication. It removes repeated source-owner
searches in targeted import batches and missing-dependency expansion. Initial requests of 32 or more identities
build an import-local owner index immediately; smaller requests use direct lookup until 32 lookups have occurred.
This bounds repeated scans while avoiding index allocation for common single-asset edits. Primary/subasset matches
retain their original first-owner ordering. Metadata refresh separately builds a subasset-owner index only when a
requested identity is not primary. Full imports move their owned source snapshot instead of duplicating it.

A diagnostic benchmark with 10,000 sources, one subasset per source, and 2,000 mixed primary/subasset targets measured
five-sample Debug medians of 1808.95 ms for repeated linear lookup and 20.1947 ms for index construction plus lookup.
This isolates the lookup algorithms; it is not an end-to-end import speedup or a Dist measurement. Run it explicitly:

```powershell
& Build/Bin/Debug-windows-x86_64/KeireTests/KeireTests.exe '--test-case=Asset owner lookup scaling benchmark' --no-skip
```

The initial always-index implementation was compared with the unchanged staged worker in three alternating pairs.
Each operation number below is the median of the three runs' five-sample medians; shader creation is the median of
three individual samples. No native compilation ran alongside these measurements. Other applications remained active.

| Operation | Before | Initial always-index candidate |
| --- | ---: | ---: |
| Scene creation | 279.523 ms | 264.097 ms |
| Import 20 files | 974.191 ms | 1005.9 ms |
| Warm material creation | 293.644 ms | 306.94 ms |
| First shader graph | 2663.43 ms | 2806.35 ms |

These small-fixture results do not establish an overall latency improvement. They motivated the bounded-lookup
refinement above; the table must not be attributed to that final version. Raw alternating results are in the local,
ignored `Build/Validation/asset-loop-comparison.json`. The test harness required the old executable to be copied into
a temporary configuration-style directory; its hash matched the staged baseline before testing.

Final refined source validation: the Debug asset suites passed 66 cases / 994 assertions, and the focused DebugASan
import suites passed 11 cases / 285 assertions. Coverage includes 40 owners addressed in reverse order through
repeated parent/subasset IDs, stable source ordering, cyclic/diamond dependencies, missing dependencies, invalid
identities, cached imports, and failed-import rollback. Symlink-specific branches could not execute because the
Windows account lacks symlink creation privileges. The existing Dist editor package is not refreshed by these
worker-only builds. Visual testing, Unix execution, full SDK packaging, and a long-session soak were not performed.

The final refined Dist worker rebuilt successfully and passed the headless worker benchmark (93 assertions).
Its last run measured scene creation at 215.96 ms, 20-file import at 573.424 ms, warm material creation at 247.849 ms
(five-sample medians), and first shader creation at 2677.19 ms (one sample). These are observed final timings, not
controlled percentage gains over the earlier runs. Clang-format dry-run, `git diff --check`, website validation,
and documentation-source validation passed. The raw final log is `Build/Validation/asset-loop-final-worker.log`.
