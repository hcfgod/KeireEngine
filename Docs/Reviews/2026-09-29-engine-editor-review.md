# Engine and editor review — 29 September 2026

## Scope and assessment

**Overall: 7/10. Engine foundations: 8/10. Editor maturity: 7/10. Broad production readiness: 6/10.**

Kéire has substantial working engine and authoring infrastructure, with explicit ownership, dependency boundaries,
transactional operations, and meaningful failure-path tests. The largest remaining investment should be in protecting
authoring data, validating complete workflows, and proving performance and platform support on repeatable workloads.
Adding more feature names alone would not materially improve this grade.

This is a sampled source and architecture review plus fresh Windows validation, not a line-by-line audit of every
subsystem or certification for a particular game. The starting revision was
`a44e5daf9358da4e2f373d91092d784ea444390e`, version 0.4.4. The checkout already contained edits to `CHANGELOG.md`,
`README.md`, `Scripts/Premake/Common.lua`, and both script regression harnesses. Other work was active in the shared
checkout. Those edits were preserved; validation represents this working tree, not an isolated immutable release.
No implementation changes were made by this review.

Scores are engineering judgments, not percentages of Unity compatibility or code coverage. On this scale, 9–10 means
consistently demonstrated production maturity; 7–8 means a strong usable foundation with important gaps; 5–6 means
substantial implementation with material acceptance work remaining. Missing mobile, console, or networking systems
were not treated as defects in the stated desktop scope.

## Findings, ordered by importance

### R1 — P1: scene saving can overwrite external edits without conflict detection

**Evidence:** `KeireClient/Source/Editor/SceneDocument.cpp:1013`,
`KeireClient/Source/Editor/EditorWorkspaceScene.cpp:315`, and the document's state in
`KeireClient/Include/KeireClient/Editor/SceneDocument.h`.

`SceneDocument::Save()` encodes the in-memory snapshot, atomically replaces the source file, marks the scene saved,
and discards recovery. The inspected path retains no expected source content or revision and performs no comparison
before publication. The workspace save wrapper supplies no such check either. The lighting bake path also saves
through this document.

**Trigger:** open and edit a scene, change its source externally through another editor or a source-control operation,
then save the still-open scene. The in-memory version replaces the external version without a conflict decision.
Atomic replacement protects against a partial write; it does not protect against this stale overwrite.

**Assessment:** confirmed from the source path; not reproduced against a user's project. This deserves priority because
the consequence is lost work. Recovery and undo support do not resolve the missing external-version check.

**Acceptance for a fix:** retain the loaded/saved source fingerprint; reject a stale save before replacement; offer
Reload, Save Copy, and an explicit overwrite choice. Cover external modification, deletion, failed publication, and
recovery preservation with focused document tests. Apply a consistent policy across editable asset documents.

### R2 — P2: editor asset readers allocate unbounded input before decoder limits apply

**Evidence:** `KeireClient/Source/Editor/EditorAssetFileService.cpp:31`,
`KeireClient/Source/Editor/AssetInspectorPanel.cpp:39`,
`KeireClient/Source/Editor/EditorWorkspaceScene.cpp:237`, and
`KeireCore/Source/Scenes/SceneAsset.cpp:898`.

The readers consume an entire file into `vector<char>`, then allocate a second equally sized `vector<byte>`. Scene
decoding enforces a 64 MiB document ceiling only after this read. Inspector and graph-opening paths also use whole-file
readers. The graph include path has a separate size guard, which does not bound the main document read.

**Trigger:** open an oversized or accidentally replaced authoring file. The editor can allocate roughly twice the file
size, plus vector growth and decoder overhead, before rejecting the input. Synchronous reads can also stall the UI.

**Assessment:** confirmed allocation/validation ordering; no deliberate out-of-memory test was run on this workstation.

**Acceptance for a fix:** use one shared bounded reader, enforce the type-specific limit while reading as well as before
allocation, detect incomplete reads, and avoid the duplicate full-size buffer. Test over-limit input and read failure
without large allocations. Keep large supported parsing/import work off the UI thread where practical.

### R3 — release-validation gap: the reference GPU performance gate cannot pass

**Evidence:** `KeireCore/Source/Rendering/RenderFrameExecution.cpp:497`,
`KeireCore/Include/Keire/Rendering/RenderSystem.h:615`, `Config/PerformanceGates.json:21`, and
[`PerformanceGates.md`](../PerformanceGates.md).

The maintained implementation leaves true GPU timing unsupported, while the reference profiles require
`GPU timing supported = 1`. Completion latency and CPU fence wait are correctly identified as different measurements.
This is honest telemetry, but it leaves a critical release-performance claim unverified.

This is not evidence that rendering is slow or incorrect. Functional readback tests cannot establish GPU execution
budgets. Implement backend-supported timestamp measurement within the existing boundary, then retain matching
snapshot/history/hardware metadata from controlled Release workloads. Do not weaken the gate by relabeling latency.

### R4 — architecture limitation: compute is drained at presentation

**Evidence:** `KeireCore/Source/Application.cpp:529`, `Application.cpp:542`,
`KeireCore/Source/Rendering/Compute.cpp:605`, and [`RevampComputeNativeLane.md`](../RevampComputeNativeLane.md).

Once the compute service exists, presentation calls `WaitIdle()`, which reaches `SDL_WaitForGPUIdle`. The separate
compute device and coarse completion boundary limit overlap and renderer resource integration. This is an explicit
interim design, not a newly discovered correctness failure.

Measure real compute workloads first, then replace whole-device drains with dependencies/fences appropriate to actual
consumers. Preserve resource lifetime guarantees. This review did not measure an FPS penalty or prove this is the
dominant bottleneck.

### R5 — maintenance risk: editor coordination remains concentrated

**Evidence:** `KeireClient/Include/KeireClient/EditorWorkspaceLayer.h:99` and the `EditorWorkspace*.cpp` implementation
family. At inspection, the header had 1,071 lines; 23 implementation units contained approximately 11,279 lines.

The workspace implements numerous unrelated panel/controller interfaces and coordinates document, selection, asset,
play, managed-runtime, and rendering state. Existing dedicated coordinators are a positive direction. File splitting
and passing size budgets do not alone establish independent ownership or reduce cross-feature coupling.

Prefer incremental extraction around document persistence, scene transitions, and build/play operations with explicit
inputs and lifetimes. Do not replace the workspace wholesale merely to improve a line-count metric. Require tests
that exercise each extracted service without constructing the complete editor.

### R6 — acceptance gap: current platform and end-to-end evidence is uneven

The repository contains real historical Windows/Linux/package validation and substantial rendered-output evidence.
However, the release documentation describes different accepted versions across platforms, with macOS distribution
gated. Older evidence cannot certify every subsequent runtime/editor change.

For the next production candidate, retain one revision's native build/test/render/package/player evidence across the
advertised platform matrix. Include clean installation and update, Unicode paths, first project, import, edit, play,
save/restart, and packaged-player execution. Foreign-target assembly and a successful compiler invocation are not
native runtime acceptance. This review did not perform that matrix.

## Subsystem grades

| Area | Grade | Reasons and remaining proof |
| --- | --- | --- |
| Core architecture and ownership | 8.5/10 | Explicit application-owned services, private native dependencies, RAII traversal, deferred layer mutations, and thread-affinity contracts. Strongest part of the design. Continued teardown/exception testing matters as services grow. |
| Scenes, gameplay, events, jobs, and time | 8/10 | Clear runtime/session ownership, bounded queues, cancellation and lifecycle handling, and extensive contract tests. Large-world and long-session behavior still needs representative workload evidence. |
| Assets, import, cooking, and identity | 8/10 | Dependency handling, staged publication, catalog/pack validation, and failure recovery are substantive. Authoring read bounds and scene conflict protection remain gaps at the editor boundary. |
| C# scripting and assembly model | 8/10 | Predefined assemblies, arbitrary folder ownership, custom references, assembly references, filters, constraints, version defines, DLL policies, and editor authoring are implemented. Native-host and reload stability must remain acceptance gates. This is not certification of Unity API or binary compatibility. |
| Rendering, lighting, shaders, and materials | 7.5/10 | Considerable rendering and authoring breadth with real GPU readback tests. Material acceptance still lists partial/planned capabilities; timing support and representative GPU budgets remain open. |
| Compute integration | 6/10 | Useful explicit API and validation, but separate-device integration and presentation-wide waits limit the architecture. Need measured consumer-driven synchronization. |
| Animation and rigging | 7.5/10 | Real animation, skinning, rigging, and editor functionality with validation of input structures. Recent optimization is not itself proof of production character/crowd scaling. |
| Physics and navigation | 7.5/10 | Jolt/Recast/Detour remain private; service/world limits, jobs, and lifecycle contracts are visible. Needs game-scale scene streaming, agent, and collision workload acceptance. |
| Audio | 7/10 | Bounded voices/routing, parameter publication, offline processing, and explicit owner-thread operations. Published callback latency/underrun goals are not equivalent to measured native hotplug and long-run acceptance. |
| VFX | 7/10 | Large implemented CPU/GPU authoring/runtime surface with explicit rejection of unsupported features. The manifest still contains disabled operations; equivalence labels do not establish Unity asset compatibility. |
| Editor workflows and data safety | 7/10 | Documents, undo, recovery, play transitions, inspectors, and asset operations have serious implementation and test investment. External-edit protection and complete fresh-project-to-player acceptance need priority. |
| UI, usability, and accessibility | 6.5/10, provisional | Recognizable scene/hierarchy/project layout and a broad authoring surface. This review did not perform fresh keyboard-only, screen-reader, DPI, multi-monitor, or extended artist usability testing; visual polish cannot be confidently scored from source. |
| Diagnostics and performance assurance | 6/10 | Useful profiling, queue and frame statistics, failure diagnostics, and correctly distinguished timing semantics. True GPU timing, controlled scale benchmarks, and sustained performance evidence are incomplete. |
| Automated tests and repository discipline | 8/10 | Large behavioral suites, failure fixtures, GPU tests, source budgets, layout and text checks. Test counts are not coverage; native platform, sanitizer, and end-to-end evidence must complement them. |
| Packaging and platform maturity | 6/10 | Explicit editor/runtime/toolchain separation and SDK/player workflows. Current same-revision native acceptance is uneven; no clean-install or SDK packaging rerun was performed here. |
| Maintainability and documentation | 7/10 | Strong architectural rules, documented limits, and detailed acceptance ledgers. Central workspace coupling and accumulated historical status make the current supported state harder to discover. |

## What is particularly well implemented

`Application`, `LayerStack`, and event ownership are distinct in the inspected source. Deferred mutation is guarded by
traversal lifetime, and failure paths attempt to retain valid state. Jobs have explicit scopes and cancellation instead
of relying on unmanaged background work. Public header scans found no direct SDL/nlohmann/spdlog includes or exposed
mutexes matching the inspected patterns; this is useful boundary evidence, not a proof of every public contract.

The asset pipeline goes beyond basic serialization: cooking validates dependencies, checks pack bounds, stages output,
and publishes after cancellation checks. Test fixtures deliberately exercise corrupt input, missing dependencies,
failed imports, rollback, and invalid installed build support. Error log lines emitted by these fixtures are not
automatically suite failures.

The editor has document/undo/recovery infrastructure and dedicated operation coordinators. The next step is to make
these guarantees consistent across every authoring path. The scene conflict finding should be addressed within that
architecture rather than by adding isolated dialogs around individual toolbar buttons.

## Feature completeness and comparison boundaries

The material ledger currently declares **145 rows: 100 Complete, 9 Partial, 36 Planned**. Important open work includes
material collection integration, multi-closure surfaces, analyzer policy, large instance-count acceptance, and source
control/semantic graph workflows. These are the repository's declared statuses, not results independently measured by
this review. See [`MaterialParityMatrix.md`](../MaterialParityMatrix.md).

The VFX manifest contains **278 entries: 248 labeled Kéire Equivalent and 30 Disabled**; the disabled backlog includes
15 P0 and 8 P1 entries. Its `unityAssetCompatibility` field is false. Disabled examples include vector-field force,
SDF attraction, event triggering, and certain camera/depth operations. The labels describe the engine's manifest,
not an externally verified compatibility percentage. See [`VfxParityManifest.json`](../VfxParityManifest.json).

The assembly work is a meaningful improvement to script organization and dependency control. Reproducing selected
assembly-definition behavior does not establish equivalence in the entire scripting API, package ecosystem, debugger,
serialization, editor extensibility, or mature production tooling. A useful comparison should be based on concrete
workflows and supported contracts, not a single “Unity parity” score.

## Editor usability assessment

A retained 28 September screenshot (`Build/Validation/qa0928-stopped.png`) shows a coherent docked scene, hierarchy,
project browser, and play controls. Dense toolbars and status text warrant testing at different display scales and
with unfamiliar users. The screenshot is historical evidence, not a new interactive session or proof that all panels
currently behave correctly.

Before assigning a confident usability grade above 7, observe a new user completing: create project, import model,
assign material, write a script, repair a compile error, create a prefab, build a small UI, play, recover after restart,
and produce a standalone player. Record completion time, failed steps, unexpected modal interruptions, lost focus,
and terminology confusion. Also test keyboard navigation and discoverability of errors. Source review cannot replace
these observations.

## Validation performed for this review

Fresh results are recorded in the final validation appendix below. Local logs live under
`Build/Reviews/2026-09-29/` and are intentionally ignored by Git.

Build commands use the repository launcher:

```powershell
./Scripts/project.ps1 build -Generator ninja -Configuration Debug -Toolset msc -Target KeireTests
./Scripts/project.ps1 build -Generator ninja -Configuration Debug -Toolset msc -Target KeireEditorTests
./Scripts/project.ps1 build -Generator ninja -Configuration Debug -Toolset msc -Target KeireRenderTests
```

The resulting core and editor Debug executables were invoked with `--no-colors`. The render build was attempted but
remained queued behind another task's project-command lock. This review's waiting command was cancelled before its
build or D3D12/Vulkan runs started; the other task was left undisturbed. Builds respected the shared workspace lock.
Concurrent work makes timing from this session unsuitable as benchmark evidence.

Also run:

```powershell
python Scripts/Tests/check-source-budgets.py
python Scripts/Tests/check-repository-layout.py
python Scripts/Tests/check-text-integrity.py
git -c core.safecrlf=false diff --check
git status --short
```

No C++ source changed, so a changed-file clang-format run is not applicable. This review does not claim a fresh Release,
ASan, native Linux/macOS/Metal, ARM64, full Windows/Unix script harness, Hub suite, SDK package/consumer, installer, or
interactive editor acceptance run. Historical results in other documents are supporting context only.

## Recommended order of work

1. **Protect authoring data.** Address R1 and R2, audit sibling document persistence paths, and test rejected saves,
   disk/read failure, recovery, and external edits. Completion means both versions remain recoverable and the editor
   remains responsive on invalid input.
2. **Establish a reproducible editor acceptance project.** Exercise import, multi-assembly scripting, prefabs,
   animation, UI, play/reload, recovery, and player build from a clean installation. Automate stable operations and
   retain a short explicit manual checklist for native dialogs and interactions.
3. **Close performance evidence gaps.** Add true GPU timing; collect controlled Release cold/warm startup, asset scan,
   import, compile/reload, frame p95/p99, memory, and sustained audio results. Include increasing scene, material,
   character, and asset counts. Optimize only after identifying dominant costs; evaluate the compute drain here.
4. **Certify one release across advertised hosts.** Build and run the same revision natively, including rendered output,
   SDK consumers, a packaged player, clean install, and upgrade/recovery. Keep previews and unavailable lanes explicit.
5. **Reduce editor coupling incrementally.** Give document persistence and state transitions narrow owners; expand
   tests around their boundaries. Avoid a broad rewrite while acceptance work is still incomplete.
6. **Publish a concise current-state ledger.** One versioned page should distinguish implemented, tested on a native
   host, shipped, preview, and unsupported. Link older evidence without presenting it as current acceptance.

These priorities would raise confidence more than another broad batch of features. Kéire already has enough surface
area to justify a stabilization milestone focused on complete, repeatable user workflows.

## Final validation appendix

| Fresh validation | Result |
| --- | --- |
| Debug core build and `KeireTests.exe --no-colors` | Passed: 1,192 cases, 12,625,763 assertions; 4 cases reported skipped. |
| Debug editor build and `KeireEditorTests.exe --no-colors` | Passed: 454 cases, 18,911 assertions; 1 case reported skipped. |
| Debug render build and D3D12/Vulkan tests | Not run: this review's queued command was cancelled while waiting for another task's workspace lock. This is not a GPU test failure. |
| Source-file budgets | Passed: 1,566 first-party files, no budget exceptions. |
| Repository layout | Passed: 535 headers and 936 implementation files checked. |
| Text integrity | Passed. |
| `git -c core.safecrlf=false diff --check` | Passed. |

The core run included managed-host reopen, transactional reload, cross-assembly class, and assembly-policy tests.
The earlier combined-host failure did not recur in this rebuilt Windows suite. The editor's explicitly skipped case
is the opt-in asset-worker timing benchmark. Several core symbolic-link/redirect branches also reported early returns
because this process lacked link-creation privileges; the suite's four-case skipped count does not capture all such
conditional paths. Passing counts must not be interpreted as successful execution of those branches.

Detected host GPU: NVIDIA GeForce RTX 3060, Windows driver `32.0.16.1664`. No fresh GPU functional result or reference
performance capture was produced by this review. Historical rendering results were considered only as supporting
context. The final working-tree check showed the original five modified files plus this new review directory; no
generated build products or logs were introduced as tracked changes.
