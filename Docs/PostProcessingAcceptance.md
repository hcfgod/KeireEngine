# Post-processing editor acceptance

This is the execution record for the first post-processing batch, not a completed validation report.
All rows start untested. The implementation and remaining scope are tracked in
[PlanetaryWaterPostProcessingPlan.md](PlanetaryWaterPostProcessingPlan.md).

## Test setup

- Record the exact tested revision and uncommitted diff, executable build, graphics backend, GPU, resolution,
  render scale, quality settings and cold/warm asset state with the results.
- Use a separate QA project. Preserve the authored planetary and CloudHdrShowcase projects.
- Use a scene with neutral, colored, dark, bright/emissive, reflective and transparent objects, foreground
  and background geometry, a camera-overlay HUD, and visible sky/clouds. Keep camera and lighting fixed
  for comparisons. Repeat selected cases while moving the camera and playing the day/night preview.
- Establish a baseline with no volume. Add a global volume with a new profile whose overrides are all off;
  it must preserve the baseline. Add local box and sphere volumes for crossing and overlap tests.
- Record failures with exact steps and a screenshot or recording. A later fix needs a new result on the
  rebuilt executable; do not overwrite the original failure evidence.

## Effects

For every implemented effect, test override off, explicitly disabled, enabled, useful low/high values,
reset, save/reopen, and combinations. Check Scene and Game views separately. Live observation is required
for animated effects and transitions; screenshots cannot prove absence of flicker or trails.

| Case | Required observation | Status / evidence |
| --- | --- | --- |
| Exposure | Sweep -2, 0 and +2 EV; brightness changes coherently without duplicate exposure; reset restores baseline. | Partial: rebuilt Scene-view +8/-10 EV crash retest and discard to 0 passed; required useful-range, Game-view and save/reopen checks pending. See Rebuilt exposure UI verification. |
| Bloom | Emissive objects bloom; intensity, threshold, soft knee and radius visibly respond; disabled profile bloom overrides environment bloom. | Partial: Scene intensity 0.1 to 1.1, one-drag Undo, readable tooltip and Discard verified. Threshold, knee, radius, Game and persistence pending. See Bloom intensity visual check. |
| Color grading | Temperature, tint, contrast, saturation and each RGB gain act independently; neutral values preserve the original image. | Partial: neutral Scene appearance, zero saturation in Scene/Game, Reset and Discard verified. Monochrome Save/import/cook/standalone persistence passed. Other parameter accuracy and persistence remain pending. |
| Vignette | Intensity, smoothness and roundness change the edge falloff; resize and aspect changes remain stable. | Partial: Scene intensity, one-drag Undo and Discard verified. Shape, resize, Game and persistence pending. |
| Film grain | Intensity and highlight response work; grain animates without temporal-history accumulation or changing the HUD. | Partial: enable/disable baseline smoke check in Scene only. Parameter response, animation, history and HUD isolation pending. |
| Chromatic aberration | Edge separation responds to pixel strength at different resolutions; zero restores the original sampling. | Partial: 1 to 20 px response in Scene/Game and Discard restoration verified. Zero, resolution changes and persistence pending. See Chromatic visual check. |
| Full initial stack | All implemented effects combine without clipping artifacts, stale frames, double bloom or HUD grading. | Not tested |
| Legacy camera materials | Before-tone-map, after-tone-map and after-UI hooks retain their authored order alongside profiles. | Not tested |
| Depth of field | Focus, aperture and focal length must affect depth-dependent blur with stable near/far transitions. | Implemented; Scene/Game focus and disable recovery, combined-lens resize smoke checks passed. Foreground coverage, parameter range, motion and GPU cost remain open. |
| Motion blur | Velocity-dependent blur must handle camera cuts, object motion and origin shifts without long trails. | Implemented; stationary/zoom smoke check passed. Live streak quality, object motion and cut/rebase visual acceptance remain open. |
| LUT and custom stacks | Resource selection, ordering, invalid inputs and extension contracts require implementation and testing. | LUT identity/swap-channel visual checks passed. Custom stacks are now implemented; core profile/volume/evaluation suites passed 30 cases / 5,505 assertions. Editor custom-stack tests are building. Stack GPU ordering, numerical LUT validation, recovery and standalone remain open. |

## Authoring and lifecycle

| Case | Required observation | Status / evidence |
| --- | --- | --- |
| Fresh project | Create profile, create volume, assign profile, author a look, save, restart and reproduce the result. | Not tested |
| Asset Browser | Double-click opens the panel; create, rename, duplicate and move preserve references and usable labels. | Not tested |
| Panel navigation | All tabs, search, numeric entry, dragging, override toggles, reset, keyboard focus and scrolling work at narrow and maximized sizes. | Not tested |
| Undo/Redo | Changes and live preview reverse/reapply; unrelated scene history is not consumed. Continuous drags have usable history granularity. | Partial: Warm preset Undo/Redo and bloom/vignette single-drag Undo verified. Custom-stack history and unrelated-scene-history UI isolation remain open. |
| Unsaved profile switch | Switching or creating another profile cannot silently discard local changes. | Not tested |
| Exit | Save, Discard and Cancel each behave correctly; save failure keeps the editor and edits available. | Not tested |
| External edit | Saving detects changed source; reload refuses to destroy local edits; discard/reload adopts the external version. | Not tested |
| Missing/invalid source | Diagnostics are actionable; last-good runtime data remains usable; closing cannot strand unsaved preview data. | Not tested |
| Preview lifecycle | Edits update assigned volumes without writing source; discard and close restore authored values, including after failed publication. | Partial: live exposure edits and discard restored the saved Scene-view appearance; close and publication-failure UI checks pending. |
| Import overlap | Save then edit again while importing; completion must not replace the newer unsaved preview with older settings. | Not tested |
| Global/local controls | Global mode, shape, bounds, weight, priority and blend distance match the rendered influence; irrelevant controls are clear. | Partial: global/local sphere, weight 0.5 and restoration verified in Scene view. Box, priority and remaining control combinations pending. |
| Bounds and blends | Box/sphere boundaries, transforms, scaled volumes and overlapping priorities blend smoothly with understandable gizmos. | Partial: Scene camera sphere exit showed partial then full color, with bounds gizmo. Box, transforms and overlapping-priority visual checks pending. |
| Camera participation | Enabled toggle and volume layers isolate effects per camera; inactive entities and disabled components contribute nothing. | Partial: disabling the component removed grading; Undo restored it. Layer masks and inactive entities remain visually untested. |
| Play and standalone | Edit, Play, stop, restart and standalone agree for the same camera/settings; temporary state does not leak. | Partial: neutral material baseline and UI-saved Monochrome profile verified in rebuilt standalone. Full effect parity, Play lifecycle and temporary-state isolation remain open. |
| Large coordinates | Translate scene and camera together to positive/negative distant origins; influence and visuals stay equivalent. | Not tested |
| Resource lifecycle | Multiple views, resize, camera cuts, reload and device recovery avoid stale effects and leaked resources. | Not tested |

## Performance and retests

Capture comparable disabled/enabled measurements with a stationary camera, camera movement, animated environment,
multiple overlapping volumes and multiple views. Report CPU/GPU timing where available, median/p95/p99 frame
times, allocation behavior and memory after repeated profile switches. Include startup and first-effect activation.
Retest the affected combinations after fixes; record unresolved risks instead of assigning a blanket pass.

## Automated evidence — November 1, 2026

- Official Ninja DevDist `KeireEditorTests` build passed, including 251 compilation/link steps. Existing
  alignment and discarded-`nodiscard` warnings remain; this was not a warning-free build.
- The complete `PostProcessDocumentTests.cpp`, `PostProcessPreviewTests.cpp`, `EditorCoordinatorTests.cpp`
  and `EditorUserWorkflowTests.cpp` filter passed: 32 test cases and 606 assertions. This includes failed
  save preservation, invalid-source rejection, publication rollback, saved-baseline restoration, workspace
  shutdown and Asset Browser routing. The filter used SDL's dummy video driver.
- Local logs: `Build/Validation/post-processing-batch1-editor-build.log` and
  `Build/Validation/post-processing-batch1-editor-tests.log` (generated, not committed).
- Official Ninja DevDist `KeireEditorDev` build passed, including the editor panel and workspace integration.
- Built-in shader generation completed successfully; GPU visual validation remains pending.
- Isolated source-only QA project prepared at `D:/Projects/KeireValidation/PostProcessingBatch1`.
- Core post-processing tests, GPU visual validation, sanitizer coverage and
  all hands-on rows above remain pending. No editor, standalone or performance acceptance has been executed
  for this batch. Automated results do not change visual/UI rows to passed.

## First hands-on session — November 1, 2026

- Launched the isolated QA project with the official Ninja DevDist editor launcher. Incremental build
  reported no work; the editor opened maximized at 3440 x 1392 capture size.
- Window > Post Processing successfully opened the dedicated floating panel. The empty state displayed
  the profile selector, name field, Create Profile button and volume-assignment guidance.
- Clicked Create Profile with the default name while the fresh project's startup import was active.
  The status changed to Creating, but no profile source had appeared during subsequent checks. The
  worker continued progressing through the startup scene's material dependencies (2 through 6 completed
  imports), so this was a queued-action delay rather than an observed editor freeze.
- Finding PP-UI-001: profile creation during cold startup needs clearer queued/progress feedback and a
  review of interactive scheduling. Do not label creation or live preview passed until completion.
- Initial scene objects used white fallback materials while imports were pending. Effect appearance
  testing must wait for the authored materials; these captures are not a valid grading/bloom baseline.
- Live import operation: d5911f85-a97d-4066-8e89-ad52309298ef in the isolated project's Library/AssetOperations.
  No planetary project was opened or modified. All effect and lifecycle acceptance rows remain pending.

### Completed observations and new failures

- Cold import completed in 579754 ms (worker operation time). Scanning took 332.513 ms;
  importing took 579416 ms. There were 21 successful imports and 10 failed imports with DXIL
  fragment compiler timeouts at 30000 ms. This invalidates the showcase as a visual baseline.
  PP-PERF-001: investigate per-pass compiler cost and startup backend selection; retain cross-platform
  cooking and binding/reflection validation. Raising timeouts alone is not the performance fix.
- The queued profile was eventually created, published, selected and opened automatically. Asset ID:
  47885181-604b-426c-a7d2-4f6dc853eda3. This verifies the creation handoff, not acceptable cold-start latency.
- Exposure override checkbox enabled correctly and marked the document dirty. Undo cleared both the
  override and dirty marker; Redo restored both. No volume was assigned, so this does not verify rendering.
- Save cleared the dirty marker and displayed Profile saved. PP-PERF-002: this single-profile save
  started a project-wide import (90 entries shown). Changed SavePostProcessProfile to request only
  the edited asset and its normal dependency closure. Source formatted and diff checked; the fix still
  requires a rebuilt-editor retest and a save/import-overlap check.
- Closed the saved QA editor through its window close button; list_windows subsequently confirmed no
  KeireClient window. The core DevDist test build is separate and still running. No effect rows passed.

### Core tests and compiler-cost isolation

- Official DevDist KeireTests build passed (111 compile/link steps). The PostProcessProfileTests,
  PostProcessEvaluationTests, PostProcessVolumeTests and PlanetEnvironmentHistoryTests source filter
  passed: 17 cases, 214 assertions. Logs: Build/Validation/post-processing-batch1-core-build.log and
  post-processing-batch1-core-tests.log. This does not replace GPU or hands-on acceptance.
- A targeted warm import of SG_12_IridescentShield and its dependent material completed in 270.531 ms
  with two cache hits in the isolated QA project. The single-asset cold probe took 30382 ms and captured
  forwardTransparent compiler commands for DXIL, SPIR-V and MSL. The core test build was concurrently
  active, so these are diagnostic measurements, not a controlled performance benchmark.
- Direct sequential DXIL compilation of the captured forward-transparent fragment: original 15.6276 s,
  diagnostic explicit-loop annotations 14.8972 s; both exit zero. This does not substantiate blanket loop
  annotations as the fix. No diagnostic shader change was applied to production.
- Captured source, commands and timings are preserved under the generated directory
  Build/Validation/PostProcessingShaderColdProbe. The initial two probe attempts failed protocol validation
  before compilation (invalid operation ID, then missing source-index path); only the corrected attempt
  contributes compiler evidence.
- The targeted-save editor rebuild is in progress. UI retest and all rendered-effect acceptance remain open.

### Targeted-save rebuilt editor retest

- DevDist editor rebuild passed (4 compile/link/staging steps).
- Hands-on Save cleared the dirty marker and displayed Profile saved. Worker operation
  162efce2-d293-49fd-b7a5-065cdabbb342 used import-assets with exactly the profile ID above,
  reason post-process-profile-save, and one imported status. Operation time was 94.5696 ms
  plus 84.5831 ms setup. No full source scan was required. This verifies PP-PERF-002's
  targeted import routing; import-overlap and rendered-effect checks remain open.
- The separate remaining-startup-refresh operation still processes the project in the background;
  it must not be confused with the profile-save request. Startup scene refresh on this launch
  took 236508 ms for 34 statuses. Cold shader compilation remains an unresolved performance issue.

### Forward-volume compiler isolation and loop optimization

- Diagnostic-only removal of volume composition compiled DXIL in 1.62 s; removing only fog segment
  density took 16.01 s; removing cloud tracing while retaining clear-air composition took 1.74 s.
  These experiments isolate cloud tracing as the dominant cost. No media were removed in production.
- Changed CloudVolume.hlsli ambient-direction loop from forced unroll to loop. All four directions,
  shadow intervals, density and transport calculations remain present. Avoids four expanded copies
  of the planetary density/shadow march through the shader cross-compiler.
- Sequential original/changed fragment compilation succeeded for DXIL, SPIR-V and MSL. DXIL:
  17.53/13.14 s; SPIR-V: 8.05/10.24 s; MSL: 7.81/8.00 s. These are single-run diagnostics on an
  active desktop, not a universal speedup claim. DXIL binary size fell from 183748 to 128888 bytes.
  SPIR-V JSON reflection was identical. Evidence: Build/Validation/PostProcessingShaderColdProbe/
  ambient-loop-comparison.json and *-reflection.json.
- The source change still needs regenerated builtins, an editor rebuild and GPU appearance/frame-time
  comparisons. Cold-start performance and full post-processing acceptance remain open.

### Shared shader intermediate implementation

- Diagnostic SPIR-V-to-DXIL and SPIR-V-to-MSL conversions produced byte-identical outputs to the
  original HLSL conversion routes for the captured forward-transparent fragment. Conversion-only
  times were 5.06 s (DXIL) and 0.36 s (MSL); retained HLSL-to-SPIR-V avoids repeating its front end.
  Evidence: shared-spirv-timings.json and SharedSpirv-* artifacts in the generated probe directory.
- RenderingAssets now compiles one SPIR-V intermediate per vertex/fragment stage and pass, then fans
  out backend conversions with shared futures. Existing validation, cancellation and complete artifact
  cache remain in place. Added a multi-backend regression checking two HLSL stages, all output variants,
  and zero additional compiler work on a warm cache hit. Build/tests are pending.
- Official builtin-shaders.ps1 completed successfully after the ambient-loop change. The official
  DevDist KeireTests build is active (shader-shared-intermediate-build.log), currently regenerating
  affected VFX builtins before native compilation. No new editor binary has been launched.

### Import-overlap protection and validation in progress

- A newer unsaved Post Processing draft now prevents a completed import from reloading that profile's
  runtime value. The catalog still publishes; discard/close retains the existing authored-source restore
  path. Document regression checks cover clean, dirty, unrelated-asset, saved and post-save-edit states.
  Rebuilt editor tests and hands-on overlap verification remain pending.
- Added shared-intermediate failure coverage: invalid HLSL must launch only the two frontend stages,
  propagate failure to all backend consumers, and publish no cache artifact.
- DevDist KeireTests build advanced into native compilation (19 planned steps). No test result is claimed
  until the rebuilt executable finishes the focused shader import/cache/cancellation suite.

### Rebuilt-worker and editor regression results

- Official DevDist KeireTests build passed (19 steps). First focused shader run: 19/20 cases,
  374/377 assertions passed. The added failure fixture removed the required instance-addressing binding,
  so validation rejected it before compilation. Corrected the fixture to append #error to the valid shader;
  retest build is active. Preserve the initial failure log: shader-shared-intermediate-tests.log.
- Official KeireEditorTests build passed (7 steps); post-process document/preview, coordinator and user-workflow
  filter passed 32 cases / 612 assertions. Logs: post-processing-overlap-editor-build.log and
  post-processing-overlap-editor-tests.log. Hands-on import-overlap rendering remains pending.
- New isolated cold project SharedShaderColdProbe-af0de2acb88941478327c45a8f3e104c imported SG12 successfully
  with the rebuilt worker: zero cache hits, one imported status, no diagnostics, 12185.1 ms operation plus
  48.7 ms setup. Earlier cold probe was 30382 ms under different system load; not a controlled speedup ratio.
  Probe operation: 2f6b3766-d0a6-46af-a2b4-4dae41541f17; path recorded in shared-shader-cold-probe-path.txt.
- Direct versus shared-SPIR-V DXIL and Metal vertex outputs were also byte-identical for the captured graph.
  The earlier fragment comparison already matched. Backend reflection and runtime visual checks are distinct;
  no full-scene/GPU acceptance claim follows from these compiler results.

### Focused regression retest passed

- Corrected shared-intermediate failure fixture retains the valid binding declarations and appends #error.
  Official two-step KeireTests rebuild passed; full selected shader import/cache/job/cancellation/snapshot
  suite then passed 20 cases / 377 assertions (1896 skipped). Log: shader-shared-intermediate-retests.log.
- Warm real-worker probe 3407fafd-86f5-4c81-b2d4-968685ca5c9a succeeded with one cache hit, no import or
  diagnostics: 150.42 ms operation + 15.48 ms setup. Cold and warm results are for one isolated graph,
  not a complete project startup measurement.
- Closed the saved QA editor through its title-bar close action. A fresh window inventory confirmed no
  KeireClient window. DevDist KeireEditorDev build is active; log:
  Build/Validation/post-processing-import-performance-editor-build.log. GPU and full-project startup retests
  remain pending on that rebuilt executable. No changes were committed or published in this pass.

### Additional backend regression and cold UI fixture

- Seven-texture production importer regression passed (1 case, 170 assertions across opaque, unlit with
  pixel depth offset, and transparent subcases). Validates pass inventories and binding metadata across
  DXIL/SPIR-V/MSL. Log: Build/Validation/shader-shared-seven-texture-tests.log. Not a GPU visual test.
- Created D:/Projects/KeireValidation/PostProcessingSharedImports from PostProcessingBatch1 Assets and
  ProjectSettings only, with a fresh project ID. No Library/import caches copied. Original QA and planetary
  projects preserved. Use this project for the rebuilt editor's cold-start measurement and visual checks.

### Exposure incident and resumed imports (November 1)

- User reports the editor exited while changing volume exposure; initial report of a manual close was corrected.
  No matching new Windows application-error entry or crash dump was found. Cause remains unconfirmed.
  Desktop reproduction was interrupted by Escape; do not mark the incident fixed from code-only checks.
- Last observed UI had a global volume assigned to the profile, exposure +3.9 EV, and startup import 32/36.
  Cold startup did not finish before exit, so this run cannot establish a complete cold-start time.
  Source scan took 394 ms; individual material/shader preparations took roughly 8–18 seconds each.
- Reopened QA project completed startup dependency import 2c6d7c85-7cde-483c-a933-241f3d8e0351:
  success, 31 cache hits, 5 imports, 36 statuses without diagnostics. This reused partial cache, not a cold run.
- Added a full-range exposure sweep at authored exposures 0.01, 1 and 16, checking finite positive outputs,
  expected EV scaling and unchanged authored settings. Focused build/test pending; GPU crash reproduction remains open.
- Exposure sweep validation completed: official DevDist KeireTests build passed (two steps), focused evaluation
  suite passed 6 cases / 4861 assertions. clang-format dry-run and git diff --check passed.
  Logs: Build/Validation/post-process-exposure-sweep-build.log and post-process-exposure-sweep-tests.log.
  This rules out failure in the tested CPU exposure evaluation; it does not verify live UI/GPU publication.

### Cache retention and live exposure reproduction

- Shader cache now probes four candidates within the existing 64-slot bound. Focused DevDist build passed
  five steps; shader cache suite passed 11 cases / 365 assertions, including corruption, invalidation,
  bounded storage, collision non-aliasing and early-set retention checks. Logs: shader-cache-probes-build.log
  and shader-cache-probes-tests.log. Updated worker/editor runtime staging is still pending.
- User reauthorized Windows control. Brought the maximized editor forward via its title bar without restoring
  its size. Scene rendered with volume/profile present, exposure 0, imports completed.
- Dragged the exposure field from (2085,410) to (2165,410). Capture immediately reported no screenshot target;
  process inspection confirmed KeireClient gone. No matching new Windows crash dump or application-error event.
  Treat as reproduced exit associated with exposure editing, with root cause still unconfirmed.
- Completed startup on the reopened partial-cache fixture: dependency refresh 92.6 s, remaining refresh 88.3 s,
  default-lit warmup 2.35 s. These are too slow and are not clean cold/warm baselines.

### Exposure exit root cause identified

- Diagnostic launch retained the real process handle and captured stderr. Repeating the exposure drag exited
  with code 1 and `Client failed: Rendering exposure must be a finite value in 0.01..16.`
  Evidence: Build/Validation/exposure-crash-exit.txt and exposure-crash-stderr.log.
- RenderSurfaceRecording reran feature selection against the evaluated packet environment. Post-process EV
  multiplication legitimately exceeds the authored exposure range, causing project-settings validation to throw.
- Capture now retains the authored FeatureEnvironment separately, and render-surface feature selection uses it.
  Evaluated exposure remains unrestricted within its existing finite rendering contract; authored validation stays strict.
- Added capture regression exercising -20/+20 EV: evaluated settings exceed authored limits while feature selection
  stays valid and preserves the authored snapshot. Official DevDist KeireTests rebuild active in
  exposure-feature-selection-build.log. Focused tests and rebuilt editor reproduction remain pending.
- Exposure fix core validation completed: official DevDist build passed 49 steps; focused environment capture,
  post-process evaluation and render-settings suite passed 39 cases / 6837 assertions. Formatting and diff checks
  passed. Logs: exposure-feature-selection-build.log and exposure-feature-selection-tests.log.
- Registered named Box/Sphere choices for the volume shape using the existing Inspector property drawer.
  Official KeireEditorDev build started in exposure-fix-editor-build.log; UI acceptance is pending.

### Rebuilt exposure UI verification

- Official KeireEditorDev rebuild completed successfully (seven steps), including runtime staging.
- In PostProcessingSharedImports, repeated the previously failing exposure drag: +8 EV rendered a bright
  scene without exiting. A subsequent drag to -10 EV rendered a dark scene without exiting.
- Discard Changes restored the saved 0 EV value, cleared the dirty indicator, and restored the baseline image.
- Inspector displayed the named Sphere choice for the existing volume. Changing the shape is not yet verified.
- These checks cover live Scene-view exposure editing and discard only. Game view, other effects, save/import
  overlap, local-volume transitions, standalone behavior, and sustained performance remain pending.
- Further UI testing encountered an unrelated application overlay covering the editor and intercepting clicks.
  No interaction with that overlay was attempted; requested that the user hide it before testing continues.
- Started the official DebugASan KeireTests build for shared shader-import lifetime validation, with two
  compile jobs. Log: Build/Validation/post-process-shader-asan-build.log. The configuration requires 629
  steps; compilation is underway, not a completed sanitizer pass. Run the shader cache/import suites after
  completion, including concurrent imports, failed intermediates, and live native-job cancellation.
- Current DevDist integration regression passed 42 cases / 5513 assertions (1875 skipped): post-process
  profile/evaluation/volume tests, planet environment capture/history, atmosphere composition, planet frame
  resolution/travel, and physics gravity sampling. Log: Build/Validation/post-process-planet-integration-tests.log.
  This is CPU-side integration evidence only; visual and standalone acceptance remain outstanding.
- Added a regression for equal-priority profiles from separate scenes sharing an entity identity, checking
  identical blends after reversing collection order. Also checks tied effect weights in the existing ordering
  test. Formatting passed; execution awaits the in-progress sanitizer build. The earlier 42-case result
  predates this additional test and must not be treated as evidence for it.
- DebugASan build stopped at ShaderCompilerJobs.cpp with C1085: no space left on D:. This is an
  infrastructure failure, not a sanitizer result. The new DebugASan intermediate directory uses roughly
  2.2 GiB. Cleanup was rejected by automatic tool policy and was not performed. Sanitizer execution and
  execution of the new cross-scene ordering regression remain outstanding.
- User-authorized cleanup restored approximately 232 GiB free on D:. Cleanup is stopped at the user's
  request. Resumed the official DebugASan KeireTests build with existing intermediates and two jobs;
  retry log: Build/Validation/post-process-shader-asan-retry.log. No sanitizer pass claimed yet.
- Panel follow-up now shows the current profile path in the selector and acknowledges queued creation instead
  of leaving the click without feedback. Successful manual profile selection clears stale panel messages.
  Formatting and diff checks passed; rebuild and visual acceptance remain pending after the sanitizer run.
- Source-order audit confirms RenderSurfaceRecording executes the legacy pre-tone-map hook, bloom,
  tone mapping/temporal-history capture, profile post-processing, the legacy after-tone-map hook,
  camera UI, then the legacy after-UI hook. RenderToneMap copies temporal history before profile effects,
  keeping animated grain out of that history. This establishes code order only, not a rendered HUD/parity pass.
- DevDist planetary/physics checkpoint run passed 58 cases / 279931 assertions (1859 skipped), exit 0.
  Selected PhysicsSystemTests, PhysicsGravitySamplingTests, SceneRuntimePlanetTests, PlanetaryTravelTests,
  and PlanetFrameResolutionTests. Coverage includes stacks and impacts at distant origins, repeated origin
  shifts, moving frame supports, and reconstruction. Log: Build/Validation/planet-physics-checkpoint-tests.log.
  This was correctness testing alongside a build, not a controlled performance benchmark or standalone pass.
- DebugASan KeireTests rebuild completed successfully after cleanup (507 remaining steps); the focused
  sanitizer run is active, with no pass/fail result yet. Log: post-process-shader-asan-tests-retry.log.
- Panel follow-up DevDist build completed all four native steps and runtime staging. Formatting for the
  panel and cross-scene ordering test, plus git diff --check, passed. Visual acceptance remains pending.
  Build log: Build/Validation/post-process-panel-followup-build.log.
- Focused DebugASan run completed with exit 0: 41 cases / 5719 assertions passed (1889 skipped),
  with no sanitizer diagnostic. Covered ShaderImportCacheTests, ShaderCompilerJobTests,
  ShaderCompilerCancellationTests, PostProcessEvaluationTests, PostProcessProfileTests,
  PostProcessVolumeTests and EnvironmentCaptureTests, including the new cross-scene ordering regression.
  Evidence: Build/Validation/post-process-shader-asan-tests-retry.log. GPU/UI acceptance is separate.
- Rebuilt panel hands-on: reopened the saved profile from the selector; its path now remains visible in
  the selector. Vignette enabled-state override and Enabled toggles produced visible Scene-view edge
  darkening. Intensity override plus a drag from 0.3 to displayed 1 produced strong edge falloff.
  One Undo restored 0.3 and the weaker appearance for this drag; this does not establish long-drag coalescing.
  Tooltip reproduced a readability issue: displayed 1 showed 0.9999999880790711 on hover.
- Discard after the vignette test cleared all test overrides and the dirty indicator and restored the baseline.
- Numeric hover tips now use the complete readable display text instead of the exact editing representation;
  direct entry retains its round-trip precision. Formatting and diff checks passed. The six-step DevDist
  KeireTests rebuild is active in scalar-tooltip-tests-build.log; rebuilt UI verification remains pending.
- Scalar-tooltip follow-up DevDist build passed all six steps; UiPanelInputTests and
  PostProcessEvaluationTests passed 23 cases / 5568 assertions (1895 skipped), exit 0.
  Logs: scalar-tooltip-tests-build.log and scalar-tooltip-tests.log. The open editor predates the tooltip
  relink, so visual tooltip verification awaits the next restart.
- Color grading partial visual acceptance: enabling neutral grading preserved the apparent Scene-view
  baseline. Saturation override at 0 visibly desaturated both Scene and Game views. The saturation Reset
  button restored 1, unchecked its override, and restored Scene-view colors. Other grading parameters,
  save/reopen, Play/standalone, and complete color-stack acceptance remain outstanding.
- Resumed hands-on testing after cleanup stopped. The same QA editor remains loaded with no active
  asset-processing progress. Grain enabled-state override and Enabled controls toggled successfully;
  a fine noise texture is visible on Scene-view surfaces at the default 0.1 intensity. This is only a
  basic Scene-view smoke check, not acceptance of animation, highlight suppression, Game view or parity.
  Discard restored the saved profile, cleared both toggles and the dirty indicator.
- Reload protection UI check: enabled the Grain Intensity override without saving, then clicked Reload.
  Reload refused with an actionable save/discard message; the override and dirty indicator remained.
  Discard then cleared the override and dirty indicator. This verifies local-edit protection only;
  external source replacement and save-conflict resolution remain untested in the UI.

## Runtime performance audit follow-up

- Source audit: RenderPostProcessing.cpp retains every encountered profile in PostProcessProfileCache;
  the only whole-cache clears found are renderer shutdown and device recovery. Long sessions that visit
  many different volumes can therefore retain inactive profile handles and last-good revisions.
  Implement bounded inactive-entry eviction while preserving active-camera leases, last-good fallback,
  asynchronous loading and alternating Scene/Game cameras. Test repeated profile switches and volume
  traversal before assigning a memory-stability pass. No memory leak measurement is claimed yet.
- RecordPostProcessing skips copy/draw only when all evaluated effect strengths are zero. Enabled neutral
  color grading still records both operations. BuiltinPostProcess.hlsli clamps the result, so blindly
  skipping neutral grading could change out-of-range values from a pre-tone-map material or disabled
  tone mapping. Define/test that color-range contract before optimizing this path; preserve authored HDR
  and legacy camera-hook behavior. Measure disabled/neutral/active GPU cost at matching resolutions.
- Implemented inactive-profile expiry after 600 accepted render frames without use. Cache aging runs
  before camera participation early-outs, so disabling post-processing does not retain old profiles
  indefinitely while capture continues. Moved AcceptedFrameId assignment before profile capture; it
  previously happened afterwards. Added boundary tests for same/adjacent frames, exact retention,
  expiry, backwards IDs and maximum IDs. DevDist KeireTests build is running in
  Build/Validation/post-process-cache-retention-build.log; test execution and sanitizer/runtime memory
  acceptance remain pending. This is time-based eviction, not a fixed cap on simultaneously used profiles.
- Added document regressions for all 18 currently exposed numeric parameters: minimum/maximum edits,
  rejection of NaN without changing draft or preview, Undo/Redo after rejection, persisted round-trip,
  Reset and Discard. These are document behavior tests, not GPU visual acceptance. Formatting passed;
  KeireEditorTests rebuild/execution is pending after the active KeireTests build.
- Cache-retention DevDist core build passed all 50 steps. Focused core regression passed 23 cases /
  5313 assertions (1896 skipped), exit 0: PostProcessEvaluationTests, PostProcessProfileTests,
  PostProcessVolumeTests and EnvironmentCaptureTests. Includes the new expiry boundary assertions.
  Evidence: Build/Validation/post-process-cache-retention-tests.log. This does not establish cache
  release under a live renderer or long-session memory behavior; those and sanitizer checks remain pending.
- The DevDist KeireEditorTests follow-up is running (three build steps) in
  Build/Validation/post-process-boundary-editor-tests-build.log for the 18-control document regressions.
- Editor follow-up build passed all three steps. PostProcessDocumentTests and PostProcessPreviewTests
  passed 6 cases / 352 assertions (685 skipped), exit 0, including all 18 numeric-control boundary,
  invalid-input, history and persistence checks. Evidence: post-process-boundary-editor-tests.log.
- Chromatic UI attempt did not produce a verifiable result: capture timed out after the tab click;
  reselected the existing window and the single recovery capture failed with FrameArrived timeout.
  Stopped UI input. PID 37172 remained present and Responding=True; this does not prove rendering health.
  The inspected project Client.log ends at 08:21 and is stale for this attempt. No crash or chromatic
  acceptance claim is supported. Sanitizer build continues independently.
- Loading-failure audit found a follow-up: profile cache insertion currently precedes Assets->Load.
  LoadErased can throw (closed/disabled system, decoder/type conflict or queue submission exception),
  leaving an inserted entry with an empty handle. A subsequent encounter sees inserted=false and skips
  loading. Make insertion transactional and add an injected-load-failure/retry regression; this has not
  yet been changed or reproduced through the running renderer.
- Implemented transactional profile-cache insertion through GetOrLoadPostProcessProfile: existing entries
  are reused, while missing entries are inserted only after load submission returns. Added injected failure,
  successful retry and cached-identity/no-extra-load assertions using actual profile asset references.
  Formatting and diff checks passed. The active ASan build had already compiled RenderPostProcessing.cpp
  before this edit; rerun the incremental build after it finishes before claiming current-source validation.
  DevDist rebuild and test execution for this follow-up also remain pending.
- Initial cache-retention DebugASan build completed successfully (54 steps). Follow-up build refreshed
  generated metadata for the new helper header and compiled the transactional RenderPostProcessing change;
  it is now linking (three native steps total). Evidence: post-process-cache-retry-asan-build.log.
  Sanitizer execution still awaits this current-source executable; no sanitizer pass is claimed yet.
- Current-source DebugASan follow-up build passed all three steps. Focused sanitizer run passed
  24 cases / 5320 assertions (1908 skipped), exit 0, with no sanitizer diagnostic: profile evaluation,
  asset profile, volume and environment capture suites. Includes cache expiry boundaries and injected
  failed-load retry/cached-identity regression. Evidence: post-process-cache-retry-asan-tests.log.
  This does not substitute for live renderer eviction or long-session memory acceptance.
- DevDist KeireTests follow-up is running in post-process-cache-retry-devdist-build.log; the open editor
  still predates these cache changes and the readable numeric-tooltip change.
- Current-source DevDist cache follow-up passed all four build steps and the focused core suites:
  24 cases / 5320 assertions (1896 skipped), exit 0. Evidence: post-process-cache-retry-devdist-tests.log.
- Desktop inventory no longer lists the prior editor window. Started the official DevDist editor launcher
  on the same PostProcessingSharedImports QA project to obtain the cache/tooltip build for visual checks.
  Launcher log: post-process-cache-editor-run.log. No new visual acceptance is claimed before it opens.
- Official DevDist editor build/staging completed successfully and reopened the QA project (PID 42096).
  Desktop capture works again, but a browser covers the editor. Asked before foreground takeover;
  no visual effect result is claimed from the obscured capture. Current editor includes cache fixes
  and numeric-tooltip formatting. Previous project log reads were stale and are not startup timings.

### Chromatic visual check (2026-11-01)

- Recovered desktop activation with a fresh returned window, then restored maximized size.
- Enabled chromatic override and effect; changing strength from 1 to 20 px produced clear RGB edge separation in both Scene and Game views.
- Discard Changes removed the effect, restored strength 1 with overrides disabled, cleared the dirty marker, and displayed the saved-profile restoration message.
- No crash observed during this sequence. Save/reopen, zero strength, undo coalescing, and standalone checks remain pending. Frame-time snapshots are not performance measurements.


### Bloom intensity visual check (2026-11-01)

- Enabled bloom and its intensity override in the maximized QA editor. Dragged intensity from 0.1 to 1.1; bright highlights acquired visible glow in Scene view.
- One Undo returned the full drag to 0.1 and reduced the glow. Discard restored all saved overrides off and cleared the dirty marker.
- Intensity tooltip showed readable 1.1 without a floating-point tail.
- Threshold, soft knee, radius, Game view, persistence and standalone bloom checks remain pending. No crash observed in this sequence.

### Broad editor regression and search follow-up (2026-11-01)

- Full DevDist editor regression run completed: 690 executed cases, 688 passed, two failed, one skipped; all 47,726 assertions passed. Both failures threw before worker execution because AssetWorkerTestRuntime rejected DevDist. Log: Build/Validation/post-process-editor-full-regressions.log.
- Updated the test harness to use Release FFmpeg dependencies for DevDist, matching Scripts/Windows/stage-ffmpeg-runtime.ps1. This does not waive the two worker tests; their rerun remains required.
- Panel search now matches effect categories and units as well as parameter labels and reports no matches explicitly. Added focused category/case/unit/nonmatch tests. Search changes are not yet built or visually verified.
- Official DevDist KeireEditorTests rebuild is running in post-process-search-editor-build.log. The open editor predates the search change.

- Search/worker follow-up: official DevDist KeireEditorTests build passed (three native steps), then all tests in EditorUserWorkflowTests.cpp and AssetOperationServiceTests.cpp passed: 20 cases / 599 assertions, 672 skipped, exit 0. Both previously rejected worker cases executed successfully. Evidence: post-process-search-editor-build.log and post-process-search-worker-tests.log. This focused rerun does not constitute a second full-suite pass. Panel source compilation and desktop search acceptance remain pending; the open editor predates the change.

Color-look implementation added: Neutral, Warm, Cool, Vivid and Monochrome replace only grading overrides through the existing document edit transaction. Added regression coverage for unrelated parameter/effect preservation, serialization and single-step Undo/Redo. Compilation, execution of the new tests and desktop acceptance are still pending; do not count these presets as visually verified.

### Color-look and search regression checkpoint — November 1, 2026

- DevDist KeireClient compilation completed, including PostProcessPanel.cpp. Initial linking failed with
  LNK1104 because the old QA editor still held the executable. The user closed it; the official launcher
  then completed a one-step link and runtime staging successfully. Logs:
  `Build/Validation/post-process-panel-client-build.log` and `post-process-panel-client-relink.log`.
- DevDist KeireEditorTests rebuilt in two native steps. The focused filter
  `--source-file=*PostProcessDocumentTests.cpp,*PostProcessPreviewTests.cpp,*EditorUserWorkflowTests.cpp`
  passed 21 cases / 960 assertions, with 672 cases skipped and exit code 0.
  Logs: `Build/Validation/post-process-looks-test-build.log` and `post-process-looks-tests.log`.
- This verifies the five color looks preserve unrelated parameters/effect overrides, apply grading values,
  support one-step Undo/Redo and save/serialization, and reject invalid indices/nonfinite source data.
  Existing document, preview and search regressions also pass. It does not prove preset visual quality,
  runtime GPU behavior, standalone parity or completion of the remaining effects.
- The editor is closed. New preset/search desktop checks remain pending; no QA scene/profile was changed
  during this checkpoint.

### Preset and category-search desktop evidence — November 1, 2026

In the rebuilt DevDist editor, PostProcessingSharedImports / SandboxShowcase, the user granted takeover.
Selected Monochrome from Color Look: the Scene view visibly became grayscale and the profile showed dirty.
Clicked Undo once: Scene colors returned and the dirty marker cleared. No profile was saved.
Typed uppercase COLOR into Search Settings: all seven grading parameters appeared (Temperature, Tint,
Contrast, Saturation, Red/Green/Blue Gain), with baseline overrides off. These specific checks passed.
Other presets, Game-view comparison, unit/no-result searches and standalone remain pending.
Found UI polish issue: the prior Color look applied message survives Undo even though the grade is restored.
Clear or update operation feedback on successful Undo/Redo in the next code batch.
Editor remains open maximized, profile at saved baseline, search text COLOR, Post Processing panel active.

Implemented the observed Undo/Redo feedback fix: each button now reports the actual history-operation result, including empty history, instead of retaining a previous preset/save success message. Formatting passed. The running editor predates this message-only change; rebuild and desktop confirmation are deferred to the next batch.

Desktop search follow-up: px displayed only Bloom Radius (buffer px) and Chromatic Aberration (px), with their enable controls. Appending zzzz displayed the intended No matching settings guidance. Both checks passed in the current DevDist editor. One Windows capture FrameArrived timeout recovered after reselecting the same returned window; no editor restart was required. Profile remains unmodified; current search is pxzzzz. These observations are not a performance benchmark.

Lens numerical checkpoint: official DevDist KeireTests build completed in two native steps; PostProcessEvaluationTests.cpp passed 11 cases / 4,894 assertions, 1,911 skipped, exit 0. Logs: post-process-lens-test-build.log and post-process-lens-tests.log under Build/Validation. This validates the new CPU thin-lens boundary cases plus existing evaluation regressions, not a rendered depth-of-field effect.

### Current integration batch (validation pending)

- Added an authoring diagnostic for a fully overridden focus distance at or below focal length, converting millimetres to metres. Inherited values are not diagnosed from a profile alone because the evaluated camera stack supplies them. Five boundary/inheritance assertions added; execution pending the combined build.
- Added selected local post-process volume outlines: oriented box boundary; spherical boundary and outer blend radius. Sphere scale follows the runtime maximum absolute scale axis. Camera-relative subtraction occurs before float conversion. Global volumes have no artificial finite outline. Box blend corners are not represented by an inaccurate expanded box.
- Windows-control check: Warm preset selection marks the profile dirty; one Undo removes the dirty marker. No profile save was performed. The temperature change is subtle in this scene and needs a stronger comparison before visual acceptance.
- New lens rendering, LUT asset/evaluation/UI changes and volume outlines still require the coordinated build, focused tests and editor acceptance. Existing test results do not validate these new changes.

### Lens integration review before GPU acceptance

- Combined DevDist EditorTests build is in progress; no new test pass claimed.
- Read-only review found near-field gather can include a farther foreground surface despite same-sign CoC. Tighten near-depth rejection before visual acceptance.
- Existing temporal continuity checks consecutive frames/environment, not camera identity or explicit cuts. Add camera history identity/reset support; an absolute-distance cutoff would wrongly reject fast planetary travel.
- Correct fullscreen draw/triangle counters for the additional HDR temporal resolve before measuring performance.
- Initial lens depth excludes transparent/VFX/world-space UI; bounded gather is not foreground-dilated/scatter bokeh. Per-workset buffers persist, are lazy, and require resize/device-loss verification. Full production acceptance remains open.
- Windows-control follow-up: Vivid visibly increases saturation/contrast in Scene view; one Undo restores the baseline and clears the dirty marker. No save performed. Closed the baseline editor normally between test builds.

### Combined authoring test result

Official DevDist KeireEditorTests build completed all 183 steps, exit 0. Focused PostProcessDocumentTests, PostProcessPreviewTests and EditorUserWorkflowTests passed 23 cases / 992 assertions, 672 skipped, exit 0. Logs: Build/Validation/post-process-lens-lut-editor-build.log and post-process-lens-lut-editor-tests.log. This covers LUT document transactions and lens diagnostic boundaries, not GPU rendering.

After that build, tightened near-field lens gather with depth-relative rejection and corrected fullscreen draw/triangle counters. Explicit camera-history identity/reset integration is being implemented next. Those later renderer changes require a new build and visual validation.

### LUT and camera-history validation preparation

The official Windows builtin-shaders launcher completed successfully after the LUT shader and near-field rejection changes; generated BuiltinUnlitShaders.h was updated. This is shader compilation/cache validation, not GPU visual acceptance. The combined DevDist core test build is now running (post-process-complete-core-build.log).

Added new, non-overwriting fixtures only under QA Assets/PostProcessingValidation: Identity32.png (51a9037b-2270-4bf6-a61c-b481fa098311) and SwapRedBlue32.png (4e1aea08-c571-4907-ab48-4a798efc6f73). Both are 1024x32 horizontal 32-cube strips with linear Data, clamp/linear sampling and no mipmaps metadata. Identity is quantized to 8-bit; compare within that tolerance. Swap-red-blue must visibly exchange red/blue objects. Fixtures are prepared, not yet imported or visually verified in the new build.

Official Scripts/Tests/test-managed-api.ps1 -Configuration Release completed exit 0 after Camera.ResetTemporalHistory integration (61 PASS entries). Includes managed runtime contracts, editor SDK contracts, binding generator and design-time API parity: 4,867 runtime and 860 editor API records match. Log: Build/Validation/post-process-camera-managed-tests.log. Native core tests and GPU/editor acceptance remain pending.

### Combined native regression result

Official DevDist KeireTests build completed all 215 native steps, exit 0. Focused PostProcessLensTests, PostProcessEvaluationTests, PostProcessProfileTests, PostProcessVolumeTests, PlanetEnvironmentHistoryTests, EnvironmentCaptureTests and ManagedRuntimeRenderingTests passed 50 cases / 6,013 assertions (1,886 skipped), exit 0. Logs: post-process-complete-core-build.log and post-process-complete-core-tests.log under Build/Validation. This validates numerical/profile/volume/history/dispatch contracts; it does not establish GPU visual quality, runtime memory safety, standalone parity or performance. Updated KeireClient build started for those visual checks.

### Updated editor visual checks

The combined DevDist client build completed successfully and the official launcher reopened
PostProcessingSharedImports. In the maximized editor, Monochrome visibly removes scene color;
Undo restores the original colors and clears the dirty marker. The LUT picker lists both new
validation textures. With Color grading enabled, SwapRedBlue32 visibly turns the red sphere blue,
the blue capsule orange, and exchanges the corresponding scene channels. This proves the LUT
selection reaches GPU rendering; it is not yet a numerical identity or interpolation accuracy check.

Selecting a LUT while Color grading is disabled produces no visible change, as intended, but
the dependency was unclear. Added a short instruction beside the LUT controls. That UI wording
change still requires rebuilding. Identity LUT, weight interpolation, lens effects, resize/device
recovery, standalone parity, ASan and performance acceptance remain outstanding.

Follow-up: switching from SwapRedBlue32 to Identity32 through the picker restores the original scene appearance visually. Numerical pixel comparison remains pending. The profile is currently an unsaved validation edit with identity LUT selected and Color grading enabled; no QA asset was saved.

Depth-of-field Windows-control check: enabling the effect works; double-clicking Focus Distance permits numeric entry, and changing 10 m to 1 m visibly increases defocus. Disabling restores the sharp scene without a crash. Strong defocus shows stepped/repeated highlights near the gold sphere and sharp object silhouettes against blurred surroundings; inspect gather coverage and depth rejection before quality acceptance. Current unsaved profile retains identity LUT, Color grading enabled, DoF explicitly disabled and focus override 1 m. No assets saved.

Search follow-up in the combined client: typing focus filters the panel to Depth of Field enable and Focus Distance controls, retaining the transparent-depth guidance. The numeric value remains 1 m and DoF remains disabled. No profile save or scene modification was performed by this search check.

Camera-history authoring regression checkpoint: official DevDist KeireEditorTests build completed all 136 steps, exit 0. Focused PlanetCameraPersistenceTests, PostProcessDocumentTests, PostProcessPreviewTests and EditorUserWorkflowTests passed 25 cases / 1,021 assertions (671 skipped), exit 0. Logs: Build/Validation/post-process-history-editor-build.log and post-process-history-editor-tests.log. This proves the tested authoring-cut versus smooth-travel history contracts, not GPU temporal stability or the subsequent DoF quality changes.

DoF quality implementation checkpoint: introduced bilateral half/quarter/eighth HDR and signed-CoC reduction, near-radius tile dilation, separate near/far half-resolution gathers, and filtered full-resolution composition. Windows builtin shader compilation completed exit 0 (post-process-dof-quality-shaders.log); the DevDist core build is now running. This is not visual acceptance. Seven additional small textures are lazy and persist per workset; their aggregate area is approximately 0.859375 full-resolution RGBA16 surfaces (about 54.4 MiB per workset at 3840x2160). Measure actual GPU cost and memory with multiple views; the extra passes may cost more despite reduced-resolution sampling. Remaining limitations include bounded circular 32px blur, thin-object coverage, missing hidden background, transparent/volumetric focus depth, and no aperture-blade shape. Resize/device lifecycle and standalone still require validation.

Read-only lifetime audit: each lens/DoF allocation is registered in SurfaceFrameWorkset.TransientTextures after capacity reservation. ReleaseResources iterates that owner list and clears the complete SurfaceResources object, including the non-owning DoF arrays; allocation is skipped when the workset pointer already exists. This supports the intended lifetime design but does not replace runtime resize/device-recovery or ASan validation. Current-frame depth validity and odd-sized viewport sampling are under additional review.

Desktop baseline restored using Discard Changes after root-created temporary LUT/DoF edits. Dirty marker cleared, DoF/focus overrides returned off and focus returned to 10 m; UI reported Restored the last saved profile. No QA asset save occurred. Review found full/reduced UV mismatch at odd viewport dimensions; correction and coordinate regression tests are being implemented before the next GPU comparison.

Odd-viewport correction implemented: full/reduced sampling maps exact source-block centers, including partial final blocks; all pyramid samples, gather coordinates and near/far composite use the mapping. Added coordinate round-trip coverage for one-pixel, odd/even extents and factors 2/4/8. These changes occurred during the native build and require a subsequent incremental build plus shader regeneration before their test results can be claimed.

Final alignment checkpoint: Windows builtin shader regeneration and 11-step incremental DevDist KeireTests build completed exit 0. Focused PostProcessLensTests, PostProcessEvaluationTests and PlanetEnvironmentHistoryTests passed 28 cases / 17,049 assertions (1,911 skipped), exit 0. Logs: post-process-dof-alignment-shaders.log, post-process-dof-alignment-core-build.log and post-process-dof-alignment-core-tests.log under Build/Validation. This covers source-coverage weighting, physical lens parameters, odd-size coordinate mapping and history evaluation; rendered quality/performance remains unverified.

Updated DoF GPU check: the seven-step DevDist KeireEditorDev build passed, and the official launcher opened SandboxShowcase maximized (PID 40756). In the profile UI, enabled DoF and focus overrides, then changed focus from 10 m to 1 m using double-click/text/Return. Strong defocus visibly renders; this capture shows softer object/ground edges and a more continuous gold-sphere highlight than the earlier stepped result. This is a qualitative stationary observation, not complete foreground, temporal, resize, transparent-depth or performance acceptance. Profile remains unsaved with only DoF enabled and focus 1 m overrides changed from saved baseline; no LUT changes this run.

Follow-up visual acceptance: switched to Game view with DoF active and confirmed strong defocus from the independent Showcase Camera. Disabled DoF in the profile panel, then inspected Scene and Game views: both restored sharp silhouettes, texture detail and highlights without a crash or residual blur. This validates effect disable/recovery for these stationary views only. Unsaved QA profile retains enabled-state override with DoF disabled and focus override at 1 m; no profile or scene was saved. Motion, foreground occlusion, resizing, standalone and measured GPU performance remain pending.

Combined lens/resize UI checkpoint: enabled Motion Blur at inherited strength 0.5 with DoF disabled.
The stationary scene stayed sharp. A Scene camera scroll zoom completed without a crash or persistent
smear; the post-input screenshot cannot establish streak quality during motion. Re-enabled DoF at
focus 1 m while Motion Blur remained enabled, narrowed the Scene pane by dragging its divider from
x1730 to x1589, then restored it. The renderer continued displaying coherent defocus at both sizes
without an obvious corrupt border or crash. Exact render-target dimensions and transient GPU errors
were not captured, so this is a resize smoke check rather than exhaustive odd-dimension acceptance.
Discard Changes restored the saved profile, cleared its dirty marker and returned focus to 10 m
with overrides disabled. No profile or scene was saved. The Scene camera remains slightly zoomed in.

The official DebugASan KeireTests build is in progress (post-process-lens-asan-build.log).
Do not count this as an ASan pass until the rebuilt executable completes the focused tests.

Local-volume UI acceptance: applied an unsaved Monochrome look to the existing QA profile.
Disabling the volume restored color; Undo restored grading and cleared the scene dirty marker.
Weight 0.5 produced partial desaturation; Undo restored weight 1. With Global disabled, moving the
Scene camera outward through the spherical boundary first showed partial color and then full color
outside the volume. Restoring Global through Undo reapplied monochrome at the same camera position.
Discard Changes restored the saved profile and cleared its dirty marker. No scene or profile was saved.
These are qualitative participation/blending checks; precise boundary values remain covered by unit tests.

Rebuilt DebugASan focused validation passed: 53 cases / 17,637 assertions across PostProcessLens,
PostProcessEvaluation, PostProcessProfile, PostProcessVolume, PlanetEnvironmentHistory,
EnvironmentCapture and ManagedRuntimeRendering source filters. Build and test exit codes were zero;
logs: Build/Validation/post-process-lens-asan-build.log and post-process-lens-asan-tests.log.
This is CPU/headless coverage, not GPU resource-lifecycle or device-loss acceptance.

Settings-search visual check: typing aperture filtered the panel to the Depth of Field aperture row
while retaining the effect enable override. Ctrl+A/Backspace restored the tabs and complete current
category. Search did not dirty the profile or scene. Windows-control capture temporarily timed out;
resetting its JavaScript connection restored window discovery and capture without restarting the editor.
Windows briefly reported the editor unresponsive during the concurrent sanitizer run, then responsive;
no crash was observed. This session is not a controlled performance measurement.

Standalone dependency audit found a concrete omission: SceneAsset's component dependency collector did
not include PostProcessVolume.profile. Added it and advanced scene importer 10 to 11 and prefab importer
3 to 4 so existing metadata refreshes. Added scene/prefab import coverage for inactive volumes and a
strict rooted cook regression asserting scene -> profile -> inactive lookup closure while excluding an
unrelated asset. The lookup fixture tests packaging dependency retention, not texture decoding or GPU
appearance. Build/test validation is pending in post-process-cook-dependencies-build.log. This correction
does not establish standalone rendered parity.

Cook regression first run: 35 cases, 33 passed. The new lookup fixture failed because its placeholder bytes lacked valid texture stream metadata; replaced them with Texture2DAsset::Encode output. A separate pre-existing scene shader test exceeded its 30-second SPIR-V compiler timeout during concurrent sanitizer testing. The corrected fixture rebuild and focused rerun remain pending; neither failure is accepted as a pass.

Corrected cook fixture validation passed after encoding a real linear, no-mipmap texture payload. Official five-step DevDist build succeeded; the focused scene/prefab dependency, rooted profile-to-lookup cook, and terminal-job prerequisite lifetime regressions passed 3 cases / 20 assertions. Logs: post-process-job-lifetime-build.log and post-process-job-lifetime-tests.log. Expanded affected suites are running; this proves catalog dependency inclusion, not standalone GPU appearance.

Expanded rebuilt DevDist validation passed all 54 cases / 9,609 assertions across ArchitectureFoundationTests, ProjectSceneTests, PostProcessProfileTests and PostProcessVolumeTests. This includes the starter-project regression that timed out in the previous run. Exit code 0; log Build/Validation/post-process-job-lifetime-expanded-tests.log. Fresh ASan coverage of the lifetime changes and standalone rendered parity remain outstanding.

Rebuilt editor smoke checkpoint: official six-step KeireEditorDev build passed, normal run.ps1 launcher opened PostProcessingSharedImports maximized (PID 10232). SandboxShowcase is rendering with colored materials, scene importer v11 is shown, and the saved post-processing profile opens from the panel selector with clean state and inherited exposure 0. No scene or profile was saved. This establishes reopening after the dependency/importer change, not complete effect or performance acceptance.

Warm preset authoring check on rebuilt editor: Apply sets temperature 12, contrast 1.05 and color overrides; one Undo restores inherited defaults and clears the dirty marker; Redo restores those values; Discard Changes returns the profile to its saved clean baseline. Inspector confirms assigned profile, enabled component, active entity, Global enabled and weight 1. The screenshot difference is too subtle to establish quantitative warm grading, so this result validates authoring/history only. No scene or profile was saved.

Rebuilt grading renderer check: Monochrome produces visibly grayscale geometry, particles and background in Scene view and the independent Showcase Camera Game view. Scene gizmos retain their colors. Discard restores Scene color and clean profile state; no source was saved. This confirms live profile propagation to both views; it does not establish numerical temperature/tint accuracy or HUD hook ordering.

Reload protection UI check: applied a temporary Monochrome preset, clicked Reload, and observed that the dirty marker and grayscale preview remained. Discard Changes then restored the clean profile and scene colors; no source save occurred. Source inspection confirms Reload rejects local changes. Action feedback was drawn below the long settings page, making the explanation easy to miss. Moved feedback above settings and added explicit successful-reload text to replace stale error messages. These UI changes await the next editor build and visual check.

Feedback build checkpoint: the first editor link failed with LNK1104 because the existing QA editor was open. Closed the clean editor normally, then official run.ps1 completed the four-step incremental DevDist build and launched the same QA project (PID 21800). No source or profile save occurred. The new feedback placement still awaits visual verification after startup.

Rebuilt feedback visual verification passed: clean Reload showed 'Profile reloaded from disk.' at the top. A temporary exposure override made the profile dirty; Reload preserved it and showed 'Reload cancelled: save or discard your local profile changes first.' above the controls. Discard restored the unchecked override and clean marker. No profile or scene was saved; source baseline remains unchanged.

Standalone prerequisite check: the existing QA Desktop Development profile actually requests Release. Normal build-player failed before importing with no compatible 0.4.8 Build Support; only older support packages are installed. DevDist's current companion runtime was ignored by development fallback, which hard-coded Debug. Added explicit DevDist-host selection for Development players only, preserving Release/Dist requirements. Regression tests assert development selection and rejection for Release/Dist; build pending in devdist-player-support-build.log. No QA configuration has been changed and no standalone parity is claimed.

DevDist player-support validation passed: 11 tests / 86 assertions after the final source rebuild. The normal AssetTool target rebuilt successfully. Added a separate development-only QA profile (Post Processing QA DevDist, output PostProcessing-QA-DevDist) without changing the original active Release profile; original settings backed up in Build/Validation/post-process-original-build-profiles.json. The normal build-player command is now running against the QA profile. This uses the current DevDist runtime and does not establish production Release-package acceptance.
- Standalone QA build succeeded through the normal build-player path, exit 0. Status: post-process-dev-player-status.json; log: post-process-dev-player-build.log. Output: PostProcessingSharedImports/Build/PostProcessing-QA-DevDist/KeireSandbox.exe. Saved profile overrides are all off, so this first build is a baseline and dependency/package check, not effect-by-effect rendered parity. Standalone launch has been requested; visual acceptance remains pending.
- Standalone launch/render smoke succeeded: KeireSandbox opened maximized and displayed colored showcase geometry, texture detail and shadows. The floor and one cube appeared substantially lighter than the editor Scene baseline; camera, lighting and material/cook differences must be isolated before claiming parity. Player closed normally after capture. Initial sky.launch_app failed before starting a process; explicit Start-Process from the player directory launched PID 8076 successfully.

- Fresh editor comparison: official run.ps1 completed four incremental DevDist steps and reopened the QA project. The saved Showcase Camera Game view also renders the floor blue and the rear cube pink, matching Scene view. This rules out the Scene-camera-only explanation for the earlier standalone discrepancy. No scene or profile was changed. The launcher relinked KeireRuntime before this comparison, so the standalone must be rebuilt with that current runtime before attributing the mismatch to material serialization.

- Refreshed standalone baseline passed: normal build-player returned exit 0 with current DevDist runtime (post-process-dev-player-refresh.log/status.json). KeireSandbox now displays the blue plinth, pink rear cube, textured cube and colored showcase geometry matching the editor Showcase Camera Game view. The previous discrepancy disappeared after runtime relink and player rebuild; no material source changes were made. This is visual baseline agreement, not pixel-exact or effect-by-effect parity. Player closed normally after capture; editor remains open.
- Shutdown correction: Alt+F4 was sent, but immediate process verification still found KeireSandbox PID 29064 alive. The log reports a 27,360 ms VFX CSSpawnInitialize pipeline creation. Clean shutdown is not yet verified; allow the in-flight driver work to finish and recheck rather than sending repeated close inputs.
- Follow-up process check confirmed PID 29064 exited. GPU VFX preparation logged CSSpawnInitialize at 27.36 s and CSSpawnOutput at 20.10 s. BuildGpuVfxPipelines checks cancellation between SDL pipeline calls; CancelGpuVfxPipelineWarmup waits before releasing device-owned resources. Driver-call latency remains unresolved; no unsafe detached shutdown workaround was introduced.

- Persistence test prepared through UI: selected Monochrome in Color Look, saw grayscale Scene preview, clicked Save, observed Profile saved and one completed asset import. Source inspection confirms saturation override true/value 0. Editor process exited after Alt+F4. Original profile backed up at Build/Validation/post-process-profile-before-persistence.json. Normal player build passed (post-process-monochrome-player.log/status.json); standalone visual check pending. Restore baseline after verification.
- Monochrome standalone visual check passed: packaged player displayed grayscale geometry, background and particles after the UI-authored save. This proves the saved profile dependency/import/cook/runtime path for saturation, not all effects. Close requested normally. Restored the original neutral QA profile bytes from the pre-test backup with editor closed; packaged QA output intentionally retains the tested Monochrome profile until rebuilt.

### Migrating legacy camera fullscreen effects (validation pending)

Migration is explicit because a camera slot belongs to one camera, while a volume can affect several.
Keep the original scene/profile saved before starting. Record each material under the camera Inspector's
Fullscreen Effects: Before Tonemapping, After Tonemapping and After UI.

1. Create or open a Post Processing profile. In Custom Effects, enable Override Custom Stack and add each
   recorded material at its matching stage. Leave these new entries disabled initially to avoid applying
   the effect twice while setting up the volume.
2. Assign the profile to a Post Process Volume. For equivalent always-on behavior, use a global volume at
   weight 1. Choose a volume layer and configure camera volume participation/masks so only the intended
   cameras receive it. Check overlapping volumes: a higher-priority custom-stack override replaces this list.
3. Clear one corresponding legacy camera slot and enable that profile entry. Repeat stage by stage, comparing
   the same Game camera to the saved baseline. Existing legacy slots run before custom entries within a stage.
   After UI intentionally processes the camera HUD; choose After Tone Mapping to leave camera UI untouched.
4. Save both the profile and scene, reopen, and compare Play and standalone output. Profile changes and camera
   changes have separate Undo histories; undo both changes or reload the saved baseline to reverse migration.

This procedure is a planned acceptance workflow, not yet a verified migration result. Automatic migration
across separate scene/profile histories has not been implemented. A local volume or weight below 0.5 does
not reproduce an always-on camera hook: custom stacks switch discretely rather than blending shader programs.

Custom-stack visual fixtures prepared in the isolated QA project's PostProcessingValidation folder: StackOrderRed and StackOrderBlue legacy materials reference the existing shared Fullscreen graph with red/blue BaseColor overrides. They are intentionally solid-color diagnostics for order, disable and clear checks, not production looks. They await normal editor import and renderer verification. No existing assets or scene were overwritten.

Custom-stack core validation passed: final official DevDist KeireTests build completed all 49 steps; PostProcessProfileTests, PostProcessEvaluationTests and PostProcessVolumeTests passed 30 cases / 5,505 assertions (exit 0). Evidence: Build/Validation/post-process-custom-stack-final-build.log and post-process-custom-stack-tests.log. Editor document/preview/material-picker build and tests are now running; custom-stack GPU appearance and UI ordering remain unverified.

Custom-stack editor automation passed: official 133-step DevDist KeireEditorTests build completed, followed by SDL dummy-driver PostProcessDocumentTests, PostProcessPreviewTests, AssetPickerTests and AssetPickerInteractionTests: 21 cases / 756 assertions, zero failures, exit 0. Logs: Build/Validation/post-process-custom-stack-editor-build.log and post-process-custom-stack-editor-tests.log. This validates document/history/preview/picker logic, not rendered custom-stack order. KeireEditorDev build for visual validation has started.

Custom-stack visual build passed all 161 DevDist steps. Normal run.ps1 reopened PostProcessingSharedImports maximized; SandboxShowcase renders colored materials and the saved neutral profile opens cleanly with the new Custom Effects tab visible. No profile or scene edits were saved. This verifies launch/profile opening only; stack rendering checks remain next.

Custom-stack UI: tab, override toggle and material-search filter work; StackOrderRed adds an enabled After Tone Mapping entry and marks the profile dirty. Initial diagnostic assets failed import because schema-4 properties lacked typed wrappers. Corrected both isolated QA fixtures to type/value objects per DecodeMaterialProperty; waiting for reimport before judging GPU output. No scene or profile saved.

Custom-stack rendered Scene checks passed after fixture correction: both assets reimported successfully (2 imports, 194 ms reported). Red After Tone Mapping produced solid red; adding blue after red produced blue; Move Up on blue reordered the list and produced red; disabling the final red entry restored blue. Gizmos and editor UI remained visible. Profile is currently unsaved with [blue enabled, red disabled], both After Tone Mapping; no scene/source profile saved. Game/stage/history/persistence checks remain pending.

- Follow-up desktop validation: Undo restored the enabled red final effect; Redo disabled it again and restored blue. Switching to Game view showed the same blue output as Scene view. Stage selection and custom-stack persistence remain pending.

- UI save/reload retained the two custom entries, enabled states and Before Tone Mapping selection; serialized JSON was checked directly. Selecting After UI for blue, then enabling the later-listed red After Tone Mapping effect, kept Scene output blue as expected from stage ordering. Camera UI occlusion and standalone custom-stack parity remain unverified.
- UI finding: adding/clearing the top status message shifts all controls vertically. Reserve its row in the next code batch so repeated history actions stay in place.

- Reserved the feedback row in PostProcessPanel.cpp to prevent status-driven vertical control movement. Source formatting and whitespace checks passed; rebuilt UI verification is pending the next batch. Depth-of-field override/enabled controls respond without a crash; optical blur behavior still needs a stronger near-focus fixture.

- Depth-of-field live test: override enabled state and focus distance; entering 0.1 m caused obvious blur in both Scene and Game views. Editor controls and Scene gizmos stayed sharp. Close prompted for unsaved profile edits; Save closed successfully. Diagnostic JSON preserved in Build/Validation/post-process-custom-stack-dof-ui.json; original neutral profile restored byte-for-byte after process exit. Optical edge quality, moving-camera history and standalone DOF remain open.

- Combined-effects checkpoint: all seven built-in effect enable overrides were active in the isolated profile (focus distance 20 m, chromatic aberration 2 px). Scene and Game rendered recognizable colored geometry without a crash during the desktop smoke check. This does not validate moving-camera quality or each effect's contribution. Reload displayed feedback while toolbar/tab/control positions stayed unchanged, verifying the reserved status row. Closed the editor normally and restored the neutral profile after confirming process exit. Standalone parity, temporal quality, camera UI stage coverage and performance profiling remain pending.

- Simplified activation checkpoint: DevDist authoring/document/preview tests passed 15 cases / 787
  assertions. In the rebuilt editor, Bloom required one Enabled click; one Undo restored inheritance.
  Editing inherited intensity activated Bloom automatically in the preceding UI pass. Search for
  focus selected Depth of Field; a roughly 500 px dock used a compact selector. The final layout
  rebuild kept the activation hint row stable and wrapped footer guidance without clipping. The
  QA profile stayed byte-identical to baseline and the editor was closed normally. LUT/custom
  automatic activation and continuous-drag history still need desktop acceptance; existing
  custom-stack rendering evidence above predates the simplified activation controls.

## Continuous adjustment and creation batch — implementation pending validation

This worker source batch has not been built, executed or exercised in the editor. The coordinator
retains sole ownership of builds, tests, generation and desktop acceptance. Existing evidence above
predates these changes.

- Numeric parameters and lookup weight now use a transient document draft while interacting. Live
  preview and dirty state follow each accepted sample, while history receives only the final definition.
  Effect activation and legacy lookup schema upgrades remain part of that same final command.
- Release, keyboard completion, focus loss, a search/category change that hides the field, Save, Undo,
  Redo and discrete document edits finish the gesture. Escape restores its starting definition and
  ignores further samples until that widget releases. Hiding/collapsing the panel, Discard, document
  replacement and Close cancel the unfinished gesture. Explicit profile-selection attempts first finish
  the adjustment so a dirty-profile rejection preserves it. Reload finishes and retains dirty-edit
  conflict protection. Invalid replacement data does not interrupt the current gesture.
- Added focused document regressions for 128 samples in one gesture, separate gestures with the same
  name, no-op round trips, redo preservation after cancellation, operation without history, rejected
  samples, preview restoration failure/recovery, save failure, lifecycle boundaries, abandoned-document
  destruction and lookup schema/history behavior. Numeric boundary coverage now includes all 23
  parameters, adding focus distance, aperture, focal length, depth-of-field strength and motion strength.
- Profile creation now uses a modal with inline validation and operation errors. Name validation is
  shared by the UI and PostProcess controller, rejects path traversal/control/reserved filename cases,
  and has focused authoring tests. Enter submits a valid name and Escape cancels. Creation still uses
  the current Asset Browser folder and the existing asynchronous import/open operation.
- clang-format was applied to the eight changed C++ files and its dry-run passed. git diff --check
  passed with existing line-ending conversion notices. These are source checks only. No worker build,
  test execution, editor launch, QA project mutation or Git staging/branch operation occurred.

### Coordinator validation batch

1. Rebuild KeireEditorTests in DevDist. Run the existing PostProcessDocumentTests,
   PostProcessAuthoringTests and PostProcessPreviewTests source filters together. Repeat the document
   lifecycle/failure cases under DebugASan. The prior 15-case/787-assertion result does not cover this batch.
2. Rebuild KeireEditorDev as well: EditorTests currently compiles the document/preview sources, but
   does not compile PostProcessPanel.cpp or PostProcessWorkspace.cpp. Validate those panel paths in the
   real editor; do not infer UI correctness from model-test success.
3. On the preserved isolated QA baseline, drag inherited Bloom intensity continuously for several
   seconds; check smooth preview, automatic activation and exactly one Undo to the full inherited start.
   Redo must restore the final value. Repeat twice on the same field: each drag must Undo separately.
   Include a final release-frame change, direct expression entry, keyboard completion and narrow layout.
4. During another drag, press Escape while holding the mouse, move further, then release. The original
   values/overrides/preview must remain, with no history entry and an existing redo branch intact.
   Repeat around focus loss, category/search changes, panel hide/collapse, rejected profile switching,
   Reload, Save/failed Save, Discard and editor close/cancel/save. Check no stuck transaction or stale
   preview after reopening a profile. Commit on focus/search changes and cancel on panel hide/collapse
   are the intended source policies.
5. In basic Color authoring, choose a valid LUT without first enabling anything. Check grading and lookup
   overrides activate together, weight drags Undo once, Clear Lookup clears inherited lookup, and
   save/reopen preserves it. Repeat from a legacy schema-1 profile; Undo must restore the old schema.
   Verify missing/incompatible lookup diagnostics separately from successful rendering.
6. In basic Custom Effects, add a compatible material while the custom-stack override is inherited.
   Check automatic activation, independent add/reorder/stage/enable/remove history, empty-stack clearing,
   save/reload, Scene/Game output and After UI behavior with a visible camera HUD.
7. Creation dialog: invalid/empty/reserved/path names remain in the modal with readable feedback;
   valid Enter submission queues exactly one asset and opens it after import; Escape/Cancel creates
   nothing; duplicate names and dirty-current-profile errors stay visible inside the dialog. Test the
   chosen Asset Browser folder and a Unicode name. Use a separate disposable QA asset and restore clean
   state through supported operations under the coordinator's editor lease.

Full built-in visual contribution/temporal quality, precise volume transitions and handle authoring,
multiple cameras with HUD stage coverage, complete standalone effect/custom-stack parity, resize/device
recovery and controlled CPU/GPU/memory measurements remain open. This batch does not close those gates.

Final source review also replaced locale-dependent narrow filesystem conversions in profile creation
and this panel's asset labels with the engine UTF-8 path helpers. The Unicode creation/reopen case above
remains pending actual editor validation. Added explicit dirty-baseline and transient-definition alias
regressions; no new executable/test result is claimed.

## LUT cache lifecycle batch — source ready, validation pending

Source review found that GPU lookup expiry ran only while a LUT was actively sampled, and CPU lookup
expiry ran after camera-participation early returns. Disabling grading or camera participation could
therefore retain inactive lookup resources indefinitely while rendering continued. Lookup handle
insertion also preceded load submission, so an exception could leave an empty entry blocking retries.

The PostProcess-only runtime now ages CPU caches before capture early returns and GPU lookup resources
before the effects early return, using the existing 600-accepted-frame retention policy. GPU resources
continue through the existing deferred Retire path. Disabled grading no longer requests unused LUTs.
Lookup and profile handles share transactional insertion: failed submissions leave no cache entry.
Existing limits of 32 lookup handles/textures and immutable packet ownership remain in effect.

Added three focused regression cases covering expiry without new loads, exact retention boundaries,
alternating-camera refresh, outstanding resource leases, failed-release retry and lookup load retry
before/after expiry. These use CPU cache fixtures; they do not prove GPU retirement or memory behavior.
Formatting dry-run and git diff --check passed. No build, tests or editor execution occurred here.

Coordinator validation requested: rebuild KeireTests and run PostProcessEvaluationTests,
PostProcessProfileTests and PostProcessVolumeTests in DevDist and DebugASan. Then verify live renderer
cache release with Scene/Game views: load a valid LUT, disable grading/camera participation, continue
recording beyond 600 accepted frames, and inspect CPU/GPU cache counts and retired resources. Reenable
and confirm reload/reupload with unchanged appearance. Exercise >32 distinct LUTs and alternating
cameras; keep in-flight resources valid and compare memory after repeated cycles. Frame IDs count
accepted frames rather than seconds; stopped rendering does not advance expiry. Device recovery,
measured frame-time/memory budgets and standalone LUT parity remain pending.

Reload rejection follow-up (source only): zero revisions are validated before continuous-edit commit. Added a regression proving invalid revision/malformed input preserve the active draft, preview and existing redo branch. Formatting and whitespace checks pass; coordinator EditorTests rebuild and DevDist/DebugASan execution are pending. No additional desktop acceptance is claimed.

## Upload recovery and actual-panel harness — source checkpoint, execution pending

Coordinator granted the narrow shared cache-entry include/member and minimal EditorTests registrations.
The prepared retry policy is now integrated: failed same-revision LUT uploads can retry after 60 accepted
frames, changed revisions attempt immediately, successful sources remain cached, and same-frame camera
attempts are suppressed. Device-loss handling and rejection of stale LUT sampling remain in place.
Three policy regressions cover interval boundaries, changed/successful revisions and frame counter resets.
Actual GPU fault recovery and device-reset behavior still require coordinator validation.

EditorTests now includes PostProcessPanel.cpp and PostProcessWorkspace.cpp. The new headless fixture
exercises real panel Hide/Collapse/Undo behavior and creation-modal Cancel, queued Escape, accepted Create and controller rejection
followed by cancellation. Creation uses a fake controller; it does not prove filesystem/import success.
The numeric gesture is initiated through the document; mouse drag/release/Escape remains a desktop gate.
Source review corrected a compound doctest assertion to a Boolean local, compatible with this vendored
version's expression decomposition. No compile or execution result is claimed.

LUT authoring now preserves selective grading inheritance when grading is already enabled. Clearing a
lookup does not activate disabled grading. Parameter-override and LUT-selection clicks explicitly finish
an unfinished numeric gesture so the discrete action has separate Undo history. Model coverage is added;
rapid actual-panel transitions and inherited-setting preservation remain coordinator UI checks.

Stable source lists and exact core/editor filters are maintained in coordination/post-processing-status.md
outside the repository. Rebuild both core and editor targets after these changes; the earlier 140-case
combined core result does not cover this new batch. Full visual, standalone and measured performance
acceptance remains open.

## Custom material relinking — November 2 source batch
Custom-effect rows now expose the existing typed material picker, including generated material resolution
and drag/drop support. A material can be repaired/replaced without removing its row or resetting its stage,
enabled state or stack position. Main's inline effect badges and row spacing are preserved.
Added a document regression for replacement, unchanged neighbors/stage/enabled state, preview, one-command
Undo/Redo, no-op selection, rejected self-reference and save/decode persistence. This is model coverage,
not evidence that the picker or GPU stack has been exercised; execution is pending coordinator validation.
Coordinator: rebuild EditorTests/EditorDev, run PostProcessDocumentTests and PostProcessPanelInteractionTests
in DevDist and DebugASan. In a QA profile, replace a disabled After UI row between two other stages; verify
position/state retention, one Undo/Redo, save/reopen and rendered stage ordering. Repeat with a missing
material and with a compiled material subasset selected/dropped from an authoring source. Verify incompatible
asset drops are rejected and inherited stacks activate only in basic mode. No production acceptance claimed.
