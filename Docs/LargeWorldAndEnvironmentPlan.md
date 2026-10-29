# Large Worlds, Planets, Space, And Dynamic Environments

Status: implementation in progress, not a supported-feature announcement.
Requested scope: large terrestrial worlds, rotating planets, surface-to-space travel, and solar-system gameplay,
with a customizable production environment system. All are required outcomes; finishing the terrestrial milestone
alone does not complete this plan.
Source review: October 29, 2026. Earlier dated checkpoints below describe their state at that time.

## Intended Author Experience

A standard installed Editor and its matching player support high-precision positions without a custom engine build,
a separate double-precision download, or a script that periodically moves the entire authored world.
Ordinary projects start in a Cartesian frame and need no planetary configuration.

Authors can choose an Open World, Planet, or Space setup, place or import streamed content, add an environment preset,
and immediately preview time, lighting, and weather. The same assets, scripting contracts, frames, and saved state work
in Edit Mode, Play Mode, and packaged players. Every advanced control needs a useful default, units, validation,
tooltips, reset behavior, and a documented quality/performance consequence.

This is an engine-wide feature release, not a same-version 0.4.8 binary hotpatch. Precise versioning is a release
decision, not an instruction to publish or mutate Git now.

## Planetary Integration Acceptance

The user's distant-environment report confirms that extending a numeric cutoff alone is insufficient.
The required end state is consistent rendering and simulation around planets placed far from the origin,
with continuous departure, interplanetary travel and return. Existing flat-cloud domain extensions are
interim fixes, not completion of this requirement.

Implementation order after the current cloud regression build:

1. Persist authored planet/environment identity and precise placement. Resolve camera and planet into
   a common reference frame before subtracting positions; keep the planet fixed during camera travel.
   Use the existing sector/frame foundation for astronomical placement instead of accumulating a single
   absolute float or assuming unrestricted precision from one absolute double.
2. Feed the same resolved frame to atmosphere, cloud shells, fog, celestial directions and lighting.
   Preserve flat-world authoring as an explicit mode. Planetary clouds need shell geometry and a stable
   planet-fixed weather mapping, rather than an infinite plane that silently follows the camera.
3. Connect authored radial sources and simulation focus to editor Play and exported runtime initialization.
   Gravity, visual surface and atmosphere must share planet identity and placement. Add local-up handling,
   rotating-frame motion and region transfer without discarding momentum or contact state.
4. Validate translated worlds at zero, 1e10 and 1e12 metres, plus sector-scale placements: matching local
   poses/time/settings should give equivalent local physics and lighting within measured tolerances.
   Then test an actual fixed-planet ascent, orbital/space segment and descent, including horizon changes,
   clouds from below/inside/above, fog termination, night-side lighting and repeated reference-frame changes.
5. Perform the same route in Scene/Game views and the standalone player. Record frame time, transition
   spikes, collision/velocity continuity, save/reload and replay evidence. Keep multi-planet visibility and
   independent simulation regions in scope; a single translated platform does not satisfy these gates.

Current blockers include authored integration and planetary cloud geometry. Native orbital tests and the
minimal script-free gravity replay have passed; they do not prove this end-to-end workflow.

### Distant Environment Acceptance Matrix

These are pending integration gates, not passing test results. Capture the same camera orientation, local
altitude, time, exposure and quality settings in Scene view, Game view and the exported player.

| Scenario | Required result | Failure this distinguishes |
| --- | --- | --- |
| Translate planet, surface, gravity source and camera together to positive and negative distant coordinates | Equivalent horizon, reference-ground radiance, clouds, fog and local gravity | Treating distance from global origin as altitude |
| Move only the camera and platform away from a fixed planet | Planet recedes; atmosphere and ground boundary remain attached to it | Dragging the planet or a sky dome along with the camera |
| Ascend from surface through cloud layers and atmosphere, then descend | Continuous shell intersections, haze and space transition; no loading screen | Flat cloud planes or abrupt environment cutoffs |
| Orbit to another surface normal at the same altitude | Horizon and gravity follow local up; weather stays fixed to the planet | Hard-coded world-Y altitude and world-up assumptions |
| Rebase at fixed physical camera and planet poses | No visible jump, weather phase change or temporal ghosting | Mixing solver, render and environment origins |
| Observe a second planet from space | Each planet retains its own atmosphere and ground boundary | Applying one global atmosphere to every world |

Record both daylight and night, with rays hitting the reference planet and rays missing it. The current
Ground / Void controls affect only the reference-ground boundary (`AtmosphereGroundRadiance`); they do
not establish an empty-space color or author scene terrain. Keep this distinction visible in authoring UI
and tests rather than making distant space inherit a ground tint. Preserve the user's existing far-test
scene; create separate translated-world and fixed-planet travel fixtures instead of silently moving its planet.

Source audit for the next integration batch:

- `AtmosphereUniforms.h` subtracts environment anchor and planet centre before narrowing camera position.
  Preserve this cancellation order when resolving authored frames.
- `RenderCloudVolume.cpp` constructs atmosphere and fog in the same view packet, but fog receives only
  `EnvironmentRelativeObserver`; sharing a packet does not give fog a planetary height model.
- `FogViewUniforms.h` subtracts observer Y from the authored base height. Both CPU `FogDensity.h` and shader
  `FogDensity.hlsli` evaluate density using Y. The shader's analytic segment average also assumes a linear
  height along the ray. A radial mode must replace that average as well as point sampling; substituting
  radial altitude in just one function would leave transport inconsistent.
- Local fog boxes and exclusion regions currently use axis-aligned coordinates in the environment frame.
  Resolve their authored frame consistently before culling and sampling; rotating a planet cannot change
  only the global height profile while leaving these regions behind.

Retain the existing flat-height mode for legacy scenes. Cover radial CPU/shader density agreement,
tangent rays, opposite surface normals, translated frames and bounded sampling cost before claiming
planetary fog acceptance. These changes are not yet implemented.

## Current Implementation Evidence

### Physics Origin Integration Gate

Validated region integration now includes:

- Double solver origins with checked conversion; body creation/writeback and presentation retain precise
  translation. Initial scene startup selects a bounds-based origin on a 1024 m grid when enabled physics objects
  exceed the default 8192 m region. VFX starts with the same origin.
- Native precise ray/capsule/overlap queries; legacy float scene queries explicitly convert through the origin.
  Checkpoints retain double origins and reject precision loss before mutation, including intermediate addition.
  Native world checkpoints now preserve gravity and empty-world state. Player physics replay has been updated to
  schema 5 with radial sources and body motion settings. A minimal script-free custom-gravity export
  passes 300-tick separate-process strict replay (GravityPlayerQA/fixed-verify.json); managed and planetary
  replay acceptance remain pending.
- Precise collision/trigger points through actual native-to-C# marshaling, preserving the legacy approximate point.
  Managed API/design-time parity and the native reload fixture pass.
- Character movement/support carry, animation grounding probes/anchors, and VFX collision frame conversion.
- Real solver stack/CCD tests at eight origins through 1e15 metres; actual distant scene startup, falling bodies,
  precise raycasts, foot grounding and moving-platform carry.
- Explicit live-origin translation preserves contacts, sleeping stacks, platform carry and foot support. Flight/impact
  tests compare repeated rebasing with a non-rebased control; callback/thread rejection preserves state. The latest
  focused batch passes 68 cases / 14,244 assertions (`physics-live-origin-edge-final-tests.log`).
- Coordinated scene migration preflights VFX before committing physics/support caches and VFX. Explicit runtime
  focus follows fixed-step movement automatically. The focused travel selection passes four cases / 15,399
  assertions (`simulation-focus-travel-tests.log`), including more than 60 km out-and-back dynamic travel at zero
  and 1e12 m offsets. Floor support and barrier collision during automatic shifts pass one case / 3,004 assertions
  (`simulation-focus-contact-tests.log`). These counts overlap earlier selections and do not establish planetary travel.
- Native uniform world-gravity control passes its focused test (18 assertions) and the physics selection
  (20 cases / 4,624 assertions, overlapping). Native radial fields, weak-field sleep and checkpoint integration now have focused coverage;
  character local-up and authored planetary configuration remain open.

Evidence: physics-scene-origin-selection-regressions.log (44 cases / 60,273 assertions),
physics-distant-grounding-regressions.log (53 / 197,593), physics-vfx-origin-tests.log (18 / 2,089),
physics-far-stack-ccd-final-tests.log (2 / 1,768), and physics-precise-contact-native-tests.log (1 / 1,322),
all under Build/Validation. Selections overlap; do not sum these counts into a unique-test total.

Remaining integration gates: author-facing focus selection and persistence, multiple simultaneous
regions, rotated reference frames/local gravity, runtime joint constraints,
stable replay world identity, and visual editor/player travel acceptance. Fixed-region tests do not establish
surface-to-space travel or performance at production workloads. Initial selection cannot represent an arbitrarily
wide scene in one float solver region; partitioning remains required.

Managed precise query bindings, explicit layout validation, Coral integration and editor/player consumers now pass
focused checks. Filter/trigger and bounded-overlap tests also passed (two cases / 360 assertions in
`precise-query-filter-final-tests.log`). The legacy float hit ABI remains compatible; precise entry points use a
separate contract. These tests do not establish runtime region ownership or moving-frame behavior.
Both editor and player services must route the world/context identity before invoking native precise scene queries.
Tests must include same-world ignored entities, cross-world rejection, triggers, masks, result limits, and no-hit
versus invalid-input behavior. Merely widening a previously rounded float hit cannot satisfy this contract.

### Standalone Replay Finding

A hidden standalone run using existing cooked QA content recorded 60 ticks and one checkpoint successfully
(`Build/Validation/physics-origin-runtime-record.json`). Strict playback in a fresh process then failed with
`Managed replay checkpoint is incompatible with the runtime behaviours`
(`Build/Validation/physics-origin-runtime-verify.stderr.log`). This is a failed acceptance gate, not a passed replay test.
The source trace identifies a likely cause: `Entity::World()` returns the `SceneState` pointer address, the runtime
scripting serializer persists that numeric world value, and `ScriptSystem::RestoreReplayCheckpoint` requires an exact
world/entity/component key match. Implement stable replay-world identity mapping, including managed serialized entity
references and additive worlds; do not remove identity validation or match only an ambiguous entity ID. The source
finding still needs a regression that proves the fix across separate player processes. These runs do not validate
planetary physics or visuals. No editor window was opened.

This is an integration gate, not a reason to reduce the requested scope: rotating reference frames, multiple
simultaneous regions, surface-to-space travel, and editor/player parity remain required.

- Native `Vector3` components are floats in `KeireCore/Include/Keire/Math/Math.h`.
  `TransformComponent` now stores double local positions and fixed-step position samples. Precise local/world
  accessors and preserved reparenting avoid float translation matrices; legacy accessors and presentation matrices
  still return floats. Precise presentation retains double translation until origin-relative matrix construction.
  The render-origin integration suite passed 100 cases / 92,357 assertions; it does not verify rendered travel.
- `SceneAsset.h` aliases `SceneVector3` to that float vector; transform/component properties, prefab overrides,
  snapshots, recovery, and replay must therefore be included in the precision migration. Component property bags
  now carry `Vector3d`, and Transform component schema 2 stores it with a schema-1 migration. The separate
  `SceneTransform` summary now stores `Vector3d` positions, with focused scene/prefab round-trip coverage.
  Remaining editing consumers still require auditing; the float `SceneVector3` alias remains for other fields.
- Managed `MathTypes.cs` defines float `Vector3`; `Handles.cs` exposes it through local/world Transform positions.
  The additional `LocalPositionPrecise` and `PositionPrecise` properties use `Vector3d` through version-named,
  size-checked calls. Managed contract tests pass; actual native host integration remains pending.
  Native interop, generated service bindings, both supported IDE API targets, and reflection require coordinated changes.
- The inspected Release dependency cache has Jolt `DOUBLE_PRECISION=OFF`.
  `PhysicsSystem.cpp` bridges positions through float `Vector3`, including its `RVec3` conversions.
- Navigation and audio position integration still need precision work. Camera and world-space UI matrices remain
  floats by design, but now operate relative to an explicit double-precision origin. Editor camera focus and saved
  positions retain double precision; runtime camera producers and UI projection/input share the relative frame.
  Focused headless tests pass; rendered behavior remains a separate pending gate.
- `StreamingSystem` supplies asset-segment residency and budgets. That is useful infrastructure, not proof of spatial
  world partition, planetary patch residency, HLOD, or transactional simulation-region transfer.
- `RenderEnvironmentSettings` now exposes atmosphere, layered and experimental volumetric clouds, bloom and stars
  alongside ambient/environment lighting, sky visibility, exposure, shadows and rendering-quality selections.
  Environment profiles also supply daily lighting and sky-color tracks. These integrations do not establish the
  complete weather, planetary geometry, surface-to-space or visual-quality contracts specified below.
- The VFX roadmap explicitly leaves world streaming and large-world rebasing open.

### Required Far-Range Physics Acceptance

User requested extensive physics validation at the distant coordinates used by the rendering tests. This is a
required part of large-world acceptance, not an optional follow-up or a claim of present support.

Current integration status: solver-local float coordinates have an explicit double world origin; precise body
pose, presentation, native query and checkpoint boundaries have focused regression coverage. Scene startup
now selects a distant local origin when needed. Fixed-region stacks, continuous collision, moving supports,
grounding, and precise managed contact delivery have passed focused tests. Managed precise query integration
passes native execution, filter/overlap-limit tests and editor/player builds. Dynamic region ownership, transfers, moving frames,
stable replay world identity and real travel validation remain blockers. Changing only the Jolt build option
or feeding astronomical floats into tests is insufficient.

Run identical local scenarios near zero and translated to positive/negative 1 km, 100 km, 10,000 km, 1e12 and
1e15 world origins. Preserve small local offsets when constructing fixtures. Compare body-relative results,
contacts and query identity with the near-origin baseline; report tolerances in metres, radians and seconds.
Use fixed seeds and fixed ticks, then repeat under variable rendered frame times and interpolation.

| Workflow | Required evidence |
| --- | --- |
| Resting bodies and stacks | Contact persistence, penetration, drift, sleeping/waking and stability over a sustained run; repeat with friction/restitution variations. |
| Fast and small bodies | Continuous collision against thin walls, grazing contacts and spawn/despawn bursts; count tunneling and missed contacts. |
| Character controllers | Grounding, steps, slopes, jumps, moving platforms and changed local-up direction; compare motion and contact state after returning to the starting region. |
| Queries and triggers | Raycasts, shape casts, overlaps and trigger enter/stay/exit; verify hit IDs, local hit points, normals and distances across a region transition. |
| Joints and constraints | Chains and constrained pairs crossing simulation-region boundaries; measure stretch, impulse spikes and broken constraints. |
| Moving reference frames | Translate and rotate a region during motion; verify transformed velocities, gravity, contact continuity and transfer rollback without duplicate/missing bodies. |
| Persistence and replay | Save/reload, checkpoint restore and replay at distant origins; retain precise positions and deterministic region ownership. |
| Editor/player parity | Repeat representative collisions, characters and queries in Play Mode and the cooked standalone player, with visual evidence and frame/physics-step timing. |

Record hardware, build, body/contact counts, timestep/substeps, collision settings, p50/p95/p99 physics-step times,
worst hitch and memory growth. Numerical frame tests and static screenshots cannot satisfy this matrix.
Planetary surface-to-space-to-surface travel must preserve these behaviors without a loading screen.

### Camera And Editor Validation

Standalone camera audit found RuntimeApplication.cpp still inverted the absolute WorldMatrix and omitted
WorldOrigin, unlike editor Game view. Replaced that duplicate construction with BuildPresentedSceneCamera.
Added coverage for nonzero presentation offsets at 1e15, local neighboring geometry, both projection modes,
and preservation of clip planes, clear color and fullscreen effects. Build/test and standalone rendered
validation remain pending for this correction; the earlier Scene-view checks do not cover it.

Standalone camera correction validation: DevDist KeireTests built successfully; `Precise*,World UI*,*camera*`
passed 48 cases / 722 assertions (`Build/Validation/player-camera-origin-tests.log`). DevDist staging compiled
and linked the actual runtime successfully in four incremental steps / 58.1 seconds
(`Build/Validation/player-camera-origin-stage.log`). This verifies the shared camera contract and runtime
compilation; a cooked standalone render/travel comparison is still pending.

Desktop checkpoint: the staged editor opened maximized and loaded CloudHdrShowcase after startup dependency
imports. Near-origin mesh picking, aligned translation gizmo, and F framing worked. Moving the selected
Studio Paint sphere to X = 1,000,000,000,000.25 through the Inspector and pressing F reproduced a failure:
the view moved, but the mesh and gizmo were absent. Undo restored X = -7.5 and F displayed the sphere again.
The temporary edit was undone, the editor closed, and the backed-up camera/tool state restored.

Source diagnosis: SceneViewportPanel computes framing bounds relative to the previous camera focus.
ScenePicker narrows that distant displacement into a float presentation matrix before accumulating corners;
adding the origin back in CenterPrecise cannot recover the lost center or small extents. Fix framing bounds
to retain precise translation independently of the old camera location, and cover a near-to-distant focus jump
in regression tests before repeating desktop QA. Existing tests with an already-nearby origin missed this path.
Also improve Inspector position formatting: migrated float values currently show long decimal tails
(observed Y = 1.1499999761581421); retain exact storage and deliberate precise edits.

Framing repair implemented, validation pending: bounds minima/maxima and center/radius accumulation now use doubles.
Each entity's rotation/scale matrix is evaluated relative to its own precise presentation position; its displacement
from the bounds origin is added separately in double precision. Added a near-to-distant focus/pick/viewport-fit
regression with both zero and opposite distant initial camera origins. Existing small-object framing coverage now
projects corners in the camera frame explicitly. No additional rendered success is claimed until rebuild/retest.

Framing repair native validation passed: the incremental DevDist editor-test build completed eight steps;
89 selected framing/camera/placement/distant/selection/rotation cases passed 2,025 assertions
(`Build/Validation/distant-framing-build.log`, `Build/Validation/distant-framing-tests.log`).
The staged client rebuild and reproduction of the same UI jump remain pending.

Framing repair desktop revalidation passed: DevDist staging completed in 92.1 seconds
(`Build/Validation/distant-framing-stage.log`). In the maximized editor, setting Studio Paint X to
1,000,000,000,000.25 and pressing F now centers the visible sphere and aligned gizmo. Dragging its X handle
moved it by a fractional unit (Inspector displayed 1,000,000,000,000.4689); Undo restored .25, and a second
Undo restored the original -7.5. The scene file hash matches the backup. Editor closed normally; camera/tool
baseline restored. This validates that reproduced Scene-view workflow only, not world UI, particles, Play Mode,
continuous travel, or planetary systems. Complete those remaining gates separately.

The camera/editor/UI integration batch passed these DevDist selections on October 28, 2026:

| Selection | Result | Local evidence |
| --- | --- | --- |
| Core cameras, precise VFX scene transforms, and world UI | 35 cases / 648 assertions | `Build/Validation/precise-camera-ui-core-tests.log` |
| Editor camera persistence, selection, placement, and group transforms | 86 cases / 1,856 assertions | `Build/Validation/precise-camera-ui-editor-tests.log` |
| Existing runtime UI geometry, overlays, and input routing | 9 cases / 217 assertions | `Build/Validation/precise-camera-ui-compatibility-tests.log` |

These are focused selections, not the complete test suites. The actual DevDist client build completed all 181
steps successfully (`Build/Validation/precise-camera-ui-client-build.log`). DevDist staging also passed, including
14 runtime/tool build steps, in 110.2 seconds (`Build/Validation/precise-camera-ui-stage.log`). The normal staged
launcher opened the QA editor; desktop validation is in progress. Editor-test compilation alone does not cover
every workspace consumer.
That pass must check near and distant camera movement, picking/framing, gizmos, placement, runtime UI, and particles
in Scene and Game views. It is not evidence of completed planetary travel.

The subsequent ground-to-space route still depends on planetary physics/reference frames, spatial streaming,
stable authored material coordinates, and atmosphere/planet geometry integration. Earlier chronological entries
below retain the findings and pending status from when they were written; use this section for the latest
camera/editor/UI validation state.

Before implementation, produce a checked inventory of every absolute-coordinate producer/consumer, binary layout,
serialized representation, implicit float conversion, and exposed C#/native signature. Include external-editor
workspaces and SDK samples, not only runtime sources.

## Architecture Decisions

### Foundation Audit: Verified Migration Boundaries

The first source audit identified the following coupled boundaries. This is an initial inventory, not a claim that
every position consumer has been enumerated. Paths below are repository-relative.

| Boundary | Current source contract | Required migration treatment |
| --- | --- | --- |
| Transform ownership and interpolation | `KeireCore/Include/Keire/ECS/Components/TransformComponent.h`: local and fixed-step samples use `Vector3d`; precise world accessors coexist with float legacy accessors and presentation matrices. | Complete presentation and relative rendering integration; preserve snapshot/savepoint stability. |
| Hierarchy composition | `KeireCore/Include/KeireInternal/SceneState.h`: `WorldMatrix` returns the float matrix; prefab instantiation accepts `Vector3`. | Introduce high-precision composition before deriving any relative float matrix; migrate spawn inputs too. |
| Scene persistence | `KeireCore/Include/Keire/Scenes/SceneAsset.h`: `SceneVector3` aliases `Vector3` for both position and scale. | Split position semantics from scale; version the persisted coordinate representation without globally widening unrelated vectors. |
| Reflected values | `KeireCore/Include/Keire/ECS/Component.h` now appends a `Vector3d` kind and variant alternative; double codecs and a generic editor drawer are implemented. | Finish special Transform controls, managed asset metadata, cloning and prefab override validation. |
| Managed Transform | `KeireManaged/Handles.cs` and `NativeRuntime.cs` now add precise local/world accessors and preserve precision in `Translate`; legacy properties and `RuntimeApi.cs` still use `Vector3`. | Verify actual host calls, finish presentation/fallback contracts and migrate absolute-position consumers. |
| Editor position operations | `SceneDocument::TransformValues` now accepts `Vector3d`; undo payloads and translation-gizmo baselines retain doubles. Special numeric controls and rotation/scale pivot matrices still use floats. | Finish the double position widget and camera-relative gizmo projection; execute editor undo/multi-edit tests and obtain UI acceptance. |
| Native implementation of managed calls | `ScriptSystemRuntimeFoundation.cpp`, `ScriptSystemRuntimeServices.cpp`, `ScriptSystemReload.cpp` under `KeireCore/Source/Scripting`, and `KeireCore/Include/KeireInternal/Scripting/ScriptSystemInternal.h`. | Reject mixed layouts before calls execute; test all local/world/presentation get/set paths. |
| Built-in property ABI | `KeireCore/Include/KeireInternal/Scripting/ManagedBuiltinComponents.h`: `NativeBuiltinProperty` is asserted to 56 bytes and carries `Vector4`; `KeireManaged/NativeRuntime.cs` mirrors its sequential fields. | Version and verify size, offsets, alignment and kinds on both sides. Never reinterpret the existing float payload as doubles. |
| Physics queries and results | `KeireCore/Include/Keire/Physics/PhysicsSystem.h`: body positions, query origins/centers and hit/contact points use `Vector3`, alongside local geometry and directions. | Classify each field as position, displacement, direction or local geometry; convert relative to the accepted simulation region. |
| Navigation | `KeireCore/Include/Keire/Navigation/NavigationSystem.h`: nodes, routes, endpoints, nearest-point/raycast and agent targets use `Vector3`. | Bind local tiles/routes to explicit high-precision origins and frame revisions; migrate author-facing queries. |
| Audio | `KeireCore/Include/Keire/Audio/AudioSystem.h`: source/listener positions and velocities use `Vector3`. | Compute listener-relative values before narrowing; retain frame-aware velocity for Doppler. |
| Environment persistence | `KeireCore/Include/Keire/Rendering/RenderSystem.h`: `RenderEnvironmentSettingsSchemaVersion` is 8 as checked October 26, 2026. | Preserve existing settings migration alongside the separate versioned environment profiles and simulation state; a schema number is not feature coverage. |

The prerequisite rotation restoration defect is fixed and checked with 17 focused test cases / 1,236 assertions.
The staged DevDist Editor also passed light-toggle, Undo, Redo, Undo and clean close in the sandbox QA project.
This closes that prerequisite only; the remaining LWE-0 inventory and numerical contracts remain open.

### Initial Numerical Evidence (before the October 28 precision migration)

The original Transform audit found that `TransformComponent::WorldPosition` extracted translation from a float
`WorldMatrix`, and `SetWorldPosition` uses a float inverse-parent matrix. `SceneState::WorldMatrix` in `Scene.cpp`
caches float hierarchy composition; fixed presentation samples and their interpolation also remain float. Transform
schema 1 stores `position` as a reflected `Vector3`; both `SceneAsset.cpp` and `SceneSerialization.cpp` decode vector
components with `get<float>()`. A storage-only double field would still lose precision through these paths. Migrate
composition, world/local conversion, reflected property codecs, scene formats and presentation samples as one
coherent slice, with signed/nonuniform-scale hierarchy and save/Undo regressions.

October 28 update: precise translation storage, hierarchy position evaluation, local/world setters, component
codecs and fixed-step position samples have now been migrated in the working tree. Render/presentation matrices
still narrow to float, and preserved reparenting still uses float matrices for rotation/scale decomposition.
The native regression build is pending; the original audit above records the starting point, not current completion.

This audit also exposed a pre-existing reparent failure hazard: the parent was changed before the preserved local
pose passed scale validation. Reparent now preflights matrix conversion, decomposition, finite position, supported
scale and rotation normalization before publishing hierarchy changes. This fixes failure safety in the existing
float path; it does not establish high-precision hierarchy support.

A host IEEE floating-point arithmetic probe on October 25, 2026 measured a requested 0.001 m displacement after
storing two absolute coordinates and subtracting them. These are representation measurements, not engine gameplay,
physics, rendering or performance qualification.

| Absolute distance (m) | Difference after float storage (m) | Difference after double storage (m) |
| --- | --- | --- |
| 1,000 | 0.0009765625 | 0.0009999999999763531 |
| 100,000 | 0 | 0.0010000000038417056 |
| 10,000,000 | 0 | 0.0010000001639127731 |
| 149,597,870,700 | 0 | 0.001007080078125 |
| 1,000,000,000,000,000 | 0 | 0 |

An initial internal numerical primitive now lives in
`KeireCore/Include/KeireInternal/Spatial/SectorCoordinate.h`, with focused coverage in
`KeireTests/Source/Math/SectorCoordinateTests.cpp`. It evaluates a candidate 65,536 m sector width and centered
double offsets in [-32,768, 32,768). It checks carries and address overflow, rejects nonfinite offsets, canonicalizes
signed zero, and subtracts integer addresses before conversion to double. `SectorPosition.h` composes three axes
with explicit nonzero world/frame identities; mismatched-space subtraction is rejected. Translation splits movement
before adding local offsets, preserves local detail during huge sector-aligned movement, and returns a new value
only after all axes succeed. Coverage includes signed normalization, nearby differences at extreme addresses,
movement, spatial-identity rejection and failure atomicity. Distant returned displacements remain subject to double
precision limits. These primitives feed the internal codec and frame conversion below, but are not yet wired into
normal scene/Transform APIs or rendering; sector width is not a frozen public or file-format contract. Identity
tags alone do not validate frame lifetime or implement moving/rotating frame conversion.

`SectorPositionCodec.h` adds an internal 80-byte, explicitly little-endian record prototype with format/version tag,
sector-width fingerprint, world/frame identities and three integer-sector/double-offset pairs. It preserves integer
and double bits without dumping native struct layout. Decode rejects wrong lengths, unsupported tags/sector widths,
nonfinite or noncanonical offsets, and invalid zero identities. Tests cover every truncated length,
exact extreme-address round trips and malformed records.
This is structural validation, not a checksum or authentication mechanism; valid-looking changed bytes require the
containing asset/checkpoint integrity contract. Existing scene, prefab and recovery formats do not use this codec yet.

`FrameRotation.h` supplies internal double quaternion normalization, inverse, composition and displacement rotation.
Composition applies the child orientation first using active right-handed rotations. Scaled normalization handles
finite extreme and subnormal inputs; invalid rotations/displacements and nonfinite results are rejected.
Tests include inverse/composition checks over 201 input rotations.
This type provides orientation math only; the snapshot layer below supplies instantaneous motion conversion.
Frame ownership, time evolution, applied inertial forces and world integration remain open.

`FrameSnapshot.h` now evaluates one immutable child-to-parent edge at an explicit finite time and nonzero revision.
Position conversion rotates the local radius and translates a sector position; velocity conversion includes parent
translation and `angularVelocity × radius` in parent axes, with an inverse path. Same-world distinct-frame checks
prevent accidental self/cross-world edges. Tests include a known tangential-velocity reference and forward/inverse
motion at a huge parent-sector address. Snapshot values do not own frames; the registry validates live bindings.
Applied inertial forces and simulation integration remain open. Rotated radii and relative velocities are doubles
with magnitude-dependent precision; this is not a
promise of millimetre detail for arbitrarily long local lever arms or arbitrarily large velocity differences.

Snapshot motion now also carries acceleration. Forward conversion includes origin acceleration, angular
acceleration, centripetal and Coriolis terms; inverse conversion removes those terms before rotating to child axes.
The combined Release suite passes 11 cases / 18,876 assertions, including stationary rotating/inertial references
and an independently differentiated translating, accelerating, rotating trajectory. These are coordinate conversions,
not a solver applying fictitious forces, a gravity model or time integration.

This supports bounded local offsets and subtraction before narrowing. Subtracting an origin cannot recover a
displacement already lost while storing absolute coordinates. Remaining numerical gates include integrated native/C#
round trips, long frame-chain error accumulation and representative physics/rendering comparisons. Sector size and
qualified gameplay bounds remain unfrozen until those integration tests establish their error budgets.

### Precision And Coordinate Spaces

Use one high-precision engine/player build by default. Do not globally replace every float or require double-capable
GPU shaders.

| Data | Proposed authoritative representation | Boundary rule |
| --- | --- | --- |
| Universe/root location | Checked 64-bit sector address plus bounded double-precision offset | Normalize negative coordinates, overflow, and boundary carries consistently; never flatten to one lossy float vector. |
| Reference-frame pose and motion | Double translation, double orientation, and double linear/angular motion | Frame graph is acyclic, versioned, world-owned, and evaluated at an explicit simulation time. |
| Entity translation and spatial queries | Double-precision frame-local translation and typed world position | No hidden narrowing through float matrices, native service calls, or component property bags. |
| Small local orientation, scale, mesh vertices, skinning, solver deltas | Float where the measured error budget permits | Promote composition when a long lever arm or planetary frame requires double precision. |
| Rendering and local effects | Origin-relative float data created from high-precision differences | Subtract the correct origin before conversion to float, not afterward. |
| Streaming/navigation addresses | Integer cells/patches plus high-precision origins | Resident memory and simulation cost depend on active content, not total coordinate extent. |

`Vector3d` now exists as a native and managed value contract; it does not yet replace authoritative transform storage.
Names such as `WorldPosition` and `SpatialFrame` remain design names, not promises of existing APIs.
Keep float vectors useful for normals, directions, UVs, and local geometry. Typed position-plus-displacement
operations should make ordinary movement convenient without requiring authors to manipulate sector indices.
Converting positions between frames must be explicit; subtracting positions in unrelated worlds must be rejected.

The public world-position contract must actually be high precision. Adding a double getter while leaving all normal
Transform, spawn, raycast, and save workflows on float positions would not satisfy the requested default behavior.

The current Transform migration boundary includes more than its backing field. `TransformComponent::WorldPosition`
extracts translation from a float `WorldMatrix`, and `SetWorldPosition` converts through the inverse float parent
matrix. Its reflected `position` property is `ComponentPropertyKind::Vector3`; `SceneSerialization.cpp` decodes each
channel with `get<float>()`. Managed `Handles.cs`, `RuntimeApi.cs`, `NativeRuntime.cs`, and the native scripting
internal calls also exchange float `Vector3`. Migrate these paths together with versioned scene/prefab property
decoding, inspector/history editing, cloning, and parent-preserving operations. A double backing value routed through
the existing matrix or property codec would still lose precision before rendering. Keep float matrices as explicit
camera/simulation-local output boundaries, not the authoritative storage or world-position API.

Prefab override persistence currently records `ComponentPropertyValue` variant indexes. A new precise-position
alternative must not shift existing numeric tags: append a stable alternative or version and migrate the format.
The managed builtin-property payload also has an explicit native size contract; migrate managed/native layouts and
validate offsets together. Position and scale currently share `SceneVector3`, but their precision semantics should
be separated instead of indiscriminately widening all vectors.

Choose and freeze the sector size, supported hierarchy depth, coordinate bounds, precision budgets, and normalization
rules after numerical prototypes. A large representable number is not a qualified gameplay range.

### Reference Frames And Simulation Regions

The ownership audit found that `Entity::World()` in `KeireCore/Source/ECS/Entity.cpp` returns the current
`SceneState` address cast to an integer. `SceneRuntimeWorld::FindWorld` and `SessionForWorld` resolve that
process-local identity by looking through loaded scenes. It must not be persisted as a spatial world ID, reused as
a durable frame key, or treated as the identity of the whole additive runtime world. Two loaded scenes can belong
to one spatial world while retaining distinct entity-resolution identities.

The internal `SpatialIdentity` fields therefore mean logical coordinate-space keys. The prototype codec preserves
those keys; it does not prove that they exist, are live, or are safe to resolve in another runtime instance.
Before integrating the registry, implement these ownership boundaries together:

- The runtime spatial registry belongs to the logical `SceneRuntimeWorld`, with a corresponding isolated authoring
  context for Edit Mode. Additive scene loading binds authored frame keys into that registry.
- Live references also carry a runtime incarnation/generation; logical keys alone cannot distinguish a previous Play
  session, closed world or unloaded/recreated frame. Reuse the existing `StableHandle` conventions where suitable.
- Persist logical keys and remap them on load/clone. Never serialize native pointers, live slot indices or runtime
  generations as durable identities. Edit and Play can share authored keys while rejecting each other's live handles.
- Preflight frame-parent changes for existence, same-world ownership, depth and cycles. Publish topology and frame
  evaluations atomically at one time/revision; reject asynchronous work tied to an earlier accepted revision.
- Unloading a scene must not remove a frame still referenced by another loaded scene or a persistent entity. Define
  reference ownership and explicit transfer/removal policy before implementing unload. Closing a world invalidates
  all live handles; immutable numerical snapshots can remain readable but cannot authorize new world mutations.

The runtime lifecycle audit identifies two integration paths that must be handled together:

- `SceneRuntimeWorld::Adopt` accepts a stopped session; the primary Play path can register ownership before
  `SceneRuntimeSession::Play` publishes the clone and runs lifecycle callbacks. The asynchronous
  `SceneRuntimeWorld::Process` activation path now also registers the session before `Play`, removing that prepared
  session and restoring the previous active handle if startup or validation fails. Spatial bindings still need to
  participate in this boundary before callbacks; failed activation must release prepared attachments without
  retiring the previous loaded scenes or changing the active scene.
- `Impl::Retire` keeps sessions with persistent roots, destroys other roots, and marks the entry persistent rather
  than erasing it. Frame ownership must follow surviving entities and cross-scene references, not merely the entry's
  `Loaded` flag. `StopAndErase` and world `Close` are distinct teardown boundaries; final world closure must invalidate
  live frame handles even when immutable snapshots or attachment counters survive outside the world.

Required integration regressions include spatial queries from loaded-scene startup callbacks, failed activation
rollback, additive scenes sharing a frame, persistent roots surviving replacement, and stale handles after Stop/Play
or world destruction. The current registry-only tests do not establish any of these runtime behaviors.

These remain required integration contracts. An internal `FrameRegistry.h` now implements the topology/lifetime
subset: unique logical keys, weak lifetime-token handles plus monotonic runtime serials, stale/foreign-instance
rejection, acyclic reparenting, refusal to remove roots or frames with children, revisions and idempotent close.
Tests cover handle reuse and destruction.
Registry-bound evaluations verify child/parent logical keys and topology revision when bound, then check runtime
lifetime, revision and requested evaluation time before owner-side acceptance. Reparenting, unrelated topology
changes and close invalidate prior bindings; numerical snapshot values remain readable. This conservatively rejects
stale topology/time results; publication additionally tracks pose epochs as described below.
Registry operations require serialized owner access; this is not a concurrent service. `SceneRuntimeWorld` now owns
one registry privately, and each adopted scene entry holds an attachment to its shared root. Retired entries with
persistent roots retain that attachment; erased entries release it, and world closure invalidates live frame handles.
The private `SceneRuntimeSpatialAccess` boundary checks owner-thread/open-world access and returns a borrowed registry
view whose lifetime is bounded by the world object. This initial context uses local logical world/root keys of 1;
these are not globally unique durable IDs or a finished authored-frame import policy. Separate registry instances
reject each other's live handles even when their logical keys match. Per-entity frame bindings, authored frame keys,
automatic synchronized evaluations and high-precision Transform integration remain unimplemented.
The private entity resolver now validates the live entity against the exact runtime scene instance and returns that
scene's root frame. It rejects authored Edit clones, stale previous-Play entities, destroyed entities and foreign
worlds, while allowing entities in retained persistent scenes. This is ownership-aware root resolution, not arbitrary
per-entity frame assignment or a high-precision position query.
Pose-preserving reparent/transfer must wrap topology edits transactionally during
integration. The existing entity-resolution API remains unchanged.

The registry now enforces a configurable maximum edge depth (internal default 256, root depth zero). Creation checks
the new depth; reparenting preflights descendants against the proposed topology before mutation. A zero limit allows
only the root. Tests include a rejected subtree move that leaves parent relationships, handles and revision unchanged.
The default is a protective algorithmic bound,
not a measured performance or accumulated-error qualification for 256 rotating frames. Reparent preflight currently
scans nodes and ancestors; representative world-scale mutation costs still need measurement.

A bounded 256-edge translation-only regression now checks forward and inverse conversion with root sector addresses
of positive/negative one trillion, exact binary fractional edge translations and millimeter-sized leaf offsets.
Its local position errors satisfy a `1e-10` meter absolute tolerance; velocity and acceleration round-trip unchanged.
The fixture also covers 256 repeated quarter-turn orientations, whose XY translations cancel every four edges,
using an analytic expected root position rather than conversion code to calculate the reference. Forward and inverse
position errors satisfy the same tolerance, and transformed velocity/acceleration satisfy `1e-10` relative tolerance.
A further 256-edge fixture uses coincident frame origins with nonzero angular velocity and acceleration along one
axis. Its instantaneous velocity and acceleration match an analytic aggregate-frame reference, including Coriolis
and centripetal terms, and inverse conversion restores the input motion within `1e-10` relative tolerance.
The nine-case registry Release suite passes 121 assertions. This does not integrate trajectories or qualify a physics
solver, arbitrary moving-frame workloads, rendering, performance or native/managed parity; those gates remain open.

Keep the entity hierarchy, spatial reference frames, render origins, and physics-region ownership distinct.
Their conversions share explicit contracts; they are not four mutable aliases of one global origin.

`FrameConversion.h` now converts motion along validated registry paths through their nearest common ancestor.
It checks live source/target handles, source-space identity, finite motion/time, unique current evaluations and the
complete required route before evaluating any edge. Sibling conversion can omit the common ancestor's parent edge,
avoiding an unnecessary distant-root round trip. Tests cover sibling round trips and
missing/duplicate/stale/wrong-time rejection.
This consumes caller-provided edge evaluations; it does not yet generate synchronized tree evaluations or publish
body changes during transfer.

The registry's `Publish` operation now preflights a complete nonroot-frame evaluation batch at one time and topology
revision, allocating all bindings before advancing a separate pose epoch. Failed publication preserves old bindings;
successful publication invalidates them even at the same timestamp without changing topology revision. Individual binding is private:
accepted snapshots can only be obtained through complete publication. Consumers may select the needed edges from
one published batch, but mixing old/new pose epochs is rejected even when time and topology revision match.
Publication does not compute trajectories or integrate frame motion.

`FrameAttachment.h` adds explicit move-only retained usage, separate from non-owning lookup handles. Removal rejects
frames with outstanding attachments; moving/resetting an attachment releases its previous ownership exactly once.
The counter can outlive registry closure, so release never dereferences a destroyed world. Attachment release may
occur on a worker, while acquisition/topology operations remain owner-serialized. The earlier seven-case registry
suite passed 71 assertions in Release and AddressSanitizer, including worker release and retained usage after close.
Runtime scene adoption now acquires one shared-root attachment per scene entry, as described above.
`SceneRuntimeWorldTests.cpp` covers transient unload, retained persistent scenes, owner-thread rejection, closure and
cross-world handle rejection. Separate entity-resolution tests cover Edit/runtime identity and previous-Play rejection.
This does not establish per-entity authored-frame attachment or frame transfer; those integration contracts remain open.

An example ownership hierarchy is:

```text
Universe
  Solar-system inertial frame
    Planet frame: orbital motion and planetary rotation
      Surface simulation regions: locally bounded positions and velocities
      Ship frame: moving interior and nearby interactions
    Other planets and independent distant simulation regions
```

Provide double-precision frame composition, relative transforms, velocities, and accelerations.
Transitions preserve absolute pose and physical motion, including the frame's linear velocity and angular contribution.
Specify treatment of rotating/accelerating frames and inertial forces; do not silently equate a visual parent change
with a physically correct transfer. Transfer interacting/constraint-connected bodies together, with preflight,
rollback, stable gameplay identity, contact/event policy, and an atomic fixed-tick commit.

A camera moving or an Editor view opening must not move authoritative gameplay state.
Multiple players, distant active regions, Scene/Game views, reflections, and shadow cameras need independent valid views.
Simulation regions may shift their internal origin safely without changing authored positions or save dirtiness.

Planet/space modes need gravity-source definitions, local-up-aware characters, and double-precision trajectory
integration. Support both authored ephemerides and simulated gravity, with explicit semantics and reference tests.
A visual orbit animation is not a substitute for the requested space-simulation path. Scientific ephemeris accuracy,
relativistic physics, and unbounded universe size are not implied; qualify and document the actual models and limits.

### Rendering

- Create an immutable high-precision scene/frame snapshot, then derive camera/pass-relative render packets.
  Keep origin/frame revisions with accepted packets, picking results, temporal history, and asynchronous work.
- Audit culling, LOD, instancing, shadows, decals, skinning attachments, terrain, lights, probes, baking, world-space UI,
  debug overlays, VFX, picking, and screenshots. Correct visible meshes alone do not close the precision migration.
- Separate translated coordinates from absolute/procedural coordinates in shader contracts. Preserve stable material
  patterns, terrain seams, cloud noise, and VFX trails across origin changes and multiple cameras.
  Version affected Shader Graph nodes/contracts and explain migration instead of silently changing node meaning.
- Validate near/planet/astronomical depth ranges. Assess reversed-depth and distant-object passes against the current
  renderer; origin-relative rendering alone does not fix depth precision or guarantee an unlimited far plane.
- Use planet surface patches and local mesh origins. Do not render an Earth-sized float mesh or feed it to physics as
  one giant collider and call the result precise.
- Maintain visible, capability-based fallbacks. Never describe a layered cloud fallback as volumetric rendering.

The proposed camera-relative strategy is consistent with the precision/performance boundary described in
[Epic's large-world rendering overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/large-world-coordinates-rendering-in-unreal-engine-5).
This is a design reference, not evidence that Kéire already implements it.

### Physics, Navigation, Animation, Audio, And Streaming

Enable and fingerprint the locked Jolt double-position configuration consistently for supported platforms/configurations.
Update conversions and query origins/results; changing its build flag alone is insufficient.
Retain bounded local physics regions for space/planet interactions, rather than imposing astronomical velocities and
rotating planetary lever arms on one solver.
[Jolt's big-world and space guidance](https://jrouwe.github.io/JoltPhysics/#big-worlds) documents that double positions
do not remove its float velocity, rotation, and broad-phase constraints.

Navigation uses bounded local tiles with high-precision origins and hierarchical connections across streamed regions.
Planetary navigation needs surface-aware adjacency and changing local up. Unload/cancellation cannot leave stale routes.
Root motion and IK targets convert through the same spatial frames; accumulated motion and checkpoints retain precision.
Spatial audio derives listener-relative positions from double differences, preserving velocity/Doppler continuity
through frame changes. World UI and input rays use the same accepted view/frame identity.

Extend existing residency services with spatial-cell and planetary-patch ownership, prefetch, cancellation, hysteresis,
HLOD/impostors, origin-aware bounds, and explicit CPU/GPU/physics/navigation budgets.
Cross-cell references use stable identities. Persist authored/runtime deltas independently of unloaded content.
Large coordinates do not justify loading or simulating every object everywhere.

## Customizable Environment System

The current integration boundary is `RenderEnvironmentSettings` in
`KeireCore/Include/Keire/Rendering/RenderSystem.h` (schema version 6), with persistence and validation in
`KeireCore/Source/Rendering/RenderSettings.cpp`. It exposes ambient color/intensity, exposure, an environment asset,
environment rotation and diffuse/specular intensity, sky visibility, and renderer quality settings. These controls
are useful output targets for evaluated environment state, but do not themselves provide a calendar, weather
simulation, planetary atmosphere or cloud system. Preserve existing project settings during migration and keep
renderer quality preferences separate from authored weather/day-night assets. The experimental atmosphere adds an
explicit seven-vector shader contract to `SkyUniforms`: the CPU subtracts the double planet center before narrowing
to kilometre-valued shader inputs. Camera transforms still originate as floats; this is not the authoritative
large-world transform migration. The initial renderer uses bounded single-scattering ray integration; LUTs,
multiple scattering, aerial perspective, atmosphere-driven IBL and rendered acceptance remain open.

Use reusable, versioned environment assets and world/planet-owned simulation state, not mutable global time/weather.
Resolve project defaults, planetary settings, local volumes, scene overrides, cinematics, and per-camera presentation
through documented priorities. Blending and transitions must be inspectable and undoable.

### Time, Day/Night, And Celestial Lighting

The private `EnvironmentLightingProfile` provides an immutable art-directed daily directional-light track. It validates
bounded, ordered phase keys, azimuth/elevation, linear RGB and renderer-representable intensity; evaluates wraparound
segments without converting the integer calendar day to floating point; and returns a normalized incoming ray in a
local east/up/north basis. Azimuth takes the shortest path, with a positive 180-degree tie. Authored ownership returns
no automatic-light contribution. This is a daily authoring evaluator, not a planetary ephemeris, atmosphere, multi-star
renderer or seasonal model. Those requirements remain unchanged.

`EnvironmentLightingRecord` writes version 2 for directional-only tracks and version 3 for tracks with optional
ambient RGB/intensity animation on every key. Ambient interpolation wraps midnight, and omission preserves project
ambient settings. Version-1 records remain readable with shadows disabled. Ambient snapshot/render-packet and editor
controls passed combined DevDist, focused Release/ASan render capture, limited Windows authoring/Play checks and a
77-asset cook with a 30-frame headless player run. Rendered standalone/backend and soak acceptance remain open. The private
`EnvironmentLightingSettingsStore` adapter reads/writes the project-default payload at
`ProjectSettings/EnvironmentLighting.keiresettings` (outer schema 1), with a 256 KiB bound, atomic replacement and
authored-light compatibility when absent. The Editor document loads it before replacing the current project and
includes it in Save, Cancel, Reset and Undo/Redo snapshots. Failed saves retain dirty state. Settings now prepare all
four payloads before grouped publication through an independent recovery journal. The payload is now included in
cooked schema-6/7 manifests, but is not a registered reusable asset or published SDK contract. Environment settings now
provide an ownership toggle and collapsible key editors for phase, angles, linear RGB and intensity, plus bounded
add/remove. Disabling preserves keys. New keys inherit preceding values in the largest cyclic interval. The first
authoring controls have model/document and navigation coverage, but still require desktop interaction acceptance.
Reusable profile assets and full Edit/Play/player visual parity remain required. Copy/assignment preserve value validity; moved-from profiles become
valid empty authored-light profiles rather than retaining automatic ownership with an empty track.

Settings-save recovery now uses an explicit settings policy in `ProjectFileTransaction.cpp`: separate journal/lock,
exact four-file allowlist, original/replacement digests, and rollback preflight against external edits. The material
entry points retain their own journal policy. Settings staging reuses a bounded active directory; only known payloads
are cleaned, and unknown files block reuse instead of being deleted. Exclusive project open recovers before settings
consumers; read-only open and document opens with stale caller-supplied values reject pending recovery. Editor audio
preferences now load after project recovery. Retry Save retains pending document values while recovering a previous
publication. The combined DevDist build and a desktop read-only-file failure/retry check passed: pending edits
survived, retry completed recovery, and Undo plus Save restored all eight QA settings files to their baseline.
External-edit conflict hardening is a subsequent batch and needs its own validation. Focused tests must cover each publication boundary, existing and initially absent
files, external edits, interrupted cleanup and unprepared staging. This is recoverable publication, not simultaneous
multi-file visibility or an additional power-loss durability guarantee.

The next propagation boundary must include all current consumers together: `AssetTool/Source/Main.cpp` loads the
project profile during cooking; `EnvironmentRuntimeManifest` validates its versioned payload; the runtime manifest
and `RuntimeApplication` retain it until world creation; and Editor Play restores the same profile before startup.
`SceneRuntimeEnvironmentAccess` now owns a per-world lighting profile with value-copy reads and combined time/profile
restore. Restore rejects non-owner calls, closed worlds and simulation-tick callbacks before publication; invalid
time leaves both values and the revision unchanged. Editor Play and player startup now pass the profile alongside
time before scenes start. Continue testing legacy manifests
against authored-light defaults, malformed new payload rejection, and matching Editor/player evaluation. Do not
silently reinterpret schema-5 time-only content as requiring a lighting profile. Renderer packet ownership and
history invalidation still need their separate integration gates before claiming visual parity.

The private manifest decoder now prepares a schema-6 time-and-lighting state with required validated records.
Schema 4/5 retain authored-light defaults and reject mislabeled lighting fields. The cooker emits schema 6 and the
player retains both fields until world creation. The old time-only decoding entry point rejects schema 6 rather than
discarding its lighting profile. Shared render requests now consume evaluated snapshots; preservation alone is not
visible lighting parity.

Development acceptance has exercised a separate nondefault environment-owned two-key profile through schema-6
cooking and a 30-frame headless player run. The cooked time and complete lighting payload matched authored settings;
77 assets produced one pack. This verifies serialization and startup acceptance, not the player's rendered light,
Edit/Play visual parity, backend parity or shipping-package compatibility. Those gates remain open.

`SceneRenderRequest::EvaluatedEnvironment` now accepts an optional value snapshot with source-lifetime ID,
discontinuity revision and optional directional sample. Capture validates finite bounded values and normalized
direction, replaces the directional slot without an authored entity/cookie, and tracks source/revision/ownership
mode in camera history under its existing publication rollback. Omission preserves authored lighting. Runtime worlds
now expose evaluated snapshots with a generated per-world lifetime identity; the shared player/Play Game-view request
builder attaches them. Scene and camera-preview requests now share runtime snapshots during Play and workspace preview
snapshots during Edit; Edit Game uses the same preview source. Automatic shadow controls,
baked-lighting policy, allocation-failure regression tests,
backend validation and visible Play/player checks remain required; this path is not production-ready yet.

A direct headless capture regression now verifies evaluated values, first-frame/revision/source/mode continuity,
authored fallback and rejected-value history preservation against the actual capture implementation. Its initial
failure exposed missing capture-side validation (normal Submit already validated); the guard was added and the
Release rerun passed. Allocation-failure rollback, GPU/backend behavior and host-driven rendering are still unproven.
The private shared profile-to-snapshot evaluator preserves source/revision and narrows validated local-frame values
consistently; it does not perform planetary observer-frame conversion.

Editor host wiring covers both requests in `SceneViewportPanel.cpp` (main view and camera preview), plus the
Edit branch in `EditorWorkspaceGameViewport.cpp`, through one controller accessor backed by workspace-owned preview
state. During Play, Scene and Game use the runtime world's source identity. The preview snapshot revision changes
on profile edits/Undo or clock discontinuities, but not unchanged draws/normal advancement; profile edits preserve
playback. Project close clears preview identity, so the next snapshot allocates a new lifetime ID. Focused model
tests cover these transitions. A Windows DevDist Scene/Edit check scrubbed an environment-owned two-key profile
from fraction .375 to zero and approximately .499, visibly removing and restoring direct-light highlights, then
reset to .375. The saved time record retained its day, fraction, pause, rate and day length; Apply was not invoked.
The existing sky remained static. This accepts only Scene/Edit directional preview, not Play/player parity,
sky/cloud/weather rendering, automatic shadows or all backends.

The internal `KeireInternal/Environment/EnvironmentClock.h` prototype now separates an integer day from a bounded
double day fraction, with configurable day length/rate, pause, reverse advancement and checked atomic updates.
Explicit seek/restore increments a discontinuity revision for future temporal consumers. Its tests cover distant
calendar dates, boundary crossings, huge whole-day skips and rejected-state preservation. Each runtime world now owns
one clock behind a private owner-thread boundary returning value copies. Project settings, Edit preview, cooked
manifests and the art-directed daily-light evaluator now consume it, as described above. It is not yet assigned to
planetary frames, persisted in scene assets, or exposed through a public managed time API.
Calendar rules, bounded event delivery and planetary celestial evaluation remain required; this implementation is not a
production day/night feature or a frozen public serialization contract.

The private `EnvironmentClockRecord` provides a versioned JSON record for future profile/save/manifest adapters.
Its signed day uses a canonical decimal string rather than a JSON number, preserving the full integer range through
consumers that parse numbers as doubles. Fraction, day length and rate remain validated numbers; pause is a strict
boolean. Missing/extra fields, unknown versions, numeric days, noncanonical day strings and invalid clock values are
rejected. Text round-trip tests include both signed 64-bit day limits. The record does not persist preview state or
process-local discontinuity revisions; the authored file adapter below is separate from runtime save ownership.

The private `EnvironmentTimeSettingsStore` now supplies an authored-time file adapter at
`ProjectSettings/EnvironmentTime.keiresettings` (schema 1, `initialTime` using the record above). It returns defaults
only for a missing file, bounds reads to 64 KiB, validates before filesystem mutation, and uses the existing atomic
text replacement helper. Filesystem tests cover round trips, corruption, size limits, invalid saves preserving bytes
and directory-target rejection. The Editor project-settings document now loads this state before replacing its old
document, includes it in edit snapshots and Undo/Redo, and saves it through the adapter. An explicit preview Apply can
use UpdateEnvironmentTime/CommitEdit; preview itself remains transient. Dirty state compares rendering, authoring,
environment time and environment lighting
to the last successful save/open, so Cancel and Undo can restore a clean document. Cooker manifests and runtime hosts
now use the same record: cooking emits schema-7 manifests, the player requires/validates environmentTime for schemas 5/6/7,
and legacy schema 4 starts with default time. Play initializes from the current project document before scene startup;
the player restores its decoded time immediately after creating its runtime world. These paths still require integrated
build and player/Editor acceptance; this is not a completed visible environment workflow or a savegame format. The record remains reusable
without requiring the player to access an Editor project directory.

The persistence audit originally found schema-4 renderer/authoring output. `AssetTool/Source/Main.cpp` now writes
`runtime-manifest.json` schema 7, including atmosphere/cloud records, while `KeireRuntime/Source/RuntimeApplication.cpp` decodes that manifest rather than
loading the project's settings directory. `PlayerPackage.cpp` verifies the packaged manifest exists, not that its
environment values round-trip. The shared codec and explicit cooker/player schema migration therefore need
value-parity tests in addition to Editor save/reload. Do not claim player parity from project settings
tests alone, or persist the transient preview clock as the authored starting time.

Runtime integration audit identified the following host split before automatic clock advancement:
`EditorWorkspaceLifecycle.cpp` and `RuntimeApplication.cpp` dispatch normal fixed ticks through `SceneRuntimeWorld`,
but `EditorWorkspaceLayer.cpp` previously routed Pause to the primary `PlaySession`, and `EditorWorkspaceTheme.cpp`
routed Step directly to that session. The current implementation routes both through `SceneRuntimeWorld` and
inherits world pause on adoption/activation. `SceneRuntimeSession::FixedUpdate` skips paused sessions, while `Step` runs a
paused session explicitly. A clock advanced unconditionally by the world would therefore diverge from paused or
manually stepped gameplay, especially with additive scenes. The clock now advances once before callbacks in an accepted
fixed tick or manual step, never in variable Update. Empty worlds or worlds with no playing sessions do not advance
normally; paused worlds advance only through a successful step preflight. Clock overflow rejects the tick before
callbacks. Callback faults do not roll back accepted time. Recursive fixed ticks/steps and clock restoration during
those callbacks are rejected. Continue to test primary plus additive sessions, paused frame callbacks, manual
stepping, scene replacement, and fresh Play
instances before claiming host parity. Do not silently choose a primary scene's clock as planetary time authority.

- Configurable day length, initial time/date, calendar, time scale, pause, scrub, time jumps, and save/replay state.
  Keep simulation ticks/epoch separate from shader float time so long sessions do not degrade motion.
- Artist-directed cycles and astronomical planet-driven mode, including axial tilt, latitude/longitude, seasons,
  multiple suns/moons where configured, moon phases, stars, and eclipses.
- Sun/moon light intensity, spectra/color, angular size, shadows, horizon response, and dawn/dusk curves.
  Provide physically motivated defaults and explicit artistic overrides.
- Coordinate ambient lighting, sky/IBL/reflection updates, fog, exposure, emissive night-light hooks, and gameplay events.
  Schedule costly updates under budgets; do not synchronously rebuild every probe every frame.
- Time skips and scrubbing define event delivery/coalescing and reset temporal histories without replaying unbounded
  sunrise, weather, sound, or lightning events.

### Sky And Atmosphere

- Customizable atmospheric density/height, molecular/aerosol scattering, absorption, haze, aerial perspective,
  ground albedo, artistic tint, and exposure.
- Continuous ground, aerial, orbital, and space views, including looking toward/away from the sun and through limbs.
  Derive altitude/up from the active planet/frame, not a universal world Y value.
- Handle atmosphere/cloud/surface depth, planet shadow, transparent objects, and local fog consistently.
  Empty space has no automatic terrestrial fog or rain.

Ground-to-space scattering and scalable lookup-table techniques are covered by the primary research paper
[A Scalable and Production Ready Sky and Atmosphere Rendering Technique](https://sebh.github.io/publications/egsr2020.pdf).
Implementation selection still requires backend prototypes, quality references, and measured Kéire budgets.

### Celestial And Artistic Sky Authoring

Implementation checkpoint: `StarFieldSettings` and its internal version-1 record now define the opt-in authored
contract (seed, density, linear HDR intensity/tint, color variation, angular radius in radians, rotation, bounded
twinkle and strictly ordered solar-elevation fade thresholds). Rendering settings schema 8 and runtime manifest 9
now carry the record through serialization, asset cooking and runtime loading. Render packets now carry a bounded
twinkle phase from the shared environment clock, and authored star edits invalidate camera history. The batched
DevDist build passed, as did capture regression tests (159 assertions) and the project-settings suite (89 cases,
3238 assertions). GPU uniforms, the seeded spherical-shell shader, atmospheric attenuation,
cloud composition and experimental controls are implemented but still await integrated visual/performance validation.
No production-ready star-field claim is made before those checks. Older projects
must migrate with stars disabled; older readers must reject new required records rather than discard authored data.

- Include a customizable star field: deterministic seed, density, brightness, color variation, apparent size,
  bounded twinkle and night visibility. Support an artistic sky texture option without claiming it is a procedural
  star simulation. Stars must be occluded by opaque geometry, clouds and atmosphere as appropriate.
- Expose sun and moon apparent size, color, intensity and artistic appearance; include moon phase and orientation.
  Separate art-directed controls from astronomical positioning so neither silently overrides the other.
- Provide sky, horizon and zenith color controls, dawn/dusk gradients and day/night blending, with clear separation
  between physical scattering parameters and artistic overrides. Color adjustments must have documented linear/HDR
  semantics and must not accidentally double-apply exposure or bloom.
- Author sky colors, star visibility, celestial intensity and atmospheric transitions on named timeline tracks.
  Support color-gradient and scalar-curve keys with interpolation controls, midnight-wrap continuity, reset-to-default
  and reusable presets. Scrubbing previews the same evaluated values used in Play and the player; it must not
  silently change the saved starting time. Show units, physical/artistic mode and overridden values beside controls.
- Track optional presentation effects separately: aurora, artistic Milky Way/backdrop textures, sun halos and
  weather-linked rainbows. These remain planned extensions, not features supplied by the existing star shader.
  Each needs explicit enablement, intensity/color controls, quality budgets and graceful disabled fallbacks;
  physically motivated effects must document their viewing and weather conditions.
- Put these controls in the Environment workspace with preview, presets, Undo/save, quality and accessibility
  controls. Validate edit/Play/player parity, temporal stability, large-world reference frames, sunset/night transitions
  and lower-quality fallbacks. These are requested scope, not claims of current implementation.

### Layered Clouds And Volumetric Clouds

Implementation checkpoint: a separate local-frame cloud volume kernel, slab/depth clipping shader and validated
authoring record are now implemented in development. Cloud record version 3 preserves authored volume settings;
sheet-only projects retain version 2 when the volume has its exact defaults. Both Environment and Project Settings
expose grouped volume authoring controls through the existing document commit flow, explicitly labeled in development.
Shader generation includes DXIL, SPIR-V and MSL variants.
Per-camera packets capture bounded wind phase, camera-relative geometry and integer quality/seed values; lazy pipeline
creation, device cleanup and draw-recording code compiled in the DevDist checkpoint. The current source batch adds
an actual graph pass after opaque indirect lighting and before transparency, with sampled-depth admission.
The authoring UI enable/Undo/save smoke check passed with the QA settings restored byte-for-byte. The current integration
batch adds per-sample-count volume pipelines and preserves multisample HDR color for the later transparency resolve;
this does not yet establish correct sampled-depth edges or transparent-material rendering.
The background shader now consumes a shared view/depth evaluator intended for transparent fragments as well.
Its radiance follows the existing pre-exposed HDR convention, with finite-target clamping after exposure and
opacity left unchanged. Built-in straight-alpha surfaces now receive the same view constants and integrate to
their fragment depth. Generated material shaders now opt into the explicit volume ABI, including composed VFX,
with mode-specific straight/premultiplied/additive/holdout composition and pixel-depth-offset clipping. Legacy shaders,
multiplicative front-scattering compensation remain unfinished. Built-in CPU particles and GPU sprite/ribbon/mesh
fallbacks now evaluate cloud transport to their fragment depth with straight-alpha composition. The new recording path has not
passed rendered acceptance. Do not use this intermediate state as a production cloud feature.

This is not production-ready volumetric rendering. Remaining integration includes the material/VFX gaps above,
MSAA edge handling, atmosphere/planet interaction, ground shadows and GPU
acceptance. The current volume model is a local horizontal slab, not the required planetary shell model. Kernel
compilation and CPU geometry/codec checks do not prove cloud appearance, performance or traversal correctness.

- Regular clouds: efficient layered/sheet or dome rendering with editable textures, coverage, opacity, height,
  movement, lighting, horizon fade, and blended layers. Useful as an artistic choice and a lower-cost quality mode.
- Volumetric clouds: editable shape/detail noise, coverage/weather maps, density, extinction/scattering,
  layer bases/thicknesses, erosion, wind/advection, cloud type, and planet-aware geometry.
- Authoring priority: the default preset should aim for a natural appearance with convincing dense cores, soft
  boundaries and irregular large-scale placement. Expose body scale, detail scale, macro variation strength/scale
  and density shaping independently; do not force every style through one pattern-size slider or hard-coded warp.
  Retain deterministic seeds, explicit metre/unitless labels, Undo/Redo, reset and reusable presets. Existing authored
  records require explicit migration defaults, and CPU validation, GPU uniforms, generated materials, cooking and
  player loading must carry every control consistently. Qualify presets with wide-horizon and moving-camera checks
  for repeated placement, sliding, aliasing and abrupt transitions, as well as below/inside/above views. Artistic
  styles remain supported; natural defaults are a visual target, not a current production-quality claim.
- Integrate sun/moon illumination, self-shadowing, ground cloud shadows, atmospheric interaction, and aerial perspective.
  Flying below, inside, above, and through clouds must all be supported.
- Budget ray steps, light/shadow sampling, resolution, update cadence, temporal reconstruction, and memory.
  Verify motion vectors/history rejection for camera cuts, time jumps, fast travel, frame changes, and planet transitions.
- Near volumetric/far planetary and layered representations need stable, authored transitions without popping,
  doubled opacity, sliding noise, or shadow discontinuities.

### Weather And Related World Effects

- Blendable regional weather profiles: clear, overcast, fog, rain, snow, and storms; artist-authored and seeded
  procedural schedules with biome, altitude, season, and planet rules.
- Wind direction/speed/gusts shared by clouds, precipitation, foliage/material hooks, VFX, audio, and exposed gameplay
  queries. Define coordinate frame and units for every wind field.
- Rain/snow use bounded simulation around relevant active views/regions, with collision/occlusion policy for interiors,
  roofs, underwater cameras, and unloaded areas.
- Lightning/thunder have configurable intensity, color, frequency, locations, physical timing/distance behavior,
  bounded lights/audio, and a reduced-flash accessibility setting.
- Expose wetness, puddle/snow coverage, drying/melting, temperature, and visibility through material/gameplay hooks.
  Do not rewrite shared source material assets or make scenes dirty just because simulated weather advances.
- Include local fog volumes, storm audio/ambience, visibility transitions, and documented gameplay event/query APIs.

Environment authoring acceptance must include genuine volumetric clouds and fog, not only layered substitutes.
Initial implementation checkpoint (superseded by the October 28 fog integration evidence below): an internal CPU volume-transport reference defines homogeneous segment
transmittance and integrated source weighting, including vacuum, thin and opaque limits and front-to-back
composition. At that checkpoint it was not connected to a GPU fog pass or authoring settings. Height/local fog is
now connected to the shared volume renderer and Environment controls; shadow coupling and exclusion remain unfinished.
The reference also integrates overlapping homogeneous media by summing extinction and per-channel lit source
coefficients before integration. Focused tests cover layer-order independence, segment-subdivision invariance,
emissive vacuum, invalid coefficients and overflow (6 cases / 166 assertions across volume transport). This is the
reference used by the shared cloud/fog interval path. This CPU reference alone does not validate the renderer. The integration model
follows the shared-medium direction of [Frostbite's unified volumetric rendering](https://advances.realtimerendering.com/s2015/).
Atmosphere now consumes a shared `VolumeTransport.hlsli` segment-weight kernel;
numeric tests compile that same scalar shader source on the CPU rather than maintaining a copied formula.
Windows and Unix shader generation both fingerprint the include. This does not substitute for GPU fog validation.
The current tone-map shader
samples scene color, temporal history, velocity and bloom but not scene depth; a sky-only overlay there would
not meet the requirements below. Integrate depth-aware radiance before bloom/tone mapping, and address transparent
surfaces explicitly rather than fogging their composite at the opaque background depth. Validate GPU results
against segment-subdivision invariance before adding temporal reconstruction or variable-density approximations.

Expose cloud coverage, density, base/top altitude, shape/detail scale, wind, extinction, scattering anisotropy,
light absorption and shadow controls. Fog needs global height/density falloff plus local volume blending and
scattering controls. Provide named quality presets and advanced bounded sample, resolution, temporal/history and
distance budgets, with visible capability/fallback explanations. Verify edit-mode, Play and cooked-player parity,
quality changes, camera cuts, origin shifts, night lighting, cloud interiors, transparent surfaces and GPU cost.
Treat these as acceptance requirements: volumetric cloud and height/local fog controls now exist, while the
remaining shadow, exclusion, planetary geometry and player/performance gates still need evidence.
A weather preset alone does not establish physical water, fluid dynamics, or a full climate simulator.

### Authoring And Diagnostics

Provide searchable grouped controls, presets, curve editing, units/ranges, revert-to-inherited/reset, asset previews,
weather/time timeline, regional-volume visualization, and clear source-of-value inspection.
Edit Mode must render the same environment evaluation as Play and player views: sky, layered/volumetric clouds,
weather and lighting are not Play-only features. Provide preview play/pause, speed, time scrubbing, reset and an
explicit Apply Preview Time action. Frozen previews must be useful for lighting authoring. Previewing alone must not
dirty scenes, change the saved starting time, enter gameplay, or write to the runtime world's clock. Apply must use
the document's Undo/Redo transaction and preserve authored time scale/pause settings. Hidden previews and Play Mode
suspend editor preview advancement without accumulating a catch-up backlog.

The private `EnvironmentPreviewClock` model implements transient time, reset, explicit apply-candidate creation,
owner-thread checks and hidden/Play suppression. It returns copies and cannot write documents or runtime state.
Tests cover isolated clocks, reverse playback, scrub revisions, invalid/overflowing changes and close/rebind behavior.
Workspace integration now owns a preview clock, synchronizes authored changes and Undo/Redo, and advances it once in
the Edit Mode preview phase. The Environment settings tab includes playback, reset, day/fraction scrubbing, speed,
and explicit Apply Time through document history and saving. These controls disclose that configured daily lighting
follows preview time while sky/cloud animation remains unavailable. Scene/Edit scrub/reset and unchanged saved time
have desktop evidence above. Required acceptance still includes sky/cloud/weather captures, Undo/Redo of Apply,
independent Play/Stop behavior and rendered parity across views. Model tests alone do not prove those visual gates.

The renderer integration audit found three independent Editor submission sites: Scene and camera-preview requests in
`SceneViewportPanel.cpp`, and Game requests in `EditorWorkspaceGameViewport.cpp`. Standalone submission in
`RuntimeSceneRendering.cpp` uses `CaptureRuntimeSceneRenderRequest`. These paths now attach evaluated environment
snapshots alongside renderer settings and float material time. Advance the preview once in the workspace frame lifecycle, not in any draw
method; sample it consistently for every visible view. Multiple panels must not accelerate time, and a hidden panel
must not stall other visible previews.

The workspace already has an `EditModePreview` update phase in `EditorWorkspaceLayer::OnUpdate`; use that single
host boundary for preview advancement. Bind after the project settings document opens and unbind before its shutdown
phase closes it. Synchronize authored time changes (including Undo/Redo) explicitly, rather than rebinding every frame
and erasing transient preview playback. Keep unscaled elapsed time and Edit/visibility gating distinct from runtime time.

`SceneRenderRequest::EvaluatedEnvironment` carries a validated value snapshot. `RenderResourceCaches.cpp` first
resolves authored directional lighting, then replaces that slot when an environment directional sample is supplied,
without rotating or recoloring authored light components. Explicit authored-versus-environment ownership preserves existing
scenes when automatic lighting is disabled. Per-view sky/history data must retain the clock discontinuity revision
without retaining references to mutable preview or runtime clocks. Test Scene, Game, camera preview and player paths,
including multiple visible views, disabled automatic lighting, scrubbing/history invalidation and additive scenes.

The render-capture source audit establishes these additional integration contracts:

- `RenderBackendInternal.h::ResolveLighting` selects one active, non-baked authored directional light by stable entity
  ID. `RenderResourceCaches.cpp` first selects from the primary contribution, then falls back through the other scenes.
  Automatic celestial lighting must have an explicit ownership mode: disabled retains that exact selection; enabled
  supplies one evaluated directional contribution rather than creating an entity or adding another authored sun.
  Preserve independent point/spot lights. Multiple simultaneous celestial light sources require a separate shader
  and lighting-budget extension; one directional slot cannot be advertised as complete multi-star lighting.
- `RenderSceneRecording.cpp` uses the directional entity ID to resolve mixed-light shadow-mask channels. A generated
  environment light must not inherit an authored entity ID or baked/mixed-light binding accidentally. The existing
  empty-ID path produces channel zero (no mixed mask); qualify dynamic shadow behavior explicitly instead of faking
  an authored light. Baked indirect illumination still needs a defined time-of-day compatibility policy.
- Camera history now also stores environment source identity, revision and directional ownership in `MotionHistoryEntry`,
  indexed by view surface and epoch. Both identity **and** revision are required: a new world or an
  Edit/Play switch can have the same numeric revision as the previous source. Profile/evaluation and spatial-frame
  identities must also participate where they change the evaluated environment. Ordinary clock advancement should
  not invalidate history every frame; discontinuities and source replacement must.
- Camera/object/skin history publication already has rollback around accepted capture queue insertion. Extend that
  transaction for environment identity/history so rejected packets do not consume a new environment revision or
  corrupt the previous accepted view state. Test rejection followed by retry, two views with independent histories,
  identical revisions from different sources, backward/forward scrubs, and source teardown/replacement.

The directional snapshot and history foundations now implement part of these contracts; broader celestial, shadow,
baked-light and backend requirements remain open. Preview time now drives evaluated directional lighting, not sky or
cloud animation. Profile authoring must preserve persistence, light ownership and accepted-frame history contracts.

### Automatic Directional Shadows: Implemented Subset And Remaining Acceptance

Source audit: `RenderShadowRecording.cpp` gates cascaded directional shadows on `Lighting.Enabled` and non-disabled
`Lighting.Shadows`, not on an entity ID. It already consumes the evaluated direction, shadow strength/bias and resolution
hint together with project cascade count, distance, split lambda and resolution. Editor authoring is now implemented;
broader rendered acceptance remains open. `EnvironmentDirectionalLight` carries validated shadow settings and capture
copies them after resetting `SceneLighting`. Daily-profile evaluation forwards persisted settings; legacy version-1
records migrate to disabled defaults, while version 2 stores explicit shadow configuration.
Merely preserving the previous authored light's shadow fields
would leak unrelated authoring state and is not the intended fix.

The implemented batch covers the following contracts, with focused model, settings, capture and cooked-player checks:

- Add profile-owned quality (disabled/hard/soft), resolution hint, strength and bias; validate enum values and finite
  strength/bias in `[0, 1]`. Carry these through immutable evaluation and public snapshot validation into capture.
  Keep entity/cookie/mixed-mask ownership empty and leave local lights unchanged.
- Version the strict lighting record deliberately. Decode existing version-1 records with shadows disabled to preserve
  appearance; write the extended record with an explicit new version. Do not silently accept extra fields in version 1.
  Test project load/save and cooked runtime manifests, including old records, malformed records and unsupported versions.
- Preserve shadow configuration through key insertion/removal/replacement, ownership toggles, copy/move, Undo/Redo and
  preview synchronization. Expose controls in Environment settings with clear distinction from project-wide cascade
  budgets. Changing the profile must invalidate view history through the existing revision mechanism.
- Capture values and rejection atomicity have focused coverage. Remaining visual gates include moving shadows in
  Scene, Edit Game, Play and player, disabled fallback, night intensity zero, additive scenes and independent views.
  A headless cook or capture test alone does not prove rendered shadows or backend parity.

Desktop acceptance on October 25, 2026 used the isolated LightingProfileAcceptance20261025 project and DevDist Editor.
Changing Disabled to Soft produced visible cast silhouettes in Edit Scene. Soft persisted after reopening the Editor.
Changing strength from 1 to 0 removed those silhouettes; restoring 1 restored them. Saved record readback confirmed
each strength value and unchanged lighting keys. The QA Editor was closed after testing.

These checks establish only that bounded Edit Scene workflow. Hard/Disabled reversal, intermediate strength,
resolution/bias editing, time-varying shadow movement, Edit Game, Play, camera preview and GPU-rendered player parity
still require acceptance. Headless player/cook checks establish data propagation, not image quality or backend parity.
Contact shadows, baked-indirect time-of-day policy, multiple celestial lights and planetary reference-frame conversion
remain separate required work.

Expose runtime region/frame, time, weather seed, cloud history validity, quality fallback, residency, and feature costs.
Small/narrow docks, multi-selection, Undo/Redo, Play isolation, reload, recovery, script autocomplete, and packaged-player
configuration are acceptance workflows, not polish deferred indefinitely after rendering.

## Delivery Milestones

Each milestone closes its listed contracts; later milestones remain required.
Batch cohesive changes before testing. Use focused tests and DevDist for iteration; reserve full release packaging
for intentional compatibility/release gates rather than every small source edit.

| Milestone | Required deliverables | Gate before moving on |
| --- | --- | --- |
| LWE-0: Baseline and contracts | Coordinate/interop inventory; error and performance budgets; reference-frame/sector design; environment dependency map; migration specification. Resolve the verified Transform snapshot normalization/savepoint defect first. | Failure-first numerical/snapshot fixtures; reviewed current-source inventory; no unsupported product claims. |
| LWE-1: High-precision foundation | Typed sector/frame/world positions, double translation/composition, spatial bounds/rays, managed parity, reflection/property support, serialization/recovery/prefab/replay versions. | Exact round trips, canonicalization, hierarchy/parenting, migration/corruption/overflow tests, native/C# ABI checks, no silent narrowing. |
| LWE-2: Relative rendering and editor | Per-view/pass origins, picking/gizmos/snapping, cameras/history, geometry/lighting/material/VFX/UI conversion, depth strategy, frame-aware authoring. | Near-origin versus translated-scene image/error comparisons; multiple distant views; camera cuts; explicit supported-backend results. |
| LWE-3: Local simulation and space motion | Jolt double-position build, bounded moving physics regions, transfer/rollback, gravity/trajectory models, characters/local up, navigation/audio/animation integration. | Contact/constraint/vehicle/character tests under huge frame translation/velocity; frame-transfer conservation and failures; verified planetary/orbital reference cases. |
| LWE-4: World/planet content | Spatial partition, planetary surface patches, residency/persistence, HLOD, cross-cell references and routing, traversal prefetch. | Bounded resource trends under fast travel and repeated load/unload; cancellation/stale completion tests; planetary surface traversal without cracks or lost state. |
| LWE-5: Environment foundation | Versioned profiles/volumes, time/calendar/events, sun/moon/stars, day/night, exposure/IBL integration, basic layered clouds, atmosphere ground-to-space. | Time scrub/skip/pause/save/replay and profile-blend tests; reference sky/lighting captures across surface/space viewpoints; usable Editor workflow. |
| LWE-6: Clouds, weather, and surfaces | Volumetric clouds/shadows, temporal/scalability paths, regional weather, wind, precipitation, fog, lightning/audio/accessibility, material/gameplay surface hooks. | Measured GPU/CPU/memory budgets; indoor/weather exclusion; origin/frame/history changes; nighttime/storm/cloud-interior tests; honest fallbacks. |
| LWE-7: Integrated qualification | Three finished playable test projects, guides/presets, upgrade workflow, SDK/player/Hub compatibility, soak and backend reports. | Every acceptance category below passed or explicitly blocks the supported release. Land-only success cannot close planet/space/environment scope. |

## Acceptance And Qualification

### Three Playable Test Projects

1. **Open World Expedition:** on-foot/vehicle travel through streamed populated terrain, day/night and seasonal/weather
   blends, interiors, vegetation/wind hooks, storms, persistent world changes, navigation, and save/reload.
2. **Planet Explorer:** walk around a rotating planet, test poles and patch seams, change local up, fly through clouds,
   climb through atmosphere into orbit, observe sunset/eclipses, descend and land with stable contacts.
3. **System Voyager:** independent moving planets/ships, ship interiors while traveling, docking/region transfer,
   distant simultaneously active regions/views, solar-scale travel, high bulk velocity with low local relative motion,
   replay and save/reload at each transition.

Use these as end-to-end workflows in the actual Windows Editor and standalone player, not only synthetic fly-throughs.
Include attach/create scripts and completion, prefabs, undo, authoring while paused, build/cook, failed dependency
recovery, and re-opening migrated projects.

### Seamless Surface-To-Space Acceptance Route

Status: **not implemented end-to-end and not editor-accepted**. The October 26 follow-up inspection confirms that
`TransformComponent` still exposes float `Vector3` positions and float `Matrix4` world transforms. Internal sector
and reference-frame tests are prerequisite evidence only; they do not qualify this route.

Run one continuous controllable session from a populated surface region through atmosphere, orbit and interplanetary
space, then return and land at the original landmark. Do not replace any leg with a scene-loading screen, restart,
teleport or pre-rendered transition. Background streaming is allowed, but simulation and player control must continue.
Use a real surface with collision, nearby props and animated actors, a moving vehicle, streamed regions and an orbital
landmark; an empty scene or a scaled miniature planet is not equivalent evidence.

Record each leg in both Editor Play and the cooked standalone player:

| Leg | Required evidence |
| --- | --- |
| Surface traversal | Stable small-object placement, contacts and camera motion while crossing streamed region and sector boundaries. |
| Ascent through clouds/atmosphere | Continuous control, terrain detail transitions, cloud/fog traversal, atmosphere depth and consistent planet-relative up. |
| Orbit and departure | Stable near-vehicle detail at large coordinates, frame handoff, velocity continuity, distant body visibility and temporal-history resets. |
| Deep-space travel | Bounded streaming memory, responsive control at high bulk velocity, no accidental terrestrial fog, and independent moving-frame behavior. |
| Return and landing | Previously authored content/state restored, no duplicate actors, missing collision or frame discontinuity, and measured landing-position error. |

Capture frame-time distributions and worst stalls, resident/peak CPU and GPU memory, streaming queue/backlog,
simulation-step overruns and positional/contact errors alongside video. Include cold-cache and warm-cache runs,
reverse travel, rapid direction changes, save/reload in orbit and missing/corrupt streamed data. Define supported
speed, world extent and hardware budgets from results; a short successful journey does not establish unlimited scale.
Keep unsupported legs explicitly pending until the normal scene, physics, renderer and streaming paths support them.

### Numerical And Simulation Gates

- Translate identical local scenarios to positive/negative 1 km, 100 km, 10,000 km, and solar-system-scale locations,
  including sector boundaries. Set exact qualified extents/error budgets from the measured results.
- Retain millimeter/centimeter local placement targets where appropriate to gameplay; measure position, angular,
  contact, trajectory, and visual errors separately. Representation precision alone proves none of these.
- Repeated parent/frame changes, world/surface/ship transfers, constrained groups, sleep/wake, teleports, fixed-step
  interpolation, loaded-cell changes, invalid inputs, and teardown must retain defined outcomes.
- Test historical source/schema migration, native/managed serialization, bit-stable saved content where required,
  corruption, missing references, and older players refusing unsupported formats safely.

### Rendering And Environment Gates

- Surface/orbit/space, polar/equatorial views, all cardinal sun angles, day/night, moons/eclipses, indoor/outdoor,
  every cloud perspective, volumetric fog/transparency, weather transitions, and quality changes.
- Fast cameras, multiple distant cameras, origin changes, moving frames, time skips, resize/dynamic resolution,
  shader reload, device/resource recreation, cancellation, and asynchronous packet/history rejection.
- Capture GPU-rendered evidence separately from CPU graph approximations; verify supported D3D12, Vulkan, and Metal
  targets on actual available hardware before claiming backend acceptance.

### Resource And Longevity Gates

Record build/dependency identities, hardware/driver/backend, seed and workload sizes, warm-up and measured duration,
median/P95/P99 frame timings, CPU/GPU time sources, streaming stalls, memory/VRAM peaks and end state, failures,
and shutdown results. Establish named quality targets on named reference hardware; do not promise universal FPS.

Run bounded functional tests first, then representative mixed gameplay loads and multi-hour traversal/weather soaks.
Include hundreds of animated agents, spawning/despawning physics objects, long audio sessions, navigation churn,
storms/clouds, distant regions, and repeated scene/frame unloads. Retain watchdog, cancellation, recovery,
resource-leak, and determinism/seed results; do not infer long-session reliability from a short successful run.

## Dedicated Environment Authoring Workspace

Add a dockable Environment panel alongside Project Settings, not an independent settings store. Both surfaces must
use the same ProjectSettingsDocument, preview clock, validation, save diagnostics, and Undo history. Panel focus
selects that history; opening both panels must not duplicate updates or overwrite an active edit. Project Settings
retains technical defaults, while the dedicated workspace prioritizes scene authoring.

- Persistent preview transport: current time/day, play/pause, speed, reset to authored time, and an explicit Apply
  Time action. Clearly distinguish transient preview values from saved start time, including during Play Mode.
- A readable day timeline with keyboard-accessible scrubbing and lighting-key selection; expose exact numeric editing
  without requiring tiny drag targets. Do not silently reorder or merge keys at day boundaries.
- Task-oriented sections for time and lighting, sun/atmosphere, clouds, weather, and post-processing. Keep useful
  controls visible at narrow dock widths with scrollable sections and concise contextual help.
- Presets and scoped resets must be undoable and show their scope. Preserve unrelated rendering settings. Unsupported
  weather/volumetric features need honest availability explanations, not controls that appear to work.
- Show save status and actionable validation errors in the panel. Retain edits after failed saves; avoid duplicate
  independently dismissible errors that disagree with Project Settings.
- Acceptance includes simultaneous-panel editing, keyboard-only use, narrow/wide layouts, Undo/Redo, save/reopen,
  preview reset/apply, edit-mode rendering, Play Mode isolation, and cooked-player parity. Validate using actual
  Windows-control workflows after a cohesive DevDist batch; a panel mockup is not production acceptance.

### Verified Integration Points

Development status: a first dockable Environment panel is implemented through the existing Project Settings
owner, with Lighting, Sky, Clouds and Bloom tabs, shared save-error state and preview visibility tracking.
It is not production-ready. A Windows DevDist session verified full-row section expansion/collapse, volume-cloud
numeric editing, visible cloud attenuation, resetting volume settings and restoring the original settings files.
That session did not qualify visual quality, cross-panel gesture isolation, keyboard-only authoring, Play/player
parity or responsive layouts. Focus/close boundaries have an implementation that commits without implicitly saving,
plus deterministic transition tests; their complete desktop workflow still needs validation. The subsequent
multiple-scattering controls also passed a staged Windows Edit-mode check: changing contribution from zero to one
visibly brightened the same paused-time volume, committing the numeric edit cleared the dirty state, and Reset
Volume Settings restored both baseline settings files exactly. Clouds still appeared overly smooth/banded, and
numeric fields lacked idle contrast. This does not establish Play/player, performance or production visual quality.

- `EditorWorkspaceLayer` owns the existing ProjectSettingsDocument and EnvironmentPreviewClock. The dedicated panel
  must borrow them through the existing controller boundary, never create another ticking clock or settings document.
- `UpdateEnvironmentPreview` includes Environment alongside Project Settings, Scene and Game in its visibility
  decision. Verify transport continues when Environment is the only visible authoring view.
- `ProjectEnvironmentPreview` deliberately returns null during Play Mode. Show an explicit runtime/preview state;
  do not work around this guard and modify the edit preview while the scene runtime owns lighting.
- `ProjectSettingsDocument::Update` retains one edit baseline until CommitEdit. Switching between two settings
  surfaces must finish the active gesture before another surface begins editing; otherwise unrelated gestures can
  collapse into one Undo action. Add regression coverage for focus transfer, panel closing and tab navigation.
- `DrawEnvironmentPreviewControls` currently performs Apply Time's document update, commit and save together.
  Keep those semantics when replacing its stacked controls with transport/timeline UI. Preview scrubbing must remain
  transient, and save failures must retain the candidate with a visible retry path.
- Register the panel through UiWorkspace, expose it in the Window menu, and draw through the existing editor-workspace
  lifecycle. Keep technical asset selection and rendering defaults accessible in Project Settings.

### Sky-Color Timeline Implementation Boundary

The initial source audit found directional/ambient interpolation but no sky-color result. The implementation now
adds independent gradient tracks to the profile, snapshot and sky shader; changing directional-light color remains
separate from authored horizon/zenith gradients.

The internal `EnvironmentColorTrack` and `EnvironmentColorTrackRecord` now provide bounded linear-HDR color keys,
per-segment Linear/Smooth/Hold interpolation and versioned records. Focused numerical tests cover midnight wrapping,
exact-key boundaries, distant dates and invalid records. Shared authoring controls now expose Horizon/Zenith strips,
selected-key numeric editing, interpolation, insertion, removal and sky-only preset/reset in both settings surfaces.
The shared lighting profile now retains optional independent Horizon/Zenith tracks in record version 4, evaluates
them into validated snapshot colors even with authored directional-light ownership, and preserves them across
lighting-key edits and daylight presets. Project serialization accepts this record and has a 256 KiB bound, tested
with maximum-size light and color tracks. Renderer consumption is now implemented with a copied 32-byte gradient
block and composition before stars/clouds, preserving the sun and physical planet-ground response. Native/shader
compilation passes. Desktop preset, scrubbing, Undo/save and preview reset have been exercised; full authoring
acceptance, rendered quality and matched-camera cooked visual parity remain pending. Older records
keep no artistic sky override; older readers reject the new record rather than dropping it.

The current validation evidence has these limits:

| Boundary | Verified evidence | Remaining acceptance |
| --- | --- | --- |
| Authoring | Preset changes the Edit sky; scrubbing changes transient time; RGB picker edits update the key and rendered sky; selected-key preview seeks exactly; Undo/save and preview reset restore the baseline | Picker drag history, precise HDR edits, crowded keys, Redo, save/reopen and narrow layouts |
| Persistence | Runtime JSON round-trip retains enabled/disabled HDR tracks, interpolation and distant-day time; malformed records reject | Additional migration cases as the profile gains more tracks |
| Actual cook | Isolated project cooked 77 assets; cooked lighting JSON exactly matched the authored profile | Repeat after subsequent schema or cooker changes |
| Player loading | Explicitly waited bounded player process exited successfully with the cooked profile | Longer sessions, transitions and multiple views |
| Player rendering | D3D12 player visibly rendered the authored HDR sky and scene | Matched camera/time/exposure Edit/Play/player comparison, sun/ground boundary checks and other backends |
| Clouds | Layered clouds visible in that player scene; generator 25 volume-only Edit comparison confirms optical density increases opacity without shrinking bodies, with Undo/save restoring the baseline | Natural appearance still fails: soft bodies and repetitive horizon bands persist at both 48 and 128 view samples. Motion, performance and matched player checks remain open. |
| Cloud noise transfer and sampling | Complete 4-cubed and 64-cubed mip readbacks matched CPU bytes on local D3D12/Vulkan; 416 shader samples across integer/fractional mips and negative coordinates matched the CPU reference within 0.004; D3D12 AddressSanitizer probes passed | These checks establish transfer and sampled-atlas agreement, not rendered quality. Cache recovery, transparency, other devices and Metal execution remain unqualified. |
| Regional cloud grouping | Generator 26 DevDist and fixed-view desktop edits pass: regional strength/scale affect grouping, and Undo/save restores all prior rendering values with the expected record migration. Focused settings/layout and D3D12/Vulkan offscreen checks pass | The combined shape trial has sharper outlines but retains repeated rows and horizon banding. Natural defaults, motion/wind, Editor/player parity and performance remain unqualified. |

See [Cloud and weather implementation checkpoint](CloudWeatherImplementation.md#current-acceptance-summary) for the
latest cloud acceptance table and the limits of each diagnostic. Older implementation notes describe intermediate
revisions; they do not override the current evidence or close outstanding gates.

For the next cloud comparison, retain the isolated upward camera, paused 09:00 clock, fixed seed, zero wind, exposure
one, render scale one, disabled dynamic resolution, disabled layered sheets and inactive showcase VFX. Wait for material
reimport to finish before assessing either appearance or cost. The Profiler exposes GPU frame milliseconds when timestamp
queries are supported; its other pass timings are CPU measurements. Record warmed enabled/disabled GPU frame samples
under the same viewport conditions as an initial estimate, not a cloud-pass measurement or cross-device performance claim.
For sustained CSV captures, enable renderer telemetry before starting, require a completed capture, and retain its
warmup and viewport settings. Only use rows with available renderer telemetry, supported GPU timing and a nonzero
`observed_gpu_timing_frame`; deduplicate that ID before summarizing `gpu_duration_ms`. Repeated asynchronous
observations are not additional GPU frames, and application sequence IDs must not be treated as GPU frame joins.
Report sample count and distribution for both enabled and disabled runs; a difference between full-frame summaries
is only an estimate under those scene conditions, not a timestamp measurement of the cloud pass itself.

The sky-color work must retain coverage of the complete path:

- Independent named sky tracks, with explicit physical-only versus artistic override modes. Sky animation must not
  depend on taking ownership of an authored directional light. Disabled tracks preserve the current renderer.
- Linear HDR horizon and zenith colors, gradient shape and override weight, evaluated from the same split day/time
  used by Edit preview, Play and the cooked player. Define midnight wrapping and per-segment interpolation once;
  use that evaluator for both timeline previews and submitted values.
- Versioned persistence with old profiles retaining physical-only behavior, bounded key counts and input ranges,
  strict malformed-record rejection and explicit cooked-runtime compatibility. The current lighting settings store
  now has a 256 KiB limit; continue checking that bound as additional tracks are integrated rather than truncating keys.
- Immutable per-view submission, validation, native uniform packing and matching shader layout. Apply artistic sky
  color before celestial/cloud composition so a sky adjustment does not recolor stars or clouds accidentally.
  Specify the planet-ground and horizon behavior before integrating the shader.
- Named timeline rows with gradient previews, key selection, numeric color/time controls, interpolation selection,
  undoable presets and scoped resets. Both Environment and Project Settings use the existing document and save/error
  flow; preview scrubbing does not save the start time.
- Focused migration, invalid-input, exact-key, midnight-continuity, disabled-identity and shared-clock tests, followed
  by native/shader builds and actual save/reopen, Undo/Redo, Edit/Play/player visual comparisons. A passing evaluator
  fixture alone does not complete the feature.

Moon phase/orientation, volumetric fog, weather transitions and planetary cloud geometry remain separate required
work in this milestone. The sky-color batch does not replace them or qualify surface-to-space travel.

### Next Integration Boundaries

The October 26 source audit confirms these implementation gaps, beyond the cloud appearance work:

- `AtmosphereSettings` supplies solar irradiance and angular radius, but no lunar appearance or orbital record.
  `EnvironmentLightingSnapshot` has one optional directional light; a moon disc alone would not provide moonlit
  surfaces, cloud illumination or the requested multiple celestial lights. Add explicit celestial appearance and
  lighting records, shared-clock evaluation, strict migration and renderer submission together. Keep astronomical
  positioning separate from artistic direction/phase overrides, and define ownership alongside authored lights.
- `BuiltinSky.hlsl` composes its background and sun, artistic gradient, stars, then layered clouds. Lunar radiance
  must use the same atmosphere/ground visibility and cloud occlusion boundaries, with phase and orientation evaluated
  before exposure. Validate new/full/quarter phases, horizon crossing, night lighting and Edit/Play/player parity;
  a textured circle in the sky is not acceptance for the requested moon system.
- `VolumeTransport.h` is a homogeneous-segment numerical reference, not fog rendering. Fog still needs authored
  global/local volume records, bounded heterogeneous sampling, lighting/shadow input, per-view submission and
  depth-aware GPU integration. Fog and clouds must share a defined overlapping-medium composition policy; applying
  two independent full-ray overlays in an arbitrary order is insufficient. Transparent surfaces, indoor boundaries,
  camera-inside cases, exposure, history invalidation and quality budgets require rendered validation.

These are implementation requirements for subsequent batches, not claims that the corresponding systems exist.

## Migration And Release Rules

The [October 28 technical review follow-up](TechnicalReviewFollowUp20261028.md) is deferred until the current
environment work, fog, and this plan's large-world implementation and travel validation are complete.
Do not start that separate review audit between these milestones.

- Version scene, prefab, environment, checkpoint/replay, cooked, interop, and package contracts as necessary.
  Old float content widens through an explicit migration path, preserving stable IDs and a recoverable original.
- Coordinate all canonical native and managed APIs, generated service bindings, source generators, reflection,
  template scripts, IDE design-time assemblies, and both SDK consumers. Float inputs may widen safely; precision-losing
  outputs require explicit conversion or migration diagnostics, not silent truncation.
- Fingerprint Jolt mode and spatial/interop schemas in caches/manifests. Reject mismatched Editor/player/SDK/native
  plugin combinations with repair guidance rather than loading ABI-incompatible binaries.
- Introduce no new Git operations, deployments, production package changes, billing changes, or dependency/Vendor
  edits merely because this plan exists. Those remain separately authorized operations.
- Documentation and the website describe implemented and validated subsets truthfully throughout delivery.
  Do not label the complete plan production-ready until all required workflows and evidence gates are closed.

### Shared fog/cloud segment shader checkpoint

`VolumeTransport.hlsli` now integrates finite nonnegative extinction and lit source coefficients, including emissive
vacuum. `CloudVolume.hlsli` consumes the shared segment result for opacity, representative-depth weighting,
radiance and transmission. This prepares a common integration boundary; it does not add fog density or rendering.
The scalar shader source is compiled directly into focused CPU tests and compared with the independent double
reference: 7 cases / 214 assertions pass. The new `volume-overlap` GPU probe passed 384,000 samples on each of D3D12
and Vulkan, with DXIL/SPIR-V/MSL compilation. Cloud density and ambient probes also passed on both backends at
zero, normal and high density. Full DevDist shader rebuild and native regression remain separate gates.

### Fog renderer integration sites verified

The existing cloud transport path is the integration point for fog, rather than a second post-tone-map overlay:

- `CloudVolumeView.hlsli::EvaluateCloudVolumeView` receives opaque depth or each transparent fragment's depth.
  Its early-out currently covers cloud/atmosphere only and must include fog.
- `EvaluateCloudVolumeTrace` and `IntegrateCloudVolumeWithDepth` currently intersect the cloud slab. Fog must
  integrate the union of relevant intervals; merely adding fog density inside that slab would omit ground fog.
- `RenderCloudVolume.cpp` skips when clouds and atmosphere are disabled and selects cloud temporal reconstruction
  when clouds are enabled. Both selection and history validity must account for fog, including fog-only views.
- `CloudVolumeViewUniforms` is ABI 7 (448 bytes), with legacy prefix sizes in `CloudVolumeUniformByteSize`.
  Append fog uniforms with an explicit ABI revision and update generated shader/import invalidation together.
- `Rendering.keiresettings` is schema 8. Fog needs a versioned record, disabled migration defaults, strict bounded
  validation, and editor/runtime propagation. Do not expose an operational enable control before rendering exists.

These are verified implementation constraints, not completed features. The shared scalar segment shader build is
still in progress; changing renderer source during that build is deferred to keep its result reproducible.

### Fog density model and record checkpoint

Internal `FogDensity.h` and `FogDensityRecord.h` now define bounded global height falloff and up to 16 local box
volumes, smooth edge blending, additive extinction, albedo, anisotropy, maximum ray distance and sample budget.
Density below the reference height is capped, and coordinates use an explicitly selected local metre frame.
The JSON codec rejects malformed fields and invalid budgets before returning settings. These internal records are
not yet wired to `Rendering.keiresettings`, scene submission, GPU uniforms or authoring UI. Existing project files
remain schema 8; do not describe fog enablement or migration as shipped functionality yet.

### Fog GPU density and camera-relative preparation

`FogViewUniforms.h` prepares a 560-byte bounded record, subtracting camera position in double precision before
narrowing visible local bounds. Volumes outside the maximum trace domain are culled before upload. A focused test
preserves a 0.125 m offset at a 1e12 m origin. `FogDensity.hlsli` samples the global height layer and up to 16 smoothly
blended local boxes. The `fog-density` GPU probe passed 384,000 samples each on D3D12 and Vulkan; shader generation
also passed for MSL. CPU density/codec/preparation tests passed 5 cases / 39 assertions. This validates the density
kernel and packing, not a rendered fog pass: scene submission, interval integration, light/shadow coupling,
transparency, history, serialization migration and UI remain to be connected.

### Fog settings integration checkpoint (October 28)

Rendering schema 9 now persists the public `FogSettings` record. Schemas 1–8 migrate with fog disabled;
mislabelled older documents containing fog are rejected. Saving validates all fog fields before replacing the
settings document. Double-precision local volume positions survive serialization, including a 0.125 m offset
at a 1e12 m origin. This supersedes the earlier schema-8/internal-record-only checkpoint above.

The focused optimized harness compiled the current `RenderSettings.cpp` and passed 30 cases / 1,704 assertions
across rendering settings, cloud settings and fog density/preparation. Documentation validation passed all 117
guides and 14 schema contracts. This is not renderer acceptance: scene submission, overlapping ray intervals,
lighting/shadows, history, runtime manifest propagation and authoring controls are still pending. Keep fog UI
unavailable until the render path works; batch the renderer changes before the next DevDist build and UI session.

### Shared fog/cloud transport checkpoint (October 28)

The shared cloud march now has a fog-aware overload. It covers the union of the cloud slab and fog distance,
clips local boxes against the ray, and splits intervals at volume boundaries so a half-metre box cannot vanish
between kilometre-spaced samples. Cloud and fog extinction/source coefficients combine before segment
integration. Fog-only samples skip cloud lighting; zero-strength cloud ambient occlusion skips its extra density
samples. The cloud-only overload remains available to existing consumers.

The `fog-transport` GPU probe checks homogeneous extinction/albedo, surface-depth and maximum-distance clipping,
radiance/attenuation agreement, thin overlapping local boxes, disabled fog and cloud/fog overlap. It passes
384,000 cases each on D3D12 and Vulkan; DXIL, SPIR-V and MSL compile. Existing cloud-density and cloud-ambient
suites pass at zero/normal/high density on both backends. Ordered (`precise`) radiance accumulation is necessary
for the pinned shader compiler's fog-only SPIR-V path; relaxed accumulation produced invalid vector selects.

This is kernel evidence, not live fog acceptance. The renderer still calls the cloud-only overload. Next: append
fog uniforms with explicit shader ABI compatibility, propagate camera-relative data, handle fog-only dispatch and
history invalidation, and complete light/shadow coupling and cooked runtime data. Height-fog quadrature at steep
falloff, local soft-edge quality, indoor exclusion and native performance still need acceptance. Authoring controls
remain unavailable until those paths are connected. No new DevDist build or editor restart at this checkpoint.

### Lower-horizon appearance finding (October 28)

The user's daylight screenshot shows a black lower hemisphere outside the finite showcase floor.
`BuiltinSky.hlsl::AtmosphereRadiance` uses the atmosphere's synthetic planet intersection to occlude the sky,
and returns only integrated air radiance along ground-hit rays. There is no reflected ground-surface term;
near-surface downward rays therefore approach black. This is an implementation limitation, not evidence that
scene lighting or the floor mesh is broken. Add an explicit configurable lower-horizon/ground appearance with
daylight/night response and a sensible default. Preserve real terrain depth, planetary occlusion, and space views;
do not fill the lower hemisphere with unrelated clouds or remove the planet intersection as a cosmetic patch.
Validate near-ground cameras, below-sea-level geometry, orbit and planet-to-space transitions. This joins the
active environment/large-world work, before the deferred technical review.

### Fog renderer wiring checkpoint (October 28)

Volume ABI 8 appends 560 bytes of fog uniforms after the unchanged 448-byte ABI-7 prefix (1008 bytes total).
Legacy material uploads retain their original byte counts. Shader validation/metadata support ABI 8 and the graph
importer revision is incremented to rebuild generated materials. Fog-only requests now require sampled scene
depth and invoke the volume pass. Shared view tracing clips fog against each surface's own depth and covers sky
rays to the fog distance. CPU view preparation uses the current local render-camera frame; this does not yet prove
end-to-end double-precision world-camera propagation.

History invalidates on fog-uniform changes and when cloud advection differs from stationary fog motion. This is
conservative correctness, not final temporal performance: fog-only dispatch remains full-resolution and mixed
fog/cloud reconstruction needs native motion acceptance and profiling. All five built-in volume/trace/resolve/
composite/cache shaders compile for DXIL, SPIR-V and MSL. The fog-view GPU probe passes 384,000 samples per backend
on D3D12/Vulkan. Layout/history/density focused checks pass 11 cases / 85 assertions. C++ consumer compilation,
cooked runtime propagation, UI, native appearance and performance remain separate gates.

### Fog UI and cooked propagation checkpoint (October 28)

Runtime manifest schema 10 now carries fog from AssetTool into RuntimeApplication through the shared validated
manifest decoder. Older manifests default to disabled fog; older schemas containing fog and current schemas
missing fog are rejected. Runtime-manifest tests pass 10 cases / 101 assertions, including high-magnitude local
positions and malformed local extents. The Environment panel has a dedicated Fog tab with grouped height,
scattering/quality and local-box controls; Project Settings shares the same controls and document transactions.
These supersede earlier notes saying the controls or cooked propagation are absent. Native undo/save/reload,
viewport appearance, performance and standalone acceptance remain pending. The combined DevDist build is running.

Lower-horizon implementation direction: add authored ground reflectance and a lit ground boundary term,
composed with air radiance/transmittance to the planet intersection. Keep scene geometry visibility separate;
retain a black space background where rays miss the planet/atmosphere. The reference model distinguishes
sky radiance, point-to-camera aerial perspective and sun/sky surface irradiance, with an explicit ground-albedo
parameter: [Bruneton atmosphere API](https://ebruneton.github.io/precomputed_atmospheric_scattering/atmosphere/model.h.html).
Kéire currently has only the occluding boundary and atmospheric path contribution. This is a planned renderer
change, not a completed fix. Preserve energy/units, test daylight/night/terminator/orbit, and avoid per-pixel
nested sky integration that would undo the recent frame-time gains. Batch settings, codec, uniforms, UI and
focused numerical/GPU tests together after the current fog build finishes.

### Ground-boundary preparation and fog build completion (October 28)

The complete DevDist editor, AssetTool and runtime build passed, and the cached development stage completed in
22.1 seconds. Native fog appearance, authoring transactions and performance still require verification.
`AtmosphereGround.hlsli` now implements a Lambertian ground boundary composed with air transmission to the
intersection, rather than the sky path's zero transmission at opaque ground. Its focused GPU probe passes
384,000 samples each on Direct3D 12 and Vulkan; DXIL, SPIR-V and MSL compilation pass. Coverage includes
reflectance variation, surface-to-orbit distances, daylight, unilluminated night and absorbing air composition.
This helper is not yet connected to BuiltinSky: authored reflectance, persistence, uniforms, controls, diffuse
sky irradiance and native visual acceptance remain pending. The black lower-horizon issue is not fixed in the
staged editor yet. Do not treat these numerical checks as visual or performance acceptance.

### Reference-ground integration checkpoint (October 28)

The sky renderer now composes reflected reference-ground radiance at the planet intersection, preserving
atmospheric transmission to that boundary. Ground Reflectance is a linear RGB control (0–1, default 0.18)
in the shared atmosphere UI. It affects the procedural reference sphere only; it creates neither terrain nor
collisions and does not replace authored materials. Direct sunlight uses atmospheric attenuation and the local
surface normal. Diffuse illumination currently uses the authored environment ambient radiance; a spatial
sky-irradiance LUT and cloud shadows on this boundary are not implemented.

The nested atmosphere record is now version 2; legacy unversioned 17-field records migrate to the neutral ground
default, while malformed/new unsupported records are rejected. The shared atmosphere/cloud uniform ABI is
unchanged; sky-only uniforms append one float4 (544 bytes total). Eight atmosphere tests / 179 assertions pass,
and the integrated sky compiles for DXIL, SPIR-V and MSL. GPU ground-transport checks from the preceding checkpoint
pass. Native visuals, terminator/orbit appearance and performance remain unverified until the next DevDist build.

### Native acceptance sequence for ground and fog

Run the rebuilt, staged DevDist editor maximized with the backed-up CloudIsolation20261026 QA project.
Record viewport sizes, time, camera and render backend for comparisons; do not compare unlike viewport workloads.

1. With fog disabled, compare Scene and Game views at noon, dusk and midnight. Look below and across the horizon,
   then upward: reference ground should receive light without painting over meshes, stars or empty-space rays.
2. Change Ground Reflectance, verify both settings surfaces agree, then Undo/Redo and save/reload. Restore the
   neutral baseline. Check existing project migration without overwriting the saved baseline backup.
3. Enable fog, vary density and maximum distance, and verify nearby surfaces remain clearer than distant ones.
   Set global density to zero and add a local box; cross its edges and verify smooth transitions. Test Undo/Redo
   and save/reload, then restore the original settings. Check add/remove behavior and the 16-box limit.
4. Compare daytime/nighttime camera motion and time preview with fog off/on, keeping panel dimensions fixed.
   Record frame-time distribution after warm-up, not a single FPS reading. Check cloud/fog overlap for halos,
   stale history, boundary stepping and dark-gradient rings.
5. Keep numerical probes and native results separate. Terrain fog shadows/exclusions, ground cloud shadows,
   sky-irradiance LUTs, standalone acceptance and large-world travel remain open unless separately demonstrated.

### Startup performance priority (user request, October 28)

After the current ground/fog UI checks, investigate the long startup before continuing the performance pass.
The user expects near-instant startup. Separate warm unchanged-project startup, first import and importer-version
migration; do not use an empty editor's frame time as evidence of startup performance. The current native run
waits for a 93-asset automatic refresh before restoring the scene, including unrelated MaterialLab shader graphs.
Observed live worker progress advanced 6, 15, 26, 38 and 48 of 93 while the editor remained responsive.

Measure time to window, time to usable restored scene, shader compiler invocations, cache hits and background
completion. Audit whether scene dependencies can be prioritized and unrelated imports deferred, whether unchanged
shader outputs are invalidated unnecessarily, and whether safe compiler/cache reuse reduces migration work.
Preserve importer correctness, stale-output rejection and runtime parity. Validate both warm restart and forced
migration with the same project, then document measured improvements and remaining costs. Target near-instant
warm startup; a first-time compile must remain visible and cancellable rather than freezing or hiding progress.

Startup source audit: `EditorWorkspaceLayer.cpp` currently defers the startup scene when any source record has
an older importer version, then queues `ImportAll`. `AssetPipeline.cpp::ImportAssetsUnlocked` skips discovered
dependencies already present in the catalog, so replacing that request with only the scene ID would not guarantee
compatible upgraded shader dependencies. A scene-priority implementation must explicitly validate the transitive
scene dependency closure (including subasset owners and newly discovered references), preserve transactional
catalog publication, and defer unrelated refresh work. Test a stale shader already present in the catalog, missing
catalog fallback, changed scene references, cancellation/failure, and a warm unchanged restart before acceptance.

Existing regression fixtures to extend: `KeireTests/Source/Assets/AssetTests.cpp` covers background detection of
offline source changes, including unchanged timestamps; `AssetTargetedPublicationTests.cpp` covers newly referenced
missing dependencies and failed dependency publication. Neither establishes importer-version migration of dependencies
already in the catalog. Keep those guarantees and add that case before enabling scene-priority migration.

Measured migration baseline (October 28 QA operation `25dac2b2-fd42-4861-922f-49e256028ed9`):
93 assets, 20 imported and 73 cache hits; total import 199.265 seconds. Worker phase timings report 0.463 seconds
scanning and 197.070 seconds importing. The 20 slow preparation entries total 195.921 seconds. Largest entries:
shared VFX 18.525 seconds, WorldAlignedStone 17.643, BrushedAlloy 16.598, AutomotiveClearCoat 15.948,
shared Lit 14.752. Prioritize shader preparation/cache reuse and scene dependency scheduling over source scanning.
This is an importer-migration measurement, not warm startup latency. The subsequent default-lit warmup operation
`57d5df09-3437-4fee-bb62-740a7c75b745` reports 2.222 seconds setup and 0.281 seconds operation; it runs in the
background and must not be reported as time to usable scene.

Ground appearance native checks on the rebuilt DevDist: intensity 1 to 0.2 visibly dims the reference ground;
red 0.18 to 0.98 produces a matching red boundary in Scene and Game views without recoloring scene meshes.
Two Undo gestures restore color and brightness separately. Save Environment clears the unsaved notice, and
Rendering.keiresettings confirms intensity 1 and RGB 0.18 afterward. Custom-value reload and fog interaction
acceptance remain outstanding. The editor is left open, maximized, with desktop control released.

Startup batch implemented: current catalog dependency traversal now discovers outdated transitive importers; editor queues the startup scene first, then remaining imports after successful completion. KeepLastGood failed statuses block the handoff. Shader cache hits no longer stage temporary sources. Focused validation passed: shader cache 9 cases / 303 assertions, dependency migration and rollback 2 cases / 43 assertions, worker startup failure/context propagation 1 case / 52 assertions. DevDist build and staging passed (20.2 seconds staging). Native warm restart restored the rendered scene; no precise startup duration or forced-migration comparison has been established yet. Those measurements and missing-catalog/cancellation native acceptance remain open.

### Confirmed execution order and travel acceptance (October 28)

Finish the startup measurement and recovery checks, then finish fog before moving to large-world travel. User explicitly reaffirmed this order. Ground-to-space and space-to-ground acceptance must exercise continuous travel in the Editor and player, coordinate precision and origin/reference-frame transitions, streamed content continuity, atmosphere/cloud/fog transitions, and measured frame times. A high-altitude camera screenshot alone does not establish seamless travel.

Additional startup regression: KeepLastGood dependency migration failure is reported in statuses, retains importer version 1 and a valid catalog, and succeeds on retry without migrating the unrelated source. Focused suite now passes 2 cases / 53 assertions. Updated native warmup operation 02560e83-5773-4cf5-beb0-5f4171cad56f reports one cache hit, zero imports, 1.747 seconds worker setup and 0.158 seconds operation. This remains background-worker timing, not time to usable scene.

Native scene-priority migration acceptance: backed up one ShaderGraph metadata file and the source index, then changed its importer version from 49 to 48 consistently in both (including metadata digest). Launch at 2026-10-28T16:54:01.6986756Z queued startup-scene-refresh fd216e3d-4578-4b07-9e07-47ee0b8db6d2. The targeted closure imported the scene and stale shader only: two statuses, one import, one cache hit, setup 342.781 ms and operation 487.952 ms. Then remaining-startup-refresh db43c1a7-ffbc-4803-b9c0-098850d7e801 processed 93 cache hits in 5156.1 ms plus 1817.66 ms setup. Native capture shows the restored scene rendering; disk metadata is back at version 49. This validates queue order and migration recovery, not a like-for-like speedup against the 199-second cold migration baseline. Full time-to-usable-frame instrumentation remains a performance follow-up; proceed with fog acceptance next.

Fog native authoring checkpoint: on maximized QA editor, Enable Fog visibly attenuates geometry and sky; Ctrl+Z removes it and Ctrl+Y restores it. Add Fog Box changes count 0/16 to 1/16 and visibly adds local haze to the showcase. Undo removes the box and restores the previous appearance; another Undo restores fog disabled. Save Environment clears the unsaved status. Baseline left disabled with zero boxes at 09:00. These checks do not establish performance budgets or shadow/exclusion/standalone acceptance. Tool activation restored the maximized window and shifted the initial click to Profiler; manually maximized before tests, without changing editor code.

Fog height integration finding: a new GPU regression exposed sample-count-dependent opacity for exponential height fog on vertical rays (old D3D12 probe failed). The shared march now uses the analytic mean height density over each interval, splits the constant-density contribution analytically when crossing the base, and uses a small-span series for horizontal-ray stability. Local boxes keep their existing midpoint sampling and interval clipping. Initial updated probe passes 384,000 cases each on D3D12/Vulkan; additional base-crossing and nearly-horizontal cases are included in the final rerun. This is a numerical quality fix, not a measured speedup. Native DevDist rendering and combined lighting quality remain pending; batch further fog work before rebuilding.
Final expanded height-fog probe also passes 384,000 samples per backend, including base crossing and nearly horizontal rays; DXIL, SPIR-V and MSL compile successfully.

Confirmed local soft-edge defect: the shared march clips at outer box boundaries but samples each resulting interval only at its midpoint. A thin box with 20-percent soft edges at one nominal step is treated as full core density. Added an analytic central-axis regression: two half-metre boxes at extinction 0.2 each should have optical depth 0.18, not 0.2. D3D12 probe fails all 384,000 cases (Build/Validation/fog-soft-edge-before.log). This regression is intentionally failing until local-volume integration is corrected. Retain bounded cost and test oblique rays, overlapping boxes, zero softness and full softness before native acceptance; do not mask this by raising default quality.

Local soft-edge integration corrected: each box's normalized distance is the maximum of six affine face distances along the ray. The shader clips each face's dominance interval and integrates its clamped cubic soft ramp, with fast rejection for outside segments and a solid-core fast path. Expanded GPU transport tests pass 384,000 cases per backend on D3D12/Vulkan: thin overlapping boxes, oblique rays against 128-sample reference, reversed rays and split-segment invariance, plus height-fog regressions. DXIL/SPIR-V/MSL compile. Boundary cost is bounded but greater than midpoint sampling; measure native frame times before acceptance. The next DevDist batch includes both height and local fog fixes.

Fog coordinate packing regression passes at the supported local-frame limit: all 16 volumes retain 0.125-metre camera-relative offsets near plus/minus 1e15 metres, and disabling fog clears the uniform record. Focused density/record suite passes 6 cases / 73 assertions. This verifies packing, not planetary motion or seamless travel.

Height/local fog integration batch built successfully (18 native actions) and staged in 19.1 seconds. Updated editor launches with the saved QA baseline. First startup after new shader binaries took time before exposing its window; this is not proof of near-instant startup. Sustained fog performance and visual acceptance are still pending.

Native stationary fog comparison (October 28): same maximized editor, camera and viewport, 09:00, clouds enabled, no local boxes, 10-second application-interval captures after two-second warmup. Fog disabled: 887 intervals, mean 11.28 ms, P95 12.64 ms, P99 13.36 ms, maximum 14.59 ms. Fog enabled: 749 intervals, mean 13.35 ms, P95 14.60 ms, P99 15.41 ms, maximum 15.83 ms. Approximately 2.07 ms incremental application-frame cost in this scene; not isolated GPU timing or a movement/player benchmark. CSV evidence: Build/Validation/fog-disabled-sustained.csv and fog-enabled-sustained.csv. Native toggle visibly changes haze and restores baseline; saved fog disabled afterward. Local-volume boundary performance, shadow/exclusion support and planetary height behavior remain open before travel acceptance.

Empty-fog regression: enabled zero-density fog with no visible local boxes changed cloud opacity by overriding cloud sampling phase and raising the march to the fog step count. New GPU case fails on the old D3D12 path. The combined march now retains the cloud-only path when the fog uniform has no density contributors. Updated transport suite passes D3D12 and Vulkan, including the phase/quality regression. Native rebuild is deferred into the next fog batch; no measured speedup claimed. Planetary audit confirms both CloudVolume slab clipping/density and FogDensity height integration are world-Y based. Ground-space-return completion requires radial layer geometry, numerically stable altitude relative to the same planet/reference frame, and ray interval handling across shell entry/exit, followed by continuous travel validation; camera altitude alone is insufficient.

Local fog authoring batch: added per-box Enabled state and Enable Box checkbox through shared Environment/Project Settings controls. Disabled records retain values and authored-limit accounting but are skipped by CPU density and GPU packing. The shared settings/runtime record decoder migrates legacy four-field boxes to enabled, emits explicit boolean state and rejects malformed flags. Focused tests pass 7 cases / 83 assertions, including disabled validation and legacy migration. Documentation validation passes 117 guides, 10 navigation groups, 29 Mermaid diagrams and schema/link contracts. DevDist build is running; native toggle Undo/Redo, reload and player acceptance remain pending. This is not exclusion-volume support: exclusion volumes must remove ambient/global fog in an authored region.

Per-box runtime persistence acceptance expanded: rendering settings file roundtrip and cooked EnvironmentRuntimeManifest roundtrip now include both disabled and enabled boxes with precise positions. Combined focused suites pass 20 cases / 233 assertions. Test harness was corrected to link one doctest implementation after an initial duplicate-main linker failure; engine sources did not need a workaround. Native UI acceptance still awaits DevDist build completion.

### Fog completion gates before travel acceptance

The per-box enable toggle is an authoring comparison tool, not an exclusion region. Finish the following paths
without conflating their evidence:

| Gate | Implementation requirement | Acceptance evidence |
| --- | --- | --- |
| Local-box authoring | Shared controls, individual enable state, history, settings and cooked runtime propagation | Native enable/disable, Undo/Redo, save/reopen; mixed enabled/disabled boxes in player |
| Exclusion regions | Bounded authored regions suppress height and additive local fog with a defined overlap rule and soft edges; preserve clouds | CPU/GPU agreement at thin boundaries, overlapping exclusions, disabled regions, camera-inside cases and transparent surfaces |
| Terrain shadows | Volume sunlight samples actual directional shadow visibility; ambient remains independent; use camera-relative positions and valid cascade bounds | Wall/roof occlusion, cascade transitions, off-screen occluders, origin changes and cost measurements in editor/player |
| Planetary layers | Fog altitude and cloud shell intersections use the selected planet/reference frame, not world Y; account for shell entry/exit on ascent and return | Surface, horizon, cloud interior, orbit and opposite-side approach without terrestrial fog in vacuum |
| Performance | Measure height-only, one box and maximum boxes, soft boundaries and moving camera with fixed viewport | Sustained frame captures and renderer timings; compare against the 10 ms target, record misses honestly |
| Continuous travel | Keep input, streaming, precision and local physics coherent through surface/orbit/reference-frame transitions | Complete ground-space-ground route in editor and standalone player with no loading screen, bounded memory and no visible jumps |

Code audit: material shading already has directional shadow matrices and texture sampling in ShaderGraphHlsl.cpp;
the shared cloud/fog view pass does not bind that visibility. Do not fake terrain occlusion by darkening fog or
sampling only the final surface's shadow. Bind a shared visibility implementation at the volume sample positions,
with explicit fallback outside valid shadow coverage. The world-Y cloud slab in CloudVolume.hlsli and height
profile in FogDensity.hlsli are both planetary blockers, rather than a fog-only parameter adjustment.

Fog projection acceptance expanded: FogViewProbe now checks off-axis perspective with a valid perspective inverse matrix, parallel orthographic rays away from screen center, and exposure scaling of radiance without changing transmission. Corrected fixture passes 384,000 samples each on Direct3D 12 and Vulkan; DXIL/SPIR-V/MSL compile. The first expanded run used the old affine center-ray fixture incorrectly as a perspective projection; that fixture error was corrected without changing renderer behavior. This does not establish native visual or planetary geometry acceptance.

Fog authoring DevDist build completed successfully (254 native actions), including client, runtime and asset tool outputs. Native staging/toggle/history/reload acceptance is pending: the running QA camera changed during compilation, so desktop control was released and the user was asked whether the editor can be restarted.

Exclusion implementation started while QA restart answer is pending: internal FogExclusion.h defines an axis-aligned region and CPU retention reference. Strongest weighted exclusion wins, making overlap order-independent and duplicate-invariant; strength and cubic soft edges multiply fog density only, never clouds. Disabled regions retain validation. Focused reference tests cover core, edges, partial strength, overlap/reordering/duplication, distant centers and invalid inputs. This is not yet connected to FogSettings, persistence, GPU uniforms/integration or authoring UI. Next wire those paths and test interval boundaries so thin exclusions cannot disappear between coarse samples. Do not advertise exclusion support until that integration and native/player acceptance pass.

Exclusion settings/CPU integration checkpoint: FogExclusionRegion is now an authored type with a separate maximum of 16 regions. FogSettings carries ExclusionRegions; the shared record encoder/decoder preserves bounds, softness, strength and enabled state, and legacy nine-field fog records migrate with no exclusions. CPU extinction applies the strongest exclusion after summing global and local fog. Focused suite passes 4 cases / 26 assertions (including persistence, disabled state, legacy migration, malformed strength and count limit). GPU packing, segment integration, shader ABI migration, controls and native/player acceptance remain unimplemented for exclusions; do not expose or claim the feature until those paths are complete. The previously built DevDist predates this settings addition.

GPU exclusion point kernel implemented in FogExclusion.hlsli and validated by FogExclusionProbe through the existing offscreen harness. The GPU test sweeps positions and strength, checks empty regions, duplicate invariance, order independence and hard boundaries. Both D3D12 and Vulkan pass 384,000 samples; DXIL/SPIR-V/MSL compile. This validates only point retention. The kernel is not yet called by the live volume renderer. Required next integration: append exclusion records to the volume uniform ABI with legacy prefix compatibility, origin-relative host packing, include exclusion boundaries in ray intervals, integrate retention against fog density over segments (not midpoint-only), regenerate material shaders, and add controls. Keep cloud extinction independent of exclusion retention.

Exclusion ray/UI integration implemented, pending broader validation: the fog march splits outer/core exclusion box intervals, then uses bounded eight-point Gauss-Legendre weighting against the exact unexcluded density average on affected segments. Unaffected segments retain the existing analytic path. GPU transport tests pass 384,000 cases each on D3D12/Vulkan, including a half-metre hard/soft exclusion at a single nominal 100-metre step, duplicate regions and partial strength. Shared controls now expose bounded add/remove, enable, center, extents, strength and softness. Remaining gates include oblique/overlapping varied-density accuracy, explicit cloud-preservation tests, ABI/codec/layout tests, runtime persistence with exclusions, build, native UI/history/reload, performance and player acceptance. These changes are not staged into the running editor.

Exclusion compatibility validation: volume layout/metadata suites pass 4 cases / 59 assertions, including ABI 9 and legacy prefixes. GPU transport now explicitly verifies a full fog exclusion preserves cloud opacity and cloud radiance; 384,000 cases pass per D3D12/Vulkan backend. Initial comparison incorrectly used the opacity-only cloud reference for RGB; corrected to the radiance-enabled reference before passing. Oblique, varying-density and native/player validation remain open.

Low-quality exclusion regression fixed: the 1–32 step angled-ray sweep initially failed on D3D12 (0.009375 failed fraction) against a 512-sample reference at a 0.0002 opacity tolerance. Four subdivisions of the eight-node integration resolve changing dominant box faces and strongest overlapping regions; both backends now pass without loosening tolerance. Added constant-core fast paths: fully clearing cores return zero immediately; a core stronger than every intersecting region scales the analytic unexcluded integral directly. Final fast-path transport probe also passes 384,000 cases per backend. Performance is not established by these numerical tests. Full DevDist build started; no live editor replacement yet.

Shader cache integration checks: Windows generated-content cache harness passes with FogExclusion.hlsli required in both shader and VFX dependency lists. Unix harness updated equivalently, but a Git Bash run cannot complete its atomic symlink-lock publication fixture: traced ln -s of the synthetic keire-owner target returns failure. Dependency assertions execute before that fixture. This is not a native Linux/macOS validation pass; rerun there before claiming cross-platform cache acceptance.

Terrain-shadow integration map (source audit, not implemented): RenderSurfaceRecording.cpp records ShadowFrameData before the volume pass, but RecordCloudVolume currently receives only the scene packet. Its direct/MSAA pass binds depth, cloud noise and background; RecordCloudTemporal trace similarly has no directional shadow binding. Thread the existing shadow frame through both paths, bind directional cascade resources with matching shader metadata, and evaluate visibility at fog integration sample positions in CloudVolume.hlsli. Preserve ambient illumination and extinction when sunlight is occluded. Surface ShaderGraphHlsl.cpp already provides cascade selection and PCF behavior to reuse consistently. Acceptance must cover fog-only, cloud-plus-fog temporal rendering, MSAA, outside-cascade fallback and moving shadow casters; avoid introducing stale shadow visibility into the cloud light cache. No terrain-shadow behavior is validated by the current exclusion GPU tests.

Exclusion capacity regression: GPU probe now exercises the sixteenth slot with all earlier slots inactive, plus defensive count clamping beyond uniform capacity. Direct3D 12 and Vulkan each pass 384,000 samples (Build/Validation/fog-exclusion-capacity.log). This extends point-kernel coverage; it does not measure maximum-region frame cost or replace native authoring acceptance.

Fog temporal-history regression: every one of the 16 exclusion records is checked for strength and softness changes, plus removal through the active count. All invalidate combined cloud/fog accumulation; unchanged data retains history. Focused CloudTemporalReprojectionTests pass 6 cases / 63 assertions (Build/Validation/fog-temporal.log). This is CPU history-policy coverage, not native visual acceptance. Frame-graph audit also found the volume pass lacks an explicit shadow-resource read; add that dependency alongside the forthcoming sampler integration, not just a texture binding.

Exclusion DevDist build completed successfully (366 native actions; editor, runtime, asset worker and tool linked). Build log: Build/Validation/fog-exclusion-devdist.log. Running QA editor still predates this build; staging/restart and native acceptance remain pending the existing question about whether that window is in use. Do not treat compilation or GPU probes as visual/performance acceptance.

Terrain-shadow prerequisite implemented: CloudVolume now explicitly reads the directional-shadow frame-graph resource. Focused HDR graph tests pass 2 cases / 127 assertions, checking shadow-before-volume ordering, the declared read and resource lifetime in both Forward+ and Deferred Hybrid. Evidence: Build/Validation/fog-shadow-graph.log. This change is newer than the completed exclusion DevDist build. It establishes dependency/lifetime only; no GPU shadow sampler or fog lighting change is connected yet. Next update direct/MSAA and temporal trace resource layouts together (their storage-buffer registers currently immediately follow sampler registers), then validate shadow visibility at volume samples and history behavior with moving casters.

Volume shadow packing started: new VolumeShadowUniforms.h defines a 304-byte record using existing cascade parameters/matrices plus view-depth direction. It folds camera translation into cascade matrices with double intermediates so fog samples remain camera-relative. Focused test passes 1 case / 22 assertions for all four cascades at a 100-million-metre camera translation and preserves source data. This helper is not yet called by rendering and does not establish arbitrary rotation/projection, GPU ABI, origin-rebase or planetary precision acceptance. Connect and extend coverage before claiming terrain-shadow support.

Fog terrain-shadow implementation checkpoint: direct/MSAA and temporal trace paths now receive ShadowFrameData, pack camera-relative cascade matrices and bind directional shadow texture plus a 304-byte uniform. New FogTerrainShadow.hlsli selects cascades and applies hard/weighted 3x3 visibility with lit fallback outside coverage. CloudVolume applies visibility only to direct fog sunlight; ambient and extinction stay independent. Both builtin fragment shaders compile to DXIL, SPIR-V and MSL (Build/Validation/fog-shadow-shaders.log). No native C++ build or rendered GPU shadow acceptance yet. Shadowed fog conservatively rejects temporal accumulation because caster-content revision tracking is not connected; performance and noise need measurement and a better history policy before production acceptance. Transparent material/VFX fog paths still use unshadowed fallback and must be integrated for parity. Add real depth-array GPU occlusion tests, full native build and editor/player wall/roof/cascade/moving-caster checks. New include tracked in both builtin shader cache dependency lists.

Fog-only particle correction: ComposeGraphVolumeSurface previously returned early for opaque draws when clouds and atmosphere were off, even with fog enabled. The early return now also requires disabled fog. Shader Graph importer advanced to 51 to invalidate cached generated materials; docs match and website documentation validator passes 117 guides / 10 groups / 29 diagrams. Shadow-batch DevDist build started (session 65139; Build/Validation/fog-shadow-devdist.log). Source compilation and native acceptance remain pending. This correction is separate from the still-unfinished transparent material/VFX terrain-shadow parity.

Shadow packing coverage expanded: 2 cases / 42 assertions now compare all clip-coordinate rows for all four nontrivial matrices and rotated view-depth extraction. Windows generated-content cache harness passes with FogTerrainShadow.hlsli required in builtin shader dependencies; Unix source assertion updated, but native Unix execution remains unverified (earlier Git Bash symlink fixture failure still applies). Shadow build session 65139 remains live during generated-content preparation.

Terrain-shadow batch DevDist build completed successfully (52 native actions; editor/runtime/worker/tool linked). Evidence: Build/Validation/fog-shadow-devdist.log, session 65139 exit 0. This proves native integration compiles, not rendered shadow correctness. No stage/restart performed; current QA window remains old build pending the existing use/restart question. Remaining before fog acceptance: actual shadow-depth GPU test, transparent/VFX parity, history cost/noise improvement, native wall/roof/cascade/moving-caster and maximum-region performance tests, standalone parity. Planetary layer geometry and continuous travel remain subsequent work.

Shadow history refinement after successful build: FogReceivesTerrainSunlight now limits conservative history rejection to enabled nonempty fog with positive solar radiance, active cascades and positive shadow strength. Empty/night/zero-strength cases can retain normal history. Shader also exits before sampling for zero strength. Focused packing/policy suite passes 3 cases / 49 assertions; updated temporal fragment compiles DXIL/SPIR-V/MSL (fog-shadow-zero-strength.log). No frame-time improvement measured. These edits postdate the last successful DevDist build and must be included in the next batch. Actual shadowed fog still lacks caster-revision-based history reuse.

Cascade-depth convention defect found during surface/volume audit: generated surface shaders use viewPosition.z directly, but the new volume packing helper negated the view Z row. Corrected the helper to positive view-space Z and added comparison against the surface matrix expression. Initial translation test had encoded the wrong convention and was corrected; it was not proof of cascade correctness. This fix postdates the shadow DevDist build, which must not be used for final acceptance. Pending native rebuild and GPU cascade/occluder tests remain required.

Actual shadow-map probe preparation: Scripts/Tests/FogTerrainShadowProbe.hlsl uses the production fog shadow sampler with four depth-array layers (intended clear depths 0.15/0.35/0.55/0.75). Cases straddle each layer depth and exercise outside-UV/near/far fallback, writing actual and expected visibility separately. Vertex and fragment compile to DXIL, SPIR-V and MSL (Build/Validation/fog-shadow-probe-compile.log). This is not yet a GPU execution pass: add the host depth-array creation/clear, uniforms, hard/soft modes and readback comparison before recording acceptance.

Actual depth-texture shadow GPU acceptance: FogTerrainShadowGpuProbe.cpp creates four D32 array layers, clears each to distinct depths, binds production packed uniforms and compares rendered visibility against reference values. Direct3D 12 and Vulkan each pass 65,536 pixels in hard mode and 65,536 in soft mode, zero failures (fog-shadow-gpu-d3d12.log / fog-shadow-gpu-vulkan.log). Covers cascade layer selection, lit/occluded samples, strength 0.75 and outside-UV/depth fallback. Does not cover spatially varying PCF edges, moving casters, transparent parity or native composition. User answered the pending restart question: restart when ready is authorized. Updated DevDist build session 89262 includes positive view-depth fix and history optimization; log fog-shadow-cascade-devdist.log. After success, close QA, stage and reopen maximized for native fog validation.

Native updated-editor checkpoint: PID18936 opened maximized at 3440x1392 with CloudHdrShowcase, paused09:00. Fog tab displays local/exclusion sections; exclusion count0/16 and Add control visible. EnableFog immediately added visible scattering, Ctrl+Z restored the clear view with unsaved-history notice, Ctrl+Y restored fog and saved-state indication. Fog remains enabled for subsequent region tests. No exclusion added yet. Single status-bar samples near10ms are not sustained performance evidence. Desktop helper released, editor left open. Region create/toggle/edit/history/reload, terrain occlusion and player tests remain pending.

Fog shadow boundary validation: the production GPU probe now samples half a texel inside both horizontal map boundaries and two corners. Independent expected soft visibility uses the outside-map weights 1/4 and 7/16; hard mode remains fully occluded. Updated fragment compiled to DXIL/SPIR-V/MSL. D3D12 and Vulkan each passed 65,536 pixels in hard and soft mode, zero failures (fog-shadow-edge-d3d12.log and fog-shadow-edge-vulkan.log). This verifies map-border filtering, not a spatially varying interior occluder or moving-caster history. Native exclusion add/disable/undo/save also passed; one enabled default region remains in the QA project. Transparent material/VFX shadow parity and planetary travel remain incomplete.

### Planetary travel work started (October 28)
User approved moving on from fog to planetary travel integration; remaining fog parity/performance gates stay open. Current source confirms TransformComponent still stores/returns float Vector3 positions. SectorPosition, FrameSnapshot and FrameRegistry are internal foundations, not an integrated editor flight feature. New PlanetaryTravelTests exercises Earth radius, ascent through 100 km atmosphere to 10,000 km altitude and return, planetary rotation, orbital velocity, distant positive/negative sector origins, sub-metre surface offsets, motion round trips and atmosphere entry/exit. Focused test passes 1 case / 128 assertions (Build/Validation/planetary-travel.log). This is a numerical baseline only, not continuous gameplay, streaming, physics or editor/player acceptance. Next required integration is authoritative high-precision scene positions and frame ownership, with persistence/managed/renderer boundaries migrated together; do not present a float camera fly-through as completion.

Distant-approach CPU reference defect reproduced and corrected: IntersectAtmosphere subtracted astronomical squared distances; approach rays at 1e12/1e15 metres produced entry errors and false limb hits (planetary-approach-before.log, six failed assertions). Closest-approach geometry and planet-scaled tangent tolerance pass 143 planetary assertions; existing atmosphere suite also passes. This helper is a CPU reference used by tests, not the live sky shader; GPU distant-observer parity remains required. No editor travel claim.

Live shader approach correction: AtmosphereMedium.hlsli now uses closest-approach discriminants and planet-scaled tolerance. Distant roots use direct addition/subtraction while near-surface roots retain the stable factored form. New AtmosphereApproachProbe initially failed 96.8% of samples; first correction left 0.2083% root-rounding failures; final correction passes without relaxing tolerance. Approach, existing atmosphere, and ground suites each pass 384,000 samples on D3D12 and Vulkan; probe shaders and BuiltinSky fragment compile DXIL/SPIR-V/MSL. Cases span axial approaches up to roughly 10 million km and limb misses; arbitrary grazing directions and camera-relative far-observer transport remain unqualified. Source shader changed, native editor rebuild/staging pending. No claim of completed travel integration.

### Requested pause checkpoint
User requested finishing this pass, committing/pushing all source work, final UI testing, then pausing. DevDist build passed 19 actions (planetary-approach-devdist.log); stage-editor passed in 20.5 seconds. Docs validation passed 117 guides/10 groups/29 diagrams; all changed C++ formatting and diff checks passed. Windows generated-content cache harness reports passed (wrapper initially misread a residual native exit code). Updated editor PID9812 opened the saved scene and rendered fog/materials; it subsequently disappeared during window-size testing, and capture reported no screenshot targets found. Process absence verified; cause not established. Final UI smoke is incomplete, not passed. Latest Client.log also reports startup scene dependencies could not be refreshed despite worker completion; investigate on resume before acceptance. Desktop helper released. Remaining work includes this startup/UI failure, full precision transform/persistence/managed integration, planetary cloud/fog geometry, streaming/physics, continuous editor/player ground-space-return and pending fog parity/performance. Generated vc140.pdb excluded from commit and left local.

Resume constraint: code-based work while user games; ask and wait before opening the editor. Startup failure root cause confirmed in operation 5a371369 worker.log: generated material HLSL references FogExclusionDensity before its declaration. Both Windows/Unix embedded-source generators now order FogExclusion before FogDensity; flattened source compiles DXIL/SPIR-V/MSL without source includes. Windows cache harness passes with order assertion; Unix assertion added, native Unix execution pending. Native rebuild/reimport and UI remain pending. This unblock precedes continued authoritative scene-position migration for planetary travel.

Native migration value contract started: Keire/Math/Vector3d.h introduces explicit double frame-local coordinates, with no implicit conversions to/from Vector3. RelativeFloat subtracts before narrowing and rejects nonfinite, overflow and caller-defined precision-budget failures. World/frame identity remains separate; callers must resolve frames first. Exported through Core.h. Transform storage, scene schema/property bags and managed ABI are NOT migrated yet; this value type alone does not provide default large-world behavior. Next batch must carry the type through those authoritative paths before claiming scene integration.

Precise property migration in progress: ComponentPropertyKind appends Vector3d and ComponentPropertyValue appends alternative15, keeping reference alternative14 and all earlier persisted tags stable. Scene component property and prefab codecs now carry double channels; precise decode rejects nonfinite values. PrecisePropertyTests adds round-trip coverage at 1e15 metres and malformed-shape rejection. Compilation/test validation is not yet complete; transform storage, property editor, managed payload and ABI migration remain open. Do not run the editor against this partial migration.

Precision codec compile checkpoint: SceneSerialization.cpp, SceneAsset.cpp, PrefabAsset.cpp and PrecisePropertyTests.cpp compile with C++20/KEIRE_STATIC. Initial standalone command omitted KEIRE_STATIC and hit an import-linkage error; corrected command passed. Round-trip test execution and remaining variant consumers are still pending, so this is compile evidence only. No editor launch or new Git mutation.

Precise-property executable validation now passes 1 case / 3 assertions: component JSON round trip preserves 1e15-plus-fraction values, malformed vectors are rejected, and nonfinite encode is rejected. All three scene/prefab encoders now reject nonfinite precise vectors rather than writing JSON null. Evidence Build/Validation/precise-properties.log. Linked freshly compiled precision test/SceneSerialization objects with existing core/assets libraries for unchanged supporting symbols; this is not a full ABI/build validation. ManagedBuiltinComponents still only maps float Vector3 and must gain a coordinated native/managed payload before transforms switch. Editor drawer support remains pending. No editor opened.

Editor precision property batch: IPropertyEditor gains a double-vector fallback using three existing double scalar controls; edits publish together only if finite, and PropertyDrawerRegistry registers the new kind. Architecture tests include precise kind coverage and 1e15-plus-fraction edits. Tests added but not yet executed; production drawer compilation in progress. Actual Transform position remains float, and managed NativeBuiltinProperty still uses its old 56-byte payload. No editor launch; ask and wait before UI testing.

Drawer compile completed successfully (C++20, KEIRE_STATIC, precision-drawer.obj). Runtime/editor test execution remains pending; this is compile coverage only.

### Active planetary travel integration order (2026-10-28)

Large-world and planetary travel is the active milestone. Do not postpone it for optional atmosphere polish.
Keep remaining fog parity and performance gates tracked, and defer the extensive technical-review backlog until
fog and planetary work are complete. Ask the user and wait for permission before opening the editor.

1. Complete precise transform storage, parenting, interpolation, scene migration and managed interop as one
   coherent batch. Do not silently enlarge the existing 56-byte managed property payload: establish explicit
   compatibility checks or a separate versioned entry point first. Preserve old scene data deliberately.
2. Connect authoritative positions to reference frames and local rendering/physics origins. Verify rebasing
   preserves velocity, parent relationships, contacts and camera-relative precision without scene reloads.
3. Connect planetary gravity and rotating-frame transitions with continuous atmosphere/cloud/fog altitude
   behavior. Current world-Y fog/cloud slabs are not a complete planetary atmosphere implementation.
4. Prepare a reproducible surface-to-orbit-to-space-and-return route, including lateral travel, landing,
   save/reload and distant origins. CPU kernel tests alone do not establish editor or standalone travel support.
5. Batch DevDist compilation and focused regressions, then request permission for maximized editor and player
   acceptance. Measure frame times, transition spikes and streaming; retain honest unresolved results.

Managed precision contract added: Vector3d mirrors native same-frame origin subtraction and rejects nonfinite,
overflow and excessive narrowing error. It deliberately has no implicit float conversion. This is a value API,
not managed Transform migration or a new wire payload. Production managed regression run is pending.

Managed production regression execution passed (Release, exit 0), including PrecisePositionTests; evidence: Build/Validation/planetary-managed-precision.log. No editor or GPU test was launched. Transform migration and native/managed interop remain pending.

Native transform storage batch (not yet live-editor validated): authoritative local positions and fixed-step
samples now use Vector3d. Precise world access walks parent linear transforms without putting translations in
float matrices; precise world edits reverse that route before publishing a finite local position. Preserved
reparenting validates the precise translation and decomposed rotation/scale before changing hierarchy. Transform
component schema 2 writes doubles and migrates schema-1 positions. Added detached codec/migration, hierarchy,
reparent and scene round-trip tests. TransformComponent.cpp and the test file compile; the DevDist KeireTests
build is running with KEIRE_BUILD_JOBS=2. Do not link this changed class layout against old scene libraries.
Remaining migration includes SceneTransform summaries/editor consumers, managed precise calls, presentation
matrices, renderer-relative origins, physics regions and live frame transitions. No editor opened.

Managed state precision integration: Vector3d is now treated as an immutable atomic value by ordinary state,
reference-graph and callback staging serializers. Regression coverage round-trips a 1e15-plus-fraction position,
an array and a SerializeReference array, excluding computed IsFinite from stored JSON. All 50 managed production
cases pass (Release, exit 0; Build/Validation/planetary-managed-state.log). The first fixture run emitted rename
fallback warnings because its fields lacked stable IDs; adding stable IDs corrected the fixture without weakening
precision assertions. Native Behaviour reflection now maps Vector3d and reads/writes finite double JSON channels;
added projection/edit regression coverage, pending the ongoing native build. ScriptableObject metadata kinds and
managed Transform access remain separate unfinished paths. Because source edits extended this batch after the
native build began, perform a final incremental freshness build before accepting its test results.

Precise managed Transform bridge: LocalPositionPrecise and PositionPrecise use separate V1 getter/setter
function-pointer fields, a 24-byte XYZ double payload and a checked byte count. Native calls reject null buffers,
unknown spaces, stale entities and nonfinite positions, and do not unwind through the ABI. C# rejects missing
runtime support, failed operations and nonfinite native output explicitly. The existing generic 56-byte property
payload is unchanged. Managed production tests pass all 50 cases, including raw double-channel layout, identity,
local/world selection, invalid input, rejection without mutation and absent callbacks
(Build/Validation/planetary-managed-transform.log). These use managed unmanaged-call fixtures, not the actual
native host. The native reload integration fixture now exercises both precise properties and verifies reflected
state at 1e15-plus-fraction coordinates; it is added but not yet executed. Native build remains active.

Precision consumer audit: Transform.Translate previously round-tripped absolute coordinates through float Position,
so small movement at distant origins disappeared. It now reads/writes PositionPrecise while retaining float local
movement vectors and world/local rotation behavior. All 50 managed tests pass, including quarter-metre translation
at 1e15 metres, local-axis rotation and rejection before native mutation
(Build/Validation/planetary-managed-translate.log). This does not migrate scripts that directly modify the legacy
float Position property. Translation-gizmo capture now stores precise local/world positions, translates via the
precise world setter and restores precise locals when a drag is cancelled. Added a scaled-parent editor regression;
execution remains pending. Rotation/scale pivot matrices and special numeric Transform Inspector controls are still
float paths and must be migrated before native UI acceptance. Precise native hierarchy tests now also cover an
exact half-turn parent rotation and overflow rejection. No editor opened; master retained, no Git mutation.

AuthoringGeometry.cpp compile passed (C++20, KEIRE_STATIC; Build/Validation/precise-authoring-compile.log). Editor regression execution and UI acceptance remain pending.

Inspector undo/multi-edit migration: undo values append Vector3d, position validation rejects nonfinite precise
values, and application uses SetLocalPositionPrecise. SceneDocument TransformValues.Position is now an optional
Vector3d; single and multi-edit setters preserve it. Updated callers explicitly distinguish position from scale.
Added distant undo/redo and invalid multi-selection rollback tests. The special numeric widget remains float-based
and explicitly widens its current value; it is not yet a precise authoring UI. SceneDocument.cpp and
InspectorTransformTests.cpp compiled before the multi-edit extension; the latest multi-edit compile is pending.
Native regression build remains active; no editor/GPU test or Git mutation.

The multi-edit compile exposed two float placement adapters (imported models and dropped VFX); both now explicitly widen their existing placement vectors into SceneDocument's precise position contract. SceneDocument.cpp and SceneDocumentMultiEditTests.cpp compile successfully after that correction (precise-multiedit-compile.log). Runtime/editor tests remain pending.

Post-migration audit gate: the legacy managed local/world/presentation getters are noexcept native callbacks but
can now reach checked float narrowing or matrix composition from precise positions. A finite double outside float
range can throw there. Before release, convert this into an explicit managed error without unwinding the ABI;
cover both legacy and precise getters. Do not advertise the new precise accessors as complete migration of the
legacy float surface. Current build has compiled native reflection and precise callbacks; test execution is still
pending, and the final freshness build remains required after source changes during compilation.

Legacy getter boundary fix implemented: native local/world/presentation callbacks catch conversion/composition
exceptions and return a nonfinite sentinel through their unchanged float ABI. Managed wrappers reject nonfinite
channels with an explicit InvalidOperationException; finite legacy values retain their existing behavior. All 50
managed production tests pass, covering each nonfinite channel and each getter
(Build/Validation/planetary-legacy-position.log). The actual native reload fixture now sets a finite 1e40 position,
resets presentation interpolation, expects all three legacy getters to fail safely, verifies the precise getter
still returns 1e40, and restores the original transform in finally. This native test is added but not executed;
a fresh native incremental build is required because callbacks changed after their first compilation.

Precise numeric authoring: Transform Inspector now reads LocalPositionPrecise and uses UiFrame::DragVector3d,
with 17-digit double input formatting and the existing grouped axis layout. Position edits reach precise undo and
multi-edit values directly, without widening an already-rounded float. The widget rejects nonfinite initial values
and publishes only finite candidates. Float vector controls share the same layout helper. Added idle preservation
and invalid-input contract checks to UiTests; test execution and interactive typing/drag/Undo acceptance are pending.
The Ui.h change requires a fresh native/editor build after the currently running baseline build completes.
Precise UiWidgets.cpp standalone C++20 compilation passed (Build/Validation/precise-ui-widget-compile.log). Formatting dry-run and git diff --check passed; full runtime/UI validation remains pending.
The two production reflected-property UI adapters now override EditVector3d with the grouped precise widget rather
than inheriting the six-digit scalar fallback. This preserves 17-digit input formatting and grouped edit tracking
for native and managed reflected Vector3d properties. Nonfinite widget input rejection also has a regression check.
Formatting and diff checks pass; the client build and input/undo UI checks remain pending.
InspectorPropertyEditor.cpp standalone C++20 compilation passed (precise-inspector-property-compile.log).
The original 350-action native build reached linking but failed with the new DragVector3d symbol unresolved:
UiTests was compiled after the UI edit while KeireCore had already compiled before it. This is not a passing build.
A fresh normal-launcher DevDist KeireTests build is now running (planetary-precision-fresh-devdist.log); source edits
are frozen until it completes. Native regression results and editor acceptance remain pending.

Post-storage consumer audit (October 28): SceneState::SnapshotObject still obtains its compatibility summary via
LocalPosition(), even though its component bag stores doubles. SceneAsset::ParseTransformData also parses that
summary through ParseVector3, and Validate checks it separately. Thus a finite double position outside float range
can still fail snapshot/decode; precise component round trips at 1e15 do not prove the full double-range contract.
PrefabAuthoring::InstantiatePrefab computes placement offsets from this float summary, and prefab override review
uses its positions. Next migration must change the summary and its codecs/placement/override consumers together,
with distant prefab placement and save/reload tests. Do not remove the summary validation to hide narrowing.
The in-flight build remains untouched; this is a confirmed follow-up boundary, not an implemented fix.

Fresh DevDist KeireTests build passed (139 actions, planetary-precision-fresh-devdist.log). Focused native suite
passed 10 cases / 821 assertions (planetary-precision-native-tests.log): precise storage/codecs/hierarchy,
reparent failure safety, planetary kernels and headless UI contracts. The actual managed reload host fixture ran
1,314 assertions with four failures in its intentionally unbound replay-only entity lifecycle; the live scene
precise-coordinate assertions passed. The new Awake probe tried to access a Transform on that unbound entity.
Restricted the fixture's probe to Entity.IsValid (the live-scene assertions remain mandatory); a four-job incremental
build is running before rerunning the complete fixture. This is not yet a passing host-integration result.
Managed host rerun passed: 1 case / 1,314 assertions (planetary-precision-native-host.log). This includes live native precise callbacks, legacy overflow rejection, retained state and replay lifecycle after the fixture correction. Editor/standalone planetary travel remains unverified.

Broader scene compatibility run: 40/41 cases passed, 631 assertions passed; project starter creation throws the
portable 16-sampler limit (planetary-scene-compatibility-tests.log). Source inspection confirms the starter material
has seven texture properties; generated lit shaders reserve two shadow, two IBL, five spatial-lighting and one
cloud-noise sampler, totaling 17. RenderingAssetValidation correctly rejects this; do not raise the portable limit
or silently remove starter material features. ShaderGraphManifest currently assigns volume ABI 9 to all world
materials, including opaque passes. Fix the generated resource contract and runtime bindings together, with shader
compile and starter-project regression coverage, before claiming project startup/creation acceptance. The already
running editor build is not proof this runtime path works. No production source edits made during this build.
Sampler-budget repair candidate: represent the same quantized cloud-noise mip chain in a read-only buffer only for
material programs that cannot fit the texture binding. Main sky/cloud passes can retain hardware texture filtering.
Before choosing this design, prove repeat wrapping, trilinear/mip interpolation and RGBA8 parity against texture
sampling on supported GPU lanes; measure the additional material-side work. Resource registers, storage-buffer
limits, manifest/ABI version, upload ownership, device recovery and VFX/scene bindings must change together. This
is a candidate under investigation, not an implemented workaround or a reason to relax sampler validation.
Noise buffer prerequisite implemented in internal CloudNoiseBuffer.h: explicit uint header/mip descriptors and
RGBA8 words retain the texture's existing quantization and mip order. Complete bounded mip chains are validated
before allocation; malformed dimensions, byte counts, missing levels and trailing levels are rejected. Standalone
C++20 tests pass 2 cases / 20 assertions (cloud-noise-buffer-tests.log). No production renderer consumes it yet;
shader filtering, GPU upload/binding, ABI/cache updates and GPU parity/performance remain required. These new files
are not dependencies of the in-flight editor build and do not alter its compiled rendering behavior.
CloudNoiseBuffer.hlsli now implements RGBA8 decode, normalized repeat addressing, half-texel-centered trilinear
sampling and linear mip interpolation, matching the existing sampler configuration. A standalone fragment wrapper
compiles to DXIL, SPIR-V and MSL using the pinned shader compiler (cloud-noise-buffer-probe.*). Compilation does not
prove rendered equivalence; reuse the existing cloud-noise GPU probe infrastructure to compare hardware texture
samples and buffer samples, including negative coordinates, wrap boundaries and fractional mip levels, before
connecting the new material ABI. The file is not included by production shaders yet.
Buffer-versus-texture GPU comparison passed on Direct3D 12 and Vulkan: 512 samples / four channels, maximum absolute difference 0.00206596 on each backend, below the existing 0.004 filtering tolerance. DXIL/SPIR-V/MSL compute compilation passed. Main editor DevDist stage also completed (planetary-precision-editor-stage.log); buffer path remains unintegrated and has no performance qualification yet.

### October 28: live precise-position authoring check

- Tested the staged DevDist editor maximized in CloudIsolation20261026, selecting Showcase Plinth in the hierarchy.
- Entered X = 1000000000000000.125 through the Inspector. The editor accepted the input and moved the plinth out of the local view; the formatted field displayed 1000000000000000.1 (17 significant digits). This screenshot verifies UI acceptance, not independently the stored fractional bits.
- Ctrl+Z restored X = 0, the visible plinth, and the clean scene state. Ctrl+Y reapplied the distant value; a final Ctrl+Z restored the baseline. Closed the editor normally without saving the temporary edit; verified the process exited.
- Authoring follow-up: improve presentation of widened legacy values (for example -0.80000001192092896) and cramped distant coordinates without sacrificing editable double precision.
- Stationary observations during this interaction were approximately 12–13 ms, 29 draws. These are snapshots, not a movement benchmark or planetary performance qualification.
- Remaining gates: starter-material sampler overflow; scene/prefab summary precision migration; camera-relative rendering and moving-frame integration; actual seamless ground/space/return validation. Production readiness is not established by this authoring check.

### October 28: shared cloud sampling path and cache coverage

- Routed all four cloud-density atlas samples through SampleVolumeCloudNoise. The default remains the existing hardware-filtered 3D texture. Defining KEIRE_CLOUD_NOISE_BUFFER selects the packed read-only buffer implementation; no production material selects that path yet.
- Added the buffer include to Windows/Unix shader and VFX staging/fingerprints, and embedded it before cloud consumers for installed-editor generated shaders. Extended cache regression assertions for both platforms.
- Compiled the actual shared CloudVolume kernel's texture fragment path and buffer compute comparison to DXIL, SPIR-V, and MSL. The buffer comparison executed successfully on Direct3D 12 and Vulkan: 512 samples, four channels, maximum absolute error 0.00206596 against hardware texture filtering. This is correctness evidence, not a performance measurement or Metal runtime test.
- Windows generated-content cache harness passed. Runtime ABI/resource ownership/bindings remain the next integration step; the seven-texture starter-material overflow is still open. The editor remains closed between validation cycles.

### October 28: material volume ABI 10 integration under validation

Implemented conditional generator selection, manifest/codec acceptance, import reflection counts, portable resource validation, runtime pipeline counts, scene/VFX storage binding offsets, and device-owned noise-buffer upload/release/recovery. Added six-versus-seven-texture generation coverage and advanced generated shader version to 36 so old generated material output is invalidated. Main cloud shaders and lower-resource materials retain texture filtering.

Native DevDist KeireTests build is running with four jobs; log: Build/Validation/cloud-buffer-integration-build.log. Do not consider the starter-project regression resolved until that rebuilt executable passes and actual material import/runtime binding is checked. Renderer device-lifecycle changes also need failure/recovery coverage. No editor process is open.

### Build-latency follow-up observed during ABI integration

The Windows built-in VFX generator recompiles all 23 entry points in three formats when any shared include changes. The cloud sampling include change therefore also rebuilt compute entry points (CSSpawnInitialize, CSSpawnOutput and others) that do not consume cloud transport. This is an observed incremental-build cost, not evidence of runtime shader slowness. Evaluate per-stage dependency-aware caching after correctness gates; preserve complete invalidation for actually consumed includes and compiler/version changes. Do not skip required variants merely to shorten this build.

### October 28: starter-project regression passes with ABI 10

The four-job DevDist KeireTests build completed successfully (209 actions; Build/Validation/cloud-buffer-integration-build.log). The rebuilt executable passed seven focused cases / 200 assertions, covering supported/canonical volume metadata, sampler/storage capacity, six-versus-seven-texture Shader Graph selection, compatibility versioning, and Projects create isolated starter assets and hold exclusive editor locks. Evidence: Build/Validation/cloud-buffer-contract-tests.log.

The original project-creation sampler-limit exception is resolved in this regression. Remaining acceptance is explicit: compile/import the seven-texture generated shader through all supported shader formats, validate actual renderer bindings and resource recovery, and measure the buffer fallback cost. The default atlas sampling path remains texture-backed. This result does not establish planetary rendering or seamless travel readiness.

### October 28: seven-texture shader import verified

Added a production-import regression with seven actively sampled material textures. The two-action incremental DevDist rebuild passed. The new case passed 55 assertions, decoding ABI 10 metadata and checking depthVelocity, deferredGBufferStandard and forwardOpaque binaries in DXIL, SPIR-V and MSL. The importer also validated reflected sampler/storage counts. Log: Build/Validation/seven-texture-import-tests.log. This validates generated binary/resource contracts; actual draw binding, device recovery and performance remain separate gates.

### Next precision regression prepared while editor staging runs

Added Precise scene snapshots retain authoring coordinates beyond float range to PreciseTransformTests.cpp. It checks both SceneTransform summary fields and decoded summaries, plus restored TransformComponent coordinates, for 1e15 fractional positions and 1e40 values. This deliberately covers the float summary gap that the earlier component-bag round trip missed. The test has not been built/run yet and the current float summary implementation is expected to fail it. It is not part of the already-passing sampler test results. Complete the scene/prefab summary migration before claiming this gate passes.

The interactive sampler test must use an explicitly seven-texture graph: ordinary Create Material uses the smaller pinned Kéire/Lit graph and cannot prove ABI-10 drawing. The production-import test now has opt-in KEIRE_TEST_SHADER_FIXTURE_ROOT export to a new absolute directory, refusing existing destinations. Its exported graph source can be imported and assigned through the QA editor. This export addition is not yet rebuilt; it does not alter ordinary test output or production behavior.

### October 28: runtime QA launched and summary precision gap reproduced

Editor DevDist staging completed successfully (183 actions, 721.7 seconds). Fixture-export rebuild passed three actions; opt-in export/import test passed 57 assertions and wrote opaque/transparent graph sources into the QA project's Assets/Validation/CloudBuffer20261028 folder. The transparent fixture is intended for live volume sampling; its runtime import/drawing is not yet validated. Editor launched maximized at 16:01 (PID 13948) and is refreshing startup shaders after generator version 36 changed.

Ran the new Precise scene snapshots retain authoring coordinates beyond float range baseline on the freshly rebuilt native executable. It fails as expected: six precision checks fail for snapshot/decoded summary fields at 1e15, and snapshotting 1e40 throws Relative coordinate exceeds the float range. Three assertions pass. Evidence: Build/Validation/precise-scene-summary-baseline.log. This is an open regression requiring SceneTransform summary, codecs and authoring consumers to migrate coherently; the separate sampler tests remain passing.

### October 28: scene-summary migration under compilation

Migrated SceneTransform.Position to Vector3d, snapshot capture and scene application to precise transform accessors, JSON position parsing to finite doubles, and prefab authoring offset/review consumers to preserve double coordinates. The four-job DevDist KeireTests build is running under Build/Validation/precise-scene-summary-build.log. The previously failing snapshot regression has not yet been rerun; do not count this migration as validated.

Runtime integration audit confirms SceneRuntimeSpatialAccess::FrameForEntity in SceneRuntimeWorld.cpp still returns RootFrame for every owned entity. Animation grounding ray origins and procedural camera-to-animator deltas also still consume float WorldPosition. Travel acceptance must cover these consumers alongside rendering and physics: assigning moving frames without converting these boundaries would leave distant-world behavior incomplete. Preserve world-space precision until subtraction into an explicitly selected local simulation or camera frame, then narrow with range checks. Existing frame-math tests do not prove this runtime integration.

### Interactive asset navigation observation

With startup imports settled, the QA showcase materials again display their distinct colors; the earlier all-white capture is no longer reproduced. This does not qualify the seven-texture fixture draw. Both fixture sources now have importer metadata. Navigating into a folder retains the current-folder name filter, producing a visually empty child folder when the filter matched only the parent name. Observed twice while entering Validation and CloudBuffer20261028. Consider an explicit filtered-empty state with a clear-filter action (or a documented navigation filter policy) so users do not mistake hidden contents for missing imports. Defer this QoL implementation until the current precision build and travel blockers are addressed.

### Precise prefab placement follow-up

Added InstantiatePrefabPrecise with double destination coordinates; the existing float entry point explicitly widens and delegates. Placement now subtracts source-root coordinates before adding the destination, avoiding cancellation of millimetre destinations when source roots are around 1e15. Added an editor regression covering multiple root spacing and serialized override destination precision. Updated affected existing position assertions to the new double summary type. These editor changes are formatted but not yet built or tested; the current native test build does not include KeireEditorTests.

### October 28: opaque seven-texture material exercised in the editor

Using the maximized QA editor, created SevenTextureOpaqueQA via Material from Shader with SevenTextureVolume selected. Inspector exposed Channel0 through Channel6. Dragged the material onto the Iridescent Shield cube; Inspector showed SevenTextureOpaqueQA as its compiled material and the mesh rendered white/blue shading while the scene remained responsive. Undo restored the original MG_12_IridescentShield material and cleared the scene's unsaved indicator. This is direct opaque scene-draw evidence for the seven-texture fixture with fallback textures, not a seven-distinct-texture correctness test, transparent-path qualification, device-recovery test, or performance benchmark. The material asset remains in the isolated QA fixture folder.

### October 28: scene summary precision regression passes

The four-job DevDist native build completed all 192 actions and linked successfully. The focused Precise* / *Prefab* selection passed 16 cases and 102 assertions (Build/Validation/precise-scene-summary-tests.log). A separate exact-name invocation of the formerly failing snapshot test records individual assertions in Build/Validation/precise-scene-summary-regression.log. This establishes double summary/component round trips at 1e15 fractional coordinates and 1e40, not runtime rendering or physics at those coordinates. Editor prefab-placement regressions are a separate pending target; four-job DevDist KeireEditorTests compilation has started (Build/Validation/precise-prefab-editor-build.log). The later occlusion-test narrowing-warning cleanup was edited after its compilation and still needs an incremental native rebuild.

### Next rendering migration boundary confirmed

TransformComponent::ApplyRuntimeFixedPresentation computes a Vector3d interpolation result but immediately narrows it through LegacyPosition into m_FixedPresentationLocal. PresentationWorldMatrix recursively multiplies float matrices. RenderResourceCaches.cpp then captures that matrix into draw items and MotionHistory. Therefore subtracting a camera origin only after draw-packet construction cannot recover lost translation bits.

The next coherent rendering batch must retain precise interpolated local/world translation, construct linear rotation/scale separately from translation, and subtract a frame-matched camera origin in doubles before packing draw matrices. Previous-frame transforms and previous camera origins must be paired explicitly across rebases; simply reusing old relative matrices under the new origin would introduce false motion vectors. Cover parent interpolation, position-only/full presentation overrides, multiple camera surfaces, teleport/reset, and origin changes with focused tests. Audio, world-space canvas projection, animation grounding and physics remain independent consumers requiring their own selected local-frame contracts.

### Transparent live material check failed

Switched the QA material to SevenTextureVolumeTransparent and explicitly selected Blend (source alphaMode 2). Applied it to the same cube. After refresh, both Scene and Game Edit preview displayed the bright magenta fallback rather than the expected translucent white fixture. Undo restored MG_12_IridescentShield and the clean scene state. The QA material asset intentionally remains pointed at the transparent graph for diagnosis, despite its original SevenTextureOpaqueQA filename. This is a failed transparent acceptance gate; do not infer success from shader source import or the earlier opaque draw. Investigate compiled pass selection/resource binding and material diagnostics before continuing the renderer qualification.

Transparent ABI10 follow-up: the QA AssetOperations result c67ebbed-dd71-4ab8-8bbf-463e2ce06b57 records successful import with no diagnostics for graph 6ace59c5-59c6-4f7d-8ed3-6d6665a66c86. Subsequent material import results also report success. This narrows investigation toward runtime pipeline/pass selection or hot reload, without proving the cause. The seven-texture production importer regression now covers both Surface and Transparent output, including forwardTransparent artifacts in DXIL, SPIR-V, and MSL; execution remains pending behind the active editor build.

Runtime evidence for the transparent fallback: QA Logs/Core.log records forwardTransparent pipeline creation starting at 16:24:45.701 and succeeding at 16:24:51.902 (6201.098 ms), with no corresponding pipeline error. The opaque fixture likewise spent roughly 6.25, 7.80, and 6.37 seconds creating its three passes. Distinguish asynchronous preparation fallback from persistent failure during the next live check; successful import alone was insufficient, and successful pipeline creation still does not prove binding/publication or final pixels. Preserve this first-use latency as a performance finding.

Next precision acceptance cases: existing SceneRuntimeWorldTests character interpolation coverage uses small float coordinates and does not establish large-world presentation. Add distant parent/child presentation tests with fractional motion, rotated/scaled parents, alpha endpoints, reset/teleport, and reparent discontinuity. Keep translation double through fixed-step interpolation and hierarchy composition; subtract the render-camera origin before float packing. Validate current and previous frame origins together for motion vectors, and independently for multiple cameras. Values beyond float range must remain representable until the explicit relative-render conversion boundary.

### October 28: precise prefab and environment editor tests verified

DevDist KeireEditorTests builds successfully after correcting the remaining Vector3d fixture expectation, three environment test includes that depended on the native-test target, the cloud preset ostream include, and relocating ObserveEditSurface beside the shared ProjectSettingsPanel implementation so the editor-test target links it. Focused prefab/Inspector/environment-name selection passed 25 cases and 380 assertions (Build/Validation/precise-prefab-editor-tests.log). A separate file-scoped run covering ProjectAtmosphereSettingsTests, ProjectEnvironmentTimeTests, ProjectEnvironmentLightingTests and VolumetricCloudPresetTests passed 29 cases and 461 assertions (Build/Validation/environment-authoring-focused-tests.log). These selections overlap and are not additive unique-test counts. Transparent shader regression compilation is now running separately. Live rendering and planetary travel remain unqualified.

### October 28: opaque and transparent ABI10 importer regression passed

The three-action incremental KeireTests DevDist build passed. The seven-texture production importer test now passes both opaque and transparent subcases: 108 assertions, including DXIL/SPIR-V/MSL pass artifacts plus blending and depth-write metadata. Evidence: Build/Validation/transparent-volume-import-build.log and Build/Validation/transparent-volume-import-tests.log. This does not close the observed live magenta fallback; successful pipeline construction in the QA core log further requires checking binding publication and visible output after preparation completes. No build remains active at this checkpoint.

### Precise presentation implementation in progress

TransformComponent now separates double presentation translation from float rotation/scale, retains double fixed interpolation samples through presentation, and exposes PresentationWorldPositionPrecise plus checked PresentationRelativeMatrix(origin, maximumError). New tests cover fractional distant interpolation, rotated/scaled parent composition, reset, invalid origins and beyond-float-range presentation. These edits are formatted but not yet built or executed. RenderCamera origin and motion-history integration remain outstanding; current renderer callers still use the legacy absolute matrix API. Do not claim live large-world rendering from this intermediate API implementation.

Precise presentation batch follow-up: runtime position-only overrides now have an explicit double API, legacy float overrides widen into it, and rotation queries decompose translation-free presentation matrices. Added coverage for beyond-float-range overrides, preserved gameplay rotation/scale, invalid-update rollback, unchanged simulation position, and reset. This batch remains uncompiled while camera/history consumers are being migrated.

### Transparent runtime fallback reproduced after a fresh launch

Reopened the staged QA editor maximized (PID 3136). Assigned SevenTextureOpaqueQA (still referencing the transparent graph with Blend) to 07 - Frosted Glass. Bright magenta persisted after more than 50 seconds and appeared in both Scene and Game Edit views. This excludes only a short first-use preparation delay; it does not establish the binding failure's cause. Undo visibly restored MG_07_FrostedGlass and cleared the unsaved scene indicator, then the editor was closed. Build activity overlapped this check, so no performance qualification is claimed.

### Correction: magenta fixture pixels do not establish a binding failure

Runtime shader decoding confirms ABI10, seven Generic texture parameters, forwardTransparent in DXIL/SPIR-V/MSL, Blend and no depth writes. Generic unassigned textures deliberately resolve to Texture2DAsset::Checkerboard, whose pixels include magenta; the fixture adds seven such samples. The previous expectation of neutral white was invalid, so the observed magenta must not be treated as proof of a missing material binding. Corrected all seven fixture parameters to BaseColor semantics (neutral white fallback), both in the production-import fixture generator and the two existing QA source graphs. Visual acceptance must be rerun after reimport. Removed the speculative pass-selection diagnostic added during investigation.

Corrected-fixture visual retry was inconclusive: operation 1a727ec0-5046-4caa-8a4b-4351568a8034 reported both fixture shader compilers exceeding 30000 ms during the concurrent native build. Last-good data was retained, so evaluating its pixels would not validate the corrected fixture. Closed the unchanged QA editor and deferred reimport until native compilation completes. Investigate shader compilation scheduling/timeout behavior as a performance and recovery issue; do not merely increase the timeout without measurement.

### Precise presentation validation completed

DevDist KeireTests build completed successfully (precise-presentation-build.log). Focused Precise*, presentation-transform, position-override, and Custom fixed presentation regressions passed: 18 cases, 132 assertions (precise-presentation-tests.log). This validates the double-precision presentation foundation and existing interpolation behavior, not camera-relative rendering or planetary travel. RenderCamera origin, render consumers, and previous-frame history integration remain outstanding.

### Camera-relative integration audit and required acceptance gates

Source inspection after precise-presentation validation confirms the following work is still required:

- SceneRuntimeRenderingInternal.h builds the camera by inverting PresentationWorldMatrix, which narrows absolute translation. Build the view from a precise origin and relative presentation matrix instead.
- RenderResourceCaches.cpp captures mesh presentation matrices independently from camera history. Keep each previous object matrix paired with the previous camera coordinate system; handle missing object history explicitly when the origin changes. Test stationary objects across rebases, moving objects, camera cuts, and independent surfaces.
- RenderBackendInternal.h resolves point/spot lights, reflection probes, and light-probe volumes through absolute WorldMatrix. Convert all spatial consumers consistently before enabling an origin in the live renderer.
- CloudVolumePacket.h derives observer coordinates solely from inverse float View. Preserve observer position relative to the environment anchor in double precision; rebasing must not move the cloud pattern or alter its altitude. Audit fog, grid, VFX, spatial materials, and shadow consumers under the same contract.
- Acceptance requires equal near-origin and distant-origin render inputs for the same local arrangement, plus stable previous-frame reprojection during origin changes. A passing transform test alone does not satisfy these gates.

The next implementation batch must connect these paths together before claiming a live planetary travel milestone. Physics reference frames and seamless surface/space travel remain subsequent integration requirements.
Corrected seven-texture production importer validation completed successfully: 1 test case, 110 assertions, including opaque/transparent variants and BaseColor fallback semantics (Build/Validation/transparent-volume-import-corrected-tests.log). Live visual reimport validation remains outstanding.

Camera-coordinate implementation started: internal PresentationCameraFrame keeps a double origin paired with its translation-free view and converts object presentation matrices relative to that origin. Added near/distant/beyond-float-range camera tests and previous/current-origin independence coverage. Build and execution are pending; live render consumers deliberately remain unchanged until the spatial conversion batch is connected. This helper does not establish renderer or planetary readiness.

Camera-frame validation passed: DevDist build plus Precise* selection, 13 cases / 88 assertions. Spatial integration batch now adds origin-aware capture for point/spot lights, reflection probes and light-probe volumes; directional-light orientation no longer requires absolute float translation. Added distant-offset and beyond-float directional capture regression coverage. These spatial edits are formatted but not yet compiled/tested; callers still default to zero origin pending the coordinated render-camera integration.

Cloud capture now accepts explicit render and environment origins, subtracting them in double precision before adding the local camera offset. Added rebase-equivalence tests for cloud geometry/pattern at zero and 1e15 origins, cloud-base evaluation, and nonfinite-origin rejection. Lighting/probe plus cloud changes are in the spatial-origin DevDist build; live camera callers still use the zero-origin defaults until the complete packet/history integration is ready.

Further coordinate-consumer audit: SkyUniformPacking.h passes a float observer to both atmosphere and regular cloud-sheet packing; RenderCloudVolume.cpp independently derives a float observer for atmosphere/fog. These callers must share the precise environment-relative observer used by volumetric clouds. AtmosphereUniforms.h currently accepts Vector3 despite double PlanetCenter settings. ScenePresentationRuntime.cpp also narrows listener/emitter positions and world UI matrices; include audio/world UI in the end-to-end large-world acceptance backlog, rather than treating a stable mesh view as planetary readiness.

Spatial-origin validation completed: initial compile exposed a missing direct Vector3d include in CloudVolumePacket.h; fixed it and the incremental DevDist build passed (spatial-origin-build-retry.log). The Precise*, Cloud*, spatial-lighting capture, and directional-resolution selection passed 80 cases / 91,821 assertions (spatial-origin-tests.log). This includes existing cloud tests; the assertion count is not a count of new scenarios. Live camera-origin routing, temporal history, fog/sky callers, VFX, and editor travel validation remain outstanding.

Next environment-coordinate batch (not yet compiled): shared EnvironmentRelativeObserver cancels double render/anchor origins before adding local camera offsets; volumetric capture and regular sky cloud packing use it. Atmosphere packing accepts the same origins and cancels the planet center before adding the local offset. New regression uses offsets below the absolute-coordinate double ULP near 1e15 and checks atmosphere/fog equivalence plus invalid-origin rejection. Remaining live packet/camera routing will be batched before another build.

Live packet integration batch in progress (uncompiled): RenderCamera now carries validated WorldOrigin and EnvironmentAnchor metadata. Mesh/light/probe capture, volumetric clouds, sky sheets, atmosphere and fog consume those origins. Camera history retains origin/anchor; missing mesh history uses the previous origin and environment-anchor changes invalidate continuity. Editor/runtime camera producers still use zero defaults. Before enabling nonzero origins by default, finish cloud temporal reprojection, fullscreen/depth velocity reprojection, VFX/grid/spatial-material consumers and focused packet tests. This intermediate working tree is not production-ready.

Temporal batch (uncompiled): cloud temporal views now retain WorldOrigin and compute scene displacement from double origin delta plus local camera delta, rejecting nonfinite/large cuts before narrowing. Added rebase-versus-real-centimetre-motion regression. Headless environment capture now tests distant mesh matrices, paired previous/current origins, invalid-camera rollback and environment-anchor history invalidation. Complete remaining spatial consumers before enabling rebasing in editor camera producers.

Remaining consumer evidence: VfxWorld::SetTransform, VfxGpuEmitter.Position and VfxRenderParticle.Position/PreviousPosition are float-only; VfxRenderSnapshot has no coordinate origin. Renderer-only subtraction cannot recover precision already lost in simulation. BuiltinGrid.hlsl intersects render-space y=0 and derives line/axis coordinates directly from that position; preserve the authored plane, periodic grid phase and true axes across rebases. Include CPU/GPU particles, ribbons, depth collisions, and grid depth/phase in acceptance tests.

Spatial-material audit: ShaderGraphExpressionLowering.cpp lowers WorldPosition directly to world.xyz/input.WorldPosition, and TriplanarSample scales its Position input directly. Passing camera-relative mesh matrices changes this semantic unless material coordinates carry a stable frame/anchor. Before enabling camera rebasing by default, define and implement stable authored material-position coordinates (including displacement and previous displacement), then test stationary triplanar/procedural patterns across camera movement and rebases. Merely adding a large float origin back would reintroduce the precision loss.

Render-origin integration validation: the full incremental DevDist KeireTests build completed successfully (194 steps; render-origin-integration-build.log). Focused Precise*, Cloud*, Atmosphere*, Environment capture*, spatial-lighting capture and directional-resolution selection passed 100 cases / 92,357 assertions (render-origin-integration-tests.log). This includes pre-existing numerical cloud tests; it is not 92,357 independent travel scenarios. New headless capture validates distant relative mesh matrices, previous/current origin pairing, anchor history invalidation and invalid-camera rollback. Cloud reprojection validates rebasing without false motion and actual centimetre-scale movement. Rendered editor validation, nonzero-origin camera producers, VFX, grid and material-position integration remain outstanding.

Grid integration batch (uncompiled): GridUniforms grows from 224 to 256 bytes with relative ground-plane height, bounded major-grid phase and actual axis positions/visibility. BuiltinGrid.hlsl intersects that plane and separates periodic line coordinates from real axes. CPU packing rejects invalid inputs and omits a grid whose plane is beyond its existing fade range. Added distant-origin/rebase, axis, height and invalid-input regressions; updated uniform-size assertion. Shader compilation on supported backends and rendered grid validation remain pending.

Grid shader compilation passed for vertex and fragment stages in DXIL, SPIR-V and MSL using KeireShaderCompiler; outputs are under Build/Validation/grid-origin-shaders. Native grid packing and newly visible/skipped-frame motion-history regressions are formatted but still await the next native test build. The compiler CLI rejected an absolute output path containing the repository's accented directory name (displayed as a replacement character); relative output paths succeeded. Record this as a Unicode CLI path defect to investigate, not a shader failure.

VFX frame foundation batch (uncompiled): VfxWorldSpecification and owning render snapshots carry a finite stable WorldOrigin. ActivatePrecise/SetTransformPrecise subtract the simulation origin before checked narrowing; existing float APIs operate in that local simulation frame. Checkpoint v2 records the origin and rejects cross-frame restores before mutation; v1 remains readable in zero-origin worlds. Added CPU/GPU local-equivalence, snapshot-origin, checkpoint mismatch and invalid-transform rollback tests. Renderer VFX consumption, moving-frame rebasing, scene adoption and GPU retained-particle origin handling remain pending; this is not complete large-world VFX support.

### CPU particle render-frame integration

CPU particle capture now converts simulation-local positions into the current render origin and previous positions into the previous camera origin. Camera-history resets select the current origin for both frames. Mesh particles use the same conversion as sprite/ribbon capture. A focused regression preserves local offsets smaller than the absolute double-coordinate spacing and checks distinct current/previous origins without mutating the snapshot.

Validation: touched C++ files formatted and `git diff --check` passed; this batch is not compiled or runtime-validated yet. GPU particle conversion, camera-producer adoption, and planetary travel remain outstanding.

### GPU particle origin audit before camera adoption

Verified remaining coordinate consumers: `RenderVfxDrawing.cpp` supplies an unadjusted camera matrix for retained simulation-local GPU buffers, including built-in mesh/sprite/ribbon paths; composed instanced mesh materials use a separate binding path. `RenderVfxVisibility.cpp` and `BuiltinVfx.hlsl` build occlusion bounds and instance outputs directly from simulation-local particles. `RenderVfxRecording.cpp` uses sampled-depth camera matrices for collision, while `RenderSurfaceStateInternal.h` retains no origin alongside those matrices.

Required integration: preserve immutable simulation coordinates, translate render outputs and visibility bounds consistently, retain the sampled-depth origin, and convert collision projection/reconstruction between simulation and sampled-depth frames. Cover composed mesh materials as well as built-in particles. Do not enable nonzero camera origins with only the sprite draw path corrected. Current grid/CPU VFX batch is building through the four-job DevDist launcher; result is pending in `Build/Validation/vfx-grid-origin-build.log`.

### First ground-to-space travel acceptance route

This is a pending integration gate, not a completed test or replacement for the full planetary scope.

1. Use one reproducible QA scene with a named planet/atmosphere anchor, surface landmarks, an opaque terrain patch,
   transparent materials, world-position/triplanar materials, CPU and GPU particle emitters, collision targets,
   spatial audio, and world-space UI. Save the initial precise camera and entity positions.
2. Verify stationary rendering and centimetre-scale movement near the starting surface. Repeat with the entire
   coordinate setup translated far from zero; compare relative positions rather than float absolute matrices.
3. Ascend continuously through fog/cloud altitude and beyond the atmosphere. Log frame time, origin changes,
   camera altitude, residency transitions, and coordinate conversion failures. Check for texture sliding,
   disappearing particles, incorrect depth collisions, shadow jumps, and discontinuous sky transitions.
4. Travel laterally in space, change camera orientation, and return to the same landmarks without a loading screen.
   Compare final precise coordinates and local interaction behavior against the baseline; repeat the route.
5. Run the same route in Scene view, Play Mode, and the standalone player. Record backend, resolution, quality,
   hardware, build revision, p50/p95/p99 frame times, worst hitch, and visual evidence. No production claim follows
   from a headless capture test alone. Rotating reference frames, additional planets, and sustained travel/soak
   remain separate required gates after this initial route.

Current producer audit: `BuildPresentedSceneCamera` still inverts an absolute presentation matrix; the editor
Scene camera builds a float view directly. `SceneRuntimeVfx.cpp` decomposes `WorldMatrix()` before float activation
and transform updates, losing precision before the new API can help. Shader Graph WorldPosition outputs the
render position directly, and TriplanarSample scales that input without an authored-frame phase correction.
These are unresolved integration dependencies, not reasons to disable the corresponding systems in the test.

Validation checkpoint: the first grid/VFX native build failed because grid drawing referenced an observer scoped to sky drawing. Grid drawing now computes and reuses its own inverse view and observer; the resumed incremental build has compiled past that failure. Results remain pending. Checkpoint coverage audit found that version-1 restore is implemented but has no direct legacy fixture regression yet; add that after this active build, alongside valid precise-transform and rejected-origin state-preservation checks. Do not infer compatibility validation from the version-2 self-round-trip alone.

Grid/CPU-VFX validation result: the resumed four-job DevDist KeireTests build succeeded (`Build/Validation/vfx-grid-origin-build-retry.log`). The selection `Precise*,Grid origins*,Editor grid shader*,VFX precise*,Environment capture*,CPU mesh VFX*` passed 19 cases / 330 assertions (`Build/Validation/vfx-grid-origin-tests.log`). This covers the newly compiled coordinate helpers, grid contracts, precise activation/checkpoint frame identity, and capture history edges. It does not validate GPU-origin integration, version-1 checkpoint migration, rendered grid/particle behavior, or planetary travel.

GPU depth collision integration: sampled depth now stores the camera world origin alongside its projection/inverse matrices. VFX dispatch composes those matrices with the simulation-to-depth translation and its inverse, so collision hits reconstruct back into simulation coordinates even when depth was captured under a different origin. Conversion is performed only for valid sampled depth. Added a projection/reconstruction regression at a distant origin with fractional local offsets. Formatted; compilation and execution pending in the next GPU integration batch. GPU draw/visibility/material paths remain unresolved; camera rebasing remains disabled.

Built-in GPU VFX draw integration now composes the camera projection with a simulation-to-render translation, leaving retained particle buffers unchanged. The translation helper is shared with collision projection and has a nearby-offset/direction-preservation regression. Custom material bindings still consume simulation positions alongside render-local lighting and therefore need a coordinated material vertex conversion; changing only their view matrix would leave lighting incorrect. Occlusion candidate/instance conversion is also pending. These edits are formatted but uncompiled, accumulated for the next batch.

Custom GPU billboard/ribbon materials now receive the simulation-to-render matrix through their existing object Model uniform. Generated VFX vertices apply that translation before evaluating world-position offsets and producing lighting/shadow inputs; normal transforms remain translation-free. CPU VFX continues using already-converted positions. Instanced custom mesh particles still need their separate instance-coordinate integration. The standalone visibility-candidate shader compiled successfully to DXIL, SPIR-V, and MSL after its origin-offset change; native and rendered validation remain pending. Generated-material cache compatibility and focused shader tests must be checked before enabling the new camera producers.

Generated material cache integration: shader generator revision advances from 36 to 37 so imported materials rebuild for the GPU billboard/ribbon Model translation contract. Added a generated-source regression verifying translation precedes world-position construction and lighting output. Existing production-importer template coverage includes VFX passes on DXIL/SPIR-V/MSL and must run for this batch. No new shader compile/import result is claimed yet.

Generated GPU mesh material integration now uses InstanceParameters.y to request simulation-to-render translation of the per-particle Model matrix, leaving the normal matrix unchanged. GPU VFX binding supplies the translation; ordinary draw bindings keep the flag zero. Generator revision 37 covers this unpublished batch. Added generated-source coverage. Native build, production shader imports, and rendered comparison remain pending. Authored/custom shader capability compatibility must be audited before camera adoption: older shaders may ignore the optional addressing flag.

Integrated GPU VFX validation checkpoint: the built-in shader generation launcher completed successfully across its DXIL/SPIR-V/MSL variants (`Build/Validation/vfx-origin-builtin-shaders.log`, process exit 0). Capability metadata, fallback policy, shader revision 37, legacy checkpoint and precise setter tests are integrated. Four-job DevDist KeireTests build is now running (`Build/Validation/gpu-vfx-origin-build.log`). Native tests, production material-import tests, and rendered validation remain pending.

Editor camera adoption audit: `EditorCameraState::Focus` remains Vector3; `SceneCameraController` forwards float SetFocus/Frame/FollowFrame and absolute ViewMatrix. `ScenePicker.cpp` uses WorldMatrix for bounds, ray intersection, and rectangle projection; ResolveSceneDropPosition returns Vector3. A camera-origin-only switch would make rendering disagree with selection and placement. Next editor batch must preserve precise focus/persistence, compute entity bounds relative to the selected camera frame, and return precise placement positions to existing double scene-document operations. Validate distant ray picks, rectangle selection, focus/lock, and drag/drop together before enabling rebasing.

GPU VFX native validation: the combined four-job DevDist KeireTests build completed all 337 steps successfully (`gpu-vfx-origin-build.log`). Focused precise-coordinate, checkpoint migration/rollback, generated material contract/capability, fallback policy, environment capture, and grid selection passed 26 cases / 455 assertions (`gpu-vfx-origin-tests.log`). The production graphics-template importer test is now running separately (`gpu-vfx-origin-production-import.log`); its result is pending. Rendered GPU comparisons and camera adoption remain unverified.

Editor selection integration started: ray picking and rectangle projection now use PresentationRelativeMatrix(camera.WorldOrigin) rather than absolute WorldMatrix, matching captured mesh positions. Added near/distant-origin selection and miss regression at 1e15 with a fractional target offset. This editor batch is uncompiled; precise camera focus/persistence, focus bounds, and drop positions remain pending before camera adoption. Production material-import validation continues separately.

Production graphics-template import validation completed successfully: 1 case / 168 assertions across the production importer (`Build/Validation/gpu-vfx-origin-production-import.log`). Editor placement now has a precise world-coordinate resolver: ray math remains camera-relative, the ground intersection accounts for the camera origin's altitude, and the result adds the double world origin after resolving the local displacement. Added distant-origin ground and horizon fallback coverage. Existing float wrapper remains temporarily for callers; routing/import/gizmo and focus integration must migrate together before editor camera adoption is considered complete. Editor additions are formatted but not yet compiled or executed.

Editor placement integration now retains Vector3d through viewport command dispatch, asynchronous external-import context/completion, mesh/emitter creation, and precise prefab instantiation. Added distant placement/scene snapshot regression and distant command-routing coverage. Focus bounds can now be evaluated relative to an explicit origin and return a precise center; Scene viewport frame/follow uses the precise camera focus. Camera and runtime VFX agent batches are integrated on disk but uncompiled. Gizmo overlays/manipulation still use absolute WorldMatrix/WorldPosition at multiple sites and must be converted before opening this editor build. No rendered or planetary-travel readiness claim is made.

Gizmo integration batch: icon/frustum/light/probe/collider/controller/rigid-body overlays and transform handles now project presentation transforms in the camera origin frame. Group capture stores translation-free hierarchy matrices, precise initial world positions and a precise drag pivot. Rotation/scale transform pivot-relative offsets in double precision and preserve local rotation/scale decomposition; translation/cancellation retain precise coordinates. Added distant rotation/scale/cancellation regression. Formatting and diff checks pass; compilation, focused tests and rendered validation remain pending. Audit remaining non-gizmo overlays before UI validation.

Read-only audit during editor integration build: RiggingStudioOverlay.cpp still feeds absolute PresentationWorldMatrix into PublishedRigChainPoints before projecting with the camera-relative view; migrate that overlay in the next source batch. Existing editor framing test around EditorArchitectureTests.cpp:1987 projects absolute bounds through ViewMatrix and must be updated to the explicit camera-origin contract (retain the fit-to-viewport assertions). Four-job DevDist editor-test build is active; source inputs remain frozen until terminal status.
World UI audit: ScenePresentationCanvasProjection.cpp resolves scene cameras using inverse absolute PresentationWorldMatrix and stores absolute panel World matrices. Both must share the active RenderCamera.WorldOrigin, including cached projection identity checks and input unprojection; changing the panel matrix alone is insufficient. These are explicit next-batch dependencies, not yet implemented.

UI frame capture audit: RenderFrameExecution.cpp pairs submission.World with the view's camera matrix at capture time; RuntimeUiRenderSubmission has no source-origin field. The pending fix must retain the panel source origin, translate into the capture camera frame, and validate camera-overlay identity with origin as well as matrices. Test projection/unprojection and a camera-origin change between submission and capture; merely changing cached panel.World would leave this race in coordinate frames unresolved.

Editor integration build stopped at EditorArchitectureTests.cpp:640: a legacy test supplied Vector3 pivot to SceneTransformGroup::Apply after its pivot became Vector3d. Updated that caller to WorldPositionPrecise; updated the framing regression to subtract camera.PreciseFocus before projection while preserving viewport-fit assertions. Rigging chain overlay now uses PresentationRelativeMatrix(camera.WorldOrigin). Formatting and diff checks pass. Build session 10332 exited 1; no test execution occurred. Complete the coordinated world-UI origin batch before resuming incremental compilation.

World UI origin integration implemented (uncompiled): RuntimeUiRenderSubmission appends WorldOrigin, default zero for compatibility; projection state retains the origin alongside World/view matrices; world panels use presentation-relative transforms; camera-overlay identity checks include origin. Capture converts submission.World into the live camera frame without adding tiny offsets to huge absolute coordinates. Finite-origin validation precedes submission acceptance. Added local-offset, changed-origin, normal/scale preservation and invalid-origin tests. UI-selected scene cameras now use BuildPresentationCameraFrame; the primary BuildPresentedSceneCamera producer must adopt the same contract before runtime UI validation. Projection/input integration tests and capture tests remain required; do not infer them from the coordinate helper test alone.

Primary runtime camera adoption now uses BuildPresentationCameraFrame, matching UI-selected cameras. Updated interpolated-physics-camera regression to compare relative transforms plus exact presented origin. Added world UI projection/input round-trip coverage at zero and 1e15 origins, alongside the submission-to-capture conversion helper regression. Full batch is formatted; build/tests pending. Planetary physics, stable authored material coordinates, simulation frames, and real surface/space travel remain outstanding.

Core camera/UI integration validation: DevDist KeireTests build completed successfully after correcting test fixture const entities and creating a real RuntimeUiTree root ID. Focused selection `World UI*,Scene VFX preserves*,*camera*` passed 35 cases / 648 assertions (`Build/Validation/precise-camera-ui-core-tests.log`). The VFX out-of-range error line is expected failure/recovery test coverage, not a test failure. Editor-test build is now running in the same sequence; editor tests and rendered validation remain pending.

Additional UI compatibility selection passed 9 cases / 217 assertions (`Build/Validation/precise-camera-ui-compatibility-tests.log`): world-runtime-UI geometry/submission ownership, camera overlays, and runtime input capture/focus routing. This supplements the new distant-coordinate tests; it does not replace rendered desktop validation. Editor build continues.

Editor validation checkpoint: DevDist KeireEditorTests completed all 229 steps (`Build/Validation/precise-camera-ui-editor-build.log`). Focused camera/placement/distant-coordinate/selection/rotation selection passed 86 cases / 1,856 assertions (`Build/Validation/precise-camera-ui-editor-tests.log`). This validates the selected headless editor workflows, not the desktop rendering or full client workspace consumers. Build/stage the actual DevDist client before visual QA.

Build-iteration backlog evidence: KeireEditorTests/premake5.lua directly compiles many ../KeireClient/Source/Editor/*.cpp files, while KeireClient/premake5.lua also compiles Source/**.cpp with client PCH settings. Thus common-header edits rebuild shared editor implementations separately for tests and the actual client (observed 229-step editor-test build followed by 181-step client build). Investigate a common editor support library with equivalent compile definitions/dependencies; do not blindly reuse objects across differing PCH/settings. Defer build-graph restructuring until this visual validation checkpoint, and measure clean/incremental cost before claiming savings.

Replay identity audit follow-up: ManagedStateSerializer.EntityJsonConverter persists entity IDs without a world
address and resolves them against the restoring behaviour's current world. Therefore ordinary same-world entity
references already avoid persisted pointers; cross-world references need separate explicit coverage. The native
ManagedBehaviourCheckpoint.World field still persists the SceneState address. Replace that replay envelope
identity with a stable scene/session mapping, preserving duplicate-instance and missing-world rejection.

Replay scope audit: RuntimeReplayState retains only the primary SceneRuntimeSession. The scene, physics,
animation and presentation serializers capture that session, while the scripting serializer captures all
ScriptSystem instances. SceneRuntimeWorld can retain persistent sessions after unloading their source scene,
and a later load can create another session for that asset. Therefore asset ID alone is not a sufficient
checkpoint world key. The replay integration must capture the complete runtime-world session inventory,
including persistent sessions and stable instance identity, before claiming additive/travel replay parity.
Required regressions: fresh-process restore, two sessions with the same source asset/entity IDs, persistent
entities plus a reloaded scene, missing/duplicate world rejection before mutation, and rollback after restore
failure. The existing single-scene replay failure remains an immediate failing end-to-end gate.
Physics workload audit: the existing eight-origin fixture settles one dynamic box for 240 ticks and checks a
floor ray. It does not cover stack stability or CCD tunneling. PhysicsSystem maps Continuous to Jolt LinearCast,
but the inspected physics test files have no continuous-body regression. JointComponents provides authoring,
validation and serialization; searches of PhysicsSystem and SceneRuntime implementations found no Jolt constraint
creation/registration. Verify and implement the runtime joint path before counting authoring round-trip tests as
joint simulation evidence. Add baseline-versus-translated stack and thin-wall CCD scenarios in the next physics
batch; retain runtime joints, region transfers and long-session timing as separate acceptance requirements.
Precise contact validation completed: the 223-step DevDist KeireTests build succeeded
(Build/Validation/physics-precise-contact-native-build.log). The actual Coral managed-runtime reload regression
passed 1 case / 1,322 assertions, including all six collision/trigger phases with exact double contact points
(Build/Validation/physics-precise-contact-native-tests.log). The focused physics/query/transform/presentation/replay
selection passed 74 cases / 58,926 assertions (Build/Validation/physics-precise-contact-regressions.log). This
supersedes the earlier pending native contact status. It does not validate solver-generated contacts in an
automatically selected distant scene region, region transfers, stack/CCD stress, or planetary travel.
Far-origin workload validation: added an eight-box stack simulated for 1,200 fixed ticks at 60 Hz at each of the
eight origins, checking resting height, lateral drift, velocity, sleep and near-origin position parity. Added a
600 m/s CCD sphere striking a 10 cm wall, checking every one of 30 ticks for tunneling and residual velocity.
Both tests passed: 2 cases / 1,768 assertions (Build/Validation/physics-far-stack-ccd-tests.log). The incremental
build and final include-hygiene rebuild passed; the final binary repeated all 1,768 assertions successfully (physics-far-stack-ccd-final-tests.log). These tests exercise real solver workloads in fixed
regions, not automatic scene-region selection, moving frames, cross-region joints, soak/performance or travel.
Legacy scene-query origin integration: RayCast, CastCapsule and OverlapSphere now convert their float world
inputs into solver-local positions before synchronization; ray/capsule hits convert back to approximate float
world positions. Existing body/entity filtering remains intact. Overflowing direction/displacement squared
lengths are rejected before synchronization, with regression cases added to the query phase test. The native
incremental build is running (physics-legacy-query-origin-build.log); tests are pending. Nonzero automatic scene
origin selection remains disabled until animation grounding, VFX and managed precise-query integration agree.
Legacy scene-query validation passed: four-step DevDist build and 76 focused cases / 60,696 assertions
(physics-legacy-query-origin-build.log and physics-legacy-query-origin-tests.log). This includes the new stack/CCD
cases and overflow-before-mutation checks; it does not yet prove legacy query behavior with a nonzero scene origin.
Animation grounding migration started: animator/support matrices and pre-landing ray origins now subtract the
physics origin before float operations. Foot probes, retained points and support anchors share that solver frame.
This source batch is uncompiled; add distant-parent cancellation regression and review diagnostic coordinate
semantics before enabling nonzero scene regions. Frame changes still require cache transfer/invalidation.
Grounding regression added to FootSupportLifecycleTests: stabilize the real animator/physics fixture, reparent
character and support under a 1e12-metre transform with cancelling local translations, preserve their precise
world positions, then lift the support and verify the foot follows. DevDist build is active
(Build/Validation/physics-grounding-origin-build.log); no passing result yet. The inspected grounding diagnostic
positions are emitted as log fields from the same probe frame; no public debug overlay position was changed in
this file. A future region transfer must translate/invalidate retained plant and smoothing caches coherently.
Grounding batch validated: the four-step DevDist build succeeded (physics-grounding-origin-build.log). The new
runtime distant-parent/support-motion regression passed 1 case / 287 assertions (physics-grounding-origin-tests.log).
The complete selected foot-support lifecycle, grounding-space and animation-IK suites passed 66 cases / 148,199
assertions (physics-grounding-origin-regressions.log). This proves the compiled batch and near-origin behavior
with cancelling distant ancestry; an actual nonzero physics scene origin and moving-frame cache transfer remain
unverified. VFX audit confirmed QueryVfxCollision forwards VFX simulation-local endpoints to physics unchanged,
and returns solver-local hit points unchanged. Convert using the difference of simulation origins before adding
local offsets, avoiding an intermediate huge absolute position, and return hits in the VFX simulation frame.
VFX/physics origin conversion implemented in internal VfxPhysicsCollision helper and connected to the scene
collision callback. It subtracts simulation origins before adding local endpoints, raycasts in the physics
region and converts returned hits to the VFX frame; invalid and overflowing segments are rejected. Added real
solver regression with a 128 m origin difference and 1/64 m local offsets at zero, +/-1e12 and 1e15 origins,
plus miss/zero-length/overflow checks. Native build active (physics-vfx-origin-build.log); tests pending.
Scene-origin selection audit: InitializePhysics still calls CreateWorld() without an origin. SceneRuntimeWorld::Add
attaches every session to the spatial registry root frame; SceneRuntimeSpatialAccess exposes read-only frame
lookup and no per-scene origin-selection policy. Physics initializes before VFX in both Play and runtime
replacement, so VFX can adopt a selected physics origin at initialization. Selection must be deterministic and
validated against all enabled body placements; a camera-only origin cannot define simulation ownership. Region
migration must also handle retained animation contacts, VFX particles, queries, checkpoints and frame identity.
VFX/physics batch validation completed: DevDist build passed all 12 native actions after regeneration
(physics-vfx-origin-build.log). Scene VFX runtime and physics system selection passed 18 cases / 2,089 assertions
(physics-vfx-origin-tests.log), including the differing-origin real raycast. No rendered VFX or automatic
scene-origin selection result is implied. The fixed-region conversion boundary is now tested; scene selection,
region migration, frame rotation and replay identity remain integration requirements.
Initial scene-origin implementation: enabled collider/controller positions select a deterministic bounds center,
snapped to a 1024 m grid. Scenes within 8192 m of zero retain the zero origin. Physics body construction retains
its checked precision conversion, and VFX initialization adopts the selected physics origin. Added a normal
SceneRuntimeSession startup/falling-body/precise-ray fixture at 1e7, +/-1e12 and 1e15 positions. Build active
(physics-scene-origin-selection-build.log); tests pending. This is initial fixed-region selection, not dynamic
migration or multiple simultaneous regions. Widely dispersed scenes still require region partitioning; no
planetary travel claim follows from this step.
Initial scene-origin build passed. The first distant-scene regression executed all four distances; 46/48
assertions passed, with two exact post-collision X/Z comparisons failing at 1e7 (physics-scene-origin-selection-tests.log).
The fixture now checks exact precise placement before simulation and a 1 mm lateral drift tolerance afterward,
rather than requiring a resting dynamic body to remain bit-identical. Rebuild active
(physics-scene-origin-selection-rebuild.log); corrected test and broad compatibility selection remain pending.
Initial scene-origin validation passed after the fixture correction: the final rebuilt binary passed 44 physics,
query-phase and scene-VFX cases / 60,273 assertions (physics-scene-origin-selection-regressions.log), including
normal scene startup, dynamic falling bodies and precise queries at all four distant placements. Initial exact
placement and post-collision sub-millimetre lateral drift checks pass. This establishes initial fixed-region
scene integration, not character/grounding acceptance in a nonzero region, moving-region transfer, multiple
regions, planetary gravity or rendered surface-to-space travel.
Nonzero scene-region grounding validated: SupportFixture can now place its character/support before Play at a
precise offset. A new runtime test at (1e12, 0, -1e12) verifies foot grounding and following a raised support.
The character-controller carry fixture also runs in an actual distant region, preserving its local offset and
remaining grounded as its support moves. DevDist build passed; targeted tests passed 2 cases / 232 assertions
(physics-distant-grounding-tests.log). Full selected foot-support lifecycle and physics-component tests passed
53 cases / 197,593 assertions (physics-distant-grounding-regressions.log). These validate fixed-region runtime
behavior, not origin changes during motion, reference-frame rotation, planetary gravity or visual travel.
Managed precise-query batch started: ScriptSystem.h now has separate double-position ray/capsule/overlap and hit
value contracts, plus service hooks. Legacy structs and methods remain unchanged. Unsupported precise service
hooks fail explicitly rather than reporting an ordinary miss. These additions are uncompiled and not yet exposed
to C#: shared native query helpers, editor/player routing, versioned interop, managed validation and actual Coral
regressions must land together before a build/acceptance claim. Do not start a broad header rebuild mid-batch.
Precise managed-query native routing added: shared helpers invoke native precise ray/capsule/overlap APIs and
validate that the context entity belongs to the supplied runtime world. Ignored entity IDs must resolve in that
scene; layer filtering comes from the context. Overlap retains the existing 256-result limit. EditorWorkspaceLayer
and ManagedWorldRuntimeServices now route all three precise hooks by SessionForWorld into those shared helpers.
These edits are formatted but uncompiled. C# result/API definitions, versioned size-checked internal calls and
managed/native regression fixtures are still required before building this coordinated header batch.
Precise-query interop boundary added (uncompiled): PrecisePhysicsQueryV1Icall uses a 128-byte query and 56-byte hit,
with native static assertions and matching explicit C# field offsets. Native validates sizes/kind/trigger flag,
returns -1 for rejection, 0 for a miss and positive hit/result counts on success; overlap uses bounded two-pass
copying like the legacy API. Registration is wired. Public C# entry points, wrappers and layout/round-trip tests
are still pending, so no build or usability claim is made yet.
Public C# precise query APIs added in PhysicsPrecise.cs: TryRaycastPrecise, TryCapsuleCastPrecise,
OverlapSpherePrecise and RaycastHitPrecise. NativeRuntime wrappers use the versioned size-checked ABI, distinguish
misses from native rejection, guard unavailable calls and bound overlap allocation to 256 results. Managed
production API check is running (managed-precise-query-api-tests.log). Dedicated layout/stub round-trip and real
Coral tests remain pending, as does native compilation of this coordinated batch; do not claim usable support yet.
Initial public managed API/parity run passed (managed-precise-query-api-tests.log). Added managed unmanaged-call
stub coverage in PrecisePositionTests for the 128/56-byte layouts and key offsets, exact 1e15 fractional query/hit
coordinates, world/entity identity, ray/capsule/overlap results, miss/rejection separation, invalid coordinates,
overflowing directions, cross-world ignores and unavailable binding. The focused contract is included in the full
managed test runner now active (managed-precise-query-contract-tests.log). This stub validates managed marshaling
and API behavior only; actual Coral/native scene execution and editor/player builds remain pending.
Managed precise-query contract run passed, including the newly added unmanaged-call stub tests and design-time
parity (managed-precise-query-contract-tests.log; 4,838 managed and 860 editor API records). Added native helper
coverage to the real distant-scene fixture for precise ray/capsule/overlap hits, ignored-hit filtering, invalid world
and missing ignored-entity rejection. Native fixture remains uncompiled. Actual Coral API invocation is the next
required fixture before the coordinated native/editor/player build; managed stub success is not native proof.
Actual Coral regression wired into the managed reload/runtime fixture: all six contact callbacks now invoke precise
ray/capsule/overlap APIs; a scoped native service checks exact coordinates/context and returns known hits. Native
call counts and C# returned identity/coordinates are checked. The separate distant-scene fixture exercises real
solver helpers. Coordinated DevDist KeireTests build is active (managed-precise-query-native-build.log). No native
pass yet; editor/player builds and production SDK checks remain required after this first compilation.
Replay restore-order audit: ReplaySystem captures serializers from a name-sorted map and restores checkpoint entries
in stored order. runtime.scene calls ReplaceRuntime, which clears animation/VFX/physics and destroys/recreates the
SceneState and managed instances. Thus pointer identity can change even within one process during checkpoint
restore. On failure the generic rollback invokes serializers in reverse order, restoring scripting/physics before
replacing the scene last; that replacement can discard the dependent rollback state. A correct runtime replay fix
needs dependency-aware transactional world restoration (or one coordinated runtime-world serializer), not only a
pointer-to-scene-ID substitution. Add a failed-restore regression that verifies scene, scripts and physics together,
plus successful same-process rewind and fresh-process verification. Generic independent-serializer rollback tests
do not establish this dependent-world guarantee.
### Precise Managed Query Integration — Native Validation

The size-checked precise ray, capsule and sphere-overlap ABI now passes both native validation paths.
`managed-precise-query-scene-tests.log` reports 1 case / 92 assertions with actual distant scene solvers at
1e7, positive/negative 1e12, and 1e15 metres. `managed-precise-query-coral-tests.log` reports 1 case /
1,325 assertions through the real Coral host, including exact double query inputs and returned hit points.
The latter fixture uses a controlled runtime service; it complements rather than replaces solver validation.
Managed API/design-time parity also passes with 4,838 runtime and 860 editor API records.

Editor and standalone-player consumer builds are in progress. These results do not close dynamic region
transfers, runtime constraints, replay identity/rollback, or interactive planetary travel acceptance.
The support matrix now distinguishes joint authoring settings from pending solver integration. Documentation
source validation passed for 117 guides after correcting shader generator/importer cache versions to match code.

Follow-up physics regression run: `Build/Validation/managed-precise-query-physics-regressions.log`.
[doctest] run with "--help" for options
===============================================================================
[doctest] test cases:   40 |   40 passed | 0 failed | 1707 skipped
[doctest] assertions: 7046 | 7046 passed | 0 failed |
[doctest] Status: SUCCESS!

### Replay Recovery Audit — Additional Failure Propagation Gate

`SceneRuntimeSession::ReplaceRuntime` currently clears the old physics, animation, VFX and scene before
initializing the replacement. Its `Impl::Invoke` catches initialization failures and marks the session Faulted
without throwing. The standalone `runtime.scene` replay restore callback does not inspect that status, so the
replay coordinator can continue restoring dependent serializers after a failed replacement. Also, a session
already marked Faulted is not returned to its previous playing/paused state by that replacement path.

The coordinated recovery batch must cover replacement initialization failures, propagate failure to replay,
and restore both session lifecycle state and dependent state. Add a failure-injection test whose first restore
faults after scene replacement and whose rollback must recover a usable playing or paused session, with the
original physics and managed state intact. Existing replay rollback tests only restore independent integers;
they do not prove recovery when restoring a scene invalidates the other serializers' objects.

Runtime-world entries already separate a handle, asset ID, persistent roots and spatial attachment. Preserve
that distinction in checkpoint identity: two loaded instances of the same asset and retained persistent roots
must not collapse into one asset-based identity. Avoid persisting SceneState addresses or treating entity IDs
alone as unique across loaded worlds.

Editor and standalone-player DevDist consumer builds passed (managed-precise-query-client-build.log and
managed-precise-query-player-build.log). Added real-solver filter/trigger/ignored-entity coverage at four origins
and a 260-body overlap fixture checking the 256-result managed bound and uniqueness. These new tests are awaiting
the incremental native build; compilation success alone is not execution evidence.

### Runtime Replacement Failure Recovery — Implementation Pending Validation

The scene replacement path now reports initialization failure to its caller instead of returning successfully
with a faulted session. The session remembers its last playing/paused state when a callback faults; a subsequent
valid replacement restores that mode and clears the prior diagnostic. Physics, Awake/OnEnable, VFX and presentation
initialization use the same failure propagation path. Added tests inject repeated Awake failures, retry from both
playing and paused modes, verify that the old physics world closes and the replacement is usable, and recover a
session faulted during ordinary paused stepping. Native build/execution is pending.

This change does not make replacement atomic: once replacement starts, the previous scene and dependent systems
have been destroyed. Replay still needs coordinated state restoration, stable logical world identities and
correct dependency ordering. Do not treat successful retry as evidence of complete replay rollback.

The final far-range query filter/limit build and rerun passed: 2 tests / 360 assertions in
`Build/Validation/precise-query-filter-final-tests.log`.
Runtime replacement recovery native build and focused tests passed; see
`Build/Validation/runtime-replacement-recovery-tests.log`. Editor/player relinking and complete replay recovery
remain pending. This result covers replacement error propagation and retry, not transactional state rollback.


### Replay Restore Dependencies — Validation In Progress

Added explicit serializer prerequisites and deterministic restore ordering. Recovery restores the old checkpoint
in dependency order too, replacing the reverse-order rollback that erased dependent state. The player marks
scene animation, physics, presentation, VFX and scripting as dependent on scene replacement. Tests model scene
recreation clearing physics state and verify successful playback and rollback, plus missing/duplicate/cyclic
dependency rejection before captures run. Build is active; runtime identity and additive-world restore remain open.

### Managed Cross-World Reference Audit

`KeireManaged/ManagedStateSerializer.cs` stores an Entity reference as its entity ID only. Its reader creates that
reference in `s_restoreWorld`, which is the restoring behaviour's world. Component references reuse the same entity
converter. This preserves same-world references across recreation but loses cross-world ownership: additive scenes
with the same entity ID can silently retarget a reference to the behaviour's own scene instance.

The replay identity batch must carry logical scene-instance identity inside managed entity/component reference
payloads and remap it to the newly restored runtime instance. Keep ordinary same-world serialized data backward
compatible. Tests must use two instances of one scene asset with equal entity IDs, references in both directions,
a retained persistent root, and a fresh-process replay. Saving a raw world pointer or remapping only the outer
ManagedBehaviourCheckpoint does not solve the nested-reference case. SceneHandle runtime generations must remain
stale after replacement; persisted logical instance keys must not revive old runtime handles.
Replay dependency native build and focused execution passed; see
`Build/Validation/replay-restore-dependencies-tests.log`. The suite includes dependency-ordered playback/rollback,
invalid-graph rejection, existing replay integrity/budget checks, and scene replacement recovery. Standalone-player
consumer compilation is running; stable logical instance identities and additive recovery remain open.

### Runtime Scene Instance Identity — Integration In Progress

Added a distinct logical instance ID to each runtime-world entry. It survives SceneRuntimeSession replacement and
retirement into persistent roots, while source asset IDs and live handles retain their separate meanings. The
internal SceneRuntimeIdentityAccess captures the complete binding set and validates a complete restore binding
before mutation: nonzero unique identities, unique existing handles, owner thread, and no processing/simulation
callbacks. Duplicate adoption of the same runtime session is rejected to avoid double stepping one session under
two identities. Tests cover equal-asset adopted sessions, replacement, persistent retirement, invalid-set atomicity,
closed-world/thread access, and callback rejection. Build and execution are pending.

These identities are not yet encoded in replay or consumed by managed reference restoration. Full world checkpoint
reconstruction, lifecycle flags, nested cross-world references and fresh-process replay remain required. The ordinary
additive Load path deduplicates an already-loaded asset; the relevant repeated-asset cases include explicit adopted
instances and retained persistent roots followed by loading that source asset again.

The replay dependency standalone-player build passed (`replay-restore-dependencies-player-build.log`).
### Next Travel Batch: Translation Of A Live Physics Region

Source audit identifies the coordinated boundary for live translation: PhysicsWorld native body poses and cached
body definitions, ActiveContacts and queued Events, debug query traces, SceneRuntimeSession character support
ReferenceWorld/ResolvedRiderPosition, MovementWorld and local body definitions, animation grounding anchors and
cached model/support transforms. Retained debug snapshots already carry their own immutable origin. VFX may retain
a different origin only while its checked conversion remains valid; long travel still needs VFX origin migration.

Compute the old-origin minus new-origin delta before adding a local float position, so small local offsets at 1e15
are not rounded through an absolute double intermediate. Prevalidate every affected conversion before mutation.
Keep body IDs, velocities, sleeping state and contact pair identity stable. Do not change all body transforms and
call that complete without checking queued contact points, resting stacks, CCD and character/foot support continuity.
The installed Jolt PhysicsSystem headers expose body state setters and save/restore but no public ShiftOrigin method;
the engine must validate its own translation procedure against actual solver behavior.

This is the next movement integration batch after the active identity build. Multi-region ownership, rotated frames,
local gravity, streamed planetary geometry and seamless player travel remain separate required acceptance gates.
Runtime instance identity build and focused world/identity/spatial/persistence test selection passed; see
`Build/Validation/runtime-instance-identity-tests.log`. Replay encoding and script-reference remapping are still
not integrated. Next implementation batch is live physics-region translation and dependent-cache continuity.

### Far-Range Physics Acceptance Matrix

User-requested extensive physics validation is a required large-world travel gate, not an optional follow-up.
Compare identical local scenes at the origin and positive/negative planetary and astronomical offsets, including
1e7, 1e12 and 1e15 metres. Distinguish absolute double precision from local solver precision in every assertion.

- Resting stacks and sleeping bodies: drift, penetration, wake state and stability across repeated origin changes.
- Fast bodies and thin obstacles: continuous collision detection, impact response and tunnelling during travel.
- Contacts and triggers: stable body identity, correct points and no artificial Enter/Exit events from rebasing.
- Character grounding, moving platforms and animated foot support: continuity before, during and after rebasing.
- Ray, capsule and overlap queries: filters, ignored bodies, triggers and precise hit positions across boundaries.
- Spawn/despawn bursts and sustained simulation: bounded resources, invalid-input atomicity and measured frame costs.
- Ground-to-space-to-ground traversal: gravity/reference-frame transitions, collision continuity and return landing.

Fixed distant-origin tests already have recorded passing evidence above. New live-origin translation tests are
currently implemented but unvalidated; moving-region, planetary-gravity, workload and end-to-end travel gates remain
open. Record actual results and tolerances before declaring readiness; do not substitute a static far-away scene
for a moving-world test. Batch implementation and focused headless checks before the next editor test.

Live-origin batch now includes repeated translation of sleeping eight-body stacks at 0, 1e7, -1e12 and 1e15
metres, plus rebasing before character platform carry and animated foot-support movement. Native build is running
(`Build/Validation/physics-live-origin-build.log`); no passing execution result is claimed yet. Documentation
validation passed (`physics-live-origin-docs-tests.log`: 117 canonical guides, 10 navigation groups, 29 Mermaid
diagrams, 14 schema contracts, two graph-view contracts).

Next migration audit: VfxWorldSimulation stores emitter Position and world-space particle Position/PreviousPosition;
local-space particles must retain their local coordinates. VfxWorldSnapshots retains its own origin and serializes
that origin in checkpoints. GPU particles persist separately and SimulationRevision requests a reset, so changing
that revision alone would destroy particle continuity. A proper migration must preserve CPU/GPU history and handles,
translate world-space state, leave local-space state intact and test trails/collisions across the boundary.

### Live Physics Origin Translation — Focused Validation Passed

DevDist native build completed successfully (`physics-live-origin-build.log`). The new live-origin selection passed
five cases and 2,386 assertions (`physics-live-origin-tests.log`): local detail below absolute-double resolution,
conversion-failure atomicity, retained debug snapshots, queued and continuing trigger contacts, repeated sleeping
stack translations, character platform carry and animated foot support. The surrounding physics/grounding/replacement
selection passed 66 cases and 13,793 assertions (`physics-live-origin-regressions.log`); these selections overlap.

Remaining validation: CCD with translation during active flight, callback/thread rejection and unchanged-state
coverage, consumer builds and applicable sanitizer checks. Automatic origin policy, VFX migration, rotated gravity
frames, streaming, workload measurements and end-to-end planetary travel remain open. Passing this explicit
translation primitive does not establish those features.

### Live-Origin Flight And Lifecycle Edges — Passed

The final DevDist edge batch passed 68 cases and 14,244 assertions (`physics-live-origin-edge-final-tests.log`).
A projectile rebases every tick at origins 0, 1e7, -1e12 and 1e15 metres while flying into a thin wall. Its local
trajectory and velocity are compared with an identical non-rebased control, including the collision. Callback and
foreign-thread origin changes reject without changing the origin; safe-boundary rebasing and subsequent stepping
still work. Earlier stack/contact/support tests remain included in this selection.

The initial flight test incorrectly assumed a requested 600 m/s would be retained. The installed Jolt
BodyCreationSettings defaults mMaxLinearVelocity to 500 m/s; observed velocity was approximately 499.6 m/s after
one damped tick. The test now checks the real solver contract and control-world equivalence, rather than weakening
position precision. Planetary travel still needs an explicit velocity/reference-frame policy, since an unexamined
solver speed cap cannot serve as spacecraft travel behavior.

Both incremental builds completed; focused formatting and diff whitespace checks pass. Consumer builds and
sanitizer coverage remain pending. No editor travel test has been run for this batch.

### VFX Live-Origin Migration — Implementation Under Validation

Added VfxWorld::RebaseOrigin with preflight CPU emitter/world-particle current and previous positions, retaining
local particle coordinates, handles, ages and spawn sequencing. GPU world resources retain the last applied
simulation origin; world particles reuse CSTransform with identity rotation and origin delta, while local particles
use their existing old/new emitter transform. A new snapshot revision publishes migration without advancing the
simulation step or requesting a reset. Tests cover CPU world/local particles, history and immutable snapshots,
GPU emitter progression, invalid-input rejection and continued updates.

Build running: `Build/Validation/vfx-live-origin-build.log`. This batch is not yet validated. Actual GPU buffer
continuity, skipped snapshots, precision-failure atomicity, shared scene migration transaction and consumer builds
remain required. GPU per-particle precision cannot be preflighted from CPU emitter snapshots; document this honestly
and validate the GPU translation at supported local ranges before enabling automatic travel migration.

GPU migration validation audit: BuiltinVfx.hlsl CSTransform translates PositionAge.xyz and
PreviousPositionStrip.xyz and rotates velocity/acceleration. Identity rotations therefore preserve world-particle
velocity during origin-only translation. CSSimulate returns on DeltaSeconds <= 0 before replacing history or
advancing age, so a migration-only snapshot should preserve both. This is source evidence, not a GPU execution pass.
The existing KeireRenderTests/Source/VfxKillShapeRenderedOutputTests.cpp provides an offscreen view/readback harness.
Extend rendered coverage for CPU/GPU world/local effects, repeated and skipped origin snapshots, stable screen
position with a fixed absolute camera and unchanged spawn counts. Pixel checks establish rendered continuity;
particle-history/trail assertions need appropriate rendered trail coverage or direct buffer readback as well.

VFX migration DevDist build completed successfully. Focused migration/precise-transform/checkpoint-frame tests passed
4 cases / 169 assertions (`vfx-live-origin-tests.log`). The VFX regression selection passed 109 cases / 4,944
assertions (`vfx-live-origin-regressions.log`); selections overlap. These establish CPU behavior and GPU snapshot
contracts, not rendered GPU-buffer continuity. The next batch must add real GPU migration readback coverage,
precision-rejection atomicity and skipped-snapshot checks before automatic scene-wide migration is enabled.
The VfxWorldSpecification origin comment still says immutable and should be corrected with the next source batch.

### VFX Origin Migration — Direct3D 12 Render Readback Passed

Precision-rejection coverage passed two origin cases / 110 assertions (`vfx-origin-atomic-tests.log`), including
byte-identical checkpoints after an unrepresentable emitter translation on CPU and GPU backends.
The hidden Direct3D 12 render test passed one case / 68 assertions (`vfx-origin-render-d3d12-final-tests.log`).
It holds simulation time still at an origin of 1e12 metres, validates visible baseline particles, then compares pixels
after repeated shifts, two shifts without an intermediate renderer snapshot, and return to the initial origin.
Both World and Local simulation spaces are covered. This proves rendered position continuity on this backend;
trail/history buffer contents, Vulkan and coordinated scene-wide migration remain unverified.

The first run collected frames before asynchronous pipeline warmup completed and correctly failed its nonempty GPU
world assertion. The harness now waits for a rendered GPU world with a bounded timeout before collecting migration
frames. The rerun passed. Cold Direct3D 12 CSSimulate/CSSimulateOutput/CSSpawn creation took roughly 24–32 seconds per
stage in this log; cached creation in the second application was tens of milliseconds. Add cold compute-pipeline
startup to performance work; do not report this warm-cache result as solving startup latency.

Vulkan passed the same hidden migration render-readback case and 68 assertions
(`vfx-origin-render-vulkan-tests.log`). Direct3D 12 and Vulkan now have position-continuity evidence for World/Local
particles, skipped snapshots and return rebasing. This does not establish trail/history-buffer correctness or
long-duration workload performance.

Next scene integration: retain the existing physics/support-cache preflight and add a VFX prepare/commit boundary.
Prepare all VFX translations first, then execute the physics/cache transaction, then commit VFX using only already
allocated values. Do not call the two public mutators in sequence and attempt allocating rollback after a failure.
The scene entry point must enforce the owner thread and reject gameplay callbacks; test VFX failure before physics,
physics failure after VFX preparation, paused operation and continued simulation. Automatic origin policy follows
this shared transaction rather than directly calling subsystem rebases.

### Coordinated Scene-Origin Transaction — Build In Progress

Implemented SceneRuntimeSession::RebaseSimulationOrigin. VFX preflights all translated values before invoking the
existing physics/support-cache migration; after that succeeds, VFX commits using preallocated values. The private
VFX prerequisite hook is accessible only to the scene coordinator and must not mutate VFX. An already-aligned VFX
origin still invokes the prerequisite, allowing physics to catch up. Owner-thread, active-state and callback guards
run before preparation. Tests cover both precision-failure orders, unchanged VFX checkpoints/body positions,
paused success, independently misaligned physics, continued stepping and stopped/callback/thread rejection.

Build session is active with log `Build/Validation/scene-origin-transaction-build.log`; execution has not yet run.
This explicit transaction does not choose an origin automatically or implement rotating frames/local gravity.

### Newly Confirmed Precision Rejection Gap — Fix After Active Build

A numeric reproduction confirms that delta=2^60 plus local offset=0.015625 rounds back to delta in double precision.
The current migration helpers then see zero double-to-float narrowing error because 2^60 itself is representable.
The recovered local offset is zero, losing 0.015625 metres despite the default 0.001-metre budget. This affects
PhysicsOriginTranslation and the VFX preflight's delta-plus-position expression; nearby-origin tests do not cover it.
After the active transaction build finishes, validate recovered local displacement as well as narrowing error,
add 2^60 precision-rejection/unchanged-state regressions for both worlds and the scene transaction, and rerun the
focused migration suites. Do not claim arbitrary-magnitude rebasing meets the precision budget until fixed.

Precision-fix refinement: subtracting the delta back from the narrowed result is insufficient when the local value
itself dominates a small translation. Numeric checks also reproduce delta=0.5 plus local=2^60 losing 0.5 metres.
Use an error-free two-sum residual for the double addition, combine that residual with float narrowing error, and
validate the total against the budget. Cover both operand orders and a normal exact case (128 + 0.015625) in the
shared translation helper tests. Reuse the checked helper in physics and VFX rather than duplicating arithmetic.
No fast-math flags were found in the searched Scripts/Config/premake configuration; retain strict arithmetic for
this helper. Source changes are deferred until the active compilation is terminal.

### Scene Transaction And Addition Precision — Focused Tests Passed

The coordinated scene transaction build completed and its initial two cases / 60 assertions passed
(`scene-origin-transaction-tests.log`). The subsequently confirmed double-addition precision loss is now fixed in
shared RelativePositionTranslation: TwoSum retains the addition residual and combines it with float narrowing error.
Physics and VFX use this helper. Regressions cover 2^60 plus a tiny local offset, a tiny delta plus a huge local
position, exact nearby shifts, and unchanged physics/VFX/scene state after rejection.

The incremental precision build completed. The combined migration/physics/VFX selection passed 131 cases / 9,615
assertions (`origin-addition-precision-tests.log`). This validates the explicit transaction and resolves the recorded
addition-loss gap. Automatic focus/origin policy, multiple regions, rotating frames/gravity, trails, consumer builds,
sanitizer coverage and actual planetary travel acceptance remain open.

### Explicit-Focus Origin Selection Policy — Focused Validation Passed

Added internal SelectSimulationOrigin with a configurable positive threshold and grid (defaults 8192 m / 1024 m).
It accepts an explicit double focus, stays put within the threshold, and snaps only after crossing it. Remainder-based
snapping avoids division/multiplication overflow at large coordinates. Tests cover boundary jitter, negative travel,
distant focus and invalid policies. DevDist build and three policy/precision/scene-transaction cases passed 60
assertions (`simulation-origin-policy-tests.log`). This helper is not yet connected to automatic runtime travel.

The runtime must own a deliberate focus (player, camera or server region), rather than silently choosing an arbitrary
entity. Existing presentation camera selection uses priority with iteration-order ties. Automatic integration must
address focus ownership, deterministic ties, focus destruction/switching, safe boundaries and distant content;
initial collider-bounds centering alone cannot follow a traveler or represent an arbitrarily wide physics scene.

### Runtime Focus Following — Integration Build In Progress

SetSimulationFocus accepts an explicitly owned runtime entity and configurable threshold/grid. FixedUpdate and
paused Step check before gameplay and again before physics. Inactive focus suspends following; deletion, scene
replacement and Stop clear the weak entity handle. Foreign/edit-scene and stale handles reject. A failed rebase
faults the step with the Simulation origin diagnostic instead of proceeding with a partially migrated frame.
Tests cover movement during gameplay, inactive/reactivated focus, replacement/deletion and precision-failure state.

The first build was deliberately stopped after spotting const entity handles used by mutating test calls. Those
handles were corrected; replacement build is active in `simulation-focus-final-build.log`. No tests have executed
for this integration yet. Follow-up coverage must include setter callback/thread rejection, focus switching/clearing,
no-physics sessions and continuous travel. This is explicit focus following, not automatic player discovery,
partitioning, rotating gravity or complete planetary travel.

Focus integration build recovery: the deliberately stopped build left its project-command lock with owner PID 29060
and token 818644ea83174ef48a8bedc8f35dce3e. Confirmed that PID absent and lock ownership unchanged, then removed only
that lock's heartbeat/owner files and empty directory. The existing waiting build continued without restarting;
`simulation-focus-final-build.log` now shows active native compilation. Documentation validation passed
(`simulation-focus-docs-tests.log`). Focus runtime tests remain pending.

Runtime focus DevDist build completed. Four focused focus/transaction/guard cases passed 91 assertions
(`simulation-focus-tests.log`). The surrounding physics/VFX/grounding/stepping selection passed 178 cases and
19,236 assertions (`simulation-focus-regressions.log`); selections overlap. Runtime tests verify focus motion
inside FixedUpdate, inactive/reactivated behavior, foreign/edit-scene rejection, invalid-policy preservation,
replacement/deletion ownership and failed migration stopping the step without committing either origin.
Additional focus setter thread/callback, switch/clear, no-physics and long-travel coverage remains required, as do
consumer builds and visual editor/player acceptance. Explicit focus following is now exercised; automatic player
selection, distant-content partitioning and planetary reference-frame/gravity integration remain unfinished.

### Repeated Runtime Travel Validation

The focused simulation-focus selection passed four cases and 15,399 assertions
(`simulation-focus-travel-tests.log`). A dynamic rigid body travels more than 60 km outward and returns,
both near zero and at a 1e12 metre coordinate offset, over 2,560 fixed ticks per run. Checks cover repeated
origin migrations, matching physics/VFX origins, bounded local focus coordinates and return error below 0.1 m.
Additional checks cover callback/thread rejection without ownership changes, explicit focus switching/clearing,
and VFX-only sessions. This closes those focused coverage gaps; it does not validate collision-rich travel,
planetary streaming, rotating reference frames, local gravity or the complete editor/player journey.

The scene-runtime floor/wall travel case also passes (one case, 3,004 assertions;
`simulation-focus-contact-tests.log`, DevDist build `simulation-focus-contact-build.log`). At offsets zero and
1e12 m, a dynamic box remains supported by a floor and stops at a wall while automatic focus following performs
multiple origin shifts. Every tick verifies floor height, wall non-penetration and shared physics/VFX origin;
the final position proves the body actually reached the barrier. The test uses an 8 m threshold and 4 m grid to
exercise migration during a short collision-rich route. It does not cover streaming boundaries, rotating gravity,
character-controller transitions, joints or high-speed planetary re-entry.

### Region Gravity Integration In Progress

Native PhysicsWorld now exposes uniform gravity acceleration in simulation axes, with finite-value validation,
owner-thread access, closed-world rejection and wake-up of affected dynamic bodies when acceleration changes.
Translation rebasing leaves the acceleration unchanged. Tests cover sideways acceleration at a 1e12 m origin,
sleep preservation for unchanged gravity, gravity-disabled bodies, rebase preservation and access failures.
Build/test execution is pending. Custom gravity still needs scene/checkpoint persistence, managed/authoring
access, character local-up handling and radial/rotating-frame integration. SDK consumers and sanitizer validation
are required for the public API addition; no packaged release has been rebuilt.

Checkpoint integration audit: `SceneRuntimeSession::CapturePhysicsCheckpoint` returns bodies only, and
`RuntimeApplication.cpp` registers `runtime.scene.physics` version 2 with a bodies-only CBOR envelope. Custom
world gravity therefore cannot survive fresh-world replay restoration. Add world-level state even for an empty
body list, validate before mutation, restore gravity before sleeping body state, and version the replay contract.
Tests must include empty worlds, sleeping bodies, gravity-disabled bodies, invalid gravity rollback and fresh-world
restore. Simulation focus identity/policy also requires explicit restoration; do not infer it from body ordering.

The same audit found unchecked intermediate addition in `RestorePhysicsCheckpoint`: it computes origin delta plus
local position before `Math::RelativeFloat`. This can lose a fractional local offset before the narrowing check.
Use the existing checked relative-position translation helper and add a 2^60 origin/fractional-offset rejection
case proving that every body remains unchanged. These source changes are queued after the active region-gravity
native compilation to avoid changing inputs during the build.

Region gravity DevDist build completed (`region-gravity-build.log`). The focused test passed 18 assertions
(`region-gravity-tests.log`); the physics selection passed 20 cases / 4,624 assertions
(`region-gravity-regressions.log`). These overlap. Gravity/checkpoint integration and sanitizer/SDK checks remain open.
The checkpoint intermediate-addition fix now uses the shared checked translation helper, with a fractional-offset
rejection regression at 2^60 and unchanged-body assertions. Its incremental build/test is pending.

Checkpoint translation incremental build passed (`checkpoint-translation-build.log`). The checkpoint/gravity
selection passed two cases / 51 assertions (`checkpoint-translation-tests.log`), including the new rejection
case and unchanged-body checks. Formatting, scoped diff checks and documentation validation passed.

### World Gravity Checkpoint Implementation

Added ScenePhysicsWorldCheckpoint with optional gravity and bodies. Null gravity represents no physics world;
zero acceleration represents an actual zero-gravity world, including when empty. The new restore API rejects world
presence mismatches, validates all body conversions and gravity before mutation, applies gravity before sleep state,
and rejects callback/foreign-thread restore. Existing bodies-only APIs keep their destination-gravity semantics.
Runtime physics replay now encodes this state as serializer/schema 3. Existing strict version checks reject older
physics recordings; no migration is claimed. Tests cover empty/absent worlds, sleeping-body restore, invalid gravity,
invalid body data with changed gravity, foreign-thread rejection and stop/start fresh-world restore. Build and tests
are pending. Standalone replay, SDK/consumer and sanitizer acceptance remain open, as does focus persistence.

Validation boundary: the KeireTests project does not compile RuntimeApplication.cpp or RuntimeReplayCheckpoint.cpp.
Passing its new world-checkpoint tests will validate the native scene boundary only. Build KeireRuntime separately
before claiming the schema-3 registration compiles, and record/restore in fresh processes to verify the wire format.
The previous standalone record report contains 60 recorded ticks and one checkpoint; playback failed with
`Managed replay checkpoint is incompatible with the runtime behaviours`. Gravity persistence does not repair that
world-identity failure. Keep those acceptance gates separate and preserve both findings until independently verified.

World checkpoint DevDist build passed (`world-gravity-checkpoint-build.log`). Three focused cases / 71 assertions
passed (`world-gravity-checkpoint-tests.log`), including empty/absent worlds, sleeping bodies, rejected malformed
state, foreign-thread access and restoration after a new physics world is created. Player build is active in
`world-gravity-runtime-build.log`; this does not establish standalone replay acceptance or callback rejection coverage.

The player build also completed successfully (`world-gravity-runtime-build.log`), compiling the schema-3 registration.
The combined physics/focus regression selection passed 27 cases / 23,076 assertions
(`world-gravity-regressions.log`, overlapping the focused selections). Documentation validation and scoped diff checks
passed. Fresh-process replay, callback-guard tests, sanitizer/SDK checks and focus checkpoint persistence remain open.

Checkpoint callback guard now passes one case / 14 assertions (`checkpoint-callback-tests.log`, incremental build
`checkpoint-callback-build.log`). Both legacy body-only and world-level restore reject during FixedUpdate, preserving
gravity, body position and paused session state. Fresh-process replay, sanitizer/SDK checks and focus persistence remain open.

Planetary character audit: SceneRuntimePhysics.cpp uses displacement.Y for upward intent/support eligibility,
world-Y recovery and snap casts, normal.Y slope classification, and Y-only step-rise corrections. CharacterGrounding.h
also classifies walkability with normal.Y. Replace these as one consistent local-up contract across capsule orientation,
movement decomposition, step recovery, ground snapping, support carry and runtime ground state; changing only world
gravity would leave character behavior inconsistent. Validate rotated equivalents of the existing floor/step/platform
fixtures at distant origins, zero-gravity behavior and changing-up transitions before claiming planetary characters.

### Character Local-Up Implementation In Progress

Movement now derives up from the same decomposed world rotation used by the capsule query. World-space commands
are decomposed into tangent and vertical components. Support eligibility, recovery, step casts and rise limits,
snapping, slope classification and edge-normal recovery use this up vector. Grounding queries and fallback normals
also follow capsule orientation. Existing world-Y callers retain their orientation; this does not automatically
align actors to gravity. New tests rotate parented floor/player fixtures through identity, sideways, inverted,
X-rotated and compound rotations at zero and 1e12 m offsets, checking landing, walking, jumping and ground normals.
Build/test pending in `character-local-up-build.log`. Rotated steps/platform carry, changing-up transitions,
radial gravity, animation grounding and player/UI acceptance still need validation and integration.

Character local-up DevDist build passed after correcting a test CAPTURE macro invocation
(`character-local-up-final-build.log`). The rotated landing/walking/jump case passed 700 assertions across ten
orientation/origin combinations (`character-local-up-tests.log`). The broader character/grounding/support selection
passed 162 cases / 114,849 assertions (`character-local-up-regressions.log`); selections overlap. Formatting and
scoped diff checks passed. This validates rotated basic movement and existing support behavior, not rotated steps,
rotated moving platforms, changing-up transitions, automatic gravity alignment or planetary travel acceptance.

Rotated step/support validation now passes two cases / 149,140 assertions
(`character-rotated-support-tests.log`, build `character-rotated-support-build.log`). The existing step matrix now
includes world-Y/zero-origin plus sideways and compound-rotated frames at 1e12 m, preserving exact/lower/over-limit
step and blocked-ceiling expectations across speeds and travel directions. Idle carry covers sideways, inverted and
compound frames with script-moved static and kinematic supports at 1e12 m. All remain grounded during 240 ticks of
translation/yaw and retain support-local rider position within 0.002 m. These are fixed-up frames; changing up during
travel, radial gravity, rotated animation grounding and editor/player planetary acceptance remain required.
Formatting and scoped diff checks passed. No engine source changes were needed for these added cases.

### Changing-Up Support Validation

Fixed stale-support retention when capsule up changes: carry/step/snap eligibility and grounded grace now require
the previous ground normal to remain walkable under current capsule up. Without this check, a 90-degree turn could
retain grounded state from a surface no longer beneath the capsule. The focused changing-up test passes one case /
1,476 assertions (`character-changing-up-tests.log`, build `character-changing-up-build.log`): abrupt turn-away,
turn-back reacquisition, and a full 360-degree authored parent-frame rotation at zero and 1e12 m offsets.
This is authored coordinate-frame motion, not integration of centrifugal/Coriolis forces or a radial-gravity
controller. Broad character/support regressions are pending; actual planetary travel and rendered acceptance remain open.

The broad selection completed: 164 cases / 216,700 assertions passed (`character-changing-up-regressions.log`),
overlapping the focused tests. Formatting, scoped diff checks and documentation validation passed. No visual editor
or standalone planetary-travel acceptance is implied by these headless results.

### Radial Gravity Integration In Progress

PhysicsGravitySource defines a double world center, gravitational parameter (m^3/s^2) and positive radius.
Outside the radius, acceleration follows the inverse-square model described by
[NASA's gravity reference](https://science.nasa.gov/learn/basics-of-space-flight/chapter3-3/).
Inside, this implementation deliberately models a uniform-density sphere: acceleration decreases linearly to zero
at the center. This is an explicit model choice, not a claim to reproduce real planetary density profiles.
Up to 64 sources add to uniform world gravity; pure planetary setups must set uniform acceleration to zero.
Only active gravity-enabled dynamic bodies receive mass-scaled forces. Sleeping bodies are not awakened every tick;
changing sources wakes eligible bodies. Forces are validated for all affected bodies before submission, with reused
scratch capacity and no source loop when the list is empty. Source centers remain absolute through rebasing.

Scene checkpoints store sources and validate them before mutation; physics replay source schema is now 4 with strict
older-version rejection. Tests cover analytic exterior/interior samples, solver mass independence, rebased equivalence,
disabled gravity, invalid sources and checkpoint source restoration. Build/tests are pending. Full orbital integration,
multi-source performance, sleeping/contact stress, gravity-frame/character alignment, authored planetary geometry and
fresh-process replay remain acceptance gates. The force field is sampled once per external fixed step; no adaptive
orbital integration or per-internal-substep gravity evaluation is claimed.

Orbital solver audit: the installed Jolt BodyCreationSettings defaults to linear/angular damping 0.05 and a
maximum linear speed of 500 m/s. PhysicsWorld::CreateBody does not override these, and PhysicsBodyDefinition /
RigidBodyComponent expose no damping or speed-limit controls. Radial acceleration therefore does not establish
orbital fidelity: bodies lose energy and high velocities are clamped. Add validated authored/native controls,
preserve ordinary-project defaults, propagate through component registration/managed APIs and live synchronization,
and verify undamped/coasting behavior plus high-speed CCD before orbit acceptance. Use explicit orbital fixtures
with appropriate limits; do not silently disable safety limits for all existing projects. Source gravity is sampled
once per external step, so convergence across timestep sizes and large-delta behavior also remain required gates.

Live-body synchronization audit: SamePhysicsDefinition compares LinearVelocity and UseGravity, and a mismatch
recreates the native body. RuntimeAddRigidBodyForce implements managed Force/Acceleration/Impulse/VelocityChange
by updating the component velocity. Therefore gameplay force commands can recreate bodies each tick rather than
preserving native angular velocity, contact identity and support bindings. Solver writeback updates the cached
velocity, so ordinary untouched gravity motion is not itself a mismatch. Fix authored velocity/gravity updates in
place, with tests that record body IDs, angular velocity, contact continuity and gravity toggles. Keep shape/mass/
filter replacement semantics separate. Queue this source change after the active radial-gravity build.

Radial-gravity DevDist build completed (`radial-gravity-build.log`). The focused radial/world-checkpoint selection
passed four cases / 85 assertions (`radial-gravity-tests.log`), covering analytic falloff/interior values, mass-independent
solver acceleration, translation rebase equivalence, disabled-body exclusion and source checkpoint validation/restoration.
This is not orbit/soak/performance acceptance, and the schema-4 player build remains pending.

Live-body motion fix is now implemented: definition comparisons no longer recreate bodies for velocity/gravity flag
changes. Synchronization updates velocity using the retained native pose/angular velocity and applies gravity flags
in place. A repeated command/toggle test checks stable body ID, preserved spin and actual acceleration changes.
Build/test pending in `live-body-motion-build.log`; contact continuity and managed integration remain validation gates.

Live-body motion DevDist build passed. The combined authored-motion/radial/focus selection passed eight cases /
18,543 assertions (`live-body-motion-tests.log`), including repeated velocity/gravity changes with stable native body
identity and preserved spin. Wider physics/character/support regressions are running in `live-body-motion-regressions.log`.
Schema-4 player build is active in `radial-gravity-runtime-build.log`. Orbital and complete planetary-travel acceptance
remain open; these focused results must not be treated as an end-to-end travel pass.

The broad selection passed 180 cases / 220,649 assertions (`live-body-motion-regressions.log`, overlapping focused
coverage), and the schema-4 standalone player build completed (`radial-gravity-runtime-build.log`). Documentation
validation passed. Fresh-process replay, orbital accuracy and rendered planetary travel remain unverified.

Gravity composition validation (2026-10-29): the incremental DevDist test build passed
(`gravity-composition-build.log`). All four radial-gravity cases passed, 58 assertions
(`gravity-composition-tests.log`). New coverage checks opposing planetary sources cancel
while another source adds to uniform gravity, at both zero and 1e12-metre origins. An
unrepresentable force rejects the step before position/velocity changes; removing the
source permits the next step without leftover queued force. These checks do not establish
orbit convergence, collision safety at orbital speed, or full planetary travel.

Next movement-control batch: expose linear/angular damping and linear/angular speed limits
through native body definitions, serialized rigid-body authoring, and managed runtime and
design-time APIs. Preserve current backend defaults for legacy scenes. Validate finite
nonnegative damping and finite positive limits, update live bodies without losing spin or
identity, and test serialization defaults, rejected values, coasting and configured caps.
Keep existing managed call signatures compatible by adding a separate motion-settings
entry point. Only then use drag-free bodies for orbit convergence and travel tests; do not
mistake the current backend damping or 500 m/s default cap for planetary simulation results.

Native motion-settings implementation (2026-10-29, validation pending): PhysicsMotionSettings
adds damping and linear/angular speed limits to PhysicsBodyDefinition. Defaults preserve
existing backend settings. PhysicsWorld::SetMotionSettings updates the retained native body
under a write lock and clamps velocity when limits decrease, without changing its pose or
recreating it. Invalid/nonfinite settings are rejected before mutation. DevDist validation
is running in `motion-settings-build.log`; new tests exercise 1000 m/s drag-free coasting,
spin preservation, live caps, damping, rejected values and closed-world rejection. Inspector,
component serialization, managed access, replay restoration and SDK validation still need
integration. Do not present this native-only stage as a finished user workflow.

Motion-settings integration audit: the design-time assembly compiles the same
`KeireManaged/**/*.cs` sources (`Scripts/ManagedDesignTime/Keire.Managed.DesignTime.csproj`),
so new runtime API declarations should automatically reach IDE completion; validate with the
managed API/parity harness rather than introducing duplicate stubs. Rigid-body Inspector
fields come from CreateRigidBodyComponentRegistration property metadata. Add grouped motion
fields there and serialize them with missing-field defaults. Deserialize into validated
locals before assigning any component state (the current body deserializer writes fields
before its final validation). SceneRuntimePhysics must copy settings during definition
creation and update them in place before applying authored velocities, especially when
raising a speed cap and changing velocity in the same tick. Test that ordering explicitly.

Motion-settings replay audit: ScenePhysicsCheckpointBody currently stores pose, velocities,
sleep and origin only; runtime.scene.physics schema 4 encodes those same fields. It cannot
restore a native SetMotionSettings override. Before declaring motion controls complete,
capture installed settings, validate all restored settings before mutating any body, apply
limits before checkpoint velocities, and update runtime caches/components consistently so
the next synchronization cannot undo restoration. Test restore after lowering a cap and
changing drag, including invalid settings leaving all bodies unchanged. Coordinate the
serializer schema change with the standalone player and scene serializer; component
serialization alone does not cover native-only overrides.

Native motion validation completed: `motion-settings-build.log` exited successfully. The
focused motion/radial selection passed six cases / 97 assertions (`motion-settings-tests.log`),
including drag-free 1000 m/s travel, retained angular velocity, in-place speed caps, damping
and invalid-value rejection. Component/managed/replay integration remains pending.

Desktop QA on 2026-10-29: opened the existing DevDist KeireClient.exe (timestamp 00:07:16,
not rebuilt with the new motion settings) maximized in CloudIsolation20261026, CloudHdrShowcase.
Fog toggle and Height/Visibility section worked; fog visibly affected sky and geometry in
edit mode and Play Mode. Stopped simulation using Discard and Stop, and restored fog to its
initial disabled state. Left editor open in edit mode, Environment/Fog visible, FPS enabled.
Spot readings: one viewport without fog about 10–13 ms; with fog about 15 ms; Scene and Game
both visible with fog about 29–30 ms. Compilation was concurrent, so these are not controlled
performance benchmarks. Repeat after compilation with fixed camera/settings and one/two
viewport comparisons. Play Mode change review shows position Before/After as only 'Changed',
while rotation displays values: record as a usability issue for precise-position review.
No far-coordinate or planetary-travel acceptance was performed in this material showcase.

Far-coordinate desktop authoring smoke: in the existing DevDist editor, changed Showcase
Plinth Position X from 0 to 1000000000000.125 using the Inspector. The complete decimal,
including .125, remained displayed after focus left the field. With Scene tab focused, F
framed the distant plinth and rendered its geometry/gizmo/grid without a visible failure.
Ctrl+Z restored X=0 and cleared the scene dirty marker; F then returned to the near-origin
object. No scene save was performed. This validates one static authoring/frame/undo path,
not continuous planetary travel, collision behavior, persistence or fractional error bounds.
The unchanged Y field displays -0.80000001192092896 from its prior float-authored value;
track precision-aware numeric display UX without silently rounding stored doubles. F while
Hierarchy has focus did not frame; it worked after focusing Scene (matches current routing).
After compilation ended, stationary one-viewport fog-off reading was about 10 ms before
interaction, 11–13 ms during selection/editing; these remain spot readings, not percentile
performance measurements. Editor remains open, plinth selected, camera framed near origin.

Authored motion integration implemented (not yet rebuilt): RigidBodyComponent now exposes
MotionSettings with validated assignment, four grouped Inspector fields with units, and
schema-1 additive serialization with historical defaults for missing fields. Deserialization
now validates locals before assigning component state, so invalid settings/type errors cannot
partially change mass or motion. Scene runtime copies authored settings into body creation
and applies live changes before authored velocity, preserving the body. Validation helper is
shared between native world and component. Added serialization/legacy-default/atomic-rejection
test and extended live-body identity/spin test to raise speed caps and velocities together
above 500 m/s. Formatting and scoped diff checks pass. Batch remains uncompiled while C# and
checkpoint integration are prepared; previous six-case pass covers only the native stage.

Managed motion integration is now implemented in source: RigidBody.MotionSettings uses
RigidBodyMotionSettings (damping in inverse seconds; limits in m/s and rad/s). Default
preserves legacy values; space coasting can explicitly use new(0, 0, 20000, 100). Added
separate getter/setter internal calls without changing existing property-call signatures.
The managed bridge test passed, including world/entity identity, round trip, validation
before native submission and native rejection. Full managed API/design-time parity harness
is still running in motion-settings-managed-tests.log. Native component/bridge changes
remain unbuilt; checkpoint integration is next before the combined native build.

Combined motion batch: managed API suite and design-time parity passed (4865 core and 860
editor public/protected API records; motion-settings-managed-tests.log). Checkpoints now
capture installed native motion settings (including direct native overrides), validate them
before mutation, restore limits before velocity and synchronize component/cache settings.
Legacy native callers omitting the optional settings preserve the destination configuration.
Player serializer advances to schema 5, with required four-scalar motion settings per body.
Added restoration/invalid-checkpoint regression. Combined native build is active in
motion-settings-integration-build.log; player/SDK builds and editor UI acceptance remain
pending. Do not edit native source while this compilation is active.

Orbital acceptance audit while integration compiles: radial force is sampled once before
Native.Update, which receives ceil(dt*60) collision steps; those internal steps do not
resample the source field. Compare fixed timestep sizes with drag disabled and caps above
the orbit speed, measure radius/energy/phase drift over multiple revolutions, and compare
near-origin versus translated/rebased trajectories. Also test slow free-flight under weak
radial acceleration: backend AllowSleeping defaults true and sleeping bodies skip radial
force. This is a test hypothesis, not a confirmed defect; resting-contact sleep behavior
must remain efficient if a free-flight sleeping policy is added.

User-authored far-physics fixture (2026-10-29): user reports successful Play Mode collisions
in FarTestWithPhysics in the current CloudHdrShowcase scene. Read-only inspection of the
saved scene confirms parent ID 4d69aab1-2bf9-42fd-b26e-d61e1bd137f8 at X=10,000,000,000 m,
identity rotation/unit scale. Showcase Plinth Copy has an enabled box collider, local
position (0,-0.800000011920929,2), scale (20,0.3499999940395355,15). Studio Paint Copy has
an enabled box collider plus dynamic mass-1 gravity-enabled rigid body, zero initial
velocity, continuous collision disabled; local position approximately (1.02377,6.06636,2.62108)
and scale 1.55. Preserve these user-authored entities and the current scene. This is a
user-reported real-editor success corroborated by saved configuration, not an independently
measured stability/soak or continuous planetary-travel pass. Next checks: resting height,
repeat Play/Stop, contact stability and controlled origin shifts without changing this baseline.

Project default gravity authoring (2026-10-29, requested; implementation pending):
The Physics tab exposes collision layers/matrix but ProjectAuthoringSettings has no gravity
field. PhysicsWorld supports SetGravity/Gravity; PhysicsSystemSpecification and CreateWorld
currently provide no project default. Add finite XYZ acceleration in m/s², preserving
(0,-9.81,0) for existing projects. Carry it through authoring serialization, editor project
load/settings application, player manifest export/import and new physics worlds. Follow
existing settings lifecycle rules; do not silently overwrite per-world radial sources or
checkpoint-restored gravity. Include zero gravity for space projects, non-Y gravity,
legacy settings fallback, invalid-value rejection, save/reload, player parity and
UseGravity=false coverage. UI acceptance uses the user's CloudHdrShowcase fixture without
altering its authored entities. Batch implementation after the active native build finishes.
Gravity integration audit: player physics JSON is emitted in AssetTool/Source/Main.cpp,
not the build scripts, and parsed in KeireRuntime/Source/RuntimeApplication.cpp. Editor
startup applies settings in EditorWorkspaceLayer.cpp; settings application is in
EditorWorkspaceInspector.cpp. ProjectSettingsPanel currently gates service application on
a frame-local matrixChanged flag. A numeric gravity edit must retain pending application
through deactivation/tab changes and save failures; do not reuse that transient flag as
its only trigger. PhysicsSystem collision matrix rejects active worlds, so distinguish
project-default updates for future worlds from intentional live PhysicsWorld overrides.

Motion-settings integration validation (2026-10-29): DevDist KeireTests build completed successfully (motion-settings-integration-build.log). Focused motion-settings plus authored velocity/gravity selection passed 5 cases / 165 assertions, including checkpoint restoration and component serialization (motion-settings-integration-tests.log). Editor/player targets and UI acceptance remain pending; project default gravity remains unimplemented.

Project gravity implementation checkpoint (2026-10-29): added PhysicsGravity authoring persistence with legacy fallback and finite validation; PhysicsSystem default gravity initializes new worlds without mutating existing overrides. Editor startup, explicit/automatic settings saves and Play entry apply defaults; Audio / Physics UI exposes XYZ, Earth and Zero presets. AssetTool exports gravity and runtime consumes it. Added settings round-trip/invalid/legacy and distant-body/default-lifecycle tests. Build session runs KeireTests, KeireClient, KeireRuntime and AssetTool sequentially; project-gravity-<target>-build.log files are the evidence. Tests and UI acceptance are pending. Preserve CloudHdrShowcase and FarTestWithPhysics. User closed editor and authorized reopening the new build when ready.

Gravity export follow-up found during compilation: emitted runtime manifest still uses EnvironmentRuntimeManifest schema 10. Older players can accept that schema and ignore gravity. Before acceptance, advance the manifest contract with compatibility tests so old players reject gravity-aware exports rather than silently simulating Earth gravity. Preserve explicit legacy import fallback for older manifests. Current build may finish, but this follow-up must be applied before claiming player parity.

Gravity native validation (2026-10-29): KeireTests DevDist build passed. project-gravity-focused-tests.log reports 10 cases / 194 assertions passed, covering project gravity, authoring settings and motion settings/checkpoints. Editor build is active; player/export builds, manifest-version follow-up and real-editor acceptance are still pending.

Gravity regression follow-up: project-gravity-physics-regression.log passed 30 cases / 4,879 assertions using the physics* and project collision matrix* selection. This covers the existing distant stacks, continuous collisions and origin-shift physics tests, not orbital travel or UI acceptance. Editor compilation remains active.

Gravity integration checkpoint (2026-10-29): the follow-up DevDist build completed successfully for
KeireRuntime, KeireAssetTool and KeireTests (gravity-manifest-<target>-build.log). The player replay motion
decode compilation defect is corrected. Runtime manifest schema is now 11; gravity is required for schema 11,
with Earth gravity fallback retained for older manifests. The focused manifest/project-gravity/authoring/motion
selection passed 41 cases and 595 assertions (gravity-manifest-focused-tests.log). These checks do not prove
an exported player consumes custom gravity: actual export/player parity and real-editor control acceptance
remain outstanding, followed by orbital convergence and continuous ground-space-ground validation.
The current editor process was observed running after the successful editor build; process presence is not
UI acceptance. Repository hygiene inspection also found an already-staged vc140.pdb generated artifact;
it must be excluded from any eventual commit, without changing the index during this uncommitted work pass.

Player parser validation (2026-10-29): ran the actual rebuilt KeireRuntime headlessly against
isolated schema-11 manifest fixtures under Build/Validation/GravityManifestParser. Missing gravity
and a two-element gravity array exit 2 with the expected gravity-specific diagnostics; a string
acceleration exits 1 with a JSON type diagnostic. Initial fixture attempts failed earlier environment
validation and were not counted. Corrected fixtures retain cooked numeric rendering enums and a
current fog record. This proves rejection at the player boundary only, not custom-gravity simulation
or export parity. Wrong-type diagnostics could be improved to name the gravity setting explicitly.

Gravity UI preset acceptance (2026-10-29): in the maximized DevDist editor, opened Edit > Project Settings > Audio / Physics. XYZ fields displayed (0,-9.81,0). Zero Gravity displayed and persisted (0,0,0); Earth Gravity restored the displayed and saved Earth default. Settings reported all saved. Read-back confirms unrelated authoring settings unchanged and CloudHdrShowcase scene SHA256 remains 4B1E0527BEA32379D3D86EBDED39DBACF2D503551B291741B7CA6C5E2D5366FF. Closed the settings panel. Numeric edits, Undo/Redo, next-Play behavior and player parity remain to validate; preset persistence alone is not full acceptance.

FarTestWithPhysics UI follow-up (2026-10-29): real-editor Play observation found the copied
Studio Paint body retains enabled ShowcaseOrbit (component 73616e64-626f-4078-8000-000000000060).
Its script writes LocalRotation and LocalPosition every Update (Assets/Scripts/Runtime/Examples/ShowcaseOrbit.cs,
lines 37-38). Inspector samples remained near initial Y=6.06636 (6.13838 then 5.99748) while rotating.
Therefore this authored fixture does not isolate gravity/free-fall or resting-contact behavior; do not count
it as an independent gravity acceptance pass. Preserve the user's scene and add a separate controlled
fixture without transform-writing animation for subsequent Earth/zero-gravity and distant-contact tests.
Stopped Play with Discard and Stop to retain the authored baseline.

Controlled far-gravity fixture (2026-10-29): created Assets/GravityAcceptanceQA.keirescene in the
QA project by copying the saved showcase and disabling only the copied body's ShowcaseOrbit.
Editor imported it as asset 200d16bc-6971-4c20-82e0-fb405a17c25a. Real-editor Play showed the body
falling from Y=6.0663646 to Y=0.1499963 at parent X=1e10. Expected rest center is approximately
-0.8 + 0.35/2 + 1.55/2 = 0.15 m. This is a controlled Earth-gravity contact observation, not an
orbital, zero-gravity, long-soak, or continuous travel acceptance result. Original showcase is preserved.

Orbital validation implementation (2026-10-29, pending execution): added a three-revolution
circular-orbit test at radius 100 m, mu=10000 m^3/s^2, initial speed 10 m/s and zero damping.
Covers origins 0 and 1e12 m, stationary origins and alternating rebases every simulated second,
60/120 Hz integration. Measures maximum relative radius/energy errors and final phase; requires
smaller-step radius-error convergence. Only KeireTests is being built. This is a solver validation
fixture, not proof of Earth-scale geometry, atmospheric transitions or player travel integration.

Orbital convergence result (2026-10-29): orbital-convergence-tests.log passed one case / 271460
assertions across eight configurations. Three revolutions at 60 and 120 Hz, origins 0/1e12 and
stationary/alternating rebases satisfied relative radius error <0.002, relative energy error <0.0001,
final phase error <0.01 rad, and at least 25% radius-error reduction at 120 Hz. Actual maxima were
not printed on success, so these are verified bounds, not exact measured values. Added an explicit
<algorithm> include afterward; the subsequent weak-field build includes that header cleanup.
Weak-field regression now checks an unsupported body at radius 10 m, mu=0.001 over 10 simulated
seconds, with zero damping. This targets unintended free-flight sleep; result pending.

Confirmed weak-field defect (2026-10-29): weak-gravity-build.log succeeds; weak-gravity-tests.log
fails three assertions. After 600 steps at 60 Hz the unsupported body is sleeping, velocity X=0,
position X=10 rather than continuing weak radial acceleration. PhysicsWorld::Step skips radial
forces for inactive bodies, and backend sleep can activate in this free-flight case. Fix must prevent
unsupported bodies under nonzero gravity from sleeping, without repeatedly waking grounded stacks,
without treating trigger overlaps as support, and without weakening force-overflow transactional checks.
The new regression intentionally remains failing until that implementation is verified. Batch the
sleep-policy fix and its support/zero-field regressions before the next native build.

Weak-field implementation checkpoint (2026-10-29): radial force preflight now includes sleeping
bodies. After all forces validate, nonzero-acceleration bodies without opposing solid contact are
activated and have the sleep timer reset. Trigger contacts do not count as support. Added grounded
sleep and trigger-overlap regressions alongside weak free-flight and existing overflow checks.
Build/test session is active; do not claim the defect fixed until these pass. Current support lookup
scans contacts per gravity body; assess batching this into a single contact pass before broad workloads.
The policy currently runs when radial sources exist; weak uniform-only gravity needs separate coverage.

Support-policy follow-up (2026-10-29): first implementation passed free-flight, trigger and overflow
checks but failed grounded sleep (42/43 assertions). Inspection found QueryContact negated Jolt's
penetration axis while ContactManifold used that same direction without negation. Corrected fallback
normals to the body-2 separation convention; added a sleeping-contact normal assertion. Expanded
sleep protection to uniform-only gravity and replaced per-body contact scans with one contact pass
using the existing body map and reusable per-body scratch fields. Native build and targeted regressions
are active in gravity-support-batch logs; all results remain pending for this revised implementation.

Gravity-support batch passed 5 cases / 2166 assertions (gravity-support-batch-tests.log), including weak uniform/radial acceleration, grounded sleeping, trigger exclusion, sleeping normal direction, overflow rejection and sleeping-stack origin translations. Broader physics/radial regression is running, including orbital convergence. Editor/player binaries have not yet been relinked with this native fix.

Gravity support regression passed 37 cases / 276416 assertions (gravity-support-regression-tests.log), including the full orbital case and physics selection. These overlap prior counts. Formatter and diff checks pass. Updated native library is validated; player relink follows, editor still uses its previous binary. No planetary visual or standalone acceptance is implied.

Minimal standalone gravity export (2026-10-29): created Build/Validation/GravityPlayerQA with
only the distant parent and dynamic collider body, no mesh/material/managed components, and project
gravity (6,0,0). KeireAssetTool cook succeeds (one asset/one pack); emitted manifest schema 11
contains that exact gravity. Rebuilt KeireRuntime recorded 120 ticks and one checkpoint successfully
(record.json). A fresh-process strict --verify failed with canonical state hash divergence at tick 1
(verify.json), not the former managed-checkpoint incompatibility. This script-free reproduction proves
that replay needs additional native/runtime-state investigation; it does not establish custom-gravity
trajectory parity. Keep the fixture and logs for diagnosis. Original QA project was not changed.

Standalone replay isolation (2026-10-29): a second independent 120-tick recording succeeds.
Validated replay chunk boundaries and uncompressed SHA-256 digests: both initial checkpoint digests
are identical (b773aeedda9e1bf758ffa6de082993ce815315bc1efe47dbf61c1007b3fb63e9), but none
of the 120 recorded canonical state hashes match. Evidence: GravityPlayerQA/recording-comparison.json
and repeat.keirereplay under Build/Validation. This rules out checkpoint restoration as the sole cause;
investigate fixed-step versus presentation-frame state before changing replay validation. No new build
was needed. Planetary travel acceptance remains pending.

Replay subsystem isolation: two independent 300-tick recordings (long-a/long-b.keirereplay)
complete successfully. Python 3.14 stdlib compression.zstd decoded the checkpoint chunks; lengths
and SHA-256 digests were verified before comparing every serializer. All five serializers match at
tick zero. At tick 300, runtime.scene, animation, physics, and presentation match byte-for-byte;
only runtime.scene.vfx differs (105-byte payloads, no emitters in this fixture). The runtime currently
calls SynchronizeVfx(deltaSeconds) from AdvanceFrame even with DeterministicSimulation enabled.
Investigate and regression-test fixed-tick VFX advancement for strict sessions; do not remove VFX
from verification. Matching physics at tick 300 is useful evidence, not full planetary acceptance.

Strict VFX timing fix implemented: deterministic SceneRuntimeSession advances VFX on FixedUpdate
and paused Step; render-frame AdvanceFrame retains VFX updates only for ordinary sessions. Added
an empty-world and active-effect regression comparing 60 fixed ticks with different render cadences,
plus paused-step equivalence. Formatting and scoped diff checks passed. Targeted DevDist KeireTests
build started (vfx-fixed-timing-build.log); tests and fresh standalone replay still pending. No shared
header changes were needed for this correction.

Strict replay timing acceptance: targeted DevDist test build compiled two files and linked; 9 selected
scene VFX cases / 422 assertions pass (vfx-fixed-regression-tests.log). Player relink completed.
A fresh 300-tick custom-gravity recording and separate-process strict verification both exit zero;
fixed-verify.json reports completed, currentTick 300, tickCount 300, checkpointCount 2. This closes
the minimal native gravity replay divergence; managed replay and planetary journeys remain separate
acceptance requirements. VFX timing contract and changelog updated.

Distant environment report (2026-10-29): user screenshot of GravityAcceptanceQA shows distant
Scene view and near-origin Game view with substantially different clouds/lighting. User closed the
editor and authorized reopening the new build for comparison. Preserve the user's scene edits.
Source-confirmed constraints: CloudObserverWithinLocalDomain rejects any anchor-relative component
above 1e9 metres; CaptureCloudVolume returns disabled uniforms outside that domain. EvaluateCloudPhase
and MakeVolumetricCloudUniforms independently enforce 1e9. FarTestWithPhysics at X=1e10 exceeds it.
Atmosphere uniforms subtract the configured planet centre and EnvironmentAnchor from camera origin;
no editor assignment to EnvironmentAnchor was found in the audited source paths. This is not evidence
that simply increasing the cutoff fixes sky, solar lighting or planetary placement.
Next integration requirements: persistent environment/world anchoring, consistent cloud/fog/atmosphere
frame selection in Scene and Game cameras, periodic phase precision across origin shifts, and a
near-origin versus translated-world comparison at identical local altitude/orientation/time. Also test
physical travel away from a planet separately: leaving its atmosphere must not drag the planet or clouds
with the camera. Native radial-source and simulation-focus authoring remain missing in the inspected
editor/managed paths; implement through shared runtime configuration rather than a test-only override.

Distant planar cloud correction implemented: horizontal periodic coordinates now accept +/-1e12 m,
where double spacing remains sub-millimetre; vertical displacement retains the existing +/-1e9 m bound.
One shared coordinate policy drives layered phase evaluation, packet domain checks and volumetric
packing. Added positive/negative 1e10 and near-1e12 comparisons against independently reduced local
periods, including fractional offsets and vertical rejection. This preserves stable world patterns;
it does not move the environment anchor with the camera or implement planetary cloud shells.
DevDist KeireTests build is active (distant-cloud-domain-build.log); regression and visual validation
remain pending. Atmosphere placement and day/night lighting still need separate integration.

QA atmosphere configuration verified read-only: Rendering.keiresettings has atmosphere.enabled=true,
planetCenter=(0,-6360000,0), radius=6360000 m, height=100000 m. The copied showcase at X=1e10
is physically outside that configured spherical atmosphere; identical sky to the origin is not an
appropriate expected result unless the entire environment is translated with the test. Initial time
is day 1234567890123456789, fraction .375, paused, rate -.5, day length 2400 seconds. Do not overwrite
these user settings. Separate acceptance scenes should compare (a) equally translated surface,
atmosphere and gravity with identical local camera coordinates, and (b) actual departure from the
fixed planet, where atmosphere disappearance is expected. This corrects the earlier broad inference
that every visible difference in the screenshot was necessarily a rendering error.


Distant planar cloud validation completed: DevDist KeireTests build exited zero (71 build steps,
including regeneration after the added internal header). Cloud/atmosphere selection passes 127 cases /
177943 assertions in distant-cloud-domain-tests.log. New positive/negative distant phase tests and
existing invalid-domain tests passed. Formatting and scoped staged/unstaged whitespace checks pass.
This evidence covers CPU evaluation and uniform packing, not rendered distant worlds or planetary
shell clouds. Editor has not yet been relinked or reopened for this change.

Editor visual checkpoint: KeireClient DevDist build exited zero after two compile/link steps, launched
PID 4464 with the existing QA project maximized. Fresh Windows capture shows GravityAcceptanceQA
(29 entities), distant Scene view with clouds now present above FarTestWithPhysics, and the original
near-origin Game view. Pink background and different environment appearance remain; this confirms
cloud presence only, not matching planetary lighting or travel acceptance. Startup assets were still
refreshing in the capture, so the displayed frame time is not a performance measurement. No UI edits
were made, and existing user scene/settings were preserved.

User follow-up confirms distant clouds look improved, but void appearance is unacceptable; include
void/horizon/lighting/atmosphere together in the next batch. Source finding: BuiltinSky.hlsl applies
SkyHorizonWeight whenever the observer is outside the opaque planet and its ray does not hit ground.
There is no outer-atmosphere/altitude attenuation, so an authored surface sky gradient paints empty
space pink even when the atmosphere transport has no intersection. Correct the surface-gradient to
space transition without altering solar-disc radiance or forcing the planet to follow the camera.
The beige near-origin lower hemisphere comes from AtmosphereGroundRadiance's synthetic reference
sphere, driven by GroundReflectance and GroundIntensity; distinguish its artistic controls from
space background. Recheck below/at/above atmosphere, limb rays, translated planets and night.
Desktop inspection was deferred when the user was actively taking screenshots; no UI input was sent.


Planet-relative sky transition implemented: shared shader kernel smoothly attenuates only authored
sky-gradient weight from planet surface to outer atmosphere. BuiltinSky uses the already planet-relative
ObserverRadius, preserving physical scattering and separate sun/stars. Added the actual shared-kernel
test plus CPU uniform integration comparing translated camera/planet pairs at 0, +/-1e10 and +/-1e12 m,
with surface, 2 km, 50 km, 100 km and 400 km altitude. Surface weight must remain one at every global
placement; above-atmosphere weight must be zero. Test build active (sky-space-gradient-build.log).
This does not supply authored multi-planet ownership or spherical cloud shells; those remain required.

Build latency finding verified in both Windows and Unix builtin-shaders scripts: one aggregate fingerprint
contains every rendering source/include. Any sky-gradient include change causes all source/variant pairs
to invoke the shader compiler before rebuilding the combined header. This explains additional shader-only
iteration latency beyond native shared-header fan-out. Next build-efficiency batch should cache variants
per source plus recursively resolved includes/compiler/options, retain output-integrity checking and
atomic aggregate publication, and cover Windows/Unix warm cache, touched-independent include, transitive
include mutation, missing/corrupt output and failed-compiler recovery. Do not edit generation scripts
while the current build is using them. Full aggregate regeneration remains appropriate when compiler
or output ABI changes; never reuse stale bytecode to shorten a build.


Sky-gradient validation: shader generation and DevDist KeireTests build completed successfully
(sky-space-gradient-build.log, 11 native steps after shader generation). The selected actual-kernel,
planet-translation and atmosphere regressions pass 25 cases / 684 assertions in
sky-space-gradient-tests.log. At equal local altitude, translated planets/cameras preserve the gradient;
outside the planet's atmosphere its artistic gradient contribution is zero. Formatting and docs validation
passed. The open editor still uses the preceding cloud-domain build; rendered transition acceptance,
reference-ground appearance and full planetary environment ownership remain pending.

Authored placement audit correction: the atmosphere alone already persists a double PlanetCenter in
Rendering.keiresettings (AtmosphereRecord schema 3) and exposes XYZ controls bounded to +/-1e15 m.
The missing authoring integration is a shared planet/environment placement and ownership contract,
not absence of all atmosphere placement controls. RenderCamera.EnvironmentAnchor exists but no editor
assignment was found; cloud/fog records are independently positioned. Preserve existing atmosphere
centres when adding shared placement, migrate legacy zero-anchor records without moving their worlds,
and do not silently reinterpret existing centres as relative offsets. Reset Earth-like Atmosphere
currently resets centre too; any new planet-owned reset must preserve explicit planet placement.

Distant-planet authoring safeguard implemented: Reset Atmosphere Appearance preserves centre, radius,
atmosphere thickness and enabled state; resets only optical/appearance/quality values and clamps default
density/absorption heights to the preserved thickness. Replaces misleading Earth-like reset that moved
all planets back to the origin. Added editor regression for enabled/disabled distant planets with 100 m
and 100 km atmospheres. KeireEditorTests DevDist build completed successfully (229 steps;
atmosphere-placement-reset-build.log). All five selected atmosphere cases passed, with 89 assertions
(atmosphere-placement-reset-tests.log). Formatting checks passed; interactive UI acceptance is pending.
Ground/void help now explicitly
states it does not color the space background. Shared planet ownership remains a separate required task.

Appearance-reset persistence follow-up: six atmosphere editor cases / 102 assertions pass after a two-step
incremental build (atmosphere-reset-history-build.log, atmosphere-reset-history-tests.log). The added case
exercises the panel's Update/CommitEdit sequence, Undo/Redo and save/reload, preserving distant placement
and unrelated cloud/star values. Its first run omitted CommitEdit and failed; correcting the fixture to
match the panel resolved that test error. This is document-lifecycle coverage, not interactive UI acceptance.

Interactive checkpoint: rebuilt KeireClient and tested maximized GravityAcceptanceQA. The initial link was
blocked by the old running executable (LNK1104); after graceful closure, the one-step relink succeeded.
At the saved distant Scene camera, the revised gradient no longer colors space, while the near-origin
Game view retains its sky. Flat clouds still extend into the distant space view: planetary integration
remains incomplete. Temporarily entering planet-centre X = 10000000000 restored the ground-level sky and
reference ground at the far sample. Reset Atmosphere Appearance retained that X, radius 6360000 and
thickness 100000. Undo reset, Undo placement and Save Environment restored the original rendering file
byte-for-byte; GravityAcceptanceQA scene hash also remained unchanged (baseline hashes in
sky-gradient-reset-qa-baseline.json). This verifies the reset interaction and a translated-atmosphere
diagnostic, not shared environment ownership, planetary cloud shells or end-to-end travel.

Radial fog batch in progress (not built or exposed in UI): FogSettings now has an explicit PlanetaryHeight
mode. The native density evaluator accepts the existing atmosphere planet descriptor, subtracts its
centre before measuring radial altitude, and retains the capped density below the base. Fog records
preserve this mode; missing fields migrate to flat height and non-boolean values are rejected. Added
opposite-axis surface tests at zero and +/-1e12 translations and record migration tests. Formatting and
diff checks pass, but execution is pending. Finish GPU view packing, radial segment integration, uniform
ABI compatibility, editor controls and export/round-trip coverage before building the complete batch.
Do not treat the new native setting as a functioning rendered feature yet. Local boxes remain in the
selected environment frame; rotating planet ownership remains required separately.

Radial fog GPU draft (still unbuilt): view preparation computes observer altitude in double precision
using the existing atmosphere descriptor; Controls.x = 2 selects radial height. The shader calculates
local altitude changes with a difference-of-squares quotient instead of subtracting planet-sized float
radii. Radial segments use bounded eight-point Gauss quadrature; flat segments retain their analytic
integral, and exclusion weighting uses the same radial samples. Environment controls expose the mode.
New generated volume shader ABI 11/12 retains the 1520-byte layout and distinguishes texture/buffer
noise bindings through shared predicates. Outstanding before acceptance: legacy shader handling,
radial quadrature accuracy tests including steep/tangent cases, GPU numerical/visual validation, export
coverage and the complete batch build. Do not publish or describe this draft as ready.

Radial-fog checkpoint: the production FogDensity.hlsli probe passes on Direct3D 12 and Vulkan
(384000 output samples per backend, zero failure pixels; radial-fog-gpu-d3d12.log and
radial-fog-gpu-vulkan.log). Coverage includes legacy local-box density, disabled density,
125-metre observer altitude, opposite surface directions, and a steep radial segment
(falloff 10/m across 100 m). Locally linear radial segments now use the analytic exponential
average; this avoids losing thin endpoint layers to quadrature. Tangential curved segments
still require numerical error coverage; these probes do not validate full rendered fog.

The DevDist KeireTests batch is building (radial-fog-build.log). Before an editor launch, update
Keire.ShaderGraph importer version 52 for regenerated ABI 11/12 artifacts and test both texture
and buffer resource limits. Shared shader-library pins and source-less cooked legacy artifacts
need an explicit compatibility policy; do not silently treat old shaders as radial-aware.
Keep current editor/player acceptance pending until these checks and the native build pass.

Tangent integration regression reproduced during the native build: a 10 km symmetric tangent segment,
observer radius 6360125 m and height falloff 10/m has reference mean extinction
0.00039980917645360286 (4096-panel CPU Simpson integration with cancellation-safe radial height).
The production eight-point shader fails the 1% tolerance on Direct3D 12. An isolated four-subdivision,
eight-point-per-subdivision draft passes the same GPU test. The first draft invocation accidentally
used the production include directory and still failed; explicitly using Build/Validation/FogDensity
as the sole include directory passes (radial-fog-tangent-isolated-d3d12.log). The draft is saved only in
Build/Validation/FogDensity/FogDensity.hlsli; do not lose this fix before the next build. Fold it into
production after the live native build finishes, format the radial block, and add the tangent test to
Scripts/Tests/FogDensityProbe.hlsl. Further extreme-length/thin-layer tests remain necessary; four
subdivisions are evidence for this case, not a universal integration error bound.

Superseding the four-subdivision proposal: a double-precision range audit against 16384-panel Simpson
integration finds relative errors of 2.3e-9 at 10 km, 6.0% at 100 km and effectively 100% at 1000 km
for falloff 10/m (FogDensity/tangent-range-audit.json). Do NOT promote fixed four-way subdivision as
the final correction. The maximum authored fog range is 1000000 m, so this is within the supported
settings domain. Next integration design: clip the segment to the radial layer with meaningful density,
split at closest approach and constant-density base crossings, integrate the capped core analytically,
and quadrature only the remaining shell intervals. Keep stable radial-height differences rather than
subtracting planet-sized squared radii. Validate surface, opposite surface, grazing, base-crossing,
small-planet, translated-planet and space-to-surface rays before acceptance. Account for actual marcher
segment lengths and view-step budgets rather than assuming every ray is subdivided enough.

Documentation source validation passed (radial-fog-docs-check.log): 117 canonical guides, 10 navigation
groups, 29 Mermaid diagrams, local links, 14 schema contracts and two graph-view contracts. This checks
source integrity, not implementation completeness or visual planetary acceptance.

Layer-clipped integration prototype now passes the 1000 km thin tangent case on both Direct3D 12 and
Vulkan (radial-fog-tangent-clipped-d3d12.log, radial-fog-tangent-clipped-vulkan.log), and compiles to Metal.
The draft at Build/Validation/FogDensity/FogDensity.hlsli clips at 24 scale heights, splits at closest
approach and the constant-density core, integrates the core exactly and uses eight quadrature points
per shell side. It preserves the analytic locally linear fast path. Production source remains unchanged
until the current native build terminates. Remaining before promotion: full range matrix, asymmetric
base-crossing and tiny-planet tests, source formatting, persistent GPU regression, importer bump and
resource-limit contract tests. Metal runtime execution remains unavailable on this Windows machine.

The corrected eight-case radial integration matrix passes on Direct3D 12 (radial-fog-matrix-d3d12.log).
Scripts/Tests/PlanetaryFogIntegrationProbe.hlsl now preserves these cases as a permanent regression.
Its first reference generator used Math.Max(0, height), selecting an integer overload in PowerShell;
that rounded sub-metre heights and caused five false failures. Replacing it with an explicit floating
height clamp corrected the references; all eight pass against the isolated clipped kernel. The new
probe will fail against the still-unmodified production kernel until the draft is integrated after
the active build. Cases: 1/10/100/1000 km tangent segments, capped-density base, asymmetric tangent,
1000 m planet radius, and oblique descent. Re-run the permanent probe on both GPU backends after promotion.

The permanent eight-case PlanetaryFogIntegrationProbe also passes on Vulkan against the isolated clipped draft (radial-fog-matrix-vulkan.log, 384000 samples, zero failure output). Renderer enabled checks use nonzero Fog.Controls.x, so mode 2 remains enabled in view preparation and volume composition. Production integration and native regression execution are still pending the active build.

Orientation follow-up: the probe supports PROBE_ORIENTATION 0..5 for axis permutations and sign reversal.
Orientation 4 (mirrored, rotated surface) passes on Direct3D 12 with the clipped draft. Selecting orientation
per pixel triggered a DXC invalid SPIR-V OpSelect error even after simplifying the helper; selecting it at
compile time avoids that compiler failure. Only orientations 0 and 4 have execution evidence so far; do not
claim all six have run. No vendor/compiler code was modified. The persistent probe now defaults to orientation 0.

All six compile-time orientations now pass the eight-case clipped-kernel regression on Direct3D 12 (orientation 0 matrix log, orientation 4 rotated-matrix log, and radial-fog-orientation-1/2/3/5.log). This covers 48 case/orientation combinations. Vulkan has the eight default-orientation cases; Metal has compilation evidence only. Production integration remains pending the native link.

Production integration checkpoint: the first DevDist KeireTests build completed successfully (316 steps,
radial-fog-build.log). Focused native execution passed 26/27 cases and 336/337 assertions; the failure
was a stale negative fixture still rejecting ABI 11 after explicitly accepting it earlier in the same
test. Corrected the unsupported boundary to 13 and added ABI 11 texture / ABI 12 buffer capacity tests.
The clipped integrator is now in KeireCore/Shaders/FogDensity.hlsli; the permanent default-orientation
probe passes against that production source on Direct3D 12 (radial-fog-production-matrix-d3d12.log).
ShaderGraph importer advances 52 -> 53 to rebuild generated volume shaders; pinned shared-library
contracts retain their explicit reviewed update requirement. Source-less legacy cooked shader handling
is still unresolved and documented as unsupported for radial mode pending a deliberate compatibility policy.
The next DevDist KeireTests build is active (radial-fog-clipped-build.log). Run focused regressions after
it finishes, then build/test editor integration. Do not edit native inputs during the active build.

New blocking radial exclusion finding: production radial-fog-exclusion-thin-d3d12.log fails. Reproduction
is Build/Validation/FogDensity/exclusion-thin.hlsl: 1000 m radius, 10/m falloff, symmetric 1000 km tangent,
soft exclusion centered at X=500000 with 1000000 m half extents and full soft edge. Symmetry requires
retaining exactly half the unexcluded integral. Base clipped integration is nonzero, but the old uniform
exclusion quadrature misses the layer and reference==0 returns zero fog. Fix by sampling exclusion
retention inside the same layer-aware radial integral; retain separate handling of local-box density
and the constant-density core. Do not accept radial mode until this interaction is covered. Current
radial-fog-clipped-build remains active; avoid changing native inputs during it.

The exclusion correction is prototyped in Build/Validation/FogDensity/FogDensity.hlsli while the native
build remains active. SampleExcludedRadialHeight clips and splits the radial layer, then integrates
retention at those samples. Local-box density keeps its separate path so boxes outside the radial layer
are not discarded. The thin-layer reproduction passes Direct3D 12. New persistent
Scripts/Tests/PlanetaryFogExclusionProbe.hlsl covers thin height alone, height plus a local box,
zero-falloff height, and local-only density; all four pass on Vulkan against the draft
(radial-fog-exclusion-matrix-vulkan.log). Integrate after the active build terminates, rerun both probes
against production and both backends, and measure the bounded extra sampling cost before acceptance.
The new draft currently adds weighted interval helpers ahead of SampleUnexcludedFogSegmentDensity and
changes SampleFogSegmentDensity; the already-promoted unexcluded integrator itself is unchanged.

Clipped native checkpoint: radial-fog-clipped-build.log completed successfully (14 native steps), and
radial-fog-native-tests.log passes all 28 cases / 345 assertions after correcting the stale ABI fixture.
The exclusion draft is now integrated into production FogDensity.hlsli. Both permanent probes pass
against production on Direct3D 12 and Vulkan (PlanetaryFogIntegrationProbe-*.log and
PlanetaryFogExclusionProbe-*.log). Added player-manifest radial mode round-trip and editor document
Undo/Redo/save coverage preserving atmosphere placement and cloud settings.

A combined DevDist batch is active: KeireTests, KeireEditorTests, then KeireClient, followed by focused
native and editor test execution. Logs are radial-fog-final-<target>-build.log,
radial-fog-final-native-tests.log and radial-fog-editor-tests.log. Do not change native inputs until
that batch terminates. Editor is still closed. GPU probes establish numerical behavior only; visual
and performance acceptance, source-less legacy cooked shaders, and complete planetary integration remain open.

Cache-contract audit found an additional required change before editor launch: MaterialGraph.cpp derives
its importer version from ShaderGraphGeneratedShaderVersion + 4. ShaderGraph importer 53 alone does not
invalidate that legacy material path. After the current combined build finishes, bump
ShaderGraphGeneratedShaderVersion 37 -> 38, update the docs schema-contract sentence, and rebuild affected
consumers. Do not modify this public header during the running build. Source-less old compiled artifacts
still need a separate explicit radial-mode compatibility policy. Production Metal compilation passes for
both permanent probes; docs source validation passes after importer and ABI documentation corrections.

Legacy production transport regression passes on Direct3D 12 after the exclusion integration (radial-fog-legacy-transport-d3d12.log, FogTransportProbe.hlsl, 384000 samples, zero failure alpha). It covers flat height profiles across step budgets, thin/soft local boxes, cloud overlap and oblique exclusions. This complements the new radial probes; it does not validate old compiled material binaries under radial mode.

User steering: finish the current test checkpoint, then use maximum agents on master for a large code
batch before returning to editor testing. This supersedes the earlier no-subagents preference. Three
read-only planning agents were started (planet_ownership, planet_clouds, planet_daynight); no agent has
edit authorization yet. Proposed split: ownership/runtime frames and transactional attachment; cloud
shell geometry/weather/uniforms; celestial orientation and planet-aware navigation. Coordinator owns
integration, project/editor/export and shared shader conflict resolution. Preserve the 12% remaining
usage stop (11% finishing allowance). Latest usage check was 23% remaining.

Build scheduling correction: deliberately terminated the earlier batch process tree at editor-test
compilation after KeireTests had succeeded. Reason: known generator-version change would invalidate
many of the 231 editor-test steps. Confirmed session 26544 terminal. Advanced generated shader version
37 -> 38, updated its explicit fixtures and docs; importer remains 53, legacy material importer derives
38+4. Documentation source validation passed. Resumed checkpoint build under session 55283 in order
KeireEditorTests, KeireClient, KeireTests, then focused tests. Logs radial-fog-contract-<target>-build.log.
Separate pre-version-bump material contract test session 77215 remains active (confirmed compiler child);
its evidence describes the prior compiled generator version, not the pending version-38 build.

Material contract run session 77215 is terminal: 57 cases, 51 passed / 6 failed, 2215/2219 assertions
passed (radial-fog-material-contract-tests.log). This binary predates generator 38. Stale fixtures remain
in ShaderGraphTests.cpp: importer expectation 48 must match ShaderGraph importer 53; code-shader
importer expectation 11 must match 12; seven-texture material ABI expectation 10 must match 12.
Correct after the current build or when its target is safely idle, then rebuild that test object.
Four production-shader compile cases also exceeded their explicit 30000 ms timeout under concurrent
native compilation. Do not simply increase the production timeout; rerun these cases without build
contention and investigate compile complexity if they still fail. Candidate follow-up is explicit
bounded loop control in weighted fog quadrature if inlining/unrolling is responsible; not yet measured.

The resumed build recovered the expired PID-9252 workspace lock automatically; no manual lock removal
was performed. Owner PID 25712 is the verified new batch, which is compiling editor tests (286 steps
including the generator-header rebuild). Latest usage check: 22% remaining. Agents remain read-only/waiting
until the user's requested current editor checkpoint is finished.

Corrected the three stale ShaderGraphTests.cpp fixtures (Shader Graph importer 53, code-shader importer 12, buffer volume ABI 12). Verified the active EditorTests Premake sources do not include this KeireTests-only translation unit; no active editor-build input was changed. The queued KeireTests phase will compile it later. Formatting and scoped diff checks pass. Material compiler timeout cases still require an uncontended rerun.
