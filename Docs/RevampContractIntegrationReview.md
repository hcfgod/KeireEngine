# Material/shader revamp contract and integration review

Reviewed September 12, 2026 against `Docs/MaterialShaderReplacement.md` and the restored working tree. This is a review-lane handoff, not a production-readiness claim. It intentionally does not amend the shared progress document, README, or changelog.

## September 22 reconciliation

The findings below describe the September 12 snapshot, not an instruction to recreate existing systems. Current
source includes the missing EditorTests translation units, reviewed editor migration with staged source/index/catalog
publication, mixed material selection and texture-transform controls, shared-shader input/package review, and a
bounded compute compiler/device API. `MaterialCookParityTests` now loads and inspects the cooked shader dependency.
The September 22 Dist material/shader/compute tests passed 168 core cases and 133 editor cases.

Remaining work includes production compiler-work-key integration, measured real compiler invocation counts,
in-progress compiler cancellation, full compute/target-preview acceptance, representative cooked-player/SDK visual
parity, performance baselines, and native platform evidence. See [current remaining work](MaterialShaderReplacement.md#current-remaining-work)
and [production acceptance](RevampProductionAcceptance.md) rather than treating all historical P0 findings as open.

## Integrated follow-up — canonical review, September 12, 2026

This section is a later review of the canonical checkout after peer changes began landing. It supersedes a
historical observation only when specifically stated; the findings below remain a separate snapshot from the
seed review. No build was run during the protected Windows UI interval.

### P0 — Ready Shader Graph connection-authoring tests are not linked into KeireEditorTests

`KeireClient/Source/Editor/ShaderGraphDocumentAuthoring.cpp` defines the newly declared
`ShaderGraphDocument::AddConnectedNode` and `ShaderGraphDocument::InsertNode`
(`KeireClient/Include/KeireClient/Editor/ShaderGraphDocument.h:106-107`).
`KeireEditorTests/Source/ShaderGraphConnectionAuthoringTests.cpp:44-141` calls both APIs and is matched by the
test project's `Source/**.cpp` glob. However, `KeireEditorTests/premake5.lua:70` explicitly adds
`ShaderGraphDocument.cpp` but does not add `../KeireClient/Source/Editor/ShaderGraphDocumentAuthoring.cpp`.

The test executable will therefore compile the new test translation unit and fail to link those two symbols.
Add the new client source to the explicit EditorTests source list before claiming the focused connection tests
passed. Add `MaterialSelectionDocument.cpp` there too when the new material-selection unit tests are introduced.

The new compute headers were observed while their owning lane is actively implementing them. They are deliberately
not recorded as a canonical defect in this follow-up until that lane reports a ready handoff.

### P1 — Benchmark fixture identity does not yet identify the exact workload cooked and run

`Scripts/Windows/render-benchmark.ps1:80-133` hashes selected `Assets` roots, but omits
`ProjectSettings/BuildScenes.keiresettings`. The same script copies that file immediately before calling
`Get-MaterialShaderFixture` (`:158-163`), and it controls which scene the cooked player loads. A BuildScenes change
can therefore change the benchmark workload while leaving `matrix.fixture.identitySha256` unchanged. Include that
file and every project/package setting that changes cooking, scene selection, rendering, or shader resolution in the
fixture manifest; update `Scripts/Performance/test-render-benchmark-fixture.ps1` to require them.

The script hashes before invoking AssetTool (`render-benchmark.ps1:163-170`) and does not rehash/compare after the
cook. It is not proof that the bytes read by the cooker equal the recorded fixture if copied-project files change in
that interval. Rehash after cook and fail on a mismatch, or cook from an immutable/locked snapshot. Use an explicit
ordinal, case-defined sort instead of the culture-sensitive PowerShell `Sort-Object path` at `:112` so the manifest
identity stays stable across Windows locale/runtime environments.

### P1 — Cook-parity test verifies only the material payload, not its required runtime shader dependency

`KeireTests/Source/Assets/MaterialCookParityTests.cpp:101-124` mounts only
`CreateMaterialAssetDecoder()` and loads the generated material. It checks the material's shader ID and property
value, but never registers a shader decoder or loads that referenced shader from the cooked catalog. This can pass
with a material that serializes the expected ID while its shader payload is unavailable or unusable to a runtime
renderer.

Extend the fixture with the shader decoder and a runtime `Load<ShaderAsset>` assertion (including the expected
variant formats and dependency availability). The existing count is still a useful importer-cache regression, but
the synthetic importer must remain documented as not proving a real compiler invocation, executable GPU binaries,
or editor/player visual parity.

## Release-blocking findings

### P0 — Migration has no editor workflow or active-database publication boundary

The public migration API can inspect, fingerprint, apply, and recover a filesystem transaction, but no production editor code invokes `InspectShaderGraphMigration`, `ApplyShaderGraphMigration`, or `RecoverShaderGraphMigration`. The only non-core callers are native tests (`KeireTests/Source/Project/MaterialMigrationRecoveryTests.cpp:85-244` and `KeireTests/Source/Rendering/MaterialGraphTests.cpp:852-915`). This leaves no reviewed list, confirmation UX, conflict presentation, or in-session asset refresh for a project opened by the editor.

`ApplyShaderGraphMigration` ends by publishing source and metadata files only (`KeireCore/Source/Project/ShaderGraphMigration.cpp:390-425`). The journal implementation likewise replaces only supplied authoring files (`KeireCore/Source/Project/ProjectFileTransaction.cpp:159-215`). In contrast, asset source-index creation/reloading belongs to a separate `AssetDatabase` operation (`KeireCore/Source/Assets/AssetPipeline.cpp:27-43`). There is no call that validates the complete converted set in an editor-owned database, regenerates/imports it, or atomically advances the source index/runtime catalog.

This directly leaves the documented gate open: `Docs/MaterialShaderReplacement.md:14-15` requires reviewed editor UI plus complete-conversion validation before atomically publishing both source index and runtime catalog.

Required integration change:

- Add an exclusive-project editor upgrade command/panel that presents every report item and diagnostic, requires a deliberate confirmation of the exact review fingerprint, and refuses apply when any item is Invalid or Conflict.
- Hold source monitoring/import publication at a defined safe boundary; stage conversion, metadata, new shader imports, generated subassets, dependency closure, source index, and runtime catalog together. Do not expose source files from the new set while the database/catalog still resolves the old set.
- Recover the same journal before the database accepts assets, surface an actionable recovery state, then refresh and select/reveal the converted source only after the refreshed revision is active.
- Add focused editor/integration tests for reviewed confirmation, stale report, mixed valid/conflict sets, interrupted publication before/after index/catalog staging, rollback, recovery on exclusive reopen, and retained renderer references plus generated `material/default` identities after a live database refresh.

### P0 — Texture tiling/offset and multi-material mixed-value editing are absent from the material model

The public value union is only scalar/vector/color/texture identity (`KeireCore/Include/Keire/Assets/RenderingAssets.h:210`), while `MaterialAssetDefinition` has only a property map and texture set/remove accessors (`:237-249`). `MaterialAuthoringDefinition` adds archived and stable property overrides, but no texture transform or per-material selection/edit transaction (`:264-285`). The editor's `MaterialDocument` represents one `m_Asset` and one definition (`KeireClient/Include/KeireClient/Editor/MaterialDocument.h:105-157`); no material-selection aggregate/mixed-value model is present.

The requested contract explicitly requires multi-selection/mixed transactional undo and texture tiling/offset semantics (`Docs/MaterialShaderReplacement.md:11`). Present vector-property tests exercise generic vectors, not texture transform persistence, material assignment behavior, per-property mixed state, or a transaction over several source files.

Required integration change:

- Introduce a versioned texture-slot value/transform contract (texture asset, scale and offset, clear defaults), carry it through reflection, authoring encoding, runtime material binding, instance inheritance, cook, and player.
- Add a multi-material document/service with explicit mixed state, compatible-shader filtering, an all-or-nothing source transaction, and a single undo/redo operation that rolls back every selected material on any write/import failure.
- Test save/reopen, reset, copy/paste, nested variants, incompatible shader changes, cook/runtime binding, and failures in the middle of a multi-edit. Cover scale/offset values separately from arbitrary vector parameters.

### P0 — Compute remains deliberately disabled; no execution contract exists

The creation menu shows a disabled `Compute (not supported)` item (`KeireClient/Source/Editor/ShaderGraphCreationMenu.cpp:17`). Compiler entry rejects a compute target before graph lowering with "requires the compute-program artifact ABI" (`KeireCore/Source/Rendering/ShaderGraphCompilation.cpp:15-39`). There is no material/shader revamp compute binding, dispatch, readback, managed resource, or device-loss integration in this lane.

Visible disabling is the correct interim behavior, but it does not satisfy the requested complete revamp. The documented gate calls for typed graph/HLSL, backend compilation/execution, bindings, direct/indirect dispatch, hazards, readback, and lifecycle safety (`Docs/MaterialShaderReplacement.md:22-24`).

Required integration change:

- Keep creation disabled until a complete cross-backend compute artifact ABI and resource ownership boundary exist. When enabled, add kernel/resource reflection validation; opaque native/managed resources; binding-set lifetime; direct and indirect dispatch; ordering/hazard rules; completion/readback; cancellation/reload/device-loss/shutdown behavior; and native/managed/backend tests. The production acceptance report must retain Compute as an open gate until those tests run on supported backends.

### P1 — Shared shader pinning has installation and tamper detection, but no upgrade or reproducible dependency lock

The public surface only offers `EnsureSharedShaderLibrary`, `ReadSharedShaderLibrary`, and path classification (`KeireCore/Include/Keire/Project/SharedShaderLibrary.h:25-31`). `ReadSharedShaderLibrary` accepts only one hardcoded version and throws that an explicit supported upgrade is required (`KeireCore/Source/Project/SharedShaderLibrary.cpp:90-121`), but no upgrade API or editor workflow is provided. Installation records only source hash, ID, name, and path (`:145-169`); it does not lock compiler version, compiler options, transitive include digests, package provenance, or visual fixture/baseline identities.

This is short of the documented requirement for package dependency integration, an explicit upgrade workflow, and compiler/include/visual-fixture pinning beyond source bytes (`Docs/MaterialShaderReplacement.md:12-13`).

Required integration change:

- Define a versioned shared-shader package lock containing package provenance/version/digest, all source and include digests, compiler identity/options, target/pass inputs, generated reflection/artifact hashes, and fixture baseline IDs. Validate the complete closure before import/cook.
- Provide an explicit reviewed upgrade operation that computes the old-to-new change set, stages the package and lock atomically, preserves a recoverable previous lock, refreshes the asset database, and clearly distinguishes editable copies from immutable shared sources.
- Add tests for known supported upgrades, rejected unsupported jumps, include/compiler changes, interrupted upgrade recovery, package provenance changes, and cooked fixture reproducibility.

### P1 — Required performance, player/SDK, and non-Windows evidence is still missing

No test or instrumentation found records shader compiler invocation counts for a material value edit. Existing coverage names property-only imports as not compiling programs, but does not count/observe a live editor/import pipeline invocation. The requirement is explicit (`Docs/MaterialShaderReplacement.md:19-21` and `:379-382`). The same contract requires cooked fixture players, both SDK consumers, visual baselines, CPU/GPU/memory measurements, Windows interaction, Linux/Vulkan, and macOS/Metal (`:25-26`), while the document repeatedly marks those as open (for example `:499`).

Required integration change:

- Add a test-visible compiler invocation metric and assert a property-only save/undo/redo/reimport has zero shader compilation, while a shader topology/keyword edit has the expected bounded compilation and stale-job behavior.
- Use the acceptance fixture to cook and run editor/player and both SDK consumers; retain screenshots/image-diff baselines plus hardware, driver, command, duration, CPU/GPU frame, memory, cold/warm compilation, value-edit latency, and variant-count measurements in `Docs/RevampProductionAcceptance.md`.
- Run and record the scoped Windows/D3D12 acceptance and, when hardware is available, Linux/Vulkan and macOS/Metal. Do not infer those results from native unit tests.

## Non-blocking integration observations

- Schema-5 stable property overrides and archived recovery are implemented and have focused native/editor tests. They are a useful compatibility layer, but they do not replace the missing texture-transform or multi-edit contract.
- The shared-library source namespace is protected from direct mutation and tampering is rejected. That is a sound base for the later package/upgrade workflow, not proof of the full lock/reproducibility gate.
- The migration file journal validates paths, backs up old bytes, and supports recovery. The remaining defect is its integration boundary with an already-running editor/database/catalog, rather than the basic byte-file rollback.

## Review validation and unresolved gates

Completed: read-only source/contract review; targeted repository searches for migration callers, shared-library callers, material transform/multi-edit representation, compute support, compile-count instrumentation, and source-index publication. No Windows UI automation, production edits, native build, test execution, or git mutation was performed by this review lane.

Unresolved release gates: all five findings above; full graph/code authoring redesign; cooked player and SDK parity; visual/performance baselines; Linux/Vulkan; and macOS/Metal. These must remain open in production acceptance until implemented and evidenced.
