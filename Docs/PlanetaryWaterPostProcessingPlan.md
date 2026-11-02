# Planetary, water and post-processing delivery plan

Expanded active-goal scope requested November 1, 2026. This plan is a completion checklist, not a claim
that the features exist. The previous usage stopping point is removed by the user. All work stays on
master; preserve authored QA projects and validate changes in separate test projects first.

## Current completion mandate — November 2, 2026

The user reaffirmed completion of all planetary, water and post-processing implementation and
production validation, including good runtime performance and fast asset loading/processing.
Earlier checkpoints and deferrals below are history, not permission to omit unfinished features.
The main chat owns integration, builds and testing; other chats/agents remain stopped as requested.

Finish and validate the active cloud stability/compiler batch, then close the outstanding work below
in substantial implementation batches with editor checks between batches:

- Planetary: complete character/dynamic-contact support and failure-safe frame/force stepping,
  multi-planet atmosphere/weather composition and performance, far-distance physics, ground/space
  transitions and standalone parity. The active character frame-following rejection and solver
  capacity rollback gaps are not production-complete behavior.
- Water: finish consuming opaque color/depth in water shading, scene/environment reflection,
  finite-volume absorption/refraction, contact foam, layered water and underwater transitions;
  complete extensible sea/lake/river/pond workflows, in-world tools, buoyancy and planetary support.
  Preserve both realistic and stylized controls. Prototype optics capture alone is not completion.
- Post-processing: close the remaining effect, volume, custom-stack, standalone and editor UI
  acceptance cases in PostProcessingAcceptance.md, including error recovery and performance.
- Performance: measure cold and warm startup, imports/reimports, shader generation and cancellation,
  camera motion, time preview, cloud quality, water and combined effects. Address the measured
  bottlenecks without silently dropping selected effects or lowering authored quality.
- Combined acceptance: save/reopen authored content, exercise Undo/Redo and recovery, run focused
  automated and lifetime checks, test real editor workflows and packaged standalone behavior,
  and record hardware, scene, quality/resolution and timing distributions for performance claims.

Do not report these systems production-ready or complete until the corresponding implementation
and end-to-end evidence exist. Keep concrete outstanding cases visible during continuation.

## Current transition to water

User direction: finish the current post-processing testing pass, repair the issues it exposes, then
move to water. Keep the remaining scope here rather than extending post-processing indefinitely.

- Current automated evidence: custom-stack core suites passed 30 cases / 5,505 assertions; editor
  document, preview and material-picker suites passed 21 cases / 756 assertions. Visual builds and bounded Scene/Game checks passed; full effect acceptance remains partial.
- Checkpoint evidence: tested custom material selection, ordering, stage selection, enable/disable,
  undo/redo, save/reload and combined built-in effects in Scene/Game views. Volume coverage remains partial.
  Repair crashes, lost edits, broken rendering and other blockers found in this pass; retest fixes.
- Record every incomplete effect/UI case in PostProcessingAcceptance.md with its actual evidence.
  Preserve standalone custom-stack parity, performance captures, device recovery, motion/cut/rebase
  acceptance and broad authoring lifecycle checks if not completed in this pass.
- Water follows this checkpoint: shared profiles/volumes provide underwater effects; implement
  customizable ocean, lake, river and pond bodies, surface/shore foam and extensible style controls,
  with editor authoring, planetary coordinates, physics interaction and performance validation.
- Deferred performance work includes slow GPU VFX pipeline warmup and duplicated editor/test
  compilation. Advanced planetary requirements and the unresolved ASan orbital test timeout remain
  open. Deferral is not completion or production-release approval.
## Editor authoring overhaul — requested November 1

The Environment, Post Processing and upcoming Water panels need purpose-built visual authoring,
not a larger vertical property list. This is implementation scope alongside the systems below,
not a documentation-only recommendation. Preserve expert access without making every technical
parameter the default workflow. Reuse established graph/timeline interaction patterns where the
data has meaningful time, curve or dependency structure; do not force unrelated scalar settings
into a node graph merely for appearance.

- Shared workspace layout: clear asset/scene context, compact persistent actions, category navigation,
  a central visual authoring area, and a focused selection inspector. Responsive layouts must support
  both a narrow dock and a maximized workspace without hiding primary actions or horizontal overflow.
- Environment: prominent time/playback controls, labelled sky-color and lighting tracks with curve
  editing, key selection and snapping; visible sun/moon context; cloud shape/style previews; organized
  atmosphere/fog/quality controls. Separate preview state from saved authoring and explain active overrides.
- Post Processing: an ordered effect stack with enable, reorder and stage controls; contextual effect
  inspectors; visual look previews and before/after comparison; color curves where supported; clear
  inherited/overridden state and volume influence. Keep advanced custom materials extensible and make
  ordering and camera-UI impact visible. Preserve document history and current data contracts.
- Water: body selection and creation tools; ocean/lake/pond presets and spline-based river authoring;
  viewport handles for extent, depth and flow; wave/foam/style previews; underwater profile links;
  quality/cost controls. Integrate a focused material/style editor instead of an undifferentiated list.
- In-world authoring is required: add/select/move river knots and tangents, insert/remove segments,
  edit width/depth/flow with viewport handles, and display current direction and water boundaries.
  Post-process boxes/spheres need interactive extent/radius and blend-distance handles, not only
  selection wireframes. Water extent, wave direction and local influence handles must share the
  editor's picking, snap and transform conventions. Draw affected regions and distinguish selected,
  hovered, inherited and disabled states. One drag is one Undo transaction; cancellation restores
  its starting state. Test handles at distant origins, with rotated/parented objects and after
  save/reopen. Keep numeric fields as a precise alternative to every interactive operation.
- Toolkit work: reusable section/navigation, curve, gradient, preview and stack controls with consistent
  spacing, typography, focus, disabled/selected states, tooltips and human-readable values with units.
  Use theme tokens rather than panel-specific hard-coded palettes. Ensure keyboard paths and useful
  empty/error states. Add reusable controls only when backed by these actual workflows.
- Performance: avoid per-frame asset scans, unnecessary preview recompilation and repeated layout work;
  cache previews with explicit invalidation, virtualize large lists and pause hidden preview work.
  Measure UI/preview cost separately from scene rendering before claiming improvements.
- Acceptance: create/edit/save/reopen real assets through the UI; test Undo/Redo, resets and overrides,
  narrow/wide docking, scaled displays, search and keyboard focus, drag/drop and effect reordering,
  missing assets and unsaved-close recovery. Capture representative screenshots and record remaining
  cases. A cosmetic screenshot or passing model test is not proof of authoring usability.

Batch this with water authoring after the current native foundation build. The existing bounded
post-processing smoke checkpoint remains valid, but it does not close this newly requested overhaul.

## Delivery order

1. Finish current rendering stability and startup performance work. Preserve shader resource contracts,
   visual output and runtime performance when reducing forward shader compile time. Complete cold-start,
   reload, sanitizer and production validation before publishing installers.
2. Validate and save the current planetary checkpoint. Defer advanced planetary integration until after
   post-processing and water, as requested by the user on November 1, 2026.
3. Implement shared post-processing profiles and blending with editor authoring and runtime integration.
4. Implement water bodies and their planetary, rendering, physics and post-processing integration.
5. Resume planetary integration: atmosphere selection/composition, planetary weather, collision-safe
   gravity/frame changes, moving supports, physical travel, far-distance physics and standalone parity.
6. Validate the combined Earth/space/Mars showcase and document verified limits before release.

Water and post-processing design/audits can proceed alongside planetary fixes, but neither replaces
unfinished planetary acceptance. Existing production release work remains tracked separately; do not
label unfinished new systems production-ready merely because an installer builds.

## Planetary acceptance

### Checkpoint before changing focus

The user approved deferring advanced planetary integration until after post-processing and water.
Preserve the full acceptance scope below; a checkpoint is not planetary completion.

- Validate and save the current native capsule-contact and conservative rotation-bound batch. These are
  prerequisites, not an automatic gravity-alignment feature. Run the complete focused filter against
  a binary rebuilt after the final source edits.
- Keep the existing character/parented-collider frame-following restrictions until transactional rotation,
  support ownership and collision coverage justify lifting them. Do not imply physical ship boarding
  or arbitrary planetary landing from the scripted flight demonstration.
- Preserve the authored QA scenes and document the demonstrated flight/far-origin workflows separately
  from outstanding multi-planet weather, moving-frame character and standalone acceptance.
- Record test results and remaining release gates before committing. The cold-start and production
  packaging gates remain independent; this checkpoint does not authorize bypassing either.

### Full acceptance (still required)

- Ground-to-space-to-ground travel without loading screens, with correct atmosphere, stars, clouds,
  fog, lighting and day/night at distant world origins and around translated/rotated planets.
- Multiple visible atmospheres and deterministic weather ownership/blending at transitions.
- Collision-safe character orientation, gravity changes, moving/rotating support, boarding/detaching,
  and appropriate zero-gravity behavior. No unchecked teleport or rotation through obstacles.
- Terrain/collision streaming and landing support with explicit supported scale; a fixed sample landing
  pad does not prove arbitrary planetary landing.
- Repeatable far-distance physics, rebase, save/load and standalone tests; frame-time and memory budgets
  measured on the test hardware, including long sessions and repeated transitions.

### Checkpoint validation — November 1, 2026

- DevDist `KeireTests` build passed after the final physics source edits (nine-step incremental build).
- Focused capsule overlap, filtering/thread-affinity, rotation-bound and cast checks: 5 tests,
  357 assertions passed. Includes stationary queries at world origins 0 and ±1e12 metres.
- All tests in `PhysicsSystemTests.cpp`: 37 tests, 276,544 assertions passed. Reproduce with
  `KeireTests.exe --source-file=*PhysicsSystemTests.cpp` from the DevDist output directory.
- clang-format dry-run and `git diff --check` passed. MSVC emitted warnings, including constant-arithmetic
  overflow warnings while compiling rejected extreme-input tests; the build was not warning-free.
- No editor flight, standalone, sanitizer or production-package acceptance was performed for this
  checkpoint. These results do not validate continuous capsule rotation, automatic gravity alignment,
  planetary boarding, multi-planet weather or the full planetary acceptance list above.
- Original QA projects were not modified. Advanced planetary integration is deferred by user request;
  resume with bounded continuous pose clearance and transactional character/support integration.

## Environment art direction across styles

- Sky, atmosphere, clouds, fog, stars, sun/moon and day/night support realistic and stylized authoring;
  realistic appearance is the default preset, not a restriction on the system.
- Reusable profiles/presets for realistic, painterly, cartoon/cel-shaded, low-poly and fantasy looks.
  Presets must expose editable parameters rather than fixed named modes that constrain customization.
- Author sky/horizon/zenith gradients and time-of-day curves, celestial disk size/color/halo and star
  appearance, cloud shape/height/coverage/detail, lighting ramps, edge softness and fog color/density.
- Separate artistic overrides from physical defaults with clear controls for inheritance, blend behavior
  and reset. Support intentional nonphysical colors and lighting without breaking numeric validation.
- Keep authored styles consistent in edit/play mode, from surface to space, across planets and through
  post-processing. Preserve large-world precision and camera-local temporal history in every style.
- Provide extension points for custom sky/cloud materials or shading where the renderer supports them,
  with explicit resource contracts and diagnostics. Do not promise arbitrary shader compatibility.
- Acceptance includes realistic and strongly stylized example scenes, day/night and altitude transitions,
  preset round trips, Undo/Redo, standalone parity and measured quality/performance settings.

## Post-processing system

Delivery should use two or three substantial implementation batches, as requested November 1:

1. Build an end-to-end authoring/runtime workflow: reusable profiles, global/local Volume components,
   deterministic per-camera blending, core effects, dedicated dockable panel and renderer integration.
   Test serialization, scene editing, edit/play behavior and representative visuals together.
2. Complete the remaining effects and extension points, presets, diagnostics, performance work and
   fixes found in the first pass. Validate multiple cameras, large-world coordinates and standalone output.
3. Use a final batch only where measured regressions or visual polish require it. Do not turn each
   individual control into a separate build/desktop test cycle. Keep underwater integration ready for water.

The batch count is a workflow target, not permission to omit acceptance checks or describe unfinished
effects as complete. Preserve compatibility with existing exposure, bloom and camera fullscreen effects.

- Dedicated dockable Post Processing panel for profiles, effect stacks, previews and diagnostics. Add a
  Post Process Volume component with global/local mode, profile reference, priority, weight, blend distance
  and bounds. Effects belong to reusable profiles rather than an expanding camera Inspector.
- Keep camera controls limited to participation and volume-layer selection where possible. Preserve legacy
  camera fullscreen effects and provide an explicit migration path into profiles without changing their order.
  Volume authoring needs scene gizmos and selection/edit workflows as well as Inspector controls.

- Reusable, versioned profile assets; global and local volumes with priority, weights, bounds, blend
  distance and deterministic overlap rules. Camera overrides and layer filtering.
- HDR-aware effect ordering with explicit exposure/tone-map boundaries; reuse existing bloom, tone
  mapping and antialiasing rather than introducing competing pipelines.
- Exposure controls, color grading/LUTs, white balance, contrast, saturation, bloom, vignette, film grain,
  chromatic aberration, depth of field and motion blur, with per-effect toggles and quality budgets.
  Validate depth/velocity-dependent effects across origin shifts and camera cuts.
- Extensible custom fullscreen effects with documented inputs, ordering, resource lifetime and backend
  requirements; safe failure behavior and diagnostics for invalid effects.
- Dedicated authoring UI with categorized controls, searchable settings, presets, reset/Undo/Redo,
  overrides, blend preview, edit/play parity and readable units without unnecessary scientific notation.
- Camera-local histories and resources; correct resize, multiple cameras, HDR output and standalone
  behavior. Tests for serialization, blending, effect order, invalid values and history invalidation.

### Required editor acceptance for each post-processing batch

Use [PostProcessingAcceptance.md](PostProcessingAcceptance.md) as the per-effect and per-workflow execution
record. Unexecuted rows remain explicitly untested.

Current first-batch implementation status (not acceptance): the profile document and panel are connected to
workspace ownership, the Window menu, Asset Browser opening and asynchronous profile creation. Source saves
check the previously read bytes for external changes; exit offers Save/Discard/Cancel. Live preview publishes
transient asset revisions and retains an authored baseline for restoration on close, including failed source
reads. Regression cases have been added for preview publication failure/rollback, saved-baseline restoration,
document lifecycle and Asset Browser routing. The DevDist editor now builds and these workflows have partial
automated and hands-on evidence in PostProcessingAcceptance.md. Focused document/preview tests passed
6 cases / 352 assertions; core profile, volume, evaluation and environment tests passed 24 cases / 5,320
assertions in DevDist and DebugASan. Scene/Game chromatic previews and Scene bloom intensity have been
exercised, including baseline restoration. This is not full workflow, standalone or performance acceptance.
Volume authoring polish, remaining effects and the full acceptance matrix are still outstanding.

Large implementation batches do not reduce hands-on coverage. After each substantial batch, use the
actual editor as a user would, then fix and retest findings before advancing. Keep an acceptance record
with one row per supported effect and one per authoring workflow: build/revision, reproduction steps,
expected result, observed result, screenshot or recording, performance evidence where applicable,
and status (not tested, passed, failed, or blocked). Never infer a pass from another effect or an
automated test. Unimplemented effects remain incomplete rather than being counted as tested.

For every supported effect, record the tested build, profile/scene, settings and visual result. Exercise
disabled, default, useful low/high values, reset and combined-effect cases. A control existing does not
prove that its effect renders. Mark unimplemented or unavailable backend effects explicitly.

- Create, rename, duplicate, assign, save and reopen profiles; test asset moves, missing profiles,
  invalid data, hot reload and useful diagnostics without destroying last-good data.
- Exercise every panel and Inspector control with mouse and keyboard, readable units, narrow/maximized
  layouts, scrolling, search, focus, tab order, undo/redo and restored workspace layout.
- Create global, box and sphere volumes, edit bounds with gizmos, move through blend boundaries,
  overlap priorities/weights, and test inactive entities, disabled components and camera layer filters.
- Compare Scene preview, Game view, Play Mode and standalone output. Test multiple cameras, camera
  cuts, origin shifts, resolution changes and temporal-history resets without stale effects or flashes.
- Verify HDR/exposure, bloom and tone-map order, legacy fullscreen effects and HUD treatment. Compare
  enabled/disabled effects and quality tiers with CPU/GPU timing and memory evidence.
- Retest discovered defects after fixes, including affected combinations. Capture representative
  screenshots and record remaining visual uncertainty; automated success alone is insufficient.
- Run complete user journeys from a fresh project: create a profile, add and assign a volume, author
  a look, enter Play Mode, save, close and reopen the editor, and verify the same look and settings.
  Include unsaved-change cancellation, failed saves and recovery; no silent loss of authored work.
- Observe animated effects and transitions live, including camera movement and repeated volume crossings.
  Still screenshots cannot establish freedom from flicker, stutter or temporal trails. Exercise all
  supported effects individually, representative combinations, and the full supported stack together.

## Water system

- Authored oceans/seas, lakes, rivers and ponds using shared extensible profiles and body-specific
  geometry. Planetary oceans follow the planet's surface and reference frame; rivers support spline
  authoring, width/depth variation, flow direction and junction handling.
- Realistic and stylized presets with independently adjustable color, absorption/scattering, depth
  tint, opacity, roughness, reflections/refraction, normals, wave shape/speed/direction and shoreline.
- Multi-scale waves, shoreline/intersection foam, wave-crest foam, wakes and interaction ripples;
  configurable foam textures, thresholds, persistence, scale and color. Explicit quality/fallback modes.
- Underwater transitions, surface crossing, underwater fog/color and visibility; integrate with the
  post-processing volume system and atmospheric fog without applying attenuation twice.
- Shared surface-height/normal/flow queries for rendering, swimming and buoyancy. Define physical versus
  visual-only wave behavior and keep CPU/GPU representations consistent within documented tolerances.
- Far-origin precision, planet-relative coordinates, temporal stability, bounded geometry/texture
  streaming, camera-local resources and scalable reflection/caustic options where supported.
- Editor body creation, spline/shore tools, material/profile inspector, presets, edit-mode animation,
  diagnostics and Undo/Redo. Runtime extension points for custom wave, flow and interaction providers.
- Test ocean, shore, lake, river, pond, underwater, ship/wake and stylized scenes. Validate seams, terrain
  intersections, transparent sorting, depth, shadows and multiple water elevations/cameras/backends.

## Evidence and release gates

Performance is continuous work in every feature batch: implement the required behavior, measure representative
CPU/GPU frame time, startup/import time, allocations and peak/resident memory, optimize measured bottlenecks,
then rerun correctness and performance checks. Cover editor UI as well as standalone rendering/physics. Record
hardware, scene, quality, resolution, cold/warm state and frame-time percentiles; averages alone hide stutters.
Keep realistic and stylized quality options scalable without silently dropping authored effects. Do not defer
all optimization until the systems are finished or trade away correctness to meet a timing target.

Each batch records code changes, focused tests, actual editor screenshots, standalone checks and measured
performance. Shader reflection compatibility alone does not prove visual or GPU-performance parity.
Use DevDist for iteration; use official clean production packaging and deployment checks for releases.
Record unsupported behavior explicitly. No blanket AAA/production claim without these acceptance results.

## Initial source audit

Existing code includes BloomSettings, Environment-panel bloom controls, tone mapping, antialiasing and
fullscreen material hooks. MaterialShadingModel includes Water, but that enum is not evidence of a complete
water-body, ocean, river or buoyancy system. Audit those implementations before selecting extensions.
Planetary gaps and previous evidence remain in PlanetaryIntegrationBatch.md and the current validation
handoff. Start by reconciling those records with source rather than repeating completed fixes.

### Post-processing integration findings

`CameraComponent::FullscreenEffects` and the render packet currently hold one material for each of three
stages: BeforeTonemapping, AfterTonemapping and AfterUi. RenderSurfaceRecording executes these hooks.
Keep existing authored camera effects compatible when adding ordered effect stacks; migration must preserve
their stage and material identity. Do not automatically grade the HUD with scene effects.

BloomSettings already specifies exposed linear HDR thresholds, soft knee and radius measured in bloom-buffer
pixels. Profile evaluation must feed the existing bloom/tone-map path rather than apply exposure or bloom twice.
Evaluate immutable camera-local settings before render submission. Volume distances must use large-world
relative coordinates so translating a scene to a distant origin does not change its blends. Define stable
priority ties and discrete-property transitions; never interpolate asset IDs or enable flags numerically.
Temporal effects require history invalidation on cuts, origin/frame changes and incompatible profile changes.

### Water integration findings

`CreateOpenPbrMaterial` currently maps Water and ThinTranslucent to the transparent shader output. This is
material routing, not an ocean simulation or water-body implementation. The initial native/editor source
audit found no WaterBody, RiverSpline, Ocean or Buoyancy component implementation under those names.

Keep body geometry and queries separate from visual styling: a shared body/profile identity should connect
surface position, normal, flow and velocity queries to rendered waves, foam and underwater effects. Planet
oceans need planet-relative radial coordinates; lakes and ponds need bounded local surfaces; rivers need
continuous spline parameterization and stable flow through segments. Do not derive physical water height
from screen-space depth or reflections. Document wave-query tolerances and visual-only detail bands.
Allow inexpensive styles to opt out of expensive reflections/refraction/caustics while retaining consistent
body shape, interactions and deterministic query behavior. The transparent path must be validated for water
depth and layering rather than assumed sufficient because it renders ordinary translucent materials.

### Remaining lens-effects integration audit — November 1, 2026

Source inspection confirms that `RenderSurfaceRecording.cpp` currently records bloom, tone mapping,
then profile display effects. `RenderPostProcessing.cpp` binds only the copied display color;
its four texture bindings are the same input. Do not implement depth of field or motion blur by
adding samples to that display-only pass.

- Add a bounded HDR lens stage with explicit sampled-depth and velocity requirements before tone mapping.
  Reconcile its ordering with bloom and existing before-tone-map fullscreen effects explicitly.
- `RenderSurfaceStateInternal.h` already tracks sampled-depth validity, inverse view projection and
  precise world origin. Require current-surface validity before reconstructing focus distance; never
  reconstruct absolute float positions at planetary coordinates.
- Velocity is optional in the current tone-map path (black fallback). Motion blur must distinguish
  unavailable velocity from valid stationary geometry, and reset on cuts, discontinuities and resize.
- Preserve the current TAA history boundary: `RenderToneMap.cpp` copies history before display-space
  grain/chromatic effects. Do not accidentally feed animated grain back into temporal accumulation.
- Verify enabled/disabled resource allocation and bounded sample counts, foreground depth rejection,
  bright HDR highlights, camera cuts, origin shifts, multiple views and HUD exclusion. A serialized
  DepthOfField/MotionBlur enum entry is not evidence that either effect renders.

This audit defines implementation constraints, not completed lens effects. The incremental DevDist
KeireClient build for the preceding panel/search changes was started separately; its outcome and
visual acceptance must be recorded when available.

### Lens-stage prerequisites found in source and reference audit

The earlier instruction to insert an HDR lens stage before tone mapping is incomplete: current
RecordToneMap combines TAA and tone mapping. AMD recommends stabilized input before DoF. Resolve this
ordering explicitly (including history ownership and HDR highlight preservation), rather than silently
placing depth blur before the current TAA or using already-clipped display color.
Reference: https://gpuopen.com/manuals/fidelityfx_sdk/techniques/depth-of-field/

RecordSampledDepth in RenderShadowRecording.cpp binds SceneDepthPipeline and draws mesh geometry;
it does not bind material alpha-clipping/displacement evaluation. Its current consumers do not establish
material-correct depth for lens effects. Reuse or extend material-aware depth/velocity output, and prove
cutout, displaced and skinned silhouettes match scene color before accepting DoF. Transparent geometry
also needs an explicit documented depth policy. Forward and deferred availability must be covered.

Thin-lens implementation should use signed circle-of-confusion radius to distinguish near/far fields,
consistent physical units, bounded pixel radii and resolution-aware quality. Avoid absolute float world
positions. Acceptance must include near-focus singularities, focus-plane zero blur, aperture response,
large-distance limits, depth discontinuities and disabled-effect resource cost. This research has not
implemented or validated the effects; both remain outstanding.

Depth-of-field numerical groundwork added in PostProcessLens.h: signed full-resolution CoC radius from positive camera-space distances, explicit millimetre/metre conversion, physical f-number, resolution-scaled radius limit, infinity-as-sky handling and invalid-focus rejection. PostProcessEvaluationTests now exercise focus, aperture, resolution, near blur clamping and 1e12-metre focus. This helper is not connected to rendering yet; it is a reference contract for the GPU implementation, not a completed effect. Focused DevDist build/tests were started; results pending.

Material-aware input audit: RenderSceneRecording.cpp RecordDeferredDepthVelocity routes through DrawScene with DeferredDepthVelocity phase, writing workset.Depth and GBufferVelocity. ShaderGraphHlsl.cpp outputs current-minus-previous unjittered screen UV motion via (currentNdc - previousNdc) * (0.5, -0.5). Preserve this sign convention when sampling blur/history. Forward availability, transparency policy and depth/material parity remain required; no new lens rendering path has been enabled.

### Superseding implementation checkpoint — lens and LUT batch

The numerical lens helper is now connected to an initial renderer implementation. HDR temporal resolve precedes bounded 24-tap lens passes and tone mapping. Dedicated material-aware opaque/cutout depth and velocity feed the passes; per-workset depth and HDR buffers are lazy and reused. This supersedes the earlier source-audit statements that no lens rendering path exists. The combined DevDist build is still running, so compilation, visual correctness and GPU cost remain unproven.

Review gates before acceptance: correct near-field cross-surface leakage; add explicit camera identity/reset history semantics without rejecting legitimate fast planetary movement; correct fullscreen draw counters; verify cuts, origin rebasing, resize, independent views and transparent/HUD policy. Foreground dilation/scatter and configurable quality remain outstanding.

LUT schema, importer dependencies, deterministic override evaluation, authoring controls and GPU application are implemented. In the rebuilt editor, the swap-red-blue fixture visibly exchanges channels and the identity fixture restores the original appearance. Numerical image comparison, weight blending, missing-resource recovery and standalone validation remain open. See PostProcessingAcceptance.md for the focused native and managed test results.

Camera history now has explicit identity and reset revisions. Native and managed regression tests passed; the updated editor test build is in progress to verify authoring cuts versus smooth planetary travel. The visual lens test confirms focus edits and disabling work, but strong defocus exposes repeated highlights and overly sharp silhouettes. Resolve sampling/coverage quality before accepting depth of field; do not treat the successful enable/disable check as full acceptance.

### Water integration prerequisites from current source

Read-only source audit while post-processing builds: ShaderGraphHlsl.cpp currently samples an environment cubemap for the refraction path; MaterialGraph/ProgramArtifact Water routing selects transparency but does not provide scene-color refraction. Water rendering therefore needs an explicit opaque-color/depth snapshot and correct render ordering, not simply another transparent material preset.

Physics exposes queries and body-state changes but no force-at-point application contract for buoyancy. Implement real force/torque integration with ownership/thread rules rather than teleporting floating bodies. PlanetFrameResolution already exposes precise planet-relative observer coordinates and orientation; ocean altitude must use that frame instead of absolute world Y. These are prerequisites, not completed water features; implementation remains after post-processing validation.

### Current validation handoff

Post-processing focused DebugASan validation now passes 53 cases / 17,637 assertions, including camera
history reset and profile evaluation contracts. The acceptance matrix has been reconciled with the
implemented lens/LUT stages and recorded local-volume UI checks; partial checks remain explicitly partial.
A broader rebuilt DebugASan planetary/physics filter is running in
Build/Validation/planet-physics-postprocess-asan-tests.log. Do not count it as passed until completion.
This filter includes atmosphere overlap/occlusion and translation invariance, far-origin support-contact
reconstruction, gravity sampling and frame motion contracts. It does not establish rendered multi-planet
composition, arbitrary surface landing, standalone parity or controlled GPU performance.

### Long-session job-registry fix in progress

Source audit found scheduler Registry and ScopeState Jobs weak-record vectors appended on every job
submission without removing expired records. Continuous physics/import work therefore retained expired
control blocks until scheduler/scope destruction. Added locked, amortized compaction to both registries;
live jobs remain registered and scan thresholds grow with live population. Regression coverage measures
actual control-block allocation retention over 4,096 records and checks cancellation of an older blocked
scoped job after repeated short submissions. Incremental DevDist build is running in
Build/Validation/job-registry-compaction-build.log; compilation and sanitizer validation are pending.
Do not attribute the duration of the still-running planetary/physics sanitizer suite to this issue without
measurement. That suite started before this change and does not validate the new cleanup.

Job-registry DevDist validation: official four-step KeireTests build passed. The complete
ArchitectureFoundationTests.cpp filter passed 18 cases / 9,033 assertions, including allocation retention,
older blocked-job cancellation after registry compaction, dependencies and scheduler shutdown behavior.
Logs: Build/Validation/job-registry-compaction-build.log and job-registry-compaction-tests.log.
clang-format dry-run and diff whitespace checks passed for the affected files. Fresh ASan execution of
this cleanup remains required; the concurrent planetary/physics ASan process uses the preceding binary.

Additional job-lifetime audit: terminal JobState objects retained Description.Dependencies indefinitely. FinishJob now swaps prerequisite handles out under its existing lock and releases them outside that lock, preserving the job result. Added a regression retaining the final successful handle while checking that an earlier failure payload is released after prerequisite handles are dropped. Build and ASan validation of this additional change are pending; no frame-time improvement is claimed.

Terminal prerequisite cleanup and rooted post-processing cook focused validation passed: 3 cases / 20 assertions on the rebuilt DevDist executable. Expanded scheduling/scene/profile suites and fresh sanitizer coverage remain pending. The older planetary/physics sanitizer process remains active and cannot validate this newer job cleanup.

Expanded DevDist scheduling, scenes, profiles and volumes validation passed 54 cases / 9,609 assertions, including the previously timed-out starter-project test. See Build/Validation/post-process-job-lifetime-expanded-tests.log. No GPU performance or planetary completion claim follows from these CPU tests.

Rebuilt DevDist planetary/physics regression passed: 105 cases / 286,385 assertions, 34.880 seconds wall-clock, exit 0, with PhysicsSystemTests.cpp and all *Planet*Tests.cpp selected. Log: Build/Validation/planet-physics-job-lifetime-devdist-tests.log. This exercises the new job cleanup with far-origin physics and planetary math/capture contracts; it is not rendered travel or controlled performance acceptance. The pre-change DebugASan run remains incomplete after over an hour and needs execution-state diagnosis; do not count it as passed.

Filter reconciliation: the sanitizer filter selects 154 cases, not just the preceding 105-case subset. The remaining PhysicsQueryPhaseReproduction, PhysicsComponent, PhysicsAuthoringComponent, PhysicsMaterialAsset and PhysicsGravitySampling source filters also passed on rebuilt DevDist: 49 cases / 161,571 assertions, exit 0. Log: planet-physics-additional-devdist-tests.log. Together the two runs cover all 154 selected cases / 447,956 assertions. The still-running older ASan binary has no completion result; no debugger CLI was found in PATH or the standard Windows Kits x64 debugger directory.

The original DebugASan planetary/physics run was explicitly terminated after exceeding one hour without a completion report. It is TIMED OUT / INCOMPLETE, not passed; no deadlock or sanitizer defect is proven. Process identity, CPU/memory and elapsed time are recorded in Build/Validation/planet-physics-postprocess-asan-timeout.log. Rebuilding DebugASan after both job-lifetime fixes and starting focused scheduling/profile/volume validation before subdivided planetary/physics runs.

Fresh DebugASan eight-step build completed with the job and dependency fixes. Scheduling/profile/volume source filters passed 30 cases / 9,212 assertions, exit 0. Logs: post-process-job-lifetime-asan-build.log and post-process-job-lifetime-asan-tests.log. PhysicsSystemTests now runs separately with a 180-second validation limit and separate stdout/stderr; remaining planetary/physics groups are pending.

Fresh isolated PhysicsSystemTests DebugASan run reached its explicit 180-second limit (exit 124) without completed output; it remains unvalidated. Log physics-system-job-lifetime-asan-tests.log records the timeout. Starting individual filtered cases to identify the stall; passing DevDist results do not resolve this sanitizer discrepancy.

- Job scratch optimization: replaced eager 64 KiB allocation/zeroing with a lazily allocated per-job monotonic resource. DevDist ArchitectureFoundationTests passed: 20 cases / 9,046 assertions. ASan verification and physics timing comparison remain pending; no measured speedup claimed. Earlier isolated ASan physics cases 10 and 11 both exceeded their 60-second limits; cases 1–9 passed individually.
- Follow-up: the same 20 job tests / 9,046 assertions also passed in fresh DebugASan. Physics timing comparison remains pending.
- Lazy scratch follow-up physics evidence: isolated weak-field radial/uniform gravity case 11 passed under DebugASan in 9.498331 seconds (8 assertions), versus the earlier 60-second timeout. Cases 12–37 then passed (26 executed cases, 4,944 assertions); doctest's summary includes filtered first/last cases, so its printed 37 is not the executed count. Logs: physics-lazy-scratch-case11.log and physics-lazy-scratch-remaining.log. This is a bounded same-machine observation, not a controlled editor benchmark. The intact circular-orbit case is running separately with a ten-minute limit; no orbit pass claimed yet.
- Intact orbit ASan run physics-lazy-scratch-orbit.log exceeded its ten-minute limit and exited 124. Its error log was empty. This remains incomplete, not a pass or proof of deadlock; diagnose its cost separately without weakening orbit/rebase assertions. The other isolated physics cases passed as recorded above.

### Ordered custom effects — data contract in progress

Profile schema 3 adds an ordered customEffects override with at most 16 material entries. Each entry stores enabled and stage (0 before tone mapping, 1 after tone mapping, 2 after UI). Old schema 1/2 profiles remain readable and round-trip without migration until edited features require it. Disabled entries remain dependencies so a packaged project can enable them later. An overridden empty list explicitly clears inheritance. Material type and self-reference validation run during import; Fullscreen shader compatibility must also be checked by renderer resolution.

This is not complete custom-stack support. Next: select a deterministic inherited stack per camera, execute each stage after its legacy camera slot without changing existing ordering, expose add/remove/reorder/enable controls with document undo and explicit camera migration, validate failure diagnostics, then test multi-camera, bounds, save/reopen and standalone. Do not count the data-format work as rendered acceptance.

Custom-stack integration follow-up: implemented deterministic whole-stack selection at influence >= 0.5 using existing scene/entity priority ordering; explicit empty override clears inherited custom effects. Rendering iterates enabled entries in authored order within each stage, following the unchanged legacy camera slot, using existing Fullscreen material binding and failure behavior. Added Custom Effects tab/search, material picker, stage selection, enable/remove/move controls and transactional document editing. Added selection and document history/save regressions. Compilation, GPU visual acceptance and explicit legacy-camera migration are still pending. The running first build predates some integration edits; perform another official incremental build before relying on its test executable.
- Custom-stack review: defer the selected-stack copy until volume priority resolution finishes, avoiding repeated vector copies for overlapping volumes. Existing selection/clear/priority tests cover the unchanged result. Formatting dry-run passed for the new/modified custom-stack C++ files before this final formatted change. First build remains live; no test pass claimed yet.
- First 137-step DevDist core/test build completed successfully. Final incremental build plus focused profile/evaluation/volume tests are running in post-process-custom-stack-final-build.log and post-process-custom-stack-tests.log. Authoring review corrected modern material selection: the Custom Effects panel now reuses AssetPicker with AssetSystem type resolution to select compiled material subassets rather than authoring-source IDs. Import validation rejects direct non-material source IDs. Database source resolution intentionally returns no record for compiled subassets, which are checked by the runtime material loader. Editor build and visual checks remain pending.

Build-performance follow-up: KeireEditorTests/premake5.lua directly compiles many KeireClient/Source/Editor files into its own target; ApplyCommonProjectSettings uses per-project intermediate directories. After the 133-step custom-stack editor-test build, KeireEditorDev still required 161 steps. Investigate extracting genuinely shared editor logic into a library, comparing compile definitions/test hooks first; do not share object files across incompatible settings. No build-layout change was made during this validation batch.

## Water integration audit and implementation gates — November 1

Source inspection after the post-processing UI checkpoint identified these prerequisites. None of the
items below should be interpreted as an implemented water feature.

### Physics force contract

`PhysicsWorld` currently exposes body state/kinematic targets and gravity, but no public force or torque
submission. `PhysicsSystem.cpp::Step` already applies radial gravity through the native force accumulator.
Buoyancy must use force integration, not repeated position or velocity replacement.

- Add force-at-center and force-at-point submission for dynamic bodies, with explicit newtons and
  simulation-axis units. A point is local to `WorldOrigin()`, matching existing query positions.
- Compute the moment arm against the actual center of mass, not the entity origin. Calculate torque
  and intermediate products in double precision before checking finite float representability.
- Reject unavailable/static/kinematic bodies, nonfinite inputs, wrong-thread and closed-world calls
  before mutation. Check accumulated force/torque overflow, not only each incoming sample.
- Define forces as lasting one successful simulation step; multiple submissions accumulate. A failed
  validation must neither wake a body nor partially add torque. Zero force must not wake resting bodies.
- Translation rebasing must preserve queued force and torque. Current scene owner transfers keep the
  solver and dynamic bodies in inertial axes (`SceneRuntimeSpatialAccess::TransferSceneOwner`), so
  preserve pending vectors unchanged across those transfers; rotating them would be incorrect.
  If a future transition rotates the actual solver basis, rotate pending vectors with that basis.
  Add an owner-transfer regression proving the next step preserves a queued force and torque.
- Test mass-dependent acceleration, off-center angular response, cancellation, step reset, waking,
  rejection without mutation, and identical behavior at local and trillion-metre world origins.
  Include a queued-force rebase test and sanitizer coverage for lifecycle/thread rejection.

### Shared water surface queries

- Use body-relative double coordinates before evaluating waves; never feed absolute planetary float
  positions directly into phase/noise functions. Bound time phase to avoid long-session precision loss.
- A surface sample supplies height/displacement, normal, surface velocity, fluid density and coverage.
  CPU buoyancy and GPU displacement share parameters and conventions, with numerical parity tests.
- Flat lakes/ponds, spline rivers and spherical oceans need distinct geometry and boundary handling.
  Do not approximate a planetary ocean as a world-axis plane. River distance/width/depth/flow require
  explicit junction and endpoint rules. Sample failure outside a body is normal, not a zero-height sea.
- Start buoyancy with bounded probe sets and displaced-volume forces, then add relative-flow drag.
  Derive buoyancy direction from the local gravity field rather than hard-coded world up. Avoid applying
  duplicate forces where water bodies overlap; resolve body priority deterministically.

### Rendering and underwater dependency

`ShaderGraphHlsl.cpp` currently obtains refraction radiance from `SampleEnvironment`. That provides no
view of submerged scene geometry. A full water implementation needs a camera-local opaque-color/depth
snapshot before water rendering, explicit pass ordering, and resource recreation on resize/recovery.

- Refraction uses opaque depth to limit thickness and reject foreground leaks. Provide an environment
  fallback for unavailable screen samples; never read and write the same render target implicitly.
- Share depth/absorption conventions between surface shading and underwater post-processing. Evaluate
  submersion against the displaced surface, with a stable transition band near the camera waterline.
- Integrate underwater effects into profile evaluation without mutating authored profiles or duplicating
  camera effects. Specify how water influence combines with existing local/global volume priorities.
- Bound wave, foam and reflection work by quality settings and visibility; keep camera histories separate.
  Track resource memory and GPU cost with one camera, Scene plus Game, and a standalone player.
- Acceptance fixtures must cover shore foam, intersecting geometry, transparent objects, dry/wet camera
  transitions, submerged/above-surface views, far origins, and planetary rotation/rebasing.

### First implementation sequence

1. Force API plus focused regression tests, and shared surface-sampling contracts with flat/spherical
   geometry tests. Preserve current physics behavior before connecting any water component.
2. Water profiles/components, serialization and dependency cooking; editor creation, Undo/Redo and
   interactive style controls; CPU/GPU waves and bounded buoyancy integration.
3. Opaque scene sampling, surface/shore foam, underwater profile composition and river authoring.
4. Extensive real-user editor validation, standalone parity and measured performance. Keep deferred
   rendering features explicit instead of treating a transparent water material as a finished system.

### Native water foundation checkpoint

Implemented PhysicsWorld AddForce, AddForceAtPosition and AddTorque with finite/accumulator
validation and native center-of-mass torque. Radial gravity checks combined queued-force overflow
before backend submission; ordinary uniform-gravity scenes no longer submit zero radial forces.
The initial force build passed 5 tests / 61 assertions. The subsequent eight-step DevDist build
passed the combined force, surface, buoyancy and gravity-sampling suites: **21 tests / 358 assertions**.
Evidence: `Build/Validation/water-foundation-devdist-build.log` and `water-foundation-devdist-tests.log`.

WaterSurface validates immutable plane/rectangle/ellipse/sphere definitions, caches orientation axes,
subtracts double frame origins before queries, reports signed depth and finite-depth membership,
and includes frame/flow velocity. Spherical height is rotation-invariant. Relative surface offsets
avoid rounding small corrections into large absolute coordinates. Tests cover six planetary
directions, distant origins, rotated waterlines/planes, boundaries, frame velocity and invalid ranges.

EvaluateWaterBuoyancy computes spherical-probe displaced volume, finite-depth clipping, hydrostatic
force and center of buoyancy. Tests include independent numerical integration, radial/zero gravity,
native weight balance at a trillion-metre origin and an asymmetric hull with an offset center of mass.
It is a locally planar small-probe approximation; shoreline coverage tests probe centers.

DebugASan build and focused tests passed: 21 cases / 358 assertions, with no sanitizer errors reported (`water-foundation-asan-build.log`, `water-foundation-asan-tests.log`). This validates the native foundation only, not water rendering or authored gameplay integration.
Warnings in new rejection tests were cleaned up after the passing DevDist run; formatting checks pass.
Remaining foundation gates include pending-force checkpoint/clear semantics and frame-transition
integration before authored buoyancy. Waves, drag, body arbitration, river splines, rendered water,
underwater composition, authoring components and editor handles are not implemented by these helpers.

### Effect activation usability (implementation awaiting validation)
Basic post-processing authoring uses one Enabled control per effect; editing a parameter activates its effect and required overrides in one undoable document edit. LUT selection similarly activates color grading. Explicit inheritance switches remain under Advanced overrides. A searchable effect workspace replaces the long settings list, with a compact selector at narrow widths. Build, regression and desktop acceptance remain pending; custom-stack edits now activate their override automatically in basic mode; drag transaction grouping still needs review.

Pending-force audit: SceneRuntime checkpoint restoration calls PhysicsWorld::SetBodyState, which currently preserves native accumulators. SceneRuntimePhysics also uses that method for authored velocity updates, so clearing forces unconditionally there would change normal velocity-edit behavior. Introduce an explicit clear/restore contract and test both callers before integrating buoyancy into scene stepping.

Drag-history follow-up: UiFrame::LastItemState exposes Activated, Active and DeactivatedAfterEdit, and UndoContext supports transactions. Use these for one-drag/one-Undo; explicitly finish or cancel a transaction on panel closure, profile switching, failed edits and reload before shipping the new controls. Do not merely merge commands by label across independent gestures.

Post-processing authoring model validation: the DevDist EditorTests rebuild passed. All three PostProcess test sources passed together: 15 cases / 787 assertions, including activation, legacy LUT upgrade, document history and preview (post-process-authoring-devdist-tests.log). This does not validate the new panel layout; the KeireEditorDev build and visual checks remain pending.

Added a pending owner-transfer force regression in SceneRuntimePlanetTests: compare identical dynamic travelers with and without transfer to a rotating planet and back, submitting force and torque before each transfer. It checks position and both velocity vectors against the inertial control. This new case is not yet built or run; queue it after the current editor build.

Editor workspace smoke: KeireEditorDev DevDist build passed (post-process-workspace-editor-build.log). Reopened PostProcessingSharedImports maximized, loaded the saved profile, enabled Bloom with one click, observed Enabled status and visible glow in Scene view, then one Undo restored Inherited status and cleared the dirty marker. Saved profile baseline remains unchanged. Remaining checks include direct parameter activation, LUT/custom activation, search, narrow layout, creation dialog and broader effect coverage. Editor remains open for continuation.

Workspace UI follow-up: dragging inherited Bloom intensity from 0.1 to 0.5 activated Bloom automatically; one Undo restored 0.1 and inheritance for this injected gesture. Search 'focus' selected Depth of Field and showed Focus Distance. Resizing the dock to about 500 px switched to the compact selector, and restoring its width restored navigation. Found activation hint removal shifted fields and the lens footer clipped at narrow width; source fixes now keep the hint row stable and wrap diagnostics, awaiting rebuild/retest. Profile remains clean; search currently contains 'focus'.

Queued-force transfer validation: DevDist core rebuild passed (four targets). The new owner-transfer case passed 34 assertions; the combined SceneRuntimePlanetTests and PhysicsForceTests suites passed 19 cases / 3,131 assertions (planet-transfer-force-regressions.log). This verifies current inertial owner-transfer behavior, not the unresolved pending-force checkpoint semantics. QA editor closed normally; saved profile SHA-256 remains 20DBDE6A3D03DEACA894A43E120E2FB2B865ED5EE37977B5A01CA17ADBC2C175. UI hint/footer polish rebuild is running.
Layout polish acceptance: DevDist KeireEditorDev rebuild passed (post-process-layout-polish-build.log). In the rebuilt editor, one Bloom Enabled click activated the effect without moving its parameter fields; one Undo restored inherited state and cleared the dirty marker. At approximately 500 px panel width, the compact selector, parameters and wrapped lens guidance remained visible. Restored the wide layout and closed the QA editor normally. Saved profile SHA-256 still matches the recorded baseline. LUT/custom activation, creation dialog and robust continuous-drag history acceptance remain open; this checkpoint does not claim complete post-processing production acceptance.

Pending-load checkpoint implementation: body snapshots and scene checkpoints now carry queued force and
torque. Restore replaces both accumulators; authored velocity synchronization reads and reapplies the live
snapshot. Added native replay/clear/invalid-state checks and a scene restore plus authored-velocity test.
DevDist KeireTests build is running in physics-checkpoint-loads-build.log. Sanitizer and regressions remain
pending; do not treat the checkpoint gate as complete until these pass.
Checkpoint batch follow-up: TryGetBody now reads all fields under one native body lock instead of separate interface lookups plus a force lock. This reduces lock operations structurally; measured frame-time impact is not claimed. Final incremental build must include this edit before running regressions.

Water wave implementation (not yet validated): WaterWaveField adds up to 16 directional Gerstner waves
with wavelength/amplitude/speed/direction/phase/steepness controls, cached coefficients, analytic normal
and particle velocity, and a bounded inverse horizontal query for physics. Positions subtract a double
origin before phase evaluation; temporal phase uses periodic reduction. Common horizontal steepness
is reduced to a total derivative bound of 0.8 to prevent folding and permit convergent inverse queries;
vertical amplitude is preserved. This is a water-local planar mathematical layer, not GPU integration,
curved-ocean mapping, a spectrum/FFT ocean, rendering, foam, buoyancy coupling or an editor feature.
Four source tests cover distant translations, long-time periodicity, derivative agreement, surface
inversion/nonfolding and input rejection. New files require regeneration after the current build.
Reference: NVIDIA GPU Gems chapter 1, Effective Water Simulation from Physical Models.
https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models

Combined validation update: initial physics-checkpoint DevDist build failed on a new planetary test using
multi-argument CAPTURE, unsupported by the vendored doctest. Split it into separate calls. The coordinator
started regenerated KeireTests DevDist build water-planet-checkpoint-build.log after both worker source
handoffs, adding WaterWaveField source/tests and final snapshot-lock/atmosphere edits. No tests have run
against this combined batch yet. Post-process continuous-edit and creation changes are source-ready in
the other chat; their EditorTests, sanitizer and full EditorDev/UI builds remain queued centrally.
Combined DevDist validation PASSED: water-planet-checkpoint-build.log completed all 85 steps; water-planet-checkpoint-tests.log passed 140 cases / 442,186 assertions. Includes physics checkpoint loads, authored velocity preservation, one-lock snapshots, water surface/buoyancy/waves, atmosphere capture/uniforms and planetary reconstruction/transfer coverage. New character-clearance files added later are not included. ASan, new post-process batches, rendered water and visual/standalone acceptance remain open.

Water shader bridge batch (source only except compiler probe): WaterWaveField::CaptureRenderPatch captures
phase in double precision at a nearby anchor, packs a fixed 528-byte block with range-checked coefficients,
and retains the double patch origin for relative vertex submission. WaterWaves.hlsli evaluates the same
Gerstner displacement, analytic normal and particle velocity. Vertical displacement is not flattened by
steepness limiting. Added packing/reference and rejection tests; these C++ changes are not yet built.
WaterWaveProbe.hlsl compiled successfully to DXIL and SPIR-V using the repository KeireShaderCompiler
(exit 0); it has NOT been dispatched/read back. No rendered-water or CPU/GPU execution parity claim.
Renderer resource submission, curved-ocean mapping, material shading, shore/foam, underwater composition,
components and visual authoring remain required.

Validation in progress: combined DevDist KeireTests regeneration/build (water-weather-postprocess-build.log). PlanetWeatherTransportProbe compiled successfully to DXIL and SPIR-V; dispatch/readback remains pending. BuiltinSky fragment DXIL failed due to duplicate declarations through slash/backslash include aliases (PlanetAtmosphereSet and PlanetWeatherUniforms); planetary worker is correcting shader include protection. No rendered-weather completion claim.

Shader retry verified: after alias-safe include guards, BuiltinSky VSMain and PSMain compile successfully to both DXIL and SPIR-V using the staged KeireShaderCompiler. PlanetWeatherTransportProbe also compiled in both formats. These are compiler checks only; hardware dispatch/readback, rendered appearance, fog/surface parity and standalone flight remain pending. Core build session remains live.

Added Scripts/Tests/Source/ShaderInvariantGpuProbe.cpp: one-group compute dispatch with float4 readback, finite/error checks and NaN-initialized output to detect missing writes. MSVC compilation and clang-format dry-run passed; git diff --check passed (line-ending warnings only). D3D12 PlanetWeatherTransportProbe returned records=3 finite=1 maximum_error=0. Water and extra-record negative control are still running; Vulkan and visual acceptance pending. Combined core build advanced into 66 native compilation/link steps.

Hardware evidence: D3D12 weather (3 float4 records) and water-wave (2 records) both returned finite=1 maximum_error=0. Weather negative control requested a fourth unwritten record and correctly returned finite=0/exit1. Added repeatable Scripts/Tests/test-shader-invariants-gpu.py launcher with per-process timeout and explicit finite-output negative-control validation; Vulkan run pending in Build/Validation/shader-invariants-vulkan.log. This verifies probe invariants, not complete rendered appearance or frame-time budgets.

Vulkan weather probe passed (3 records, finite output, zero numerical error) and rejected the unwritten-record negative control. Coverage limitation: these initial records use vacuum and establish cloud-event ordering/clipping/decomposition only. Nonzero atmosphere segment integration is assigned a shader-only analytic regression before broader composition claims.

Vulkan water-wave probe and missing-write negative control also passed; the initial Vulkan launcher run exited0. Extended weather probe now has six records: original vacuum invariants plus analytic nonzero-air extinction/radiance and transparent-event partition invariance. Both-backend extended run is in progress in planet-weather-air-gpu.log; no pass asserted yet.

Extended six-record weather D3D12 positive check passed: finite=1, maximum_error=6.25849e-06 against1e-5 tolerance. Combined launcher FAILED because the subsequent seven-record negative-control process timed out after180seconds; no surviving probe process observed. Vulkan extension was not reached. Earlier vacuum negative controls remain valid, but this extended suite is NOT fully passed; investigate lifecycle/startup timing and rerun missing checks.

Combined DevDist KeireTests build PASSED66/66 and staged runtime. Expanded focused core regression run started (water/weather/character clearance/physics/postprocess); editor integration build started using existing generated graph. Added flushed probe lifecycle stage markers and preserved timeout stdout/stderr in Python launcher to diagnose extended negative-control timeout. Instrumented probe host compiled successfully.

Instrumented D3D12 seven-record negative-control retry completed with expected exit1: sdl-ready/device-ready/pipeline-ready/submitted/gpu-complete, then records=7 finite=0 maximum_error=6.25849e-06. Confirms missing-write detection for expanded shader; earlier timeout root cause remains unproven. Vulkan expanded-only run started in planet-weather-air-vulkan.log. Reviewed draft physics query allocation and requested allocation-free filtering before integration.

Core focused run:184cases,183passed/1failed;449350assertions,449349passed. Failure was old EnvironmentCapture disabled-atmosphere expectation with still-active clouds and9invalid candidates. Updated regression to require active-weather rejection, then disable all weather before expecting no atmosphere capture. This test-only correction and nodiscard casts are pending rebuild/retest. Expanded Vulkan weather six-record positive and seven-record negative passed (max6.25849e-06); launcher exit0.

Water optics source batch: WaterOptics.hlsli adds exact unpolarized dielectric reflectance including underwater total internal reflection, Beer-Lambert path transmission, and complementary reflection/refraction composition with explicit artistic depth-tint approximation. WaterOpticsProbe tests normal/grazing/equal-index/total-reflection limits, path partitioning, zero-depth and radiance conservation. Renderer integration is not yet present. Both-backend GPU probe started; pending results.

Water optics GPU launcher PASSED on D3D12 and Vulkan, including NaN-initialized extra-record negative controls; Vulkan maximum absolute error2.98023e-08 (tolerance1e-5). New WaterBodyComponent authoring header/source/tests delivered and formatted, with integration patch pending current editor-build freeze. Supports0-16 serialized waves and double geometry/local flow; no render/buoyancy integration claim. Custom water Inspector delegated next to replace raw wave-slot list.

Initial BuiltinWater shader validation failed at DXIL vertex: BuiltinLighting.hlsli requires environment texture/sampler/parameters/irradiance declarations absent from dedicated pass. Rendering agent is correcting shared BRDF dependency structure or real environment wiring; dummy bindings are not accepted. Water draw source and pipeline integration remain unbuilt/unrendered. Existing optics-helper GPU passes do not validate this new surface shader.

BuiltinWater revised shader (pure shared BRDF include plus actual cloud/fog/atmosphere transport bindings) compiled VSMain/PSMain successfully to DXIL and SPIR-V. MSL translation pending. This is compilation, not rendered appearance or performance. Renderer external integration revised for lazy pipeline creation and mesh/water depth-order merging; VFX tail ordering remains existing limitation. EditorTests build111/146 and active; shared-source hold remains.

2026-11-02 full work resumed by user. Main restored seven water source/test files from the verified SHA-256 hold manifest (copies retained) and integrated water component registration, custom Inspector dispatch, renderer capture/draw ordering/pipeline cleanup and shader build registration. Renderer patch applied cleanly to current master; minimal registration/Inspector patches applied against current context. This is unbuilt source integration, not rendered acceptance. Planetary and post-processing chats resumed with distinct ownership; main retains all builds/test execution/editor/staging and shared documentation. Next: inspect new water API/render binding integration, finish source registration, then coordinate a combined source freeze and DevDist validation.

2026-11-02 combined validation started: built-in shader generation completed with exit0, including registered water stages through normal Windows script. Main regenerated DevDist native graph and started KeireTests build (resumed-water-planet-core-build.log, session49852, 4 jobs). Planetary 18-file character/frame/query batch and PostProcess custom-material replacement are handed off and frozen; workers continue read-only review/external drafts. Added InspectorWaterBody test-target source registration and default-scene component creation regression. No new native test pass or rendered-water acceptance claimed yet.

Combined build found MSVC C2338 from stable_sort's temporary aligned_storage for WaterRenderPatch. Main replaced this with sorting indices plus a typed aligned vector, retaining original capture order at equal depths and moving large shader blocks only once per output entry. Added finite-depth rejection and stable-order regression. Incremental retry live session68668, resumed-water-planet-core-retry.log. Original failed session49852 terminated exit1. Prepared separate WaterIntegrationQA.keirescene in PostProcessingSharedImports (camera/light plus Still Water, Gentle Ripples, Crossing Waves); original SandboxShowcase untouched. QA scene has not been imported or visually validated yet.

Water QA fixture preparation verified from JSON: three schema-v1 rectangular Water Bodies at X=-24,0,24; still, two-wave ripples and two-wave larger crossing waves; independent camera and directional light. Component IDs match registration; dimensions/spacing positive. This checks authored fixture structure only; importer/render verification remains pending. Main confirmed retry build session68668 remains live with compiler workers; no restart on unchanged log alone. Next focused core filter: *PlanetCharacter*Tests.cpp,*CharacterMovementAccessTests.cpp,*SceneRuntimePlanetTests.cpp,*PhysicsComponentTests.cpp,*PhysicsForceTests.cpp,*Water*Tests.cpp,*PostProcess*Tests.cpp,*UndoTests.cpp,*EnvironmentCaptureTests.cpp,*PlanetEnvironment*Tests.cpp,*PlanetWeather*Tests.cpp. Stage/editor only after native validation.

Core DevDist build completed successfully after water stable-index-sort compatibility fix and const-handle correction in new character fixture. Integrated reviewed ±1e12 character movement/rebase regression. Focused run:177 cases,171 passed/6failed;185490 evaluated assertions all passed. Six failures all throw required-scene-name validation from water fixture SceneDefinition{}. Corrected two core fixture sources and matching editor fixture to named definitions; core rebuild/rerun pending after current editor build. Planetary far-origin collision/mask/no-replay checks passed in this run, not proof of full frame completion. EditorTests build active session44437, resumed-water-postprocess-editor-build.log,135 steps. Do not start concurrent build.

Editor DevDist build passed135/135. Focused editor regression run passed55/55 cases,2266/2266 assertions (resumed-water-postprocess-editor-tests.log), covering WaterBody authoring, PostProcess sources and selected scene-document/drop/gizmo workflows. Corrected core fixtures rebuilding in session98410, followed by full177-case focused rerun. Staging/visual acceptance still pending.

Corrected core focused rerun PASSED177/177 cases,185530/185530 assertions (resumed-water-planet-core-tests-final.log). Together with editor55/55 cases,2266 assertions, this closes the initial source batch's focused DevDist gate. Started normal DevDist stage-editor.ps1, resumed-water-planet-stage.log; editor still closed. ASan, visual water/material replacement acceptance, standalone parity and remaining feature integration are not covered by these results.

2026-11-02 initial water visual checkpoint: DevDist stage completed all161 steps (1099.5s). WaterIntegrationQA imports and opens through the Project browser; all three surfaces render and Water Body Inspector exposes Appearance/Quality/Geometry/Fluid/Waves. Observed frame samples approximately9ms are incidental, not a benchmark. User rejected appearance: broad moving circular foam patches and difficult individual-wave workflow. This is not visual acceptance. Main changed foam input from normalized wave height to horizontal Gerstner area compression, removing the redundant per-vertex amplitude summation. Water-wave GPU probe now covers flat/zero-compression, convergent crest, and raised-but-uncompressed waves; D3D12 and Vulkan numerical probes plus deliberate missing-write rejection PASS with maximum error0. Normal builtin-shaders.ps1 completed exit0. Updated shader is not yet staged into the open editor; visual confirmation pending. Coordinated water setup presets and advanced-wave UI are in progress. Depth-aware absorption/refraction, shoreline/contact foam, finer surface detail, underwater composition, spherical rendering and complete river authoring remain required work. User removed the previous usage cutoff.

2026-11-02 second batch source integration: water setup/looks and simple wave height/direction controls landed with Advanced wave list. Entity > Water and Hierarchy Water/Water Child offer Sea/Lake/Pond, configuring the new component within one history transaction. New authoring/creation/rollback tests are unrun. Imported reviewed pure WaterHydrodynamics and WaterForceBatch headers/sources and tests from external draft, without activating automatic scene loads or registering Buoyancy yet. Remaining bridge must preserve pending external forces, checkpoint tick/latch state and fixed-time phase. Post-processing worker integrated seven reviewed picker/cache-fallback files, all unbuilt. Planetary worker authorized to integrate its transaction/prepared-load batch; shader ABI stays reserved. Main retains all validation; do not claim these sources passed the earlier232-case gate.

Cloud performance regression reported during second build: user sees10-11FPS on Defined/Dense Billows; main read-only UI capture confirmed12FPS83.2ms with both Scene/Game visible and Scene67% scale. Preserved saved configuration under Build/Validation/cloud-billows-user-20261102/ProjectSettings. MSAA4, deferredHybrid, Irradyn quality, volume48view/6shadow, coverage.77, extinction.0374, no fog. Source confirms MSAA bypasses half-resolution temporal trace and uses separate full-resolution attenuation/radiance passes. This is a candidate multiplier, not isolated measured root cause; build90709 is also active. Await user authorization for profiler/A-B settings capture, preserve configuration afterward. Next optimization must preserve MSAA edge colors and colored fog transmission; simply compositing resolved color would lose per-sample edges. External audit/draft assigned; no live renderer mutation during build.

### Billows MSAA comparison — November 2, 2026

Saved the user's complete ProjectSettings before testing. With the same upward cloud view and panel dimensions, single Scene view at 67% dynamic resolution, ten-second sustained captures measured mean asynchronous GPU durations of 47.05 ms with MSAA 4x and 19.37 ms with AA None. CPU submission averaged 0.31/0.29 ms; command recording 0.35/0.31 ms. Both captures reused the cloud-light cache (zero builds, one reuse per observed frame). Evidence: Build/Validation/cloud-billows-user-20261102/{msaa4,none}-single-view.csv. Four-job compilation and a video overlay were present, so this is diagnostic evidence, not a release benchmark. GPU durations cover the renderer frame, not isolated cloud passes.

Restored MSAA 4x, disabled temporary profiler telemetry, closed the profiler, and returned to the original Scene/Game views. Rendering settings matched the saved baseline byte-for-byte before the final repeated capture; verify the final restoration hash again. No camera or cloud-shape settings changed. MSAA currently bypasses half-resolution cloud history; an external integration draft now preserves MSAA samples using separate history attenuation/radiance composites. Not yet integrated or validated.

The final comparison restoration hash matched the saved Rendering.keiresettings baseline. The second core batch passed 272 cases / 193,142 assertions (water-planet-second-core-tests.log), after correcting a new checkpoint test to use the RayCast hit vector correctly. This validates the focused native cases, not editor acceptance, ASan, standalone parity or full multi-planet completion.

Integrated the reviewed MSAA cloud-history change on master: half-resolution trace/resolve now feeds per-sample attenuation/radiance blends, with distinct pipeline cache entries and existing recovery/shutdown ownership. Added blend and cache-policy regression cases. Built-in shader generation and Ninja generation completed successfully. Native build cloud-msaa-core-build.log is underway; new regression execution, shader reflection verification, ASan lifecycle checks and actual editor visual/performance comparisons remain required. Exact depth-edge fallback remains potentially expensive. Multi-planet volume ABI draft remains external until all producers/consumers are ready together.

Cloud MSAA validation update (2026-11-02): DevDist KeireTests build completed successfully. The focused cloud, render-pipeline-contract, and environment-capture run passed 175 cases and 244599 assertions (`Build/Validation/cloud-msaa-core-tests.log`). This includes the new blend and pipeline-index regressions. The second-batch KeireEditorTests build is in progress; updated-editor GPU measurements, visual edge checks, and lifecycle validation remain pending. No post-change frame-time improvement has yet been measured.

Continuation evidence: shader compiler JSON reflection for the updated cloud composite reports four samplers, two storage textures, one storage buffer and three uniform buffers; renderer declares six sampled textures. Read-only backend audit is pending to distinguish a real cross-backend binding mismatch from explicit-register behavior. Do not claim reflection parity yet. Frozen external planetary volume handoff contains 48 files; all draft and live-base SHA-256 checks passed before integration. It remains external/unbuilt to preserve the current cloud performance comparison. Water runtime bridge and optical surface integration continue as external drafts while the editor test build runs.

Cloud binding audit resolved: current generated SPIR-V retains texture/sampler bindings0-5, storage6 and uniforms0-2; generated MSL textures0-5, active samplers0/1/4/5, uniforms buffers0-2 and storage buffer3 match SDL declarations. D3D12 explicit SRV slots t0-5/storage t6 also cover direct texel loads. JSON4/2 is active-resource classification, not evidence of a binding failure. Keep Load semantics. Reusable concrete-artifact regression is being prepared externally; actual GPU/editor validation remains required.

Second editor batch validated (2026-11-02): KeireEditorTests DevDist build completed145/145; focused water authoring, post-process, asset-picker, scene document/drop/gizmo and project environment/atmosphere/planet run passed125 cases/4366 assertions. Logs: water-postprocess-second-editor-build.log and water-postprocess-second-editor-tests.log. Current-source cache/header artifact check passed concrete SPIR-V and MSL cloud-composite bindings; DXIL was hash-verified only, no disassembler available. Report: Build/Validation/cloud-composite-abi/binding-report.json. Normal DevDist stage-editor started in water-cloud-second-stage.log; actual updated-editor tests and performance remain pending. User Rendering settings still match baseline SHA256001B57EEBC785427DF9598058CE296486E543038834335528863FF0ADBADFB6E; editor absent from returned window inventory before staging.

Reusable cloud artifact verification now lives in Scripts/Tests/check-cloud-composite-bindings.ps1 and .py. Running with -RepositoryRoot and -OutputDirectory verifies current shader/include/compiler cache provenance, generated-header bytes, concrete SPIR-V/MSL resource addresses and exact texel-read semantics. Without -Dxc it reports DXIL unverified and returns2 unless -AllowUnverifiedDxil explicitly accepts partial coverage. Repository run with explicit partial acceptance passed; a corrupted-header-hash negative control returned1 as expected. DXIL/GPU checks remain pending. Staging session53391 remains live; do not start another native build or regenerate shared rendering inputs during staging.

## Single-chat continuation — November 2, 2026
User requested all planetary, water and post-processing work consolidated in the original chat to reduce usage. Other development chats were told to stop and preserve final handoffs; local agents are no longer running. Main owns every remaining integration and validation. Do not restart parallel goals without renewed user authorization.

Reviewed handoff queue (do not reapply already integrated work):
- Post-processing: seven-file picker/typed-load batch is already live; central125-case editor run and prior focused core run cover the new source tests. Remaining work is visual/lifecycle/ASan and broader acceptance in Docs/PostProcessingAcceptance.md. External proposal patches are provenance only.
- Planetary volumes: frozen48-file planetary-volume-draft plus separate4-file performance delta. Verify base hashes at integration; performance delta is based on frozen draft, not current live files. ABI15/16, legacy source compatibility, multi-body transport and single-body fast path require combined shader/native/GPU validation.
- Planetary frames: unfinished five-file planetary-frame-next-draft has no final manifest. Review/merge manually. Retain guards until pre-step failures, gravity bookkeeping, dynamic-contact bounds and parent/rotation handling are solved. Never treat this draft as completed transaction safety.
- Water runtime: twelve-file water-runtime-bridge includes registration, fixed-step wave phase, checkpoint time, moving-anchor velocity, atomic loads and six unrun regressions. Integrate as one feature; shared SceneRuntimePhysics/Scene headers overlap planetary frame work.
- Water optics: water-optics-next contains capture/settings/finite-path/sky-cache foundations, but consumer/reflection binding, volumetric-cloud reflection, stacked transparency and underwater remain incomplete. Do not enable optics presets without the consumer. Main continues implementation.

Current validation sequence: finish live staging session53391, run saved-settings cloud performance/edge comparison and new water/PP editor checks, then integrate the reviewed remaining batch. Preserve user QA content and mixed staged/unstaged work. Standalone parity, ASan and full acceptance remain outstanding.

Second DevDist stage completed169 steps/1015.9seconds, exit0. Reopened PostProcessingSharedImports through normal run-staged launcher. Main corrected draft WaterMediumPath shallow-ray/large-ellipse epsilon and unsupported-shape behavior plus HLSL reserved identifier; three-record GPU probe and missing-write controls passed D3D12/Vulkan (max error0). Draft still external; optical consumer remains pending.

Updated editor performance capture: updated-msaa4-auto-single-view.csv records708 measured application intervals/10.008s, average70.74fps, p95 15.626ms/p99 16.359ms/max16.782ms. Asynchronous GPU mean14.121ms/max16.226ms. Scene retained original camera/cloud settings and MSAA4; automatic resolution settled84%, versus original67%, and compilation was no longer active. This is a strong diagnostic improvement but not a controlled fixed-resolution benchmark. Telemetry disabled afterward. User reports cloud pattern changes and frame-time spikes whenever automatic dynamic resolution adjusts. Source confirms EnsureSurface replaces/retires whole resources on changed extent and EnsureCloudTemporalResources discards history on size change. Need history continuity, allocation/reuse strategy and stable resolution control; not yet fixed. Preserve legitimate cut/projection/weather invalidations. Add repeated scale-change visual and timing regression before accepting automatic resolution.


2026-11-02 dynamic-resolution follow-up (solo): implemented eight-sample median, distinct GPU timing
IDs and sixteen-sample post-adjustment settling. DevDist RenderSettings regressions passed 28 cases /
1819 assertions (`dynamic-resolution-stability-tests.log`). Added transfer of submitted cloud history
across surface replacement, preserving old writer fence retirement and normalized-UV reuse at a new
extent. New transfer/failure/continuity tests are in the active DevDist core build
`cloud-resize-history-build.log`; not yet validated in the editor or with ASan. Other render targets
still allocate on resize, so allocation-spike performance work remains open. No staging/publication.

Dynamic-resolution core verification completed: DevDist build passed; selected cloud, settings and
render-pipeline contracts passed 181 cases / 245945 assertions (`cloud-resize-history-tests.log`).
Clang-format dry-run and git diff --check passed. QA Rendering.keiresettings SHA256 remains
001B57EEBC785427DF9598058CE296486E543038834335528863FF0ADBADFB6E. DebugASan build/tests
started separately (`cloud-resize-history-asan-build.log`); GPU/editor resize acceptance still pending.
Frozen planetary handoff audit: all 48 draft hashes valid; only CloudTemporalResources.h and
RenderCloudTemporal.cpp now differ from saved bases and require a three-way integration preserving
this resize fix. No external handoff files integrated by this follow-up.

2026-11-02 continuation: found and removed inverse-pixel-dimension equality from cloud reprojection
eligibility. The normalized-UV resize regression still rejects projection-type/FOV changes. Updated
DevDist and DebugASan focused runs both passed 42 cases / 1947 assertions
(`cloud-resize-extent-tests.log`, `cloud-resize-extent-asan-tests.log`). The initial wider ASan build
also passed 41 cases / 1944 assertions before this final regression. Editor visual checks remain pending.

Prepared the frozen volume handoff + performance delta outside the live tree at
`coordination/planetary-volume-reviewed-integration`, with a per-file live/prepared hash manifest.
Preserved main resize-history changes and added a DXC include guard to CloudVolumeGeometry.hlsli.
Preflight reflection proved the volume numerical probes retain one noise sampler; their original
compute shader used fragment space2 and the host declared no samplers, causing D3D12 pipeline creation
failure. Prepared probes now use compute space0; the prepared host supports optional --cloud-noise
with an initialized constant 3D texture and sampler. Host compilation passed. This is a real resource
binding test, not visual/noise-quality coverage. Numerical positive/missing-write controls on D3D12
and Vulkan are running in `planet-volume-preintegration-gpu.log`. No planetary draft source copied
into the live repository yet; only manifest-listed files may be applied, not the copied untouched
shader dependency snapshot used for isolated preflight. No active worker chats or native build.

Planetary preflight and integration update: the full transport probe passed five records on D3D12
and Vulkan (maximum absolute error 5.96046e-08), with sixth-record missing-write rejection. Candidate
probe passed three records on both APIs (maximum error 0), with fourth-record rejection. Fixed an
unsupported HLSL struct ternary in CloudVolumeView before the candidate run. Evidence:
`planet-volume-preintegration-gpu.log` and `planet-volume-candidate-preintegration-gpu.log`.
All 51 manifest-listed files were integrated into the live master worktree after hash preflight;
prior live versions are preserved outside the repository at
`coordination/planetary-volume-main-preintegration-backup`. Copied shader dependency snapshots were
NOT applied. Builtin shader generation is active (`planet-volume-integrated-shaders.log`). New native
source discovery/build/tests, ABI reflection, legacy permutation compilation, editor visuals,
standalone parity and multi-planet performance remain unverified. Earlier resize/core test results
predate this 51-file integration and must not be presented as validation of the integrated batch.

2026-11-02 integrated runtime batch: builtin shader generation completed successfully (session
57553 exit 0). Integrated the five-file staged-frame target draft after reviewing its complete delta;
added side-effect-free ValidateStepClock before scene movement commits and reused it in native Step.
This catches invalid duration, exhausted ticks and restored water-duration mismatch before consuming
character input. New tests include staged-start velocity overflow, converted statics, clock exhaustion,
pending-force preservation and thread rejection. It does NOT solve native solver-capacity rollback or
the existing dynamic-contact frame-motion guard; those remain explicit unfinished work.

Integrated the 12-file automatic Buoyancy runtime bridge after SHA256 verification and manual merging
of SceneRuntimePhysics with the frame/clock changes. Runtime waves and forces share fixed-step phase;
checkpoint restore preserves that phase and accepted load latch; independent external loads remain
additive. Backups are in coordination/planetary-frame-main-preintegration-backup and
coordination/water-runtime-main-preintegration-backup. Rendering optics/underwater and spherical ocean
rendering remain separate unfinished features. No performance or editor acceptance claim for this bridge.

New-source generation, DevDist core build and broad relevant regression selection are now running
sequentially: planet-water-integrated-generate.log, planet-water-integrated-core-build.log and
planet-water-integrated-core-tests.log. Prior passing results do not validate this newer combined batch.

Additional integrated shader checks: legacy CloudViewTransportProbe and FogViewProbe compiled to
DXIL and SPIR-V without multi-planet opt-in (`planet-volume-legacy-shaders.log`). Current-source cloud
composite artifact/binding audit passed with AllowUnverifiedDxil
(`planet-volume-cloud-composite-abi.log`): SPIR-V/MSL concrete bindings and source/artifact hashes are
checked; DXIL reflection itself is still not parsed. The actual D3D12 numerical volume probes are
separate evidence, not proof of the fullscreen fragment pipeline. Build session21986 is still active,
advancing through generated VFX shader permutations before native compilation; do not restart it.

### 2026-11-02: camera-motion cloud instability (fixed resolution)
- User confirms distant clouds change/flicker during camera movement even with dynamic resolution disabled. Treat separately from retained resize history; no visual fix claimed.
- Density filtering currently uses longitudinal march spacing only. Prepared a provisional view-footprint filter outside the live tree in `coordination/cloud-view-filter-draft` (workspace), preserving legacy kernel wrappers and keeping camera filtering out of shadow density.
- Candidate compilation is running; not integrated or visually validated. Before promotion, separate half-resolution trace footprint from full-resolution surface footprint, add projection/filter regressions, and compare real heterogeneous cloud noise during fixed-resolution camera rotation. Investigate temporal depth rejection and angle-dependent march spacing if instability remains.
- Integrated native build found a test-only invalid multi-argument doctest CAPTURE; split the two captures. Incremental build and targeted integrated tests resumed; results pending.
- Follow-up: provisional footprint probe passed D3D12 and Vulkan (3 records, finite output, maximum error 2.58442e-08); proves projection invariants only. Initial complete trace variant compiled both APIs. Revised candidate now uses explicit pixelSpan=2 only in BuiltinCloudTrace; shared default=1 for full-resolution materials. Revised trace compile is pending (logs cloud-view-filter-final-*).
- Integrated KeireTests build completed after CAPTURE repair; targeted test execution is now running. No editor restart or live shader promotion yet.
- Integrated result: 479 cases, 472 passed and 7 failed by exception; all 693873 assertions passed. Failures traced to RenderingAssets.cpp manifest JSON allowlist still ending at volume ABI 14 while generated sources use 15/16. Updated that reader consistently with codec/runtime support. Incremental native build + same regression set rerunning; logs planet-volume-manifest-build.log and planet-water-integrated-core-tests-final.log. Do not report full integrated pass yet.
- Follow-up integrated run after manifest repair: 475/479 cases passed, all 693893 assertions passed. Four cases now reach real shader compilation but exceed the existing 30-second importer deadline on multi-planet fragment shaders. Deadline unchanged; investigating compiler expansion rather than masking asset-loading cost.
- Promoted view-footprint filtering (full-resolution surface default; explicit half-resolution cloud trace) plus explicit loop retention in new multi-planet transport/interval/weather loops. Backups and before/after hashes: workspace coordination/cloud-view-filter-preintegration. Full trace compiles DXIL/SPIR-V; loop-retained SPIR-V trace measured 26.016 seconds. Material importer cases rerunning (cloud-view-filter-importer-tests.log); no staged editor yet.
- Added maintained cloud-view-footprint GPU suite. Projection invariants passed both APIs with max error 2.58442e-08; fourth unwritten record rejected on both. This does not prove visual cloud stability. User's editor is now closed (Windows inventory and process check); preserve QA data and coordinate foreground before reopening while their game is active.
- Important validation correction: the first focused importer rerun used the old embedded CloudVolumeSource.h compiled into KeireTests. Stopped that obsolete run (parent 39976 and its compiler child); it is NOT evidence for new shader source. Need builtin shader regeneration and native relink before importer regression.
- Captured transparent importer shader snapshot under Build/Validation/planet-volume-import-reproducer and substituted source blocks exactly (asserted single matches) for isolated measurement. With view filtering + loop retention: 30.9069 s SPIR-V. Refactoring EvaluateCloudVolumeTrace to select one medium and call the single-profile lighting kernel once: 19.5298 s on same captured shader. Multi-medium branch remains complete. This is one measured shader, not a universal speedup claim.
- Extended sampled-cloud GPU regression to compare selected single-planet transport/depth against direct single-volume integration. D3D12 passed 5 records with max error 0; 6th unwritten record rejected. Vulkan run pending. Updated maintained footprint suite passed both APIs including missing-write controls.
- Vulkan sampled-cloud comparison also passed all 5 records with maximum error 0 and rejected the unwritten sixth record. Shader regeneration -> DevDist native rebuild -> combined 479-case selection is now running sequentially (session93446), logs cloud-view-filter-builtin-build.log, cloud-view-filter-native-build.log, cloud-view-filter-combined-tests.log. Do not start another native build or stage until terminal. Editor remains closed; visual acceptance still outstanding.

### Build-memory and iteration-cost follow-up — November 2, 2026
- Full scope reaffirmed by user; the completion mandate at the top supersedes earlier deferrals.
- Cloud shader generation passed, but native build session93446 terminated with MSVC C1060/out-of-heap and a compiler stack failure. Generated builtin/VFX byte-array headers were about 114 MB each; this is not a successful build or a visual acceptance result.
- Changed Windows and Unix builtin/VFX generators from per-byte integer initializers to adjacent escaped byte strings. Explicit sizes exclude the terminating byte; VFX upload selectors now use generated Size constants. All byte values validated through the actual Unix emitter and MSVC static assertions; a 1 MiB byte-string compile probe passed. Full 168-blob before/after SHA-256 and length verification precedes the rebuild.
- Cache-only builtin regeneration succeeded, but wrapper session68591 stopped early because its unset LASTEXITCODE guard treated a successful PowerShell-only script as nonzero. No VFX regeneration/native testing occurred in that wrapper. Corrected continuation session66428 is live: VFX regeneration, exact payload comparison, native build and combined regressions. Respect this process; do not start overlapping native builds.
- Separately prepared (NOT integrated) a VFX compute/render dependency split at workspace coordination/vfx-shader-dependency-draft. It preserves shared quaternion helper usage and routes compute sources separately in both platform generators, so cloud-only edits stop invalidating simulation kernels. Draft rebased on compact embedding scripts, with baseline hashes. CSSimulate SPIR-V compiles, but differs from previous binary (318040 vs 312272 bytes); semantic/reflection and broader entry-point validation are still required before promotion.
- Preserved QA baseline (five authored files + hashes) at Build/Validation/cloud-view-filter-qa-baseline. Scene/editor visual testing and sanitizer follow-up remain pending. No Git staging/commit or publication performed.
- Compact embedding payload comparison passed for all 168 shaders, but 128-byte literal chunks still caused MSVC C1060. Session66428 terminated before tests. A separate 10 MiB embedding probe using 8192-byte literals compiled successfully; changed both platform generators to bounded 8192-byte chunks to reduce concatenation intermediates. New generation/build/test session50442 is live (shader-embedding-large-* logs). Do not claim native success until this run finishes.
- Existing VFX dependency draft now predates the 8192-byte chunk adjustment: rebase its two generator scripts again before any future integration; do not overwrite current embedding fixes from that draft.
- Physics audit confirmed SceneRuntimePhysics.cpp still rejects enabled character controllers in the frame-following commit loop, in addition to the separate dynamic-contact preflight gate. Pending frame commands are consumed before water force preparation/Jolt step can throw. These remain explicit implementation gaps requiring real scene-level acceptance, not just the staged native-kinematic tests that currently pass.
- Native rebuild with 8192-byte embedding chunks now PASSED all 13 steps; generated payload hashes and upload lengths still match all 168 baselines. Actual Unix emitter also compiled with static assertions covering two full chunks and a one-byte tail (16385 bytes, every byte value). MSVC compilation still peaked around 3 GiB per process; further dependency isolation remains worthwhile. Combined tests are running in session50442, now reaching seven-texture importer cases. Pending user question asks whether to reopen/foreground Kéire after successful tests because their game is active; do not act on elapsed time as consent.
- Follow-up integrated result: session50442 finished with 475/479 cases passing and all 693893 assertions passing. Four cases threw SPIR-V fragment compiler timeouts at 30000 ms (generated graph, graphics templates, typed BSDF and seven-texture material). Native build success does not clear these import blockers.
- Direct compilation of the preserved seven-texture reproducer succeeds in 34.252 seconds (`Build/Validation/seven-texture-current-compile.log`), confirming a compiler-cost failure rather than invalid HLSL. An experimental duplicate density-loop hint took 34.494 seconds and was not promoted; the live source already has those hints. Further compiler dependency/kernel simplification is required; do not merely increase the timeout or claim asset-processing performance is fixed.

### 2026-11-02: reduce multi-planet shader temporary-array expansion
- Replaced the dynamically indexed array of expanded CloudVolumeParameters with local reads from immutable planet uniforms in PlanetVolumeTransport.hlsli. No candidate, density sample, lighting or atmosphere feature is removed. The exact preserved seven-texture fragment compiles to SPIR-V in 22.125 seconds versus 34.252 seconds before the change; this is compiler timing, not runtime GPU timing.
- All three planet-volume GPU suites passed on D3D12 and Vulkan after the change. Extended cloud sampling acceptance to eight mixed extinction/albedo media with a disabled middle slot, matched against the analytically equivalent single-volume reference. Six output records pass on both APIs with maximum error 5.96046e-08; unwritten seventh-record negative controls fail as expected. Evidence: Build/Validation/planet-local-parameters-gpu.log and planet-local-parameters-mixed-gpu.log.
- GPU test runner now records compiler wall time and host wall time separately; the latter includes device/pipeline creation and readback and must not be reported as kernel performance.
- Generated shader version is now 42, with compatibility assertions updated, so prior generated material caches receive the corrected shader. C++ formatting, Python syntax compilation and git diff --check pass.
- Repository DevDist KeireTests rebuild and focused graph/material/planet/cloud/water/volume suites are running in session95884, logs planet-local-parameters-native.log and planet-local-parameters-tests.log. Do not start another native build until terminal. Four previous importer timeout cases remain unverified until this run completes. No editor startup, Git stage/commit or publication performed this pass; visual cloud stability and all broader production gaps remain open.

### 2026-11-02: scene character frame integration, validation pending
- The current shader/native run remained in generated-shader preparation while this batch was added, so no competing build was started. SceneRuntime.cpp now accepts synchronized, unparented kinematic character bodies for owner transfer only when no dynamic bodies exist. SceneRuntimePhysics.cpp no longer rejects every enabled character at commit; it retains the parented-physics rejection and the existing swept-clearance/dynamic-contact preflight.
- Added SceneRuntimePlanetTests coverage for queued character movement during a rotating frame and transfer back to inertial control at 0 and +/-1e12 metres, checking precise authored/native positions and consumed commands. The existing newly-added-controller rejection remains covered. This is an incremental implementation, NOT completion of character/dynamic contact support or rollback. Tests are included in session95884's selected Planet suites; no passing result yet.
- Rebased the external vfx-shader-dependency-draft Windows and Unix emitters on current 8192-byte literals. Both scripts parse, and the compute include closure contains only BuiltinVfxCompute.hlsl, excluding cloud dependencies. This draft remains outside live source until all entry-point binding/behavior checks pass.
- Native generation finished and session95884 is compiling 182 steps. Preserve the running session; do not restart because it is quiet. Logs remain planet-local-parameters-native.log and planet-local-parameters-tests.log. Additional lifetime/ASan and editor travel checks remain owed.
- Follow-up on the external VFX dependency draft: verified the entire compute declaration/function source is byte-for-byte identical after line-ending normalization, with only the shared quaternion helper relocated intact. Evidence: Build/Validation/vfx-compute-source-equivalence.log. Compiled CSSimulate SPIR-V reports identical SDL resource counts and workgroup dimensions to the current generated baseline (8 read-only buffers, 5 read/write buffers, 1 sampler, 1 uniform buffer, 256x1x1). This is aggregate reflection, not detailed binding/offset or all-entry-point validation. Draft remains unintegrated.
- Session95884 has compiled and archived the changed core scene/rendering libraries successfully and moved into test translation units. No test execution result yet; new character-frame regression and previous shader timeout cases remain pending.
- Session95884 completed all 182 native build steps successfully and is now running the selected regression suites. The new scene-level character frame travel test was also run separately against the rebuilt binary: 1 case / 60 assertions PASS, covering accepted movement, rotating-frame/native pose agreement and inertial transfer-back at 0 and +/-1e12 metres. Evidence: Build/Validation/character-frame-travel-focused.log. This does not prove dynamic contacts, parented characters, rollback, standalone or visual acceptance.
- Pinned Jolt v5.6.0 source audit confirms Update collects and returns capacity errors after jobs and native updates; SaveState/RestoreState includes global, body, contact and constraint state. A transform-only retry would not restore the full simulation. Full rollback implementation/performance validation remains open.
- Integrated session95884 is TERMINAL SUCCESS: DevDist build passed and selected graph/material/planet/cloud/water/volume suites passed 361/361 cases and 254806/254806 assertions, including the previously timed-out ShaderGraphTests importer cases. Evidence: Build/Validation/planet-local-parameters-tests.log. This test selection differs from the earlier 479-case selection; do not describe it as rerunning all 479 cases.
- Started normal repository DevDist editor staging via stage-editor.ps1 (four native jobs), log Build/Validation/planet-local-parameters-editor-stage.log. Editor remains closed; pending foreground/open permission is still unanswered. Source fixes are not yet visually accepted. ASan, combined runtime performance, standalone and broader production gaps remain open.
- External VFX split validation strengthened: direct SPIR-V inspection now compares the 16 resources reachable from CSSimulate, including descriptor set/binding, storage class, scalar/vector/matrix/array types, array stride and member offset/matrix decorations. All match. Deliberately changed live bindings and member offsets are rejected. Evidence: Build/Validation/vfx-compute-binding-layouts.log, .json and verifier .py. Initial whole-module comparisons found unused graphics declarations retained in the old module, so aggregate reflection alone was not treated as sufficient. This validates only the captured simulation entry, not every VFX stage or runtime behavior; do not promote the draft yet.
- Editor stage session94802 remains active, passing 116/199 compilation steps with no build errors observed. No new native build was launched and no editor was opened.
- Editor stage session94802 is TERMINAL SUCCESS: all 199 build steps passed and the DevDist distribution is ready at Build/Distributions/keire-editor-windows-x86_64-DevDist; staging took 719.6 seconds including recompilation. This is not a shipping-package or standalone acceptance result. No editor opened; foreground permission remains pending.
- Verified all five authored QA files still match the preserved hashes (scene plus Rendering/Planets/EnvironmentTime/EnvironmentLighting), recorded in Build/Validation/cloud-view-filter-qa-preservation.json.
- Started external VFX compute-split validation across all 14 affected compute entries, checking live resource addresses/layouts, SDL resource counts/workgroups, and DXIL/MSL compilation. Log: Build/Validation/vfx-compute-all-entries.log. Live shader source remains unchanged by this draft. Do not integrate until results and runtime coverage are assessed.
- All-entry VFX validation is running in session29923 (not a native build). First six entries, including CSSimulate and CSSimulateOutput, passed compiled binding/layout/count/workgroup equality plus DXIL/MSL conversion. Remaining entries are still pending. The editor stage is complete and no editor process has been opened.

### 2026-11-02: integrate VFX shader dependency isolation
- Session29923 completed successfully: all 14 compute entries preserve live SPIR-V resource addresses, reachable layouts, SDL resource counts and workgroup sizes; DXIL and MSL conversion also pass. Complete compute source equivalence was verified earlier. Evidence: Build/Validation/vfx-compute-all-entries.log and VfxComputeSplit/results.json.
- Integrated BuiltinVfxCompute.hlsl, the graphics wrapper and both platform generators after checking every base/draft SHA-256. Preserved originals plus manifest in workspace coordination/vfx-shader-dependency-preintegration. Live compute include closure contains only the compute source, so cloud changes cannot change its per-stage input identity. Existing 8192-byte embedding chunks remain intact.
- Windows generated-content cache regression and Unix shader-blob-cache regression pass (including dependency isolation, corrupt-cache recovery and stale-prepared-identity rejection). No Vendor changes, Git staging or publication.
- Captured all seven original graphics SPIR-V payloads before regeneration. Integrated DevDist test build, graphics binding/layout/count comparisons and VFX test suite are running in session26065; logs vfx-split-native.log, vfx-split-graphics-contracts.log and vfx-split-tests.log. Do not start a competing native build. This does not yet prove runtime particle behavior or visual acceptance.
- The ready editor stage is the successful pre-VFX-split cloud/planetary build from session94802. It has not been opened; pending foreground permission still has no reply. Keep the QA baseline intact.

### 2026-11-02: VFX integration and rotating-ground character regression
- Session26065 completed successfully: integrated DevDist build, all seven graphics binding/layout/reflection comparisons, and 137 VFX cases / 9,144 assertions pass. Visual particle acceptance remains separate.
- Expanded the scene-level character regression to cover standing on rotating static support as well as free flight at zero and positive/negative trillion-metre origins. It reproduced six failures: the support anchor used the pre-transport matrix even though frame transport had already moved the rider, applying the rotation again after transfer to inertial control.
- Frame-carried non-dynamic supports now record the current authored support matrix for the rider anchor. Dynamic-support delayed carry remains unchanged. The regression passes all 138 assertions after the fix; the planetary selection passes 15 cases / 3,599 assertions, and related character/physics movement coverage passes 52 cases / 163,907 assertions.
- Evidence: Build/Validation/character-frame-support-focused.log (failure), character-frame-support-fixed.log, character-support-reference-regressions.log, character-support-movement-regressions.log. C++ formatted. No Git staging, commit, publication, QA edits or editor launch.
- Current staged editor predates this support-anchor fix and VFX split. Restaging, visual cloud-motion acceptance, latest sanitizer coverage and full startup/runtime measurements remain pending. The complete planetary, water and post-processing mandate remains active; this checkpoint does not close those systems.

### 2026-11-02: sustained frame carry and indexed-import follow-up
- ASan build/test session86103 is live, with 361 native steps in planet-support-asan-build.log. It is the only native build. Its command runs planetary/character regressions afterward; poll the session rather than launching another build. No sanitizer result yet.
- Extended the character regression with four additional no-input rotating-frame ticks before inertial transfer. This checks accumulated double-carry, not only a single frame transition. The prior 138-assertion result predates this extension; updated assertions remain unverified.
- Replaced the indexed-import rescan guard's full AssetSourceRecord copy and per-request catalog scan with one parent/subasset identity set captured under the database mutex. The guard retains missing-source fallback behavior. Extended existing tests for cached primary sources, duplicate mixed parent/subasset batches and database identity preservation. No end-to-end startup speed claim.
- AssetDatabaseWorkerAccess.cpp was changed after session86103 had compiled it: after that session finishes, perform an incremental rebuild before testing this asset optimization. Asset tests and extended frame tests are not yet accepted. Updated files are formatted; no Git mutations or editor launch. Pending foreground permission remains unanswered.

### 2026-11-02: sanitizer acceptance and water merge audit
- Session86103 completed successfully: 361-step DebugASan build and 34 planetary/character cases / 6,583 assertions pass, including sustained rotating support movement. No sanitizer diagnostic appeared. This does not validate dynamic-contact frame following or full failed-native-step rollback.
- Session33326 now serially rebuilds DevDist, runs the two indexed-import guards plus sustained character test, then incrementally rebuilds DebugASan and runs the two asset guards. Logs use indexed-guard-devdist-* and indexed-guard-asan-*. Do not run a competing native build; current result is pending.
- Compared the water optical draft's baseline manifest against live sources: 12 match and six diverge. Evidence: Build/Validation/water-optics-live-baseline-audit.json. Divergent files are RenderBackendInternal.h, RenderSurfaceRecording.cpp, WaterBodyComponent.h, WaterRenderCapture.cpp, RenderWater.cpp and BuiltinWater.hlsl. Rebase rather than copying these draft files over newer work. No optical draft changes integrated by this audit.
- Confirmed native failure-transaction gap: Step increments the clock before Jolt Update, which can return a capacity error after native state changes; later contact/event reconstruction also allocates. Transform-only recovery is insufficient. Keep the dynamic-contact guard until native state, pending loads/commands, contacts, sleep and clock restoration are implemented and measured together.
- Session33326 is terminal success. DevDist focused run: three cases / 544 assertions; incremental DebugASan asset run: two cases / 262 assertions. Both include the mixed parent/subasset rescan guard and newly missing-source fallback. The sustained rotating-character case now passes 282 assertions. This verifies behavior, not end-to-end startup latency.
- Started serial DevDist editor staging in session21392, log Build/Validation/cloud-frame-assets-editor-stage.log, with four jobs. Includes VFX dependency isolation, current planetary support fix and indexed-import guard. No editor launch; foreground permission is still pending. Formatting dry-run and diff whitespace check pass. Preserve QA settings and all pre-existing staged work.
- Session21392 completed successfully: six incremental native steps and DevDist staging in 64.6 seconds. The current editor contains the verified VFX/character/asset fixes. It remains closed; no visual or standalone acceptance claim.
- Rebased all six divergent external water optical draft files against current live sources, preserving runtime wave phase, 13,392-byte multi-planet water uniforms, planetary fog composition and cloud temporal/history changes. Preserved original draft in workspace coordination/water-optics-before-nov2-rebase and refreshed baseline/draft manifests. Live renderer remains unchanged. Three reference files match live behavior; only intended optical additions remain in the other three. External C++ formatting checked with the repository configuration explicitly. This prepares integration but does not implement the optical consumer or validate GPU capture.
- Expanded the planetary character regression to all three rotation axes, including tilt, sustained no-input carry, free flight and tangential movement after inertial transfer at zero and +/-1e12 origins. Session69581 DevDist build/test passes one case / 846 assertions (planet-character-tilted-tests.log). This extension has not yet run under ASan; prior Y-axis sanitizer acceptance remains valid for its tested scope.
- User explicitly authorized reopening/foreground testing. Editor PID46268 launched at 11:34:07 local, window initially unavailable, then Windows reported Not Responding before first visible frames. Do not launch a second instance. Current window6882454 is maximized, WaterIntegrationQA open, foreground testing authorized. Source shader version change triggered 95 background asset refresh targets; 83/95 on last observation.
- First 10-second UI profiler capture saved via profiler and copied to Build/Validation/cloud-footprint-initial-two-view.csv: 271 application intervals, 10.01118 sec, 27.0697 average application FPS, p95 38.8792ms, p99 39.2779ms, max40.5282ms. Both Scene/Game views visible, Scene reports67% dynamic scale, imports active. This is not a controlled comparison to previous single-view captures and not evidence that cloud flicker is fixed.
- Environment tab selected afterward to hide Game view; initial UI frame read22.88ms, not a sustained measurement. No authored environment setting changed. Pending testing: imports-complete single-view capture, fixed-resolution camera translation/rotation and resolution transition cloud stability, detailed render telemetry and startup diagnosis. Current sky automation bindings: keireWindow/keireState in node_repl; use fresh state before input. API drag has no button/modifier argument, so do not invent right-drag/held-Alt support. Camera wheel navigation and visible UI controls remain available.
- Additional code audit: frame transport updates character transforms after movement velocity is computed; verify world-space resolved velocity includes frame motion before claiming full character integration. No velocity change implemented yet.
- Resumed UI testing with explicit user permission. Imports-complete single-view capture saved as Build/Validation/cloud-footprint-single-view-imports-complete.csv: 802 intervals over 10.00636 sec, average80.149 FPS, p9513.9595ms, p9914.6525ms, max16.0126ms; Scene scale100%, render telemetry disabled. Environment settings changed between observations (regional coverage1 to0.37, base-height variation550 to540); do not treat this as a controlled before/after comparison. Assistant did not alter those fields.
- Scene wheel dolly verified functioning; cloud shapes remain broadly consistent in the before/after stills at100% scale. Still images do not establish transient flicker or rotation/DRS stability. Those acceptance checks remain pending; no cloud-flicker fix accepted yet. Editor remains open, maximized, with current user environment configuration preserved.

### 2026-11-02: user-requested pause checkpoint
- Fixed resolved character velocity omitting planetary frame transport. The expanded regression first failed180 assertions, then passed1,116 after adding local-space carried displacement before movement commit. Broad DevDist character/planet selection passes137 cases /171,420 assertions. Changed C++ formatting and diff whitespace checks pass. Latest fix has not been staged into the running editor or rerun under ASan; previous sanitizer results cover the earlier batch only.
- Performance baseline and remaining cloud rotation/DRS visual acceptance remain as recorded above. Water optical consumer integration, planetary dynamic-contact/failed-step transaction work, standalone parity and remaining post-processing acceptance are unfinished. Do not treat this checkpoint as production completion or publish it as a release.
- User requested all repository work committed and pushed on master, then paused. External water optics draft remains preserved under C:/Users/keith/Documents/ChatGPT/Keire/coordination/water-optics-next with its manifests and original backup. Build/Validation retains local evidence; generated logs and binaries are excluded from Git.
