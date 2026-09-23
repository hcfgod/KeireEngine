# Material/shader production acceptance

## Camera effects and UI shaders — September 23, 2026

Fullscreen camera stages and UI Document material assignment now execute in the editor and cooked player.
See `RevampShaderGraphicsDetails.md` for the native Windows scenarios and exact focused test results.
The local cooked-player check found and fixed missing camera/UI material dependencies; scene/prefab import cache
versions advance to include these references. All 21 scene tests passed under ASan and optimized Dist (314 assertions each), including
the two focused dependency/cook tests (19 assertions). The disposable project is
`D:/Projects/KeireProjects/MaterialAnimationUserTest0922`; its saved scene contains the tint and custom UI material.

This does not close authored VFX billboard/ribbon execution, arbitrary custom-pass scheduling, continuous material
drag-time flicker, exhaustive lighting/shadow acceptance, or the clean installer/SDK publication gates below.

## Existing-workflow and installer follow-up — September 22, 2026

Reviewed the current material replacement, integration, parity, shader/material, contract-review, and production
readiness documents against their implementation. Historical gaps that now have production callers are reconciled
in those documents; this does not close the remaining compiler, performance, platform, or full visual acceptance gates.

The material Inspector accepted raw code shaders with non-surface targets although its Shader Graph picker already
excluded them. `MaterialInspectorPanel` now supplies the same surface-target predicate to raw shader selection and
resolution in `AssetInspectorPanel`. Unavailable or incompatible references retain saved overrides. Focused coverage
in `MaterialIdentityTests` checks rejection, source preservation, and recovery when compatible reflection returns.

Windows testing in `D:/Projects/KeireProjects/MaterialAnimationUserTest0922` reproduced the old picker showing both
`PickerMaterial` and `PickerFullscreen`. Material edits and undo retained Roughness 0.85. Creating an instance took
87.730 ms of worker execution; its Metallic 0.5 override was saved, and assignment through the Cube's Mesh Renderer
slot survived scene save/reopen. This warm sample is not a cold-creation benchmark. One earlier unmonitored editor
exit was not reproduced during the monitored editing session, which closed normally with exit code 0. Continuous
drag-time black flicker has not been established as resolved by these observations.

After rebuilding Dist `KeireClient`, Windows control confirmed `PickerMaterial` remained selectable and
`PickerFullscreen` was absent. Assigning the compatible code shader refreshed the preview; Undo restored the original
shared Lit shader, pink base color, and Roughness 0.85. Selecting the instance after restart showed Metallic 0.5 and
the inherited values intact. No default reset occurred in these tested actions.
During Play, changing the instance's Metallic to 0.7 persisted after stopping and discarding simulation changes;
Undo with the Inspector focused restored 0.5. The existing Fox animation script passed all seven playback checks
again. The editor closed with exit code 0. Tested client SHA-256:
`3AE55A432DD290A6854FA7D12E472287B6862AA41E5D282C90D077B87ED30B7E`.

The installer slowdown came from pruning nonempty parent directories after each owned file removal: Windows retried
`ERROR_DIR_NOT_EMPTY` for about 2.575 seconds per parent. `InstallTransaction` now skips known nonempty parents before
the anchored removal operation. Ownership verification, file hashing, drift rejection, rollback, and user-file
preservation remain enforced. Both NSIS templates show the worker phase; the Windows runtime harness records timing
and includes 32 sibling files to exercise the pathological case.

- Dist installer transaction selection: 15 cases / 225 assertions passed.
- Debug and DebugASan install-worker suites: 19 cases / 247 assertions each passed, including pruning and failure paths.
- Hub and Editor NSIS worker runtime matrices passed. Hub fresh install/update/uninstall measured 2.706/8.671/4.054
  seconds; Editor measured 8.171/3.927/3.212 seconds. These isolated fixtures ran under concurrent machine load and
  are not production payload benchmarks. Intentional timeout tests still wait for their configured timeout.
- Windows control exercised the actual Hub NSIS wizard with isolated test registration and a disposable destination:
  install and uninstall both reached completion within the first approximately nine-second observation.
- Dist core material/shader/compute selection: 168 cases / 3,404 assertions passed.
- Dist editor material/shader/graph selection: 133 cases / 14,247 assertions passed. The new code-shader rejection
  regression also passed independently: 1 case / 25 assertions.
- Source budgets passed for 1,512 first-party files; changed C++ formatting and `git diff --check` passed.
- Documentation validation passed for 99 canonical guides, 9 navigation groups, 31 Mermaid diagrams, local links,
  and 11 schema contracts. This checks documentation structure/contracts, not every guide's interactive workflow.
- Dist `KeireClient` and `KeireInstallWorker` builds passed through `Scripts/project.ps1`. Both
  `test-hub-package-windows.ps1` and `test-editor-package-windows.ps1` passed; these package-contract harnesses do not
  replace compilation/execution of the two extracted SDK consumers.
- Logs are under `Build/Validation`: `install-transactions-{dist,debug,asan}.log`,
  `nsis-{hub,editor}-performance.log`, and `material-readiness-{core,editor}.log`.

The public download has not been republished with these changes. Full production-sized package timing, both packaged
SDK consumers, Linux/macOS validation, and the remaining combined material/lighting visual matrix are still open.

## Migration follow-up validation — September 15, 2026

Continued in the canonical checkout with one agent. The existing staged migration fix and regression changes were
preserved. Validation now uses a unique system temporary directory, avoiding additional project-path nesting;
durable recovery journals and backups remain in the project. Added failure-path scratch cleanup coverage.

- `./Scripts/project.ps1 build -Generator ninja -Configuration Debug -Toolset msc -Target KeireTests` passed.
- The equivalent build with `-Configuration DebugASan` passed. Both configurations rebuilt 482 build steps.
- With `SDL_VIDEODRIVER=dummy`, `KeireTests.exe --test-case="*material migration*,*material publication*"` passed
  17 cases / 118 assertions in both Debug and DebugASan. The latter used the MSVC ASan runtime directory from
  `Scripts/Windows/common.ps1`. Logs: `Temp/continue-migration-debug.log` and `Temp/continue-migration-asan.log`.
- Debug `KeireTests.exe --test-case="*material*,*shader*,*compute*" --success --no-colors` passed 164 cases /
  3,231 assertions. Log: `Temp/continue-rendering-debug-diagnostic.log`. An earlier quiet run was interrupted to
  enable diagnostic output during the concurrent sanitizer build; it is not counted as passing evidence.
- `clang-format --dry-run --Werror` passed for both changed C++ files. `python Scripts/Tests/check-source-budgets.py`
  passed for 1,506 files. Working-tree and staged `git diff --check` passed; final status contained only the two
  existing C++ changes and intended documentation edits. No staging or commits were performed.

This closes the previously failing Windows migration regression. It does not establish general Windows long-path
support. Release, editor interaction, opt-in GPU execution, SDK packaging, Linux/Vulkan, and macOS/Metal were not
rerun in this follow-up and remain subject to the broader acceptance gates below.

## Single-agent integrated validation — September 12, 2026

All four additional tasks and their twelve subagents are stopped. These results use the canonical checkout and the
disposable project `Temp/MaterialAcceptance/MaterialAcceptance0911`. Debug/MSVC integrated builds succeeded for
KeireTests, KeireEditorTests and KeireEditorDev. The final UI client SHA-256 was
`f3e22dfe2f2455668679f5f44ba7ac2baefa8731d816b7ce53d01c57e1fbcb48`.

- Focused editor tests: 129 cases / 14,156 assertions passed (`Temp/revamp-single-editor-final.log`).
- Native failure retests: 6/7 cases passed (`Temp/revamp-single-native-retest2.log`). The remaining migration failure
  reports a missing reused asset pack at a 286-character staged Windows path. Long-path handling is a suspected
  cause, not yet proven. Changing validation to a full import did not eliminate the failure.
- Actual D3D12 compute execution: one case / 2,074 assertions passed (`Temp/revamp-single-compute-D3D12.log`).
  Direct/indirect buffer dispatch and readback were exercised. Shared renderer scheduling/device-loss injection
  remain unvalidated. Managed API tests passed Debug and Release; native Release/ASan still require execution.
- Windows control: creating `SoloRevealAcceptance0912` with a nonmatching Project search cleared that search and
  scrolled the selected tile into view. Its source GUID is `79bd1d4e-9333-42aa-9245-d08019c65992`.
- Windows control: the original five-pin Unlit Frame All case passed at roughly 460-pixel panel width. The taller
  Lit output exposed the 35% minimum-zoom limit; after changing it to the existing 20% canvas minimum and rebuilding,
  its full output fit inside the short viewport. A focused deterministic tall-node test also passed.
- Save on close and reopen preserved the disposable graph's Unlit selection. A subsequent Lit edit was saved on
  final close. The test editor exited successfully; no acceptance editor or build remains running.
- Recorded functional timings: initial automatic import queue 10.191 ms / execution 40,391.846 ms; shader creation
  queue 2.242 ms / execution 3,709.094 ms (`Temp/revamp-single-ui.log`). On reopen, targeted import queue 11.553 ms /
  execution 2,569.626 ms (`Temp/revamp-single-ui-framing.log`). These are worker durations, not controlled latency
  benchmarks. The long initial import and repeat generated-source refreshes need investigation.
- Source budgets passed for 1,506 first-party files; changed C++ formatting and `git diff --check` passed.

This is focused acceptance, not exhaustive completion. The scenarios and platform gates below remain open unless
explicitly covered by the evidence above. No new staging, commits, dependency changes, or CI changes were performed.

## Provenance and scope

Validation worktree: `C:/Users/keith/.codex/worktrees/7475/KéireEngine`.
Seed: `C:/Users/keith/Desktop/KéireEngine/Temp/revamp-parallel-seed-0912`.
The clean worktree HEAD matched seed base `7caeabaeffb0da572635fd7d6e2343376fe62f52`.
`git apply --check` passed before applying the seed without an index update. Every untracked archive destination
was checked to remain inside the worktree and to be absent before extraction. Seed modifications include unrelated
historical work; they are baseline inputs rather than additions by this acceptance lane.

The canonical disposable project was copied to
`Temp/MaterialAcceptance/MaterialAcceptance0911` in this worktree. The canonical project remains the source fixture.
Historical results in `MaterialShaderReplacement.md` are retained as historical evidence and are not rerun claims.

## Current execution state

The user subsequently authorized all lanes to work directly in `C:/Users/keith/Desktop/KéireEngine`.
Only this lane's incremental files were transferred there; the old worktree and copied project remain available.
The coordinator confirmed the canonical seed rebuild completed all 666 steps and exited successfully. Current
source edits by peer lanes can differ from these seed binaries; their validation requires a subsequent build.

The benchmark worker's broad Windows script harness unexpectedly initialized and partially altered `Vendor/SDL`
in the old isolated worktree. The worker stopped its own child process tree. That harness was interrupted, did not
pass, and its Vendor state was not transferred or reset. Future broad harness execution requires inspecting bootstrap
and cleanup behavior first.

- Windows Computer Use initialized successfully and enumerated available applications on September 12, 2026.
  No editor window was running at that observation. No new interactive pass is recorded yet.
- The canonical rebuild remained active at the initial observation: 574 of 666 steps, with Ninja `-j 2`.
  Its preceding unrestricted rebuild was interrupted, as recorded in the seed. This lane has not started a native build.
- Native build execution must hold an exclusive open handle to
  `C:/Users/keith/Desktop/KéireEngine/Temp/revamp-parallel-native-build.lock` and use at most two Ninja jobs.
  Benchmark captures must also have no overlapping native build.

## Seed runtime observations — September 12, 2026

The completed canonical Debug/MSVC seed client SHA-256 was
`bedea09e40f853cc747c0c4c94332ea1d69721566252684084f5f65ef95da42c`.
The editor used the copied project in the old worktree; all command logs remained in canonical `Temp`.
The exclusive native lock was held throughout runtime staging, tests and UI, and released after the editor exited.
Repository managed-host and pinned FFmpeg staging helpers supplied missing runtime dependencies after raw Ninja.
An initial test start failed with Windows missing-DLL status `0xC0000135`; after staging, the actual test run passed.

- Passed: final rebuilt `KeireEditorTests.exe` with
  `--test-case=*asset browser*,*Asset Browser*,*Shader Graph*,*Shader graph*`, and `SDL_VIDEODRIVER=dummy`
  only in the test process environment: 47 cases / 13,225 assertions, zero failures.
  Log: `Temp/revamp-production-seed-focused.log`.
- Passed: Project search `Keyword`, then Plus > Shader Graph > Fullscreen Effect created
  `RevealLayoutAcceptance0912`; search cleared, graph and Inspector selected the new shader, diagnostics were clear,
  and the flat white fullscreen preview appeared. The source hash after the session was
  `78722e22d79c5429ad5c1f1611da3e23310a022f20ce0d5ab16af1330ad19213`.
- Failed full reveal visibility: the three-column Project grid stayed at the initial folders, while the selected
  shader tile was offscreen. Scrolling revealed its selected tile. The materials lane owns this follow-up.
- Passed narrow width: resizing the floating graph from about 1,100 to 460 pixels wrapped the toolbar, kept canvas
  width within the panel and omitted the preview. Closing and reopening at that width framed all five output pins.
- Failed explicit Frame All: after resize, clicking Frame All enlarged the output beyond the roughly 217-pixel-high
  visible canvas and clipped its lower pins. The same panel's first-open framing fit them. The shader lane owns this
  follow-up; initial framing and explicit navigation must remain distinct acceptance results.
- Creation operation `e8d770ef-a3ec-4e0f-8cff-907f5f90761f` recorded queue 1.728 ms / execution 707.698 ms.
  Log: `Temp/revamp-production-seed-ui.stdout.log`. This single functional sample is not a controlled benchmark.

These seed results do not validate subsequent material, graph, or compute source changes in canonical checkout.

## Remaining scenario execution

Each scenario requires the executable/source provenance, project fixture, expected and observed behavior,
source-byte or runtime corroboration where applicable, and an explicit pass/fail/open result.

1. Search reveal: keep a Project search active, create a differently named shader, and verify search clears and
   the created asset is visible and selected. Repeat within the current folder and across folders.
2. Narrow Shader Graph: open a new graph, inspect first-open framing, dock narrowly, and confirm the canvas fits,
   toolbar controls wrap, and named output defaults remain accessible. Save/reopen and compare serialized positions.
3. Creation and assignment: create ordinary material, graph shader, code shader, and subgraph through supported menus;
   assign a shader to a material and drag the material onto a renderer. Verify saved references and rendered result.
4. Property persistence: edit scalar, HDR color, texture and supported surface values; exercise reset and clipboard;
   edit during Play, Stop, Undo/Redo, Save and reopen. Verify stable property identities and source changes.
5. Shader failure recovery: break a disposable shader, verify diagnostics and last-good behavior, repair and reimport,
   and confirm values/references survive. Check asynchronous stale completion and cancellation where exposed.
6. Keywords and variants: first-click keyword change, unavailable variant, coalescing, cancellation, inherited values,
   and exact source restoration through Undo/Redo. Distinguish value import from shader compilation counts.
7. Graph/code authoring: node/pin diagnostics, subgraph reuse, generated source, target restrictions and supported
   preview behavior. VFX and custom-pass previews require target-specific evidence rather than a generic sphere.
8. Migration integration: reviewed conversion, apply, stale-review rejection, reload and source/catalog consistency.
   Pending implementation from the materials lane; do not execute against the seed as if the workflow exists.
9. Shared shader upgrade integration: review explicit upgrade, pin preservation and rejection of modified sources.
   Pending implementation from the materials lane.
10. Multi-material integration: mixed selection values, compatible-only edits, one transactional Undo and failure
    rollback. Pending implementation from the materials lane.
11. Compute: record menu availability accurately. Creation, compilation, dispatch, readback, resource lifetime,
    reload and device-loss acceptance remain open until executable implementation exists.
12. Cook/player and SDK: use the same material/variant/graph/code fixtures, verify dependency closure and rendered
    parity in a cooked player, and build/run both low-level and managed-entrypoint SDK consumers directly and via CMake.
13. Performance: record hardware/build/fixture identities and isolated repeated cold/warm compilation, value-edit
    latency, compiler invocation counts, variant counts, CPU/GPU frame time and memory. Worker execution timing alone
    is not click-to-preview latency or a GPU measurement.

## Evidence lanes and unresolved gates

Companion lane documents will record deterministic test/cook/SDK coverage, benchmark fixture preparation, and
contract/integration review. No full production-acceptance claim is made by preparation or isolated tests.
Linux/Vulkan and macOS/Metal remain open without the required hosts and hardware.

## Additions relative to seed

- `Docs/RevampProductionAcceptance.md`: acceptance provenance, pending scenarios and lane integration record.
- `Docs/RevampContractIntegrationReview.md`: dated seed and integration contract review.
- `Docs/RevampBenchmarkFixtureEvidence.md`: fixture provenance, measurements still required, and interrupted harness.
- `Docs/RevampTestCookSdkEvidence.md`: deterministic cook test scope and remaining packaged-consumer gates.
- `KeireTests/Source/Assets/MaterialCookParityTests.cpp`: synthetic shader fixture tests material cook dependency and
  cooked runtime value preservation without additional fixture importer calls; native execution remains pending.
- `Scripts/Windows/render-benchmark.ps1`: deterministic fixture manifest included in recorded benchmark output.
- `Scripts/Performance/test-render-benchmark-fixture.ps1`: isolated manifest behavior validation.
- `Scripts/Tests/test-windows.ps1` and `Scripts/Tests/test-unix.sh`: fixture contract assertions.
- The disposable project copy resides under ignored `Temp` and is not a source change.

Main progress, README and changelog integration is deferred until implementation and evidence are ready.
