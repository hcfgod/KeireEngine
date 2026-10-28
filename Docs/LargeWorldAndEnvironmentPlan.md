# Large Worlds, Planets, Space, And Dynamic Environments

Status: proposed implementation plan, not a supported-feature announcement.
Requested scope: large terrestrial worlds, rotating planets, surface-to-space travel, and solar-system gameplay,
with a customizable production environment system. All are required outcomes; finishing the terrestrial milestone
alone does not complete this plan.
Source review: October 25, 2026.

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

## Current Implementation Evidence

- Native `Vector3` components are floats in `KeireCore/Include/Keire/Math/Math.h`.
  `TransformComponent` stores float local positions and returns float world/presentation positions.
- `SceneAsset.h` aliases `SceneVector3` to that float vector; transform/component properties, prefab overrides,
  snapshots, recovery, and replay must therefore be included in the precision migration.
- Managed `MathTypes.cs` defines float `Vector3`; `Handles.cs` exposes it through local/world Transform positions.
  Native interop, generated service bindings, both supported IDE API targets, and reflection require coordinated changes.
- The inspected Release dependency cache has Jolt `DOUBLE_PRECISION=OFF`.
  `PhysicsSystem.cpp` bridges positions through float `Vector3`, including its `RVec3` conversions.
- Navigation node positions, audio positions, camera matrices, and world-space UI matrices also use current float types.
- `StreamingSystem` supplies asset-segment residency and budgets. That is useful infrastructure, not proof of spatial
  world partition, planetary patch residency, HLOD, or transactional simulation-region transfer.
- `RenderEnvironmentSettings` now exposes atmosphere, layered and experimental volumetric clouds, bloom and stars
  alongside ambient/environment lighting, sky visibility, exposure, shadows and rendering-quality selections.
  Environment profiles also supply daily lighting and sky-color tracks. These integrations do not establish the
  complete weather, planetary geometry, surface-to-space or visual-quality contracts specified below.
- The VFX roadmap explicitly leaves world streaming and large-world rebasing open.

Before implementation, produce a checked inventory of every absolute-coordinate producer/consumer, binary layout,
serialized representation, implicit float conversion, and exposed C#/native signature. Include external-editor
workspaces and SDK samples, not only runtime sources.

## Architecture Decisions

### Foundation Audit: Verified Migration Boundaries

The first source audit identified the following coupled boundaries. This is an initial inventory, not a claim that
every position consumer has been enumerated. Paths below are repository-relative.

| Boundary | Current source contract | Required migration treatment |
| --- | --- | --- |
| Transform ownership and interpolation | `KeireCore/Include/Keire/ECS/Components/TransformComponent.h`: local, previous-fixed, current-fixed, world and presentation positions use `Vector3`; matrices use `Matrix4`. | Migrate stored, interpolated, composed and returned positions together; preserve snapshot/savepoint stability. |
| Hierarchy composition | `KeireCore/Include/KeireInternal/SceneState.h`: `WorldMatrix` returns the float matrix; prefab instantiation accepts `Vector3`. | Introduce high-precision composition before deriving any relative float matrix; migrate spawn inputs too. |
| Scene persistence | `KeireCore/Include/Keire/Scenes/SceneAsset.h`: `SceneVector3` aliases `Vector3` for both position and scale. | Split position semantics from scale; version the persisted coordinate representation without globally widening unrelated vectors. |
| Reflected values | `KeireCore/Include/Keire/ECS/Component.h`: the variant has scalar `double`, but its vector alternatives remain float vectors. | Add explicit high-precision position/vector kinds and round-trip codecs, Editor drawers, cloning and prefab override support. Scalar double support alone is insufficient. |
| Managed Transform | `KeireManaged/Handles.cs`, `NativeRuntime.cs`, `RuntimeApi.cs`: position getters/setters and unmanaged delegates use `Vector3`. | Change public APIs, fallback runtime interface, native declarations, implementations and internal-call registration as one contract. |
| Native implementation of managed calls | `ScriptSystemRuntimeFoundation.cpp`, `ScriptSystemRuntimeServices.cpp`, `ScriptSystemReload.cpp` under `KeireCore/Source/Scripting`, and `KeireCore/Include/KeireInternal/Scripting/ScriptSystemInternal.h`. | Reject mixed layouts before calls execute; test all local/world/presentation get/set paths. |
| Built-in property ABI | `KeireCore/Include/KeireInternal/Scripting/ManagedBuiltinComponents.h`: `NativeBuiltinProperty` is asserted to 56 bytes and carries `Vector4`; `KeireManaged/NativeRuntime.cs` mirrors its sequential fields. | Version and verify size, offsets, alignment and kinds on both sides. Never reinterpret the existing float payload as doubles. |
| Physics queries and results | `KeireCore/Include/Keire/Physics/PhysicsSystem.h`: body positions, query origins/centers and hit/contact points use `Vector3`, alongside local geometry and directions. | Classify each field as position, displacement, direction or local geometry; convert relative to the accepted simulation region. |
| Navigation | `KeireCore/Include/Keire/Navigation/NavigationSystem.h`: nodes, routes, endpoints, nearest-point/raycast and agent targets use `Vector3`. | Bind local tiles/routes to explicit high-precision origins and frame revisions; migrate author-facing queries. |
| Audio | `KeireCore/Include/Keire/Audio/AudioSystem.h`: source/listener positions and velocities use `Vector3`. | Compute listener-relative values before narrowing; retain frame-aware velocity for Doppler. |
| Environment persistence | `KeireCore/Include/Keire/Rendering/RenderSystem.h`: `RenderEnvironmentSettingsSchemaVersion` is 8 as checked October 26, 2026. | Preserve existing settings migration alongside the separate versioned environment profiles and simulation state; a schema number is not feature coverage. |

The prerequisite rotation restoration defect is fixed and checked with 17 focused test cases / 1,236 assertions.
The staged DevDist Editor also passed light-toggle, Undo, Redo, Undo and clean close in the sandbox QA project.
This closes that prerequisite only; the remaining LWE-0 inventory and numerical contracts remain open.

### Initial Numerical Evidence

The detailed Transform audit confirms that `TransformComponent::WorldPosition` extracts translation from a float
`WorldMatrix`, and `SetWorldPosition` uses a float inverse-parent matrix. `SceneState::WorldMatrix` in `Scene.cpp`
caches float hierarchy composition; fixed presentation samples and their interpolation also remain float. Transform
schema 1 stores `position` as a reflected `Vector3`; both `SceneAsset.cpp` and `SceneSerialization.cpp` decode vector
components with `get<float>()`. A storage-only double field would still lose precision through these paths. Migrate
composition, world/local conversion, reflected property codecs, scene formats and presentation samples as one
coherent slice, with signed/nonuniform-scale hierarchy and save/Undo regressions.

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

Proposed names such as `Vector3d`, `WorldPosition`, and `SpatialFrame` are design names, not existing APIs.
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
Implementation checkpoint: an internal CPU volume-transport reference now defines homogeneous segment
transmittance and integrated source weighting, including vacuum, thin and opaque limits and front-to-back
composition. It is not yet connected to a GPU fog pass or authoring settings.
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
These are pending renderer and authoring requirements; current layered-cloud controls do not fulfill them.
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
