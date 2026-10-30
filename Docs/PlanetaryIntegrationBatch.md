# Planetary integration batch

This coordination plan supplements [the large-world acceptance plan](LargeWorldAndEnvironmentPlan.md).
It does not replace its requirements or establish that planetary travel is ready.

## Order and ownership

Finish the current fog editor checkpoint first. Then implement a coordinated code batch on `master`,
followed by native/GPU checks and editor testing. Do not create branches, commit, or publish as part of
this batch without separate authorization. Preserve the existing staged work and user QA scenes.

| Owner | Proposed work | Boundaries |
| --- | --- | --- |
| `planet_ownership` | Authored planet identity, sector/frame resolution, runtime frame ownership and attachment transactions | Internal spatial contracts, `SceneRuntimeWorld`, dedicated spatial/runtime tests |
| `planet_clouds` | Spherical shell geometry, continuous planet-fixed weather, cloud uniforms and focused numerical tests | Dedicated cloud helpers; coordinate shared volume and sky shader changes with the coordinator |
| `planet_daynight` | Consistent celestial directions, planet rotation, explicit planet-aware camera navigation and migration | Celestial evaluator, camera controller and dedicated tests; coordinate runtime call sites and shared sky shaders |
| Coordinator | Integration, authoring/persistence/export, shared shader edits, build scheduling and UI acceptance | Assign exact existing-file ownership before edits; serialize overlapping changes |

The first editor checkpoint is complete and parallel implementation is authorized. Do not let any agent
modify inputs being compiled by an active build. Run one coordinated build rather than concurrent builds.

## Shared contract to agree before implementation

- Use stable planet identity and the existing spatial world/frame identities, sector positions, rotations,
  snapshots and attachment lifetime rules. Do not add a second unrelated global-origin system.
- Resolve observer and planet into a common frame before subtraction or float conversion. The view sample
  needs planet-relative observer position, planet-fixed rotation and inverse, radius, local up, identity,
  revision and evaluation time. Gravity additionally needs the gravitational parameter.
- A frame transfer must define preservation of position, orientation, linear/angular velocity and attachment
  ownership. Validate the whole transaction before mutation; changing a frame label alone is insufficient.
- Opposite surface observers must see a consistent global sun direction. A separate camera-relative sun
  aligned to each observer's up vector would produce incorrect day/night behavior.
- Define planet-fixed wind and weather orientation, and include planet identity, rotation and discontinuities
  in temporal/cache validity. Preserve legacy flat-mode appearance and navigation through explicit modes.

## Required implementation and validation

1. Connect authored placement, runtime frames, rendering and gravity. Preserve existing planet centers and
   project behavior during migration. Provide editor and player parity, including serialization and history.
2. Replace planetary cloud slabs with shells: handle both shell intervals, grazing/inside rays, opaque planet
   occlusion and orbital entry. Separate distance to the shell from the sampling budget inside it. Extend the
   layered fallback and shadows; do not present translated flat clouds as planetary support.
3. Keep weather continuous across poles and seams. Measure cache or inline-shadow costs; temporarily bypassing
   a planar cache is not final performance acceptance. Validate rotating weather and rebase continuity.
4. Connect celestial orientation and explicit navigation modes. Preserve legacy daily tracks and camera records.
   The full scope also includes a visible, phased moon coupled to surface/cloud lighting; a sprite alone does
   not satisfy that requirement.
5. Test ground-to-space-and-return travel, multiple planets, far-range physics and frame transfers. Include
   zero, positive/negative distant translations, sector-scale cancellation, opposite normals and pole crossings.
   Exercise Edit, Play and standalone player behavior, long sessions and transition frame times.

The numerical fog probes and existing orbital physics tests are component evidence only. They do not replace
the integrated travel tests above. Keep unresolved items explicit and stop near the user's requested 12%
remaining account usage, using 11% only to finish a safe checkpoint.

## Confirmed integration gates

Checkpoint evidence (October 29): DevDist `KeireEditorTests`, `KeireClient` and `KeireTests` builds passed.
The focused native run passed 28 cases / 345 assertions; editor atmosphere and planetary-fog history/
persistence checks passed 7 cases / 111 assertions. The running editor displayed the new fog control,
and clicking it persisted `planetaryHeight: true`. Enabling fog visibly attenuated the near Game view;
disabling it restored the prior sky/material appearance. The distant Scene view still shows the known
atmosphere/cloud mismatch: overall planetary visual acceptance is not passed. The editor exited after the
check. The QA scene hash is unchanged; rendering settings match the previous hash after excluding the
new default `planetaryHeight: false` serialization field. These results do not establish travel readiness.

The startup agent now prioritizes document/readiness separation before its celestial/navigation batch.
The cloud agent owns `CloudVolume.hlsli` integration as well as cloud helpers; fog-density source stays with
the coordinator. The coordinator retains shared sky layouts, material ABI validation and version changes.

- Startup performance is part of this batch. The October 29 fog checkpoint stayed on an empty scene for
  minutes while `startup-scene-refresh` rebuilt its material dependency closure. `EditorWorkspaceLayer.cpp`
  currently defers startup when any known importer version increases; `EditorWorkspaceAssets.cpp` opens the
  pending scene only after the refresh completes. Investigate opening a compatible last-good catalog or
  source scene promptly with explicit temporary rendering fallbacks, then publishing refreshed assets
  transactionally. Never execute incompatible cooked binaries to hide the delay. Measure cold, warm and
  importer-migration startup separately, and measure shader compilation without native-build contention.

- A pure frame-conversion helper is insufficient for live travel. Runtime transfer must occur at a fixed-step
  safe boundary and prepare root transforms, interpolation samples, body motion, gravity, character support,
  grounding/smoothing anchors and VFX before mutation. Descendant local transforms remain local to their roots.
  Physics must preserve body identities, sleeping state, contacts and constraints across rotation; ordinary
  body setters and the existing translation-only rebase do not establish that guarantee. Retain the source
  attachment until target admission and all preflight checks succeed. Require nonthrowing commit or exact
  rollback; publish attachment ownership last. Until implemented, reject unsupported live transfers explicitly.
- Resolve planet-relative observers through sector/frame conversion before narrowing. Keep signed day and
  fraction separate from frame snapshot time; astronomical absolute seconds lose clock precision. Opposite
  observers share celestial directions, while cloud weather stays in the planet-fixed basis.
- Legacy cooked volume materials also need an explicit compatibility policy. Current surface and VFX
  submission paths push the shared volume payload according to the declared ABI. Old source-less binaries
  cannot acquire radial fog semantics merely by receiving the new mode value. Verify surface and VFX paths
  together; source regeneration and ABI-version tests alone do not prove old cooked payload behavior.

## Current implementation checkpoint (validation pending)

The coordinated batch now authors `ProjectSettings/Planets.keiresettings`, including sector placement,
orientation, rotation rate, radius, selected planet, and explicit Flat/Planetary and Uniform/Radial/Combined
modes. Runtime manifest schema 12 embeds this record for cooked players; older manifests retain legacy
flat/uniform behavior. Renderer requests carry a validated per-view resolved planet sample, shared by
atmosphere, fog, spherical clouds, celestial directions, and inertial stars. Editor Play and player rendering
use runtime-owned planet state rather than the editor preview resolver.

Startup source documents can open before asset refresh completes, with incompatible cooked assets excluded
and Play gated on readiness. Refresh must preserve edits, undo history, and camera state. This is an
implemented scheduling change, not a measured startup-speed claim; cold, warm, and migration timing still
require the next editor check.

New native tests and an eight-case GPU cloud geometry probe are authored but unrun. The 28 native and seven
editor passing cases above describe the preceding fog build only. Full rotating live physics-frame transfer,
multiple visible atmosphere composition, moon lighting, and long ground-to-space-and-return acceptance remain
open. Do not label the current batch production-ready before its combined build and integrated testing.

October 29 visual checkpoint: full DevDist KeireClient build passed (433 steps), then AssetTool/AssetWorker
passed (six steps). Initial shader staging missed the new planetary includes; Windows/Unix built-in and
embedded material staging lists were corrected and the Windows generated-content-cache regression passed.
The editor opened the source scene with pending material fallbacks while refresh continued, rather than waiting
for the full material closure. Initial window startup still took tens of seconds and remains a performance issue.

At the existing distant Scene camera near X=1e10, legacy settings retained the black sky. Creating a selected
planet, enabling Planetary geometry, and placing its center at X=1e10, Y=-6360000 visibly restored coloured sky,
clouds, sun and the lower-hemisphere appearance. This is a placed-planet preview result, not a complete travel pass.
The isolated QA project now has an explicit planet record (sector X152588, offset X-7168) for repeat testing;
GravityAcceptanceQA scene SHA256 remains A85D25F5CE5A5073B1684BDF1889A48B95B694ABAF7A3F98D603E440F98CAB7C.

UI testing exposed a five-file project-settings save rejected by the old four-file transaction bound. Save and
both journal recovery bounds now share MaximumSettingsFiles=5. The incremental editor rebuild passed (three
steps). Save/reopen acceptance is in progress. New native/editor regression suites and planetary GPU probe
execution remain pending; do not cite previous fog-suite passes as validation of this new batch.

Save-fix acceptance: reopened DevDist editor PID35948, window27527448. Far Scene sky remained coloured
with persisted planet placement. Clicking Save Environment succeeded with no error; all five settings files
were atomically written at 12:33:12. Original-origin Game camera shows space because the active planet is at
X=1e10; automatic per-region/nearest-planet selection and multi-atmosphere composition remain unfinished.
Restart was delayed by an orphaned QA worker from the forcibly closed prior process; requested its documented
cancel file, worker exited, new editor completed startup. Record worker-parent lifecycle cleanup as a follow-up.
Editor is left open maximized for the user. Account usage remaining was13% at this checkpoint. No Git mutations.

Final focused native checkpoint: DevDist KeireTests built successfully. The selected planetary, asset mount/status, cloud layout/cache/temporal, and volume ABI tests passed: 60 cases, 1921 assertions. The initial mount fixture incorrectly rewrote resolved absolute pack paths; converting them back to catalog-relative paths fixed the fixture without changing production catalog behavior. The planetary GPU probe, full editor suite, and continuous ground/orbit/return acceptance remain unrun. Usage remaining was 12%; editor remains open for user testing.

## Using the current planetary QA scene

Open Environment > Sky > Planet Placement to inspect the selected planet. In the current
CloudIsolation QA project its centre is X=10,000,000,000 m, Y=-6,360,000 m, Z=0 and its
radius is 6,360,000 m. The nearby top surface is therefore Y=0. Sector coordinates are
an exact placement representation, not altitude; do not type the full X coordinate into
the bounded sector-offset field.

Scene view uses the editor camera. Game view uses Showcase Camera near the original
world origin. With only the distant planet selected, that original camera is outside its
atmosphere and shows stars. This does not mean a distant world coordinate is inherently
space. Place the game camera near the distant showcase to compare both views, or place
the planet beneath the original showcase when testing there.

Fly upward near the planet's top surface (increase Y while keeping X near the planet's X)
to inspect the atmosphere/cloud transition. This previews the environment; it does not
create a terrain sphere, spacecraft controller, or prove continuous gameplay travel.
Enable Planet surface navigation for navigation aligned to the selected planet's local up.
Ground-to-orbit-and-return gameplay, rotating physics transfers and multiple-planet
selection remain acceptance work.

White fallback materials are not the intended showcase appearance. Inspect failed asset
imports before evaluating lighting. The October 29 user capture had eleven shader imports
that exceeded the compiler's 30-second timeout during the earlier build/import overlap.

October 29 material recovery follow-up: original showcase colours returned in Scene/Game view and the user confirmed success. Startup compatibility now accepts an importer's supported PreviousNames aliases; Keire.MaterialGraph metadata was previously rejected despite Keire.Material supporting it. The editor also applies compatible published imports before reporting an unsuccessful mixed refresh, preserving exclusion of failed assets. Both incremental KeireClient builds passed. The new editor alias regression build is still running (KeireEditorTests, session 54069); earlier attempts failed at the live worker executable's link lock and were not test passes.

Separate external acceptance project: D:/Projects/KeireValidation/EarthSpaceFlight. FlightDirector.cs compiles against bundled .NET 10 with zero warnings/errors; project validation passed. Authored flight lasts 150 seconds, uses precise positions, has clickable launch/pause/return/view controls and an exponential cockpit/follow blend. Initial scene errors (parent ordering, far plane above supported limit, unsupported flex-start stylesheet keyword) were corrected. Original QA scene remains untouched. Runtime flight acceptance remains incomplete: editor exits while opening the corrected demo. Prior application access-violation reports exist; current early exit needs diagnosis. Do not claim the 150-second flight or HUD clicks have been tested. BuiltinSky altitude-based star visibility is edited but not yet built/validated.

Flight startup diagnosis: stderr reports invalid directional cascade stabilization. Reconstructing the far clip corner at a 0.05 m near / 10,000,000 m far ratio produces infinite coordinates after float projection rounding. Pending fix constructs shadow slices from two interior clip points in view space, then transforms them to world space. Perspective and orthographic regression coverage added; build/runtime verification pending.

October 29 flight verification checkpoint: the shadow-slice fix is built and the demo now opens reliably. Focused directional-shadow tests passed (4 cases, 49 assertions), and startup importer-alias tests passed (1 case, 16 assertions). The initial alias test fixture used an unsupported .txt extension; changing it to the registered .cs extension corrected the fixture. Client and editor test builds passed.

The external flight demo completed its 150-second loop. Clickable cockpit/follow transitions were exercised in both directions, with stars and the atmospheric limb visible above the cloud layer. User reports of severe cloud and Earth flicker remain an open acceptance item. Raising the near clip diagnostically reduced visible cloud breakup; the demo now smoothly adjusts it from 0.2 m near the surface to 10 m above 5 km. This is a demo mitigation, not a general engine depth-buffer solution. The simple Earth sphere still needs ground/shadow stability inspection, and temporal flicker has not been conclusively ruled out. Do not call planetary rendering production-ready from these snapshots. The original QA scene is preserved. Early-return behavior and default-input catalog readiness remain follow-ups.

## Current continuation checkpoint — October 29

The EarthSpaceFlight user acceptance now passes the 150-second route and camera transitions;
the user watched the rebuilt flight and confirmed cloud/Earth flicker is gone. This supersedes
the older flight-specific pending notes above. See Samples/EarthSpaceFlightCheckpoint/README.md.

Remaining integration order:
1. Measure startup readiness and ground/ascent/orbit/landing CPU/GPU frame-time distributions.
2. Add an explicit per-view planet-selection policy with persisted backward-compatible settings,
   deterministic overlap behavior and matching editor/player behavior. Current selection is manual.
3. Implement multiple visible atmosphere composition with bounded cost; selection alone is insufficient.
4. Implement transactional live physics-frame migration, preserving constraints, velocities, sleeping
   bodies and failure rollback. Mathematical frame conversion tests do not establish solver migration.
5. Exercise far-origin collision workloads, standalone save/reload parity and long-session stability.

Runtime planet-frame snapshot reuse is implemented. DevDist KeireTests build passes; focused frame and runtime planet tests pass 10 cases / 243 assertions. Clang-format validation passes.
No end-to-end frame-time improvement is claimed before measurement.

Nearest-surface selection batch: optional per-camera selection is implemented in editor preview and runtime.
Planet settings schema 2 stores the mode; legacy schema 1 remains manual. Distance is absolute distance
to the spherical surface, and stable planet identity breaks exact ties. Existing shared appearance settings
remain shared. Multiple visible atmosphere composition and physics ownership transfer are not supplied by
this selection policy. Native build/test validation is running; editor control validation remains pending.

Selection native checkpoint: DevDist KeireTests built successfully. Focused settings, runtime planet,
manifest and history tests pass 16 cases / 164 assertions. New coverage verifies nearest selection and ties
in preview plus legacy manual decoding. Dedicated runtime nearest-mode and nested-frame parity coverage,
editor build/control testing and visible transitions still remain; the existing runtime tests cover manual mode.

Runtime selection and physics checkpoint: DevDist build passed; settings, runtime, manifest and physics suite
passes 42 cases / 276310 assertions. New tests compare nested far-origin planet selection against preview,
checkpoint round-trip, and the runtime render-camera capture. Existing physics tests cover resting/sleeping
stacks, collision/query behavior and repeated translation rebasing up to 1e15 metres. These establish local
simulation translation behavior, not rotating solver-frame migration or a standalone player launch.
Editor test build remains active under exec session 5626 (233 steps); do not launch a duplicate.

Standalone preparation found and fixed the external demo's inherited sandbox build-scene list: it omitted
the flight startup scene. Only the separate EarthSpaceFlight project was changed, with backups. BuildScenes,
BuildProfiles and Player settings are now included in the source checkpoint. AssetTool validate-project passes.
The active editor build remains session 5626; do not run competing imports while native compilation is active.

Player/runtime audit: Release, Dist and Debug player binaries are dated October 25 and cannot validate the
new selection behavior. Use a newly built DevDist KeireRuntime with --content after cooking the flight scene.
RuntimeRenderBenchmark now permits DevDist (reports already preserve configuration); this independent runtime
source is not in the active editor-test target. Compilation/CLI acceptance and timing runs remain pending.

Composition implementation requirements (not implemented): resolve candidate planet centers in double precision
relative to each view, intersect atmosphere/ground intervals, and composite radiance with accumulated
transmittance in ray order. Opaque planet hits must stop background transport; overlapping media require
combined extinction/in-scattering rather than independently blending sky colors. Extend sky, opaque aerial
perspective, cloud/fog integration and history identity together. Test two visible limbs, occlusion ordering,
inside/outside transitions and overlap against a numerical reference before visual acceptance. Bound work
using visibility culling and declared quality budgets, with diagnostics for any exceeded candidate limit.
Reference for segment radiance/transmittance contracts: Eric Bruneton's primary implementation,
https://ebruneton.github.io/precomputed_atmospheric_scattering/atmosphere/functions.glsl .

Editor checkpoint: session 5626 completed successfully. Focused project planet, lighting preview, camera
persistence, environment lighting/time suite passed 25 cases / 403 assertions. The existing undo/save/reload
case is now extended to NearestSurface; its follow-up rebuild/test remains pending. Sequential batch session
81280 builds KeireRuntime DevDist (including the benchmark guard) then KeireEditorTests. Do not duplicate it.

Rebuild root cause and correction: the runtime build passed, but the editor follow-up scheduled 233 steps.
Ninja -d explain identified the relinked KeireAssetWorker.exe as a timestamp input of every editor-test object.
Stopped verified Ninja PID 6460 and its compiler children; session 81280 exited 1 deliberately. The generated
Ninja repair now moves only recognized runtime asset-worker prerequisites on C/C++ compile edges to order-only
dependencies. It preserves other inputs and link edges. Five Python regressions pass, including Unix-style edges.
After repair the same editor dry run schedules two steps, not 233. Final build/test session 87679 is active.

Final follow-up: DevDist runtime build passed (7 steps). With the dependency repair, the editor-test rebuild
actually completed in 2 steps and the focused suite passed 25 cases / 403 assertions, including NearestSurface
undo/save/reload. Runtime benchmark invocation and rendered performance remain unverified.
Standalone acceptance checkpoint (2026-10-29): rebuilt DevDist AssetTool successfully (7 steps), then cooked
EarthSpaceFlight successfully: 8 assets / 1 pack. Ran the freshly built DevDist KeireRuntime directly against
that cook, avoiding the older packaged BuildSupport binaries. Observed ascent, space at 150 km, cockpit and
follow button changes, and COMPLETE at 150/150 seconds and 0.0 km. Sparse captures do not establish absence
of brief flicker throughout the route. The runtime was closed normally after completion.

A separate stationary landed benchmark completed 300 warmup + 2000 measured frames with immediate present
on D3D12 / RTX 3060 / i7-12700F. Median / p95 milliseconds: owner update 0.294 / 0.383; capture 0.077 / 0.108;
render CPU 1.185 / 1.417; admission wait 13.762 / 15.285; GPU retirement latency 27.686 / 28.958. Retirement
latency includes queued work and is not GPU execution time. Two frames were allowed/in flight; renderer queue
high-water mark was one. This is a landed baseline, not a full-flight performance acceptance or a before/after
comparison. Viewport dimensions are not recorded by this benchmark yet; record them before comparisons.
Local raw evidence: Build/Validation/earth-space-landed-devdist.json (generated, intentionally untracked).
The report identifies DevDist c82b32be dirty: the benchmark guard was built before the later checkpoint commit.
The original CloudIsolation20261026 GravityAcceptanceQA scene SHA-256 remains
A85D25F5CE5A5073B1684BDF1889A48B95B694ABAF7A3F98D603E440F98CAB7C.

Next: measure GPU pass costs and fixed-resolution flight phases; finish multi-visible-planet atmosphere
composition and transactional rotating physics-frame migration. Do not label the whole system production-ready
from the single-planet standalone trip or existing translation-rebase tests.
Performance measurement follow-up: RuntimeRenderBenchmark now records drawable pixelWidth/pixelHeight and
rejects nonpositive or changing resolution. The Release matrix launcher requires positive integral dimensions
and equal resolution across presentation modes. DevDist runtime rebuilt in 4 steps. A direct D3D12 immediate
capture passed 300 warmup + 2000 measured frames at 1280x720; median owner 0.282 ms, capture 0.080 ms,
render CPU 1.142 ms, admission wait 11.337 ms, retirement latency 22.502 ms. This is not a demonstrated speedup:
no rendering optimization changed and the earlier report omitted resolution. A second run was maximized through
Windows control; the runtime exited and produced no benchmark JSON. Its redirected log was empty, so no exception
text was verified. PowerShell launcher parses successfully; the full Release matrix was not rebuilt/rerun.

Rotating-physics implementation audit: do not apply ConvertFrameBodyState directly to PhysicsWorld state.
Jolt linear velocity belongs to the center of mass whereas exposed body position is the object origin; off-center
shapes require COM-aware velocity conversion. Rotating solver axes also require continuous Coriolis, centrifugal,
Euler and accelerating-origin terms. Velocity setters and sleep restoration can change motion. The coherent next
batch should retain a root-oriented inertial solver and transactionally transfer scene ownership, converting at
scene/solver boundaries. SceneRuntimeWorld.cpp Entry currently stores FrameAttachment without explicit coordinate
frame; Add attaches root and SceneRuntimeSpatialAccess::FrameForEntity always returns root. Coordinate changes
must update PhysicsRelativeTransform, BuildPhysicsDefinition, SynchronizePhysicsBodies, PullDynamicBodies,
StepPhysics and presentation interpolation in SceneRuntimePhysics.cpp together. Include animation, VFX, gravity
and checkpoint consumers. Preserve inertial body IDs/contacts/sleep, reject stale epochs before mutation, test
COM-offset bodies and round-trip motion, and publish attachment plus scene changes atomically. No live migration
API was added in this audit; it remains required work.
Active COM migration prerequisite batch: added private PhysicsFrameAccess::TryGetCenterOfMassPosition and an
offset-convex-collider test covering far-origin rebasing, body removal and closed-world rejection. DevDist
KeireTests build/test is live in exec session 83020 (202 steps after the central physics header change); do not
start a duplicate or edit its sources until terminal. Changes are not yet validated or committed.
The subsequent scene-owner implementation will preserve root-oriented transform/solver storage, apply evaluated
frame motion to owned non-dynamic entities, and keep dynamic bodies inertial. It must not be merely an ownership
label change. Existing SetKinematicTarget teleports; add a velocity-aware Jolt MoveKinematic path and a validated
motion-type transition before moving planetary supports. Current static bodies are not created with explicit
mAllowDynamicOrKinematic, so preserve-or-reject rules require inspection before changing backend motion type.
COM prerequisite validation completed: exec session 83020 exited 0 after 202 build steps. The targeted
"physics frame conversion queries*" regression passed 1 case / 12 assertions. This validates the local COM
query and offset-convex/rebase/lifecycle behavior only, not full scene ownership transfer. No builds remain active.
Kinematic prerequisite: PhysicsFrameAccess::MoveKinematic now uses Jolt velocity targets without teleporting,
validates finite pose/duration and representable velocity, and only accepts existing kinematic bodies. Build86649
completed in 6 steps, and COM plus motion tests passed 2 cases / 22 assertions. A further off-center rotating COM
velocity assertion was added after that run and awaits the next batch build. Scene ownership agent is now
implementing root-storage ownership and actual non-dynamic frame-follow ticks; do not start a build until its
source mutations stop. Initial static-collider transfers must reject before mutation until a safe backend
conversion exists; that is an intermediate limitation, not completed planetary support.
Further prerequisite coverage added (not yet built): a far-origin kinematic platform lifts a contacting dynamic
sphere over 60 solver steps after settling; off-center convex rotation checks the required COM linear and angular
velocities. These will run with the scene-owner batch. Two implementation agents are active: physics_migration
owns actual scene owner/tick/checkpoint integration; planet_composition owns a new reference transport helper
and analytical tests. Do not build until both stop source mutations. Full GPU multi-atmosphere composition,
static-support conversion, and broad scene-content transfer remain open requirements.
Combined owner/composition batch is now building in exec session 3405, log
Build/Validation/planet-owner-composition-build.log (192 steps after Scene.h changes). Both agents have released
files; no source mutations until terminal. The command then runs focused scene-owner, kinematic/COM, runtime
planet and atmosphere composition cases. New ValidateKinematicMove preflights all velocity targets before any
mutation; reference geometry now rejects overflowing finite coordinates and preserves first opaque ties.
Do not claim test completion yet. Changes remain uncommitted pending the build/test result.
Combined validation: initial build3405 stopped on const test entities in SceneRuntimePlanetTests; fixed those
fixture declarations. Incremental rebuild47441 completed its remaining 23 steps and all selected tests passed:
22 cases / 215 assertions (scene owner motion/persistence, kinematic support/COM, runtime planet selection and
multi-atmosphere reference transport). No build remains active. These are native focused tests, not editor/GPU
multi-planet acceptance. Scene-owner transfer is internal and rejects static colliders, characters and parented
physics; ordinary tick faults retain existing semantics. Numerical composition is not yet wired into rendering.
Next batch underway after 7ce20c17: runtime ResolveAll now resolves every configured atmosphere candidate from one
published epoch, sorts by planet identity, and validates root-coordinate observer identity. Added nested far-origin
ordering/epoch/selection-parity and invalid-frame assertions (not built). planet_composition agent owns renderer
candidate capture, uniforms/shaders and editor resolver integration; physics_migration owns static-support
creation/promotion and scene ownership integration. Shared World.cpp/test edits are region-separated; defer
whole-file formatting and builds until both agents release sources. Full multi-planet cloud/fog volumes and
unrestricted content transfers remain requirements, not established by the current batch.
Review during active integration: added Flat-mode ResolveAll empty-result regression. Flagged promotion/contact
stability and cross-scene prevalidation/commit allocations for physics agent. Renderer sky union transport is
being wired first; surface aerial/cloud/fog ABI, remote GPU angular precision and visibility-budget culling remain
required subsequent integration. A normalized/relative float representation alone does not prove arbitrary
astronomical precision. Preview all-candidate preparation should avoid resolving nearest candidates twice.
No build started while agents are editing; no new validation result is claimed.
Sky increment source freeze: editor/runtime candidate collection, stable capture/history, eight-candidate uniforms
and multi-medium sky shader are drafted with native tests. Shader validation is live in exec session75041,
log Build/Validation/planet-multi-sky-shaders.log. Do not start duplicate generation or native build while it runs.
Physics static-support batch is still editing: preallocated motion state permits stable-ID promotion and triangle
mesh kinematics; live-added-support and contact tests drafted. Checkpoint reconstruction ordering must restore
owner promotion before body velocity state; agent is correcting this before source release. No native validation
yet. Surface/cloud/fog union transport, perplanet volumes and distant GPU precision/culling remain unfinished.
