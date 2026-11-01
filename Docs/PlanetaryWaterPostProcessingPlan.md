# Planetary, water and post-processing delivery plan

Expanded active-goal scope requested November 1, 2026. This plan is a completion checklist, not a claim
that the features exist. The previous usage stopping point is removed by the user. All work stays on
master; preserve authored QA projects and validate changes in separate test projects first.

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
