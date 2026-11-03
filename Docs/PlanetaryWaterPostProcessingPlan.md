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

## Active performance and rendering-correctness pass — November 2

User replaced the previous feature-completion goal with performance-only work, including rendering bugs/artifacts. Target8ms sustained application frame time, DevDist allowance10ms, with tails and spikes reported separately. Do not claim universal performance from one scene or silently lower quality to pass. Preserve current QA settings; baseline copies and hardware/commit hashes are in Build/Validation/performance-nov2-baseline.

Required coverage:
- Inventory every supported environment/render enum, toggle and quality setting. Enumerate discrete combinations and report counts/unsupported fallbacks. Continuous controls require documented default, minimum, maximum and artifact-triggering samples; finite sampling cannot prove every real-valued combination.
- Cover ForwardPlus/DeferredHybrid/Automatic, all five AA modes, all five GI modes, all Irradyn quality levels, DRS off/on, atmosphere/cloud/fog/post-process/water combinations. Exercise relevant time-of-day, cloud morphology, density/coverage/height, samples, multiple planets and near/far viewpoints. Pairwise smoke coverage may prioritize runs but does not replace outstanding full discrete coverage.
- Fix rendering artifacts alongside performance: cloud flicker, shape changes during rotation/translation or DRS changes, distant boundaries, banding, shadow/lighting errors. Preserve comparable visual quality. Measure stationary and moving workloads after warmup, plus cold/warm asset startup separately.
- Record hardware/driver, build identity/configuration, actual drawable resolution and scale, authored/effective settings and fallbacks, fixture hashes, application timing percentiles and asynchronous GPU timing separately. Test native resolution as well as explicit DRS policies. Do not compare concurrent game/GPU workloads as controlled benchmarks.
- Validate DevDist and full Dist using Project CLI build/package paths; install and launch standalone player, Hub and installer options, including existing-install maintenance/location behavior. Use isolated validation destinations; no live publication implied by this pass.
- Existing baseline hardware: RTX3060, i7-12700F, driver32.0.16.2101. Current Config/PerformanceGates.json pins older32.0.15.9597, so its historical acceptance is not transferable. Existing render-benchmark.ps1 is Release-only and tests presentation modes, not render/environment combinations. RuntimeRenderBenchmark rejects Dist. Resolve these harness gaps before claiming shipping-build coverage.
- Resume priorities: extend reproducible benchmark configuration/coverage, obtain matched DevDist/Dist baseline, profile worst combinations, batch measured optimizations/artifact fixes, then repeat matrix and real-user editor/install checks. Planetary/water/post-processing feature expansion is outside this pass unless required to fix a measured performance or rendering defect.
- Benchmark configuration implementation: RuntimeRenderBenchmark now permits Dist alongside Release/DevDist; launcher accepts the same set, validates returned build.configuration, records it in the matrix, and isolates non-Release output/temp folders. Added a focused accepted/rejected configuration regression. PowerShell parser and deterministic89-file fixture regression pass. Native test build is still live in exec session1789 (parent PID37244); current log performance-benchmark-config-build.log only reports parallelism, so no native pass or Dist runtime acceptance yet. Do not start a competing build or infer completion from the fixture test. Full environment matrix and performance optimizations remain next work.
- Session1789 completed successfully: DevDist KeireTests build and benchmark-configuration regression pass1 case/9 assertions (performance-benchmark-config-tests.log). Slow pre-Ninja work was verified active via a KeireShaderCompiler child, not assumed stalled; isolated toolchain lookup4.07s and generation fingerprint3.04s, compiler-cache lookup0.20s. Shader-generation progress visibility and redundant invalidation remain performance investigation items.
- Started Project CLI build of DevDist KeireRuntime in session69103, log performance-benchmark-runtime-build.log. It reached RuntimeRenderBenchmark.cpp compilation. This validates the actual benchmark implementation separately from the small configuration-policy test. Do not start simultaneous native builds. Shipping Dist execution, full combination coverage, frame-time improvement and artifact acceptance remain unproven.
- Session69103 completed successfully: Project CLI DevDist KeireRuntime build passes. Actual runtime benchmark translation unit compiles and links; rendered execution still pending.
- Full cooked DevDist benchmark started in session96310 via render-benchmark.ps1; log performance-devdist-baseline.log. AssetTool/Runtime builds passed and cook is active (worker36748, operation73de6ff9-caf7-4530-82f4-1bb9142b7f92 under Build/Benchmarks/DevDist/Sample/KeireSandbox/Library/AssetOperations). Last progress11/89 material imports. Preserve this live run; no duplicate build/cook.
- Measurement audit: RenderFrameTimeline exposes stage durations, not application start-to-start intervals. Add separate sustained application interval capture before asserting8/10ms acceptance; do not sum overlapping asynchronous stages or call GPU retirement GPU execution.
- Fixed fixture identity omission: hash all ProjectSettings, not just BuildScenes. Focused fixture test now passes96 files and proves a Rendering.keiresettings mutation changes identity. The already-running baseline loaded the older89-file fixture function; retain its limited provenance and do not represent it as the expanded fixture run.
- Cook session96310 remains live, progressed to46/89 imports. Added RuntimeBenchmarkIntervals with preallocated bounded sampling,300 excluded warmup intervals and2,000 measured intervals; runtime report adds explicit application_render_submission_intervals_not_displayed_fps scope, mean/median/p95/p99/max and raw intervals. Completion now waits for both application samples and renderer timelines. Added deterministic warmup/spike/monotonicity/nonfinite/zero-window regressions. Formatting/diff checks pass; native rebuild/tests pending until current baseline completes. The running baseline executable predates this collector and cannot satisfy the8/10ms application gate.
- Added validate_runtime_capture.py to independently recompute application interval mean/median/p95/p99/max from raw samples, reject incomplete/nonfinite/mismatched captures and report separate mean/p95 target results (8ms Dist/Release,10ms DevDist). Python regression suite results are in performance-capture-validator-tests.log. This is capture validation, not measured performance acceptance. New native interval collector still needs build/test after active baseline session96310 finishes; cook last verified54/89, worker36748 remains live.
- Baseline session96310 ended with failure: AssetTool's600-second worker timeout at79/89. Worker log confirms many material/graph imports took20–27seconds. This is an asset-processing performance failure, not a frame-time result. Worker/compiler processes exited; retained sample Library includes26 shader-cache entries. Do not rerun the destructive full benchmark setup; resume cooking against this preserved sample/cache after native validation.
- Serial Project CLI DevDist test/runtime builds are live in session70716; logs performance-interval-tests-build.log, performance-interval-native-tests.log and performance-interval-runtime-build.log. Adding the new header triggered generation/bootstrap; it reached native compilation. Focused native collector acceptance remains pending.
- Session70716 completed successfully:3 native benchmark configuration/interval cases,24 assertions pass; DevDist KeireRuntime compiles/links with application timing. Collector conversion emits double-to-float warning for percentile scratch vector; precision cleanup remains small follow-up. Resumed AssetTool cook without deleting the preserved sample/cache in session68232, log performance-devdist-cook-resume.log; same600-second deadline retained. No rendered benchmark currently running; wait for cook success before starting runtime capture.
- Session68232 remains live. Preserved-cache retry completed asset-worker import (operation f07ef2ce-1371-4ce7-8249-08ea3b8d3453 removed after completion), then managed build logged preparation12427ms/setup1076ms/build9214ms/publication26ms. Current AssetTool PID36472 is compiling target shader binaries (observed SPIRV-to-DXIL VFX ribbon fragment child), so no rendered benchmark can start yet. Stock fixture rendering is ForwardPlus/realtime GI and is not an all-environment workload.
- Removed percentile scratch double-to-float conversion by templating Percentile and retaining double application samples. Formatting passes; this small precision cleanup is not yet rebuilt. Existing runtime binary includes the tested interval collector and remains suitable for initial capture; next native build should verify the cleanup.
- Session68232 completed successfully:77 assets cooked into1 pack. Relative --output was resolved under the sample project, so actual content root is Build/Benchmarks/DevDist/Sample/KeireSandbox/Build/Benchmarks/DevDist/Content/KeireSandbox. Initial launch at the assumed shorter path exited2; no render result. Corrected to resolved absolute paths and started immediate-mode runtime capture in session55635, PID15476, output Build/Benchmarks/DevDist/application-immediate-initial.json; stdout/stderr logs performance-player-stdout.log/performance-player-stderr.log. Do not start builds or another renderer during measurement. Running binary has the tested collector before the unbuilt percentile double-precision cleanup.
- Both initial rendered captures failed strict timeline continuity, not the frame-time target. Added failed-report diagnostics; application-immediate-gap-diagnostic.json shows frame793 followed by3883 and misleading0.115ms application intervals. Root cause confirmed in RenderPipelineLifecycle::EnqueueFrame: asynchronous VFX compilation deliberately defers admission while the owner keeps pumping events. These are not rendered frames. Runtime capture now restarts warmup only before measured samples exist while VfxPipelineWarmupPending, records startup delay and compilation time, and rejects any interruption after measurement starts. A three-minute pending-compilation watchdog bounds startup. No renderer scheduling behavior changed.
- Session47843 completed: DevDist KeireTests/Runtime builds pass; focused interval/warmup tests pass3 cases/21 assertions, including rejection of measured-sample discard and preservation of an80ms stall. Percentile precision cleanup and failure diagnostics are compiled. Logs performance-warmup-tests-build.log, performance-warmup-tests.log, performance-warmup-runtime-build.log.
- New rendered baseline session31122 (PID31756) is running, application-immediate-warmup-gated.json; no concurrent builds. Baseline still uses stock MaterialLab fixture, not the full cloud/environment matrix. Rendering artifacts across combinations remain acceptance failures independently of performance.
- Session31122 completed successfully (runtime exit0). Capture application-immediate-warmup-gated.json:1280x720, immediate presentation,300 warmup/2000 measured frames, application mean1.378ms/p953.355ms/p994.157ms/max9.765ms over2.757seconds. Python independent validator passes10ms mean/p95 gate for this limited stock fixture only. Startup baseline59.581s and VFX pipeline warmup58.208s remain major failures to optimize. This short capture is not sustained cloud/editor/all-combination acceptance, and no user-visible FPS claim follows from submission intervals alone. Next: preserve this baseline, inspect VFX pipeline preparation and establish fixed-resolution heavy-environment captures with visual artifact coverage before broad optimization claims.
- Reopened existing DevDist stage through Project CLI and performed real-user UI isolation in WaterIntegrationQA, maximized3440x1392 window, Scene and Game views both visible. Saved30-second captures: performance-heavy-clouds-original.csv has562 intervals/30.019644s (mean53.4157ms), p9554.5273ms/p9955.1428ms/max55.5082ms. Disabling only volumetric clouds and restoring Game tab yielded performance-heavy-clouds-disabled.csv:3000 intervals/30.0049411s (mean10.00165ms), p9511.1957ms/p9911.7259ms/max12.8299ms. DRS remained automatic: cloud-on Scene67%, cloud-off100%; therefore this isolates a major cloud cost but is not a fixed-resolution comparison or acceptable quality reduction. No camera/layout changes during captures; no concurrent builds.
- Restored cloud enable using Environment Undo then Save Environment. Rendering.keiresettings SHA256 matches baseline exactly: F7A49001E25B332C5349C306EC031F8EB1B8F55D676153FAF032A258DE607B01. Closed editor through Alt+F4 after capture. Native runtime baseline cannot represent this expensive authored cloud configuration.
- Current heavy volume:128 view steps/16 shadow steps, AO0.76, multiple scattering0.65, coverage0.7, pattern3000m, maxdistance100000m, base1500m/thickness1200m, independentweather enabled. Flat cloud light-cache grid only spans roughly +/-6000m at this pattern size (129 cells at93.75m), with direct shadow and four-direction ambient marches outside. Investigate cache coverage/fallback and measured pass timings at fixed resolution before choosing optimization; this is a code-derived hypothesis, not a GPU-profile proof. Both render paths/AA/DRS and distant-cloud artifact checks remain required.
- Startup logs show cost distributed across CSSpawn15.67s, CSSpawnInitialize21.10s, CSSpawnOutput21.20s plus multiple forward material pipelines6–7.5s; background VFX duration58.21s includes its serial pipeline work. CSSimulate/SimulateOutput were warm-cache cheap in this run. Do not attribute all startup cost to one shader or claim a universal cache fix.
- Rendering correctness is an explicit acceptance requirement for this performance pass: reproduce cloud artifacts across density/shape/coverage/lighting combinations, camera translation and rotation, near and distant clouds, day/night, AA modes, and fixed/dynamic resolution. Retain visual comparisons alongside timing captures; faster settings that introduce flicker, rings, unstable detail, or lighting discontinuities do not pass. Record unsupported combinations rather than claiming universal coverage.
- Fixed-resolution heavy-cloud standalone session87729 exited1. cloud-isolation-original.json failed strict timeline continuity at frame665 ->114823 after baseline664. Do not use its application intervals as a performance result. The existing warmup statistics gate still admits stale pre-warmup timelines; investigate renderer admission/completion synchronization before further capture comparisons. No cloud-renderer optimization has yet been validated. QA settings remain restored and the editor is closed.
- Corrected benchmark startup boundary: after compilation, wait for a newly accepted frame, then fence all already accepted work rather than only completed timelines. This excludes old in-flight startup frames without relaxing measured continuity or permitting measured samples to be discarded. Focused DevDist tests pass4 cases/31 assertions; DevDist Runtime and Tests builds pass (performance-admission-*-build.log). New fixed-resolution cloud capture session8055/PID32276 is running, output cloud-isolation-admission-gated.json, logs performance-cloud-admission-stdout/stderr.log. Do not start builds during capture. No renderer quality/performance claim yet.
- Session8055 completed exit0: cloud-isolation-admission-gated.json validates continuous timelines and raw application intervals. Fixed1280x720/immediate/DRSoff,300warmup/2000measured:135.115s sustained, mean67.558ms/median67.311/p9570.251/p9971.037/max72.183ms. Target fails. Median owner update0.340ms/render CPU1.327ms/admission wait66.658ms; GPU retirement133.585ms is multi-frame latency, not GPU execution duration. Warm-cache startup1.650s/VFX0.220s does not negate cold-start costs. This is the stock material scene with QA heavy cloud+atmosphere settings, not WaterIntegrationQA.
- Started isolated no-AO comparison session9589/PID1652; CloudIsolationNoAO copies the exact baseline content and changes only volume.ambientOcclusionStrength to0. Output cloud-isolation-no-ao.json; logs performance-cloud-no-ao-stdout/stderr.log; manifest hash saved. This is a diagnostic feature isolation, not an accepted optimization or replacement for the user's authored quality. Original QA and baseline content remain untouched. No builds during capture.
- Session9589 completed exit0. No-AO diagnostic at the same1280x720: mean41.994ms/p9543.722/p9944.371/max45.155 over83.988s. Removing AO saves25.564ms versus the heavy baseline, but is not a quality-preserving optimization. Both captures pass measurement validation and fail the10ms target.
- Implemented bounded early termination of flat and planetary cloud light marches. AO uses depth scale1; sunlight/cache conservatively use squared multiple-scattering occlusion so all three cached orders remain valid independent of contribution. Rays terminate only below1e-6 transmission in the least attenuated order; zero occlusion disables termination. Added independent complete-versus-shortened optical-depth tests covering zero/thin/dense rays and all retained orders. Build/test sequence session15912 is live: performance-cloud-shadow-tests-build.log, performance-cloud-shadow-tests.log, performance-cloud-shadow-runtime-build.log. Performance and visual acceptance are pending; baseline content/QA settings unchanged. Do not launch captures until this build is terminal.
- Session15912 remains verified live: shared built-in cloud/trace/composite/water shader compilation progressed into VFX graphics variants (KeireShaderCompiler PSCpu DXIL at15:47). No terminal build/test result yet; do not restart merely because the redirected launcher log is quiet. Next timing comparison must use original CloudIsolation content, not the no-AO diagnostic. Existing cooked material shaders retain their prior embedded cloud kernel until recook; distinguish initial built-in-kernel performance measurement from full material-path acceptance.
- Session15912 completed successfully: DevDist Tests and Runtime build; shared DXIL/SPIR-V/MSL shader variants compiled; focused cloud lighting tests pass5 cases/543 assertions (performance-cloud-shadow-tests.log). No speedup proven yet. First optimized capture session77440/PID23168 exited1 because window resolution changed; no valid report. User confirmed maximizing the window. This is expected fixed-resolution benchmark rejection, not evidence of an editor crash.
- Retrying original1280x720 heavy-cloud workload in session81415/PID35444, output cloud-isolation-shadow-cutoff-retry.json, logs performance-cloud-shadow-retry-stdout/stderr.log. User told to preserve capture size; maximized testing remains separate and required. No concurrent build. Original QA settings unchanged.
- Session81415 completed exit0. Fixed-resolution optimized capture validates but does not improve target workload: mean69.400ms/median68.972/p9583.041/p99113.136/max189.260 over138.800s, versus baseline67.558/p9570.251. Startup2.478s, VFX0.052s. Cannot attribute all tail spikes to the shortcut without further controlled repeats, but no useful gain is established. Removed only this experiment's three shader edits/test and changelog claim; saved reverse-applicable patch Build/Validation/cloud-shadow-cutoff-experiment.patch for evidence. Benchmark instrumentation fixes remain. Runtime binary still contains experimental shaders until rebuild; do not mistake it for restored-source baseline. Next: rebuild original renderer and isolate shadow/atmosphere/cache coverage, including dynamic-light cache rebuild cost, before larger changes.
- Restored-source Runtime build session52297 passed. Added resize-failure diagnostic JSON with expected/observed dimensions and measured interval count; Python validator regression passes5 tests and rejects that diagnostic as performance evidence. This is reporting only; resize still aborts capture.
- Isolated shadow comparison session99013 exited0: CloudIsolationNoAOLowShadow changes only shadowSteps16->1 relative to NoAO. Fixed1280x720/immediate: mean18.335ms/median18.085/p9523.121/p9939.268/max93.944 over36.670s. Versus NoAO41.994ms, direct shadow cost is substantial. Tail differences require controlled repeats; this is a quality-reduced diagnostic, not an accepted optimization.
- Atmosphere diagnostic session43769 exited0: CloudIsolationNoAOLowShadowNoAtmosphere changes only atmosphere.enabled=false relative to the low-shadow case. Mean11.767ms/median11.737/p9513.679/p9915.114/max24.356 over23.535s. Output cloud-isolation-no-atmosphere.json. Reports validate and still fail10ms. Ordering interactions mean differences are not independent additive GPU pass measurements. Strongest next target remains repeated cloud-lighting density work outside the bounded cache, followed by atmosphere and view-density cost. Preserve all authored quality for final acceptance.
- QA Rendering settings hash rechecked unchanged: F7A49001E25B332C5349C306EC031F8EB1B8F55D676153FAF032A258DE607B01. No capture/build is currently running after session43769. Full combination/visual coverage, native-resolution editor, cold startup, full Dist and CLI/Hub/installer acceptance remain pending. No renderer optimization from the rejected cutoff experiment remains in source or rebuilt Runtime.
- Added sampled cloud-light-cache telemetry to runtime reports: unique published frame count/builds/reuses/cells. Scope explicitly sampled_published_frames_not_complete_timeline; repeated/stale snapshots are ignored and warmup resets clear observations. DevDist Tests/Runtime builds session17018 pass;5 focused benchmark cases/41 assertions pass (performance-cache-observations-tests.log).
- Instrumented original-quality capture session75054 exited0: cloud-isolation-cache-observations.json, mean68.746ms/median68.497/p9573.094/p9989.921/max153.190 over137.493s at1280x720. Observed1997 of2000 measured frames,1997 cache reuses,0 builds/0 cells. This rules out repeated cache construction as the dominant cost for the stationary paused-clock baseline. It does not cover animated time or moving-camera invalidation. Next renderer work should extend lighting coverage with stable world-aligned multiresolution cache levels, preserving direct fallback outside valid levels and validating seams, camera/wind continuity, planetary mapping, memory/update cost, and all scattering orders. Current +/-6km flat grid versus100km cloud range is code evidence; exact out-of-grid query frequency has not been measured.
- Implemented three cloud-light-cache levels (horizontal scales1/4/16 for flat worlds; planet-fixed scaled cells for planetary grids), preserving near resolution and vertical flat-layer resolution. Shared storage uses separate fixed-capacity ranges; each level has independent recorded/submitted validity. Shader blends fine-to-coarse at boundaries and retains direct fallback outside valid coverage. Current pattern3000m has nominal horizontal reach6/24/96km. Buffer budget increases from8.38MiB to25.14MiB per surface workset; update cost under animated sunlight/time and moving cameras is unverified and remains a required acceptance check.
- Added CloudLightCacheLookup.hlsli and expanded lookup uniform block48->144bytes; compute uses GridDimensions.w as buffer offset, lookup sampling applies it. Updated all three builtin cloud consumers and RenderCloudVolume publication/counters. Added grid/wrap/world-alignment tests and independent per-level submission/reuse tests; updated standalone lifecycle SDL doubles and GPU motion probe for level arrays. Source edits are not accepted as a performance or visual fix yet.
- Build/test sequence session93412 remains live: performance-cloud-levels-tests-build.log, performance-cloud-levels-tests.log, performance-cloud-levels-runtime-build.log. It compiles shared shaders before native builds. After terminal result: run standalone ASan lifecycle harness (existing Build/Validation/CloudHdr55/cache-batch-lifecycle.rsp, updated source; import Scripts/Windows/common.ps1 and Enter-VSDeveloperEnvironment), then fixed original-quality runtime capture and real UI visual checks. Do not overlap builds/captures. Existing CloudCacheMotionGpuProbe readback still primarily validates the near level; extend far-level GPU readback before claiming planetary/transition coverage.
- Session93412 passed: shared shader generation, DevDist Tests/Runtime;10 cache cases/4298 assertions. Added a guard disabling distant Cartesian levels for planetary clouds because isotropically coarse cells can miss thin spherical layers; original planetary near cache/direct fallback remains active. Radial-resolution-preserving planetary distant caches are unfinished, not waived. Guard Runtime build session80773 passed.
- Standalone lifecycle harness rebuilt with ASan using Build/Validation/CloudCacheLevels/lifecycle.rsp:14 cases/1005 assertions pass. Tests include disjoint level offsets, independent reuse across near-grid crossings, abandonment/retry after a partial multi-level failure, and planetary coarse-level guard. GPU probe rebuilt using motion-gpu.rsp (links current libraries, no stale explicit resources object).
- Extended CloudCacheMotionGpuProbe to read all three levels, enable AO0.76 with oblique sunlight, check finite/nontrivial transmission, and compare unaligned/aligned construction at phase0.012345 and near256 wrap. D3D12 produces meaningful ranges (~0.005..1); phase0.012345 max differences near0.000226021/middle0.000102639/far0.000186592 exceed chosen0.0001 tolerance, with tiny means4.8e-8/2.2e-8/2.7e-7. Near-wrap maxima8.34e-7. Keep tolerance failure open; near is unchanged algorithmically, so investigate numeric baseline rather than asserting a new-level regression or relaxing blindly.
- Vulkan initially appeared exact but stronger nontrivial-output checks reveal all-zero readback on every level; probe now correctly fails. GPU work ~0.038ms versus D3D12~17.8ms for3-level build. Must diagnose shader/binding/probe/backend issue; no Vulkan acceptance. SPIR-V top-level CacheConstants offsets inspected and match CPU (GridOrigin13296,CellSize13312,GridDimensions13328); descriptor sets also match nominal SDL compute0/1/2 layout. Nested layout/actual execution not yet verified.
- Session75284/PID39044 now captures original-quality D3D12 prototype performance: cloud-isolation-cache-levels.json, logs performance-cloud-cache-levels-stdout/stderr.log. No concurrent build/GPU probe. This is an experimental performance measurement with open numeric/Vulkan/visual issues, not release acceptance. Poll same handle; do not restart solely on elapsed time.
- Session75284 completed exit0. cloud-isolation-cache-levels.json validates raw continuous application intervals at original settings: mean20.232ms, median19.902, p9525.789, p9944.808, max169.375 over40.465s/2000 frames. Compared with original67.558ms mean, prototype reduces mean about70%, but misses10ms target and has substantial tail spikes. Numeric tolerance, Vulkan zero readback, visual transitions and animated-time cost remain open; this is not release acceptance. User confirmed maximizing caused the earlier benchmark exit; keep fixed-size capture separate from maximized editor testing.
- Vulkan cache root cause confirmed: SDL Vulkan MAX_UBO_SECTION_SIZE is4096, while CloudLightCacheUniforms was13344 bytes and grid dimensions began at13328. SPIR-V member offsets/local size match CPU; debug GPU mode produced no diagnostic. Compact the compute-only block to1664 bytes using the1616-byte resolved CloudVolumeView prefix; the builder never consumes the multi-planet transport arrays. Keep planetary geometry and all density/lighting inputs. Do not change Vendor. Other environment shaders still upload13296-byte view blocks and require a separate full transport binding audit; Vulkan rendering is not yet accepted.
- Shared shaders regenerated; standalone probe links freshly compiled CloudLightCacheResources.cpp to avoid stale native-library definitions. compact-vulkan.log now has meaningful transmission~0.005..1 and15.85–17.15ms cache builds, rather than all-zero data/~0.04ms. compact-d3d12.log preserves previous values. Numerical alignment tolerance remains failing (D3D12 max0.000226; Vulkan wrap max0.000572), not waived. Tests use GPU debug mode now.
- Compact uniform ASan lifecycle regression passes15 cases/1009 assertions (compact-lifecycle-tests.log), including byte-for-byte resolved input preservation, exclusion of unrelated multi-planet arrays, and the4096-byte limit. Shader build and both diagnostic compilations pass; production Runtime/Tests need rebuilding after this compact-block change. Current probe exit1 is the retained numerical tolerance failure, not a successful full acceptance result. No GPU/build processes remain from sessions21433/75809.
- Added Scripts/Performance/audit_spirv_uniform_ranges.py to inspect declared compiled SPIR-V member offsets, array strides and descriptor bindings against4096-byte SDL Vulkan range. Conservative declaration audit: unused members/stages are not proof of runtime reads. Four regression tests pass (exact boundary, oversized array, malformed instruction/header). all-uniform-range-audit.json flags fragment spatial lighting13408, water13392, cloud trace/composite/volume13296, and resolve current/previous13296; cache1664 passes. Next transport fix must preserve all8 planetary bodies, not truncate weather arrays or reduce the supported planet count. Prefer existing per-workset dynamic storage-buffer upload infrastructure, with matching shader reflection/bindings, lifecycle and GPU tests; resolve can use a smaller dedicated reprojection prefix after checking every read.
- Runtime followed by Tests rebuild is active in session86928 (performance-compact-runtime-build.log and performance-compact-tests-build.log),4 jobs. Do not overlap GPU timing captures with it. QA Rendering.keiresettings SHA256 stillF7A49001E25B332C5349C306EC031F8EB1B8F55D676153FAF032A258DE607B01. No QA edits or desktop takeover this turn.
- Session86928 finished successfully: DevDist Runtime and Tests builds pass. Focused cache/grid/composite tests pass9 cases/4276 assertions (performance-compact-focused-tests.log). This validates the compact compute-block batch before the next binding edits.
- Implemented full cloud-view storage transport for builtin volume/trace/composite: one13296-byte dynamic upload per workset/frame, shared by both temporal passes; two storage slots (cache plus view). Keep all planet profiles. Small16-byte pass mode replaces per-draw full-view copies. Trace controls move to uniform0, shadows to2, cache remains1 (3uniforms). Resolve uses the1616-byte resolved view prefix only; its ray origin/direction reads do not use multi-planet transport. Per-workset buffer/transfer are included in emptiness and normal release paths. Existing dynamic uploader retains cycling and deferred retirement. Resource lifetime/GPU rendering validation required before acceptance.
- Session61103 now generates shared shaders, runs compiled cloud-uniform range audit, then builds DevDist Runtime (storage-shaders.log, storage-uniform-audit.json, performance-cloud-storage-runtime-build.log). Poll same handle; no parallel builds or GPU captures. New storage-layout parity, backend rendering, ASan lifecycle, focused updated tests and performance captures remain required. Water and material spatial-lighting oversized bindings remain unfixed; no full Vulkan acceptance yet.
- Session61103 shared DXIL/SPIR-V/MSL shader generation passed; compiled cloud uniform audit exits0, all cloud blocks <=4096. Native Runtime build now live (2/45 when inspected); no timeout/restart. storage-layout-parity.json independently verifies all20 CloudVolumeView member offsets,13296-byte structured-buffer array stride, both matrix decorations and set2 storage bindings5/4/7 for volume/trace/composite. This validates binary layout, not visible output. Benchmark-capture Python suite8 tests and uniform-audit suite4 tests pass. Native changed pipeline tests, GPU lifecycle and visual/performance capture still pending. No QA edits this turn.
- Session61103 completed Runtime build exit0. Session30216/PID17284 completed runtime exit0 and validator exit1 (target failure): cloud-isolation-storage.json original1280x720 settings mean24.077ms/median21.053/p9532.726/p99105.909/max581.434,48.154s sustained. Startup baseline24.0s/VFXwarmup0.222s. Cache observations1966frames/5847reuses/0builds. Manifesthash unchangedC0E3AD6BDFEF81F32FD84B1DE1C6503DD94B4C47931F59D0A9E9088D7211028E.
- Treat comparison as confounded, not accepted regression or improvement: Aether cl.exe network-probe.rsp build observed immediately afterward (unrelated user work, not stopped). Median renderCPU3.706ms versus1.316previous, queue0.364vs0.007ms, GPU retirement median38.425vs38.396ms. Tail attribution unproven; retain result and repeat during quiet compiler period. Runtime cold pipeline creation includes5.69s deferred G-buffer even on Forward+ fixture; lazy/warmup behavior needs investigation.
- Separate visual Runtime PID16404 opened original content after capture; Windows-control screenshot shows clouds, colored material showcase, shadows and VFX rendering with the new storage path. No gross black/zero output. This is one static D3D12 view, not seam/motion/night/planetary acceptance. Closed own validation player with Alt+F4 afterward; no QA project edits.
- Prepared next water binding fix in source, NOT part of measured binary: WaterFragmentUniforms13392->96 bytes, full environment via sharedCloudView storage t3. Reuse cloud pass upload when available; when no volume pass runs, explicitly upload fresh view before transparency (including on/off transitions), never reuse stale prior-frame data. Water shader storage count1. Water shared-shader/native/reflection/tests still pending; do not claim fixed until verified. No build or GPU capture currently running from this turn.
- Session81349 completed: water shared shaders and4096-byte uniform audit pass, DevDist Runtime/Tests builds pass,55 focused water/cache/composite cases5107 assertions pass. water-storage-layout.json verifies full13296-byte storage stride/all20 offsets and set2binding3. Water on/off transition visuals and GPU resource lifecycle remain unverified; compiled checks are not end-to-end water acceptance.
- Startup root cause confirmed in ResolveShader: enumerates every RuntimePassRoles variant, so Forward+ material readiness waits for depth/G-buffer/decal variants too. Implemented deferred-on-demand preparation: default consumers schedule forward/primary/VFX roles, DrawScene requests deferred roles when active path is DeferredHybrid. Same-frame material cache now permits escalation from forward to deferred and retains the escalation until frame changes. On hot reload, rebuild exact resident variants together, preserving last-good publication, instead of expanding every resident combination across all supported roles. Added role-policy and same-frame escalation regressions. Real Forward+/Deferred switching and hot reload still need GPU testing; do not claim startup gain yet.
- Session12140 is active: DevDist Tests build, focused new material tests, then Runtime build. Logs performance-lazy-material-tests-build.log, performance-lazy-material-tests.log, performance-lazy-material-runtime-build.log. Four jobs, no timing capture concurrently. Prior runtime binary includes water storage, not this unbuilt lazy-preparation change. Material spatial-lighting uniform overflow still pending alongside cloud precision/cascade quality and broader performance/Dist workflows.
- Session12140 completed successfully: DevDist Runtime/Tests builds and focused lazy-material preparation tests (3 cases,29 assertions). Session54078 then completed two fixed1280x720 captures, both runtime exit0 and target validation failure. Original quality: mean21.041ms,median20.885,p9522.841,p9926.152,max135.037; atmosphere-only-off diagnostic: mean14.453ms,median13.800,p9516.826,p9929.237,max400.330. Neither meets10ms. No compiler process observed at1Hz during either capture; short-lived processes cannot be excluded. Raw captures are lazy-material-original.json and lazy-material-atmosphere-off.json under Build/Benchmarks/DevDist.
- Cooked-workload startup baseline was1.508/1.520seconds with13 material pipeline creations and no deferred-only roles, versus roughly24seconds in the previous storage capture. This is not a cold asset-import benchmark or proof of universal startup improvement. Real Forward+/Deferred switching and hot reload remain required. Atmosphere-off remains diagnostic only, not a quality-preserving optimization.
- User confirmed maximizing the earlier benchmark player immediately preceded its exit. Benchmark code deliberately writes drawable_resolution_changed and terminates when drawable dimensions change after baseline; keep timing captures fixed-size and use a separate maximized visual-validation window. Do not treat this deliberate benchmark stop as an editor crash.
- Next profiling step: collect actual GPU timestamp execution durations keyed by GpuTimingFrame, with explicit sampled coverage; existing GPU retirement measurements are multi-frame latency, not execution cost. Preserve outliers and report unsupported timestamp queries explicitly. Broader rendering-combination, artifact, resource-lifetime and full Dist/CLI/Hub/installer acceptance remain outstanding.
- GPU execution sampling implemented in RuntimeRenderBenchmark: unique GpuTimingFrame within the measured frame range, raw durations and sampled-coverage summary; no samples means unavailable, never zero-cost. Focused regression covers unsupported/stale/duplicate/out-of-range results, invalid durations and retained400ms stalls. Formatting and git diff --check pass. DevDist Tests build is live in session39095 (performance-gpu-timing-tests-build.log); must poll that same handle before tests or another build. Runtime rebuild and fresh GPU-timed capture remain pending. Do not run benchmarks alongside the currently observed compiler activity.
- Session39095 completed successfully; GPU sampling regression passes1case/7assertions. Runtime rebuild now started, log performance-gpu-timing-runtime-build.log. New instrumentation is not yet validated in a real capture.
- Runtime GPU instrumentation build session94183 passed. Native benchmark suite6cases/48assertions and Python capture validator7tests pass. Validator recomputes GPU summaries, rejects duplicate/stale/invalid frame samples and unsupported fake zero-cost summaries; old captures without the optional GPU block remain supported. README and PerformanceGates document sampled coverage.
- Diagnostic session73764 completed runtime exit0: gpu-timing-original.json,1280x720,300warmup/2000measured, original heavy-cloud settings unchanged. Application mean21.6807ms,p9524.3117,p9940.6579,max292.0672; GPU1982unique sampled completed frames mean20.7434ms,median20.6756,p9521.3289,p9921.7805,max86.4502. Validator confirms raw summaries and target fails. This confirms substantial device-timestamp cost, but Aether compiler activity overlapped (gpu-timing-original-compiler-observations.json); do not use as clean performance acceptance. Player auto-exited; no own build/capture remains running.
- Next: GPU-timed feature isolation at identical resolution/quality, quiet capture when other compilation finishes, then optimize measured cloud/atmosphere hot paths. Do not confuse the old GPU retirement latency with new execution durations. Full visual/settings matrix, actual water transition and material switching checks, cold imports, Dist/CLI/Hub/installer coverage remain incomplete.
- GPU isolation session98047 completed runtime exit0: atmosphere-only-off mean application14.6421ms/p9516.014/max344.55;1988GPU samples mean14.4779ms/p9515.1388/max30.0063. Session34795 clouds-only-off completed runtime exit0: applicationmean5.0137/p956.5498/p999.4378/max1496.9114;1920GPU samples mean4.0990/p955.0371/max20.0141. Both raw reports validate; all three runs overlap Aether compilation and are diagnostic, not clean acceptance. Clouds-off passes mean/p95 gate but the1.5s retained stall prevents any broad smoothness claim. Summary saved CloudCacheLevels/gpu-isolation-summary.json. CloudOnlyOff content is an isolated full copy whose manifest differs only at rendering.clouds.volume.enabled; QA unchanged.
- Code-derived next cost: cloud integration repeatedly evaluates view density and per-step foreground-air/sun quadrature; costs interact, so subtraction is only diagnostic. Implemented conservative density optimization: calculate type-dependent height profile first, reject empty profile before regional coverage weather lattice/atlas sampling. Nonempty samples retain original coverage/density evaluation. Expanded production-HLSL density regression across coverage and contrast to prove zero-envelope output independent of skipped coverage.
- DevDist Tests build session60714 currently live, log performance-cloud-profile-tests-build.log. Poll same handle, then run Empty cloud coverage* and envelope regressions. Shared shader generation, Runtime rebuild, fixed-quality recapture and visual comparisons still required; this source change is NOT a proven GPU improvement. No concurrent benchmark currently running.
- Continued same build session60714: authoritative live shader compiler progressed from cloud trace DXIL through MSL and into cloud composite DXIL; no error reported, native tests not yet reached. Formatting dry-run and git diff --check pass. Keep polling same session; do not start duplicate build or benchmark against stale binary.
- Session60714 completed shared shader generation and DevDist Tests build successfully; density regressions5cases/2882assertions pass (performance-cloud-profile-tests.log). Runtime build session3399 passed. No new compile errors; existing padding warnings remain.
- Full-quality profile-early-out capture session57541 completed runtime exit0. Original manifest SHA256 unchangedC0E3AD6BDFEF81F32FD84B1DE1C6503DD94B4C47931F59D0A9E9088D7211028E. Application mean20.6502ms,p9521.897,p9922.545,max23.4374; GPU2000samples mean20.6383,median20.6193,p9521.0872,p9921.5992,max22.1030. Raw summary validator passes but10ms gate fails. Compared with prior GPUmean20.7434, difference is too small for reliable improvement attribution under differing compiler overlap; do not declare optimization accepted. Visual regression and clean paired capture still pending.
- Important startup scope correction: benchmark Started is constructed with RuntimeLayer, after application/device setup. Its baseline22.724s here excludes substantial earlier renderer startup. Full stdout log shows logger17:59:23, GPUcreated17:59:25, builtinpipelinecreation18:00:02, attachmentformats18:00:24, gameplay18:00:27, remainingmaterialpipelineworkthrough18:00:49. Therefore previously reported1.5s warmed capture baseline is NOT total application launch time. Need wall-clock launch-to-first-present measurement and individual builtin pipeline creation profiling, including cold/new-shader and warm launches. Do not hide those delays under asset import or claim startup solved.
- No own capture/build remains active. Shader source includes the new conservative profile early rejection, compiled binaries updated; QA untouched. Main GPU target, clean combination coverage/visuals, oversized material lighting binding, cold startup, full Dist/CLI/Hub/installer validation still incomplete.
- Startup audit found CreateGeometryResources eagerly probes/creates Deferred pipelines, and PipelinesFor eagerly created all four CPU/GPU VFX graphics families for every sample count. Deferred creation remains unchanged because capability publication/resource allocation currently depends on actual successful pipeline creation; simply removing it would break path selection.
- Implemented on-demand built-in VFX graphics: removed CPU/GPU sprite/ribbon/mesh creation from PipelinesFor; actual fallback draw paths resolve only the required handle and cache it in the existing owning RenderPipelineSet. No shader features removed. New OnDemandPipelineInternal helper publishes only nonnull successful handles; exceptions leave slot empty. Existing device shutdown/recovery ownership unchanged. First use can still stall; no startup speedup claimed yet.
- DevDist Tests build and new on-demand failure/retry/reuse/recovery-slot regression pass1case/12assertions. KeireRenderTests DevDist build also passed (session30714). D3D12 validation-enabled rendered tests are LIVE in session71414: authored CPU/GPU billboard/ribbon hot reload, GPU Kill Shape and origin migration. Log performance-lazy-vfx-d3d12-tests.log; process19096. Fixture shaders/import completed and first live renderer reached asset pipeline creation. Poll same session; no duplicate GPU tests, benchmark or runtime rebuild while it runs.
- Runtime binary has NOT yet been relinked with lazy VFX change; do that after rendered tests. GPU mesh fallback, Vulkan and full resource-lifecycle acceptance remain needed. Current rendered tests may expose other existing issues; do not call this startup fix accepted from helper test alone. CHANGELOG records implementation without performance claims.
