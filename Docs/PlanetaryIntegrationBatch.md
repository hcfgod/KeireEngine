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
