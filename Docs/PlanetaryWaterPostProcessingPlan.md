# Planetary, water and post-processing delivery plan

Expanded active-goal scope requested November 1, 2026. This plan is a completion checklist, not a claim
that the features exist. The previous usage stopping point is removed by the user. All work stays on
master; preserve authored QA projects and validate changes in separate test projects first.

## Delivery order

1. Finish current rendering stability and startup performance work. Preserve shader resource contracts,
   visual output and runtime performance when reducing forward shader compile time. Complete cold-start,
   reload, sanitizer and production validation before publishing installers.
2. Finish planetary integration: atmosphere selection/composition, planetary weather, collision-safe
   gravity/frame changes, moving supports, physical travel, far-distance physics and standalone parity.
3. Implement shared post-processing profiles and blending with editor authoring and runtime integration.
4. Implement water bodies and their planetary, rendering, physics and post-processing integration.
5. Validate the combined Earth/space/Mars showcase and document verified limits before release.

Water and post-processing design/audits can proceed alongside planetary fixes, but neither replaces
unfinished planetary acceptance. Existing production release work remains tracked separately; do not
label unfinished new systems production-ready merely because an installer builds.

## Planetary acceptance

- Ground-to-space-to-ground travel without loading screens, with correct atmosphere, stars, clouds,
  fog, lighting and day/night at distant world origins and around translated/rotated planets.
- Multiple visible atmospheres and deterministic weather ownership/blending at transitions.
- Collision-safe character orientation, gravity changes, moving/rotating support, boarding/detaching,
  and appropriate zero-gravity behavior. No unchecked teleport or rotation through obstacles.
- Terrain/collision streaming and landing support with explicit supported scale; a fixed sample landing
  pad does not prove arbitrary planetary landing.
- Repeatable far-distance physics, rebase, save/load and standalone tests; frame-time and memory budgets
  measured on the test hardware, including long sessions and repeated transitions.

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
