# Planetary integration batch

This coordination plan supplements [the large-world acceptance plan](LargeWorldAndEnvironmentPlan.md).
It does not replace its requirements or establish that planetary travel is ready.

## Current status — October 31

This section summarizes current evidence. Dated notes below are historical checkpoints; their pending items
and usage targets are superseded where this section records a later result or instruction.

- Implemented: multi-planet sky candidate composition, planetary scene ownership and frame-following rigid-body
  support, pass-specific Shader Graph generation, decimal numeric editor fields, and opt-in per-planet sky profiles.
- Verified before the current profile build: UI integration (35 cases / 1,477 assertions), focused UI/physics
  (10 cases / 3,193 assertions), and planetary integration (24 cases / 470 assertions). Reconstruction resumes
  contacts at both the origin and a distant translation. These are not results for the new profile implementation.
- Shader slowdown fix, committed in `d2d5c983`: production importer regressions passed 3 cases / 289 assertions,
  including seven-texture opaque/unlit/transparent permutations. Starter creation passed 1 case / 93 assertions.
  Corrected depth/velocity and G-buffer DXIL compiled in approximately 0.20 and 0.22 seconds, with actual compiled
  resource bindings and declared input/output signatures matching the original binaries. Retaining the same cloud
  samples in a loop also reduced measured forward compilation from about 37 seconds to about 12 seconds.
- Decimal controls were visually checked in the editor today, including a screenshot of Rayleigh coefficients in
  fixed decimal notation. This supplements the display/edit tests; it does not establish all-panel visual coverage.
- New profile shader regeneration and compiled DXIL/SPIR-V layouts pass; embedded DXIL/SPIR-V/MSL artifacts match
  the inspected binaries. The DevDist native build passed, followed by 84 focused planet/atmosphere/capture
  cases and 1,656 assertions. A subsequent fog/planet/atmosphere batch passed 104 cases / 1,903 assertions.
  GPU/editor profile appearance, eclipsing and transition acceptance remain pending.
- Outstanding: additive-scene replay consistency; character and parented-collider transfer support; multi-planet
  surface/cloud/fog composition; distant GPU precision and atmosphere visibility admission; remaining authoring,
  standalone parity, long-session and measured transition-performance acceptance. Planetary travel is not yet
  established as production-ready.

The latest user instruction is to stop before **65% remaining account usage**, save a safe checkpoint, and share
scene screenshots in the chat for phone review. This supersedes historical 12%/11% targets below. Committing and
pushing tested work on master is authorized; preserve user QA content and use development builds rather than full
production packaging for this batch.

## Per-planet sky profile integration — October 31

The reviewed profile implementation is now built and natively tested on master, but has not yet been visually
accepted. Each authored planet can override its sky atmosphere; absent overrides inherit the unchanged global
profile. Explicitly disabled media remain opaque reference planets and block sunlight without atmospheric
extinction. Candidate profiles are copied into each render view and participate in temporal-history invalidation.

Planet settings schema 3 and runtime manifest schema 13 carry these overrides. Project schema 1/2 and runtime
schema 12 planet records retain inheritance. The AssetTool producer uses CurrentSchema and the runtime consumes
DecodeState; a schema-3 planet profile is rejected under runtime schema 12 rather than silently dropped.

The bounded override optics contract is a physical surface radius from 1,000 to 1,000,000,000 metres and
atmosphere thickness from 100 metres through that physical radius. Density/absorption heights retain their
existing constraints. Controls and serialization use the selected planet's actual radius; hidden legacy
center/radius fields remain unchanged. A new override copies global appearance and bounds its initial heights
only when needed. Existing profiles are not coerced. Unsupported radius edits are rejected with guidance;
planets without overrides keep their existing geometry/gravity acceptance.

This slice covers sky atmosphere only. Independent per-planet cloud/fog appearance, full surface aerial
perspective, more-than-eight visible candidate admission, distant angular precision, and integrated physics /
standalone / performance acceptance remain open. Do not call this complete planetary rendering.

Required next validation: rebuild focused profile/manifest/capture tests; regenerate builtin shaders; check DXIL
and SPIR-V layouts (PlanetAtmosphereSet size 1,040 bytes; SkyUniforms size 1,728; ground array absolute offset
1,600); visually test Earth and Mars independently enabled/airless, eclipse rays, different view positions,
far translations, and temporal transitions. The sunlight test includes the production scalar HLSL kernel,
but CPU execution alone will not prove backend rendering correctness. No native build or shader regeneration
was started during integration while the coordinator's screenshot session was active.

Profile shader validation checkpoint: the official Windows builtin-shader regeneration completed successfully.
Actual Sky DXIL disassembly and SPIR-V member/array/type decorations agree on the 1,728-byte sky buffer,
1,040-byte planet set at offset 688, and ground array at absolute offset 1,600. The reflected binaries match
the generated embedded DXIL, SPIR-V and MSL artifacts byte-for-byte. Evidence is in
`Build/Validation/PlanetProfiles/Reflection/`. Visual profile/eclipsing acceptance is still pending.

Native profile checkpoint: the DevDist `KeireTests` build completed, and the focused planet/atmosphere/
environment-capture run passed 84 test cases and 1,656 assertions. The development editor stage is being
rebuilt for visual acceptance. This does not establish GPU eclipse correctness, independent cloud/fog
profiles, or standalone parity.

## Order and ownership

Finish the current fog editor checkpoint first. Then implement a coordinated code batch on `master`,
followed by native/GPU checks and editor testing. The original instruction was not to create branches, commit, or publish as part of
this batch without separate authorization. Commit/push authorization has since been given; production publishing is not part of this checkpoint. Preserve the existing staged work and user QA scenes.

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
the integrated travel tests above. Keep unresolved items explicit. Historical usage instruction from the October 29
checkpoint: stop near 12% remaining, using 11% only to finish safely. The October 31 instruction at the top of this
document supersedes that historical limit: stop before 65% remaining.

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

## 2026-10-31 shader compilation blocker

User requested verification before changing the generator. Repeated captured DXIL fragment compiles with identical compiler/source inputs except the final ABI-retention addition: depth/velocity 48.8037 s versus 0.9364 s; G-buffer 43.6121 s versus 1.6928 s. All exited zero. This confirms the final-lit-color dependency keeps unnecessary work observable; removing retention altogether is not production-safe. Actual DXIL reflection of the diagnostic depth shortcut loses inputs (13 to 3) and resources (41 to 4).

Generator fix is drafted: separate cheap ABI accumulator; exclude full lighting and volume composition from depth/G-buffer; keep pixel depth offset independent; bump generated shader version40. Added unlit seven-texture depth-offset importer coverage and opt-in generated-source capture. New Scripts/Validation/inspect-dxil.ps1 compares actual compiled resource and input/output signatures.

DevDist KeireTests build is running in session45673, log Build/Validation/shader-pass-isolation-build.log. No updated production-fix tests or reflection parity claimed yet. After build: run generated-HLSL importer, seven-texture and Starter regressions, compile exported HLSL directly for both passes and inspect actual reflection. Do not run competing builds. Preserve existing local FogDensity loop hint and ShaderCompilerJobs diagnostic changes until reviewed.

Follow-up audit: custom resource declarations already require AllowOfflineResourceDeclarations; normal runtime compilation rejects them explicitly. The sampler-only edge is therefore outside the supported runtime importer path, not a new production blocker. Preserve that rejection. Planetary completion scope remains unchanged and unfinished; prioritize this shader-import blocker before returning to it.

Checkpoint audit against committed10f1dce2: runtime physics RestoreAfter runtime.world.planets, which depends on runtime.scene, is present; reconstructed-owner velocity test also exists. Remaining validation is full saved-scene reconstruction followed by ticks/contact checks at far origin, Replay rollback chain, and additive-scene save/restore (runtime serializers currently capture primary session while planet owners include all entries). This is source evidence, not a new test pass. Build45673 remains live; do not restart it because the pass needs a provenance-triggered621-step rebuild.

Decimal Environment UI request (2026-10-31): verified g-format fallback and separate expression-edit path; cloud distance summaries also used general notation. Reviewed seven-file implementation is staged only as scratch copies under Build/Validation/DecimalUiDraft (not applied to actual sources). It renders bounded decimal presentation with explicit middle elision/full tooltip and edits full fixed-decimal round-trip values, preserving storage and drag precision. Standalone formatter checks passed extremes, promoted-float readability and GM. Headless ImGui harness passed25cases: drag keyboard/Ctrl/doubleclick and slider keyboard/Ctrl across -0,1e-20,1e30,denorm_min,max, with edit→Escape and Enter preservation. Actual UiFrame/engine tests and visual checks remain pending after application.

A near/1e12 full scene→owner→physics restoration/contact test is drafted in Build/Validation/SceneRuntimePlanetTests.next.cpp; original test file SHA256938EBCCE18111F8B2940935D1D63CA254AB83918470B980893648F9B4D1C8409. It is not applied/built yet. Generator's missing SV_Position cold retention remains a probable exact-reflection mismatch to check after currentbuild.

Prepared Build/Validation/run-shader-pass-isolation.ps1 runs importer regressions, captures actual Starter-generated HLSL during project creation, times both fixed DXIL passes, and compares against captured original binaries. Run only after build45673 successfully terminates. Its strict comparison may reveal the expected SV_Position difference; fix then rerun rather than weakening the gate.

Build45673 completed621steps successfully. Applied the reviewed decimal UI and far-origin reconstruction drafts, corrected Unlit test construction to create the proper Color pin, and explicitly retained SV_Position through the existing cold material-sentinel branch. First incremental29908 passed7steps; copied draft timestamps hid two test recompiles, so refreshed destination timestamps and removed duplicate-local warnings. Rebuild7303 passed6steps and explicitly compiled both UiPanelInputTests and SceneRuntimePlanetTests.

Focused validation passed10cases/3193assertions: decimal display/edit arithmetic and activation, far-origin frame-following full reconstruction with resumed support contacts at0 and1e12, static support promotion/identity, triangle mesh support, live-added supports, and owner-before-body restore. Log Build/Validation/ui-planet-focused-tests.log. This does not establish arbitrary scene-content migration or additive-world replay parity.

Shader importer/capture/timing/reflection harness now running session82465. Logs under Build/Validation/ShaderPassIsolation. No corrected-DXIL parity/timing claimed until terminal.

Shader harness82465 terminated failure: generated-HLSL importer andStarter schema passed, seven-texture opaque+transparent forwardDXIL timed out30s; unlit subcase had incorrect lit-ABI14 expectation (corrected to13, notyetrebuilt). Corrected-depth/Gbuffer timing/reflection stillpending because harnessstopped beforeStartercapture. Agent shader_abi_audit investigating forwardretention A/B in scratch. Broad UI integration35cases/1477assertions passed. No shaderproduction completion claimed.

### Shader compilation and decimal-field verification

The forward-pass slowdown was independently isolated to cloud-density expansion: the unchanged forward-retention baseline took 37.173 seconds, while preserving the three-sample density loop reduced current-generator compilation to 12.584 seconds. Removing the earlier fog-loop diagnostic annotation further measured 11.842 seconds. The production change preserves sample count, order, and effects; it does not raise the importer timeout. Final combined importer and binary ABI checks remain pending.

Decimal UI integration passed 35 cases / 1,477 assertions, and targeted planetary integration passed 24 cases / 470 assertions. Numeric fields display decimal notation and retain exact round-trip editing, with full tooltips for abbreviated long values. Editor visual acceptance remains pending.

## Earth and Mars playable showcase — requested October 31

Create a separate self-contained sample project; do not overwrite CloudIsolation or EarthSpaceFlight QA content. Reuse validated flight/controller code where suitable, but replace the geometric acceptance fixture with authored presentation and complete player workflows.

Required experience:

- An Earth surface launch site with convincing materials, terrain silhouettes, atmospheric depth, volumetric clouds, configurable day/night lighting, and visible stars in space.
- A distinct Mars destination with authored surface materials, atmosphere configuration, and a landing site; preserve physically meaningful world coordinates while offering accelerated travel for testing.
- FPS walking, ship boarding, manual flight, smooth cockpit/follow camera switching, landing, and safe disembarking. Retain an optional 2–3 minute guided demonstration route for repeatable comparisons.
- Clear controls and accessible UI, useful defaults and quality settings, recoverable errors, and no hidden dependence on unrelated QA assets.
- Test surface-to-space transitions, both destinations, repeated boarding/landing, save/reload, collisions and gravity, and standalone parity. Capture frame-time distributions at fixed quality/resolution; do not substitute screenshots for temporal or performance acceptance.

Sequence: complete current shader/UI verification; establish the separate project and minimal end-to-end playable route; fix planetary integration exposed by that route; then refine assets/lighting and perform visual and standalone acceptance. The existing procedural flight is a reference implementation, not a completed showcase or proof of production readiness.

October 31 combined checkpoint: built-in shader regeneration and the 13-step DevDist KeireTests build completed successfully. Combined importer/Starter/DXIL reflection validation is now running; no passing result claimed yet. The separate PlanetaryExplorer bootstrap now includes 63 selected authored files, NASA texture sources, new project/scene identities and physics/audio settings. Static dependency validation passed 51 known asset references and 26 metadata/source pairs. Engine import, gameplay and visual acceptance remain pending.

Replay audit additionally confirmed missing persisted scene-instance rebinding on fresh-process replay, including root-owned flat worlds. A complete fix must restore the loaded/persistent scene roster before planet ownership and per-scene physics, and map managed world IDs through stable scene identities. See the scratch implementation contract in Build/Validation/PlanetReplayAudit.md; no production replay fix is claimed yet.

Final shader validation, October 31: production importer regressions passed 3 cases / 289 assertions, including seven-texture opaque/unlit/transparent permutations. Starter project creation passed 1 case / 93 assertions. Direct compilation initially failed only on the non-ASCII absolute source path; rerunning with relative filenames passed. Corrected depth/velocity and deferred G-buffer compiled in approximately 0.20 and 0.22 seconds, respectively. Actual DXIL disassembly matched original resource binding and declared input/output signature contracts for both passes. Results: Build/Validation/ShaderPassIsolation/timings.json and per-pass reflection directories. Editor visual/GPU acceptance remains pending.

Showcase progress, October 31: separate Earth/Mars scene sources and texture material metadata are authored under PlanetaryExplorer; original QA scene hash still matches A85D25F5CE5A5073B1684BDF1889A48B95B694ABAF7A3F98D603E440F98CAB7C. Source-only manual flight/camera/autopilot modules are being prepared while the editor build runs. These have not been compiled or play-tested and do not establish physical ship collision, boarding, planetary character support, or Mars-specific weather. Poly Haven Terrain Red 01 (Rob Tuytel, CC0) diffuse/OpenGL-normal/roughness maps downloaded and verified against provider MD5; source URLs/SHA256 recorded outside the repo for landing-site authoring.

PlanetaryExplorer managed source check: bundled .NET SDK 10.0.302 compiled all four new flight source files against the actual DevDist Keire.Managed.dll with zero warnings/errors (Build/Validation/planetary-explorer-managed-build.log). Shared nearest-planet geometry passed focused standalone checks. This validates public API compatibility only; import, input, camera, depth and gameplay behavior remain untested. Independent per-planet media implementation contract is saved in Build/Validation/PlanetProfiles/ImplementationContract.md; shared-global atmosphere candidates and missing multi-domain clouds/fog remain real engine gaps.

Windows-control preparation October 31: sky.list_windows timed out on initial call, retry, and one retry after resetting/reinitializing node_repl. No app inputs were issued. Per the computer-use recovery procedure, desktop automation stopped for this attempt. Editor visual acceptance remains pending; this does not block source/CLI work. DevDist KeireClient build session52011 was still live at step231/289.

## Cloud variety and fog checkpoint — October 31

The revised sample-only cloud candidate uses varying 2.1–6 km envelopes, regional base offsets and bounded
coverage. Editor captures show more distinct gaps and taller formations during ascent, but this is **not an
accepted default**: near-cloud lighting is washed out and the full-size editor reported approximately 55 ms at
18.2 km and 36 ms at 128.1 km. These are UI observations, not controlled benchmarks. NVIDIA driver installation
occurred during the session, so pre/post-driver timings must not be compared as a performance result.

A conservative per-view height-fog optical-depth rejection is implemented and tested (104 focused cases /
1,903 assertions); the DevDist update staged successfully in 57.1 seconds. It retains local-volume and
orthographic paths. It does not solve the cost of distant cloud lighting or bound arbitrary HDR radiance.

Next: isolate evaluated timeline ambient and multiple scattering at the same camera pose before increasing
quality. AO=0 supplies unattenuated ambient throughout cloud interiors. The current planetary cloud lighting
cache covers approximately 12.5 km for this sample; outside it, occupied samples use direct shadow marches.
Consider a measured coarse outer cache with continuity/error tests rather than increasing sample counts.

The bright landing wedge has source evidence pointing to the Earth texture's longitude discontinuity at the
exact north pole: its top row contains a bright final 2% sector. The buried Showcase Plinth does not overlap
the new launch cap. A pole-continuous sample material or texture treatment remains needed; no fix is claimed.

Phone-review captures are saved outside the repository under the chat workspace screenshots folder:
planetary-cloud-variety-revised.png and planetary-cloud-variety-orbit-revised.png. The original QA scene remains
untouched. Two rendering-setting backups preserve both preceding cloud candidates.

## Planetary gravity integration batch — October 31 (build in progress)

Implemented shared native gravity sampling, an owning-world managed character query, and an explicit arbitrary-up
FPS motor overload. Existing stock controller behavior stays +Y until collision-safe alignment and authored
policy are integrated. Frame-following character guards remain intact. Managed production tests pass 52 checks;
focused motor checks pass including zero-G 100 m/s momentum across up changes (terminal cap applies only with
gravity). Native compilation and the new native regressions remain pending.

The pre-batch far-distance baseline passed 3 cases / 5,771 assertions: rotated distant character support,
reconstructed planetary support motion and far-origin frame-following contact restoration. These do not prove
character frame transfers or fresh-process additive-world replay. A real ScriptSystem execution test for the
new icall remains to be added beyond wrapper ABI tests and native query-helper tests.

Gravity batch validation completed: DevDist KeireTests build passed after correcting a test-only doctest
CAPTURE macro invocation. The focused gravity/cache/far-physics run passed 12 cases / 5,899 assertions.
The production ScriptSystem/Coral reload fixture passed 1 case / 1,328 assertions, including six compiled
managed callbacks through the newly registered gravity icall with exact world/entity and acceleration checks.
This supersedes the pending native/actual-icall notes immediately above. Editor/player service adapters still
need their DevDist build and interactive acceptance; complete planetary FPS alignment/transfers remain open.

DevDist editor/player staging passed (557.1 seconds). Strict Windows content cook of the separate Planetary
Explorer sample passed: 22 assets, one pack, runtime manifest schema 13 and expected startup scene. A 120-frame
headless startup run exited 0 and loaded Assembly-CSharp and KeireFirstPerson. This is startup/content evidence,
not complete interactive flight, gravity-character or fresh-process replay acceptance.

Visible standalone baseline completed 300 warm-up + 2,000 measured frames, immediate presentation, fixed
1280x720, RTX3060/D3D12 driver32.0.16.2101. Median CPU update0.3245ms, CPU render1.0124ms, frame-admission
wait13.8311ms (p95 16.2633), GPU retirement28.0628ms with two accepted frames in flight. Retirement includes
queued-frame latency and is NOT per-frame GPU execution time. Initial window occlusion/foreground change is
an uncontrolled condition; use this as diagnostic evidence, not a clean A/B optimization result or proof of
10ms performance. Report: D:/Projects/KeireValidation/PlanetaryExplorerStandalone20261031/surface-benchmark.json.
Standalone renders the same ship/HUD/clouds and reproduces the known polar texture wedge. Screenshot saved:
C:/Users/keith/Documents/ChatGPT/Keire/screenshots/planetary-standalone-surface.png.

## Cloud shape and cost follow-up — October 31

The current type mapping uses 35% to 100% of the authored thickness. With a 6 km layer, even the
shallowest type is 2.1 km deep. A genuinely thin deck alongside tall towers needs a wider authored
development range or separate layers; the current comparison must not be presented as complete cloud-type
authoring. Planetary weather also varies vertically, so base/type coherence through a cloud column needs
visual verification before changing its mapping. Preserve existing scenes when extending these controls.

Two appearance-preserving shader optimizations are prepared: reject density samples whose envelope cannot
cross the coverage threshold, and use power-of-two wrapping for planetary weather hashes. Shared production
kernel tests cover threshold neighbors and exact hash equivalence. Native/shader validation is pending.

A separate copied cooked output with only volumetric clouds disabled completed the same 300 warm-up / 2,000
frame benchmark. Median admission wait was 20.5787 ms, render CPU 1.1434 ms and queued GPU retirement
41.4067 ms. This was slower than the earlier clouds-on diagnostic, and both runs included foreground/occlusion
changes. These results do not isolate cloud cost or establish an optimization gain. Retain fixed visibility,
camera, time, resolution and GPU workload for the next controlled comparison. The original sample and its
cooked baseline were not modified by this diagnostic variant.

Lighting audit found no demonstrated duplicate atmosphere or exposure in the main cloud composition.
Isolate evaluated timeline ambient, multiple-scattering contribution and bloom one at a time at a fixed pose.
A separate parity issue remains: the exact-ray thin-surface fallback in BuiltinCloudComposite.hlsl does not
include terrain fog visibility, unlike BuiltinCloudTrace.hlsl. Verify and repair its shadow binding parity
with thin-silhouette regression coverage; this does not explain broad high-altitude cloud washout.

The existing staged standalone completed its interactive 150-second Earth-space-return trip, switched to
cockpit and back to follow view, and returned to Manual at displayed altitude 0.0 km. The player exited 0
after closing normally. Captures show ascent at 32.7 km, stars and the Earth limb at 141.3 km, and the final
surface view. These are discrete visual observations, not a continuous flicker recording or acceptance of
the new unbuilt optimization. Cloud washout and the known polar texture wedge remain visible.

Optimization validation completed: the official DevDist KeireTests build regenerated the affected builtin
shader artifacts and passed all 16 native build steps. Focused density/weather/planetary-cloud regressions
passed 14 cases and 74,140 assertions. Formatting and diff checks passed. This establishes preserved kernel
results, not a measured GPU speedup. Staged editor/player and post-change visual checks remain pending.

DevDist editor/player staging subsequently passed in 35.8 seconds. The updated editor opened the separate
sample successfully, displayed its cloud/ship scene without an import error, and was checked maximized before
closing between batches. The surface sanity capture is planetary-editor-cloud-optimization-check.png in the
chat screenshots directory. No obvious appearance regression was visible in that fixed view; animated
post-change flight and controlled GPU timing remain pending. Earlier flight captures used the preceding build.

## Thin cloud types, fog fallback shadows and shader preparation — October 31

The next batch adds Minimum Type Height (%) with a legacy-preserving 35% default and 1–100% range. Volume
schema 10 stores the authored fraction; schemas 1–9 migrate to 35%. The reserved RadiusGeometry.Z lane carries
it for flat and planetary views, zero retaining the old producer default. Light caches and temporal history
invalidate when the control changes. Sample authoring accepts schemas 9 and 10 without retuning existing
values. The isolated sample harness passes. This supersedes the earlier fixed-35% model limitation, but visual
acceptance of thinner regional bodies remains pending.

The composite fallback now uses the same terrain-fog shadow kernel as the main trace. Direct DXIL/SPIR-V/MSL
compilation and reflection pass: volume 1616 bytes at b0, cache 48 at b1, fog shadows 304 at b2, shadow t5/s5 and
cache storage t6. Generated embedded fragments match those validated outputs byte-for-byte. Evidence is under
Build/Validation/CloudTypeFogReflection. Native tests and thin-silhouette visual acceptance remain pending.

Shader preparation used one aggregate fingerprint per generator: a cloud edit triggered 93 builtin calls and
69 VFX calls. VFX fragment cloud dependencies are real; independent visibility shaders do not share them. New
Windows/Unix helpers cache individual outputs by recursive source/include closure, compiler and adjacent
library identity, stage, entrypoint, destination and options. They verify stored hashes and reject failed,
empty or concurrently changed inputs. Existing outer locks and content-aware header publication remain.
Isolated fake-compiler regressions pass on PowerShell and Git Bash, including dependency isolation, corruption
and failure recovery. A first real population and repeat-run timing are still needed; no speedup is claimed.

The existing Windows generated-content regression also passes. The existing Unix regression reaches its
simulated vanished-PID lock race, then exceeds that test's two-second deadline under Git Bash. A traced rerun
reproduced the timeout; no existing timeout was relaxed. This is an unpassed compatibility check on this host,
not evidence of a fully validated Linux/macOS build. The new Unix shader-cache harness passes separately.

Focused native validation passed 62 cases / 95,105 assertions, with formatting and diff checks passing.
Final integration review also caught the planetary light-cache reframe recreating geometry with the legacy
minimum type height. It now preserves the authored fraction when snapping the cache frame; the next stage
build includes that correction. Initial staging exposed Unicode-path handling in the new Windows blob cache
(the real compiler cannot read the accented repository path directly); correction and real warm-up are pending.

The corrected Windows compiler staging uses an ASCII dependency snapshot while retaining original source
identities in the cache key. Unicode-path regression and a real DXIL compile pass; the probe matches the
previous embedded output. Unix prepared identities are now revalidated before cache hits as well as after
compilation. Both isolated blob-cache harnesses pass. The official DevDist stage then passed all 166 native
steps in 774.5 seconds, including first population of 162 shader blobs. Warm unchanged builtin+VFX preparation
subsequently completed in 0.748 seconds; this is not an edited-source speedup measurement.

The rebuilt editor was checked maximized in the separate PlanetaryExplorer project. Minimum Type Height
loaded the legacy 35%, accepted 8%, and saved volume schema 10 / minimumTypeThickness 0.08 while retaining
48 view / 6 shadow samples. A paused ascent capture at 7.4 km shows cloud bodies with varied vertical extent,
but lighting remains pale/soft and observed editor frame times are roughly 45–53 ms in this dense-cloud view.
These status-bar observations are not a controlled GPU benchmark or production acceptance. Screenshots in
the chat screenshot folder: cloud-height-35-percent-baseline.png, cloud-height-8-percent-comparison.png and
planetary-cloud-height-variety-7km.png. The later Play-mode comparison was confounded when clipboard paste
into an Environment numeric field triggered the demo camera's V hotkey; follow camera was restored. Record
editor/gameplay input isolation as a follow-up. No claim of clean fixed-camera Play-mode A/B or continuous
flicker verification. Thin-silhouette fog visual acceptance remains pending.

Editor closed normally after discarding temporary flight transforms; the sample retains the 8% candidate.
The final focused tests, relinked against the corrected cache implementation, pass 62 cases / 95,105 assertions.
Original GravityAcceptanceQA scene SHA256 remains
A85D25F5CE5A5073B1684BDF1889A48B95B694ABAF7A3F98D603E440F98CAB7C.

## Follow-up: authoring input isolation and empty cloud-shadow paths

The Play-mode paste finding was traced to managed raw keyboard reads bypassing editor text-input capture.
A workspace-owned gate now suppresses managed action/control values and edges while editor text input is
active and for the first neutral frame after editing ends. Persistent Game ownership, cursor/Escape policy,
and runtime HUD routing remain separate. Focused input tests pass 6 cases / 84 assertions; real-paste acceptance remains pending.

Cloud shadow marching now returns zero before density queries when the flat shadow distance or both planetary
cloud intervals are empty. Planet-ground occlusion remains checked first and still returns opaque depth.
Occupied-path sampling is unchanged. Shared-kernel regressions cover empty, tangent, zero-budget and nonempty
shell paths. Focused cloud geometry, density, cache, temporal and fog-binding tests pass 51 cases / 111,111 assertions. This is a bounded work reduction, not a demonstrated dense-cloud frame-time fix. The official DevDist stage and visual acceptance remain pending.

Follow-up acceptance: the official DevDist stage passed in 285.4 seconds (63 native steps).
In maximized PlanetaryExplorer Play mode, double-clicking Cloud Base Height, pasting 1500 and
committing with Enter retained follow view and the Cockpit [V] button label. The HUD camera
button subsequently switched to cockpit and back normally. Screenshot:
planetary-input-isolation-verified.png in the chat screenshot directory. The test used the
existing value, preserving authored cloud settings. Temporary Play transforms were discarded,
and the editor exited normally. This verifies the reported paste/camera regression; it is not
full keyboard-device or planetary travel acceptance. Surface cloud rendering remained visible,
but pale lighting and the Earth polar texture wedge remain outstanding. No controlled GPU
speedup is claimed. Changed C++ files pass clang-format dry-run, and git diff --check passes.
