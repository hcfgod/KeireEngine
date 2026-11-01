# Planetary Explorer foundation

## Verified development checkpoint — October 31

The isolated showcase now opens in the DevDist editor. Its compact flight HUD, pause/resume,
cockpit/follow switching and 150-second Earth excursion have been exercised in Play Mode.
The trip returned to surface altitude without a scene load. Matching the two cameras' altitude-aware
near planes removed the large depth patches observed when switching to cockpit at approximately
149 km. This is visual development evidence, not standalone or long-session acceptance.

The Environment panel's scattering fields were also inspected: values such as Rayleigh scattering
display fixed decimals rather than scientific notation. A warm asset refresh recorded 37 cache hits
in approximately 3 seconds; the first 37-asset import took approximately 106 seconds, so cold-start
performance remains an open concern. Managed script compilation is additional startup work.

Surface geometry and orbital appearance still need improvement. In particular, the original built-in
sphere has only 16 latitude segments, which is unsuitable for a close-up Earth-sized surface.
The optional [NASA orbiter workflow](NasaOrbiter.md) adds a credited model with verified import
and scene-wiring tests; its appearance still requires editor acceptance.

This is a separate showcase foundation, not the finished Earth/Mars game. Run `Bootstrap.ps1` from PowerShell to create `D:/Projects/KeireValidation/PlanetaryExplorer`. The destination must not already exist. The existing EarthSpaceFlight QA project is read only.

The bootstrap copies a selected authored-source dependency closure, the first-person controller/input, existing flight/UI sources, shared shaders, and explicit project settings, including physics/audio authoring and a portable desktop build profile. Settings participate in dependency closure, so the default audio mixer is included. It writes a source SHA256 inventory and new project/startup-scene identities. It does not copy Library, caches, editor layouts, binaries, unrelated scenes, or machine-specific editor preferences. No import or compilation is performed.

Bootstrap output: copied single-Earth scripted flight scene, atmosphere, volumetric clouds, fog, stars, and time settings. At this first step, NASA Earth/Mars source images and their credit document are included but are not assigned to materials. The subsequent Author-PlanetSurfaces.ps1 step below assigns those textures and writes explicit import metadata. The baseline scene is renamed PlanetaryExplorerBaseline. Existing component and referenced asset identities are preserved within this separate project.

The surface-authoring and manual-flight scripts below add textured Earth/Mars, kinematic ship controls, a HUD, and blended cockpit/follow cameras. Remaining showcase work includes distinct landing sites, a detailed spaceship and cockpit, on-foot/boarding/landing/exiting states, and a safe exit query. These scripts do not provide physical ship flight. Stock FPS movement assumes world-Y up and needs work before arbitrary planetary surface walking. Individual Earth/Mars environment profiles also require verification/implementation.

Acceptance before claiming the showcase works: fresh import with no missing materials or input; walk/board/fly/land/exit on both sites; airborne exit rejected; repeat boarding and camera transitions; atmosphere/cloud continuity and night lighting; standalone run and measured frame-time/startup checks. Initial editor loading exposed a malformed script asset identifier, now corrected in the installation script; visual and interactive acceptance remains pending.

Source dependencies default to D:/Projects/KeireValidation/EarthSpaceFlight and D:/Projects/KeireValidation/PlanetaryShowcaseSources/NASA. Parameters allow different locations. NASA credits and usage references are preserved in Assets/Textures/NASA/SOURCES.md; no endorsement is implied.

Run Validate-SourceDependencies.ps1 for static completeness against known source asset IDs. It validates metadata identifier syntax, detects duplicate IDs across canonical/compact UUID forms, and checks source pairs and the startup scene. Run Test-SourceDependencies.ps1 for isolated regression fixtures; it does not modify the QA projects. These checks do not validate engine-reserved ID meaning, managed compilation, imports, or gameplay. Bootstrap resets the player title/product/application ID and clears source-machine external-editor selection. The original sandbox template provenance and FirstPerson assembly source roots are retained; they are not runtime dependencies on the original QA directory.


## Textured surface authoring

After Bootstrap.ps1, run Author-PlanetSurfaces.ps1 once. It preserves PlanetaryExplorerBaseline and creates a distinct startup scene, PlanetaryExplorer, with Earth/Mars standard shader-property materials and explicit texture metadata. The engine's supported .keiremateriallegacy property format is used; no material graph is generated. Both maps use sRGB/mipmaps and longitude-repeat/latitude-clamp sampling.

Mars has a separate planet/frame record and radius of 3,389,500 metres, placed 40,000,000 metres along X for this compressed showcase. This is not real astronomical separation. Manual environment selection deliberately stays on Earth: Mars-specific atmosphere/dust/cloud profiles remain unimplemented in this sample. Do not switch to nearest-planet selection and assume the shared Earth weather becomes correct for Mars.

The source scene contains textured spheres, not surface terrain or collision coverage. No manual travel, boarding, Mars landing, import success, or visual quality is claimed by this authoring step. The automated Earth flight is retained as a baseline. Import and inspect texture orientation, sphere UV seams/poles, lighting, and distant precision before accepting the appearance.

## Manual ship source milestone

Run Install-ManualFlight.ps1 after surface authoring. It adds focused ShipFlightController, ShipCameraRig and FlightAutopilot sources and a separate HUD, replacing the FlightDirector component only in the new showcase scene. The original baseline scene/script/UI remain intact.

Controls: WASD forward/strafe; arrows yaw/pitch; Space/Q nearest-planet radial movement; Shift boosts from 1,000 to 10,000 metres/second. V blends cockpit/follow; P pauses translation; Enter starts a 150-second radial excursion from the current location around its nearest planet; R returns control immediately without teleporting. The demo starts piloting, not on foot. Camera switching remains available while paused.

These are kinematic demonstration controls, with analytic planet-radius exclusion boundaries only. They do not implement collision sweeps, inertial/rocket physics, safe landing or airborne exit, rotating-frame attachment, arbitrary radial-up walking, a finished cockpit interior, or Earth/Mars transfer autopilot. Altitude and radial excursion direction use the nearest planet. The showcase camera far plane uses the engine-supported maximum of 10,000,000 metres. Mars is initially outside that mesh visibility range; travel closer before expecting to see it. Adaptive near-plane behavior is retained; cockpit and distant-surface depth quality still require visual acceptance. All four managed source files compiled against the actual DevDist Keire.Managed.dll with zero warnings/errors on October 31. Source dependency and focused geometry checks also passed. Engine import and interactive checks remain required.

Run Test-SetupContracts.ps1 to exercise bootstrap and manual-flight installation against isolated minimal fixtures. It checks that copied authoring settings select the valid system editor, source settings remain unchanged, and the generated flight camera stays within its supported range. It does not import assets or establish rendered correctness.

The manual-flight HUD has its own compact stylesheet and short button labels. To refresh only that HUD in an existing showcase after stopping Play Mode, run `Update-FlightHud.ps1 -Project <project-folder>`. It replaces the showcase HUD/style sources and their metadata, retaining the existing HUD asset identity. It does not rewrite the scene, flight scripts, or original baseline UI. The setup contract also checks those refresh boundaries and required controller targets.

Run `Test-FlightHudImport.ps1` with the staged DevDist tools available to validate the actual visual-tree and stylesheet importers in an isolated temporary project. The HUD uses stylesheet schema 2 for text wrapping; the test also confirms that downgrading it to schema 1 is rejected. Pass `-AssetTool <path>` to select another compatible staged tool executable.

Additional close-up Mars surface sources are staged at D:/Projects/KeireValidation/PlanetaryShowcaseSources/PolyHaven/TerrainRed01 (CC0, Rob Tuytel; credits and hashes alongside maps). They are not yet imported or assigned. Use their tiling PBR maps for a landing site rather than stretching the planetary globe texture over nearby terrain.

Shared PlanetaryFlightGeometry selects nearest surface and supplies HUD altitude, radial lift, autopilot direction and analytic safety. Finite inputs are checked and a deterministic up direction handles planet centers. Incremental quaternion steering preserves initial ship orientation. Both camera views ramp the near plane from 0.2 to 10 metres with altitude. Switching to cockpit no longer reduces planetary depth precision; a future close-up cockpit interior requires a separate near-field rendering solution. Test-FlightGeometry.ps1 compiles and exercises the actual dependency-free geometry helper without an engine build.
## Explicit sky-profile authoring

After the updated schema-4/profile-capable editor has been built and Play Mode is stopped, run
`Author-PlanetAtmospheres.ps1 -Project <project-folder>` once to create explicit Earth and Mars sky profiles.
The script is restricted to this sample's project and planet identities. Earth copies the existing global
atmosphere exactly; Mars defaults to an airless opaque reference planet for testing that it does not inherit
Earth's optical medium. It leaves global rendering, scenes, geometry, planet selection, clouds and fog unchanged.

For an illustrative thin optical medium instead, choose `-MarsMode Thin -MarsDensityScale 0.02` before the first
run. The scale can range from 0 through 1. This is an artistic test profile, not calibrated Martian atmospheric
physics. It does not author independent dust weather, cloud layers or fog for Mars. Use **Environment > Sky >
Planet Placement and Ownership** to add cloud and fog overrides for each planet. New overrides copy global
settings; disable their cloud layers, volume and fog for an airless world. Removing an override restores global
inheritance. Keep manual Earth selection until the other planet's weather has been authored and checked.

The script preserves a byte-exact `ProjectSettings/Planets.before-atmosphere-profiles.keiresettings.backup`,
refuses existing atmosphere overrides or an existing backup, and writes planet schema 4 only after validation.
Existing cloud and fog overrides are preserved. It does not
replace an old installed editor or import assets. Back up and edit authored profiles through the updated editor;
do not rerun this setup expecting it to overwrite your work. `Test-PlanetAtmospheres.ps1` exercises isolated
temporary projects and does not touch the QA project. Script validation does not establish GPU appearance,
eclipse correctness, runtime manifest export or complete planetary travel acceptance.
## Cloud shape variety comparison

`Author-CloudVariety.ps1 -Project <project-folder>` is a one-time, sample-only shape starting point, pending
visual and performance acceptance. Run with the editor closed or Play stopped and settings saved. It requires
cloud settings version 3 / volume version 9 or 10 and the existing 48 view samples, 6 shadow samples, zero ambient
occlusion budget; it rejects other quality budgets rather than overwriting them.

The revised comparison uses a 6,000-metre thickness and 2,500-metre pattern size, Cloud Type 0.55 / Type Variation
1.0, coverage 0.5, regional strength 0.7, detail 0.6 at frequency 2, erosion 0.25, and vertical profile
`[0, 0.08, 0.4, 1]`. With the legacy 35% minimum, regional cloud types can use approximately 2,100 to
6,000 metres of the height envelope. In the Environment panel, **Minimum Type Height (%)** adjusts the thin
end of regional types from 1% to 100% without changing the maximum layer height or density. For example, an
8% minimum permits a 480-metre shallow envelope beside fully developed 6,000-metre clouds. This requires
Independent Weather; Type Variation determines how much regional variation occurs. Thin features need
sufficient view and shadow samples and must be checked during camera motion and ascent/descent.

Volume version 10 stores this control as `minimumTypeThickness`; older records retain 35%. The authoring
script preserves an existing version-10 value exactly and leaves version-9 records unchanged in schema;
it does not silently opt the project into thinner clouds. Visual acceptance must still verify visible
thin/tall variety, stable edges through transitions, and frame time at the selected sampling budget.
Base height stays at 1,500 metres with 800 metres of regional variation. These are candidate artistic values,
not a guarantee of particular cloud silhouettes.

The earlier full-strength regional weather could produce coverage 1 in cloudy regions. In the density shader,
that removes the shape threshold and fills much of the height envelope; high extinction can make it read as
a broad soft slab. Regional strength 0.7 keeps maximum local coverage at 0.85, retaining a shape threshold.
Lower detail frequency makes the secondary billows easier to resolve at the existing march budget. Warp period
64 and weather region period 2 remain unchanged: warp scale does not control weather type variation, and
reducing it at the same strength increases conservative filtering and can blur detail.

Thickness, pattern size, height profile and weather type control shape. Extinction controls opacity and light
absorption; raising it does not make a flat cloud taller. This script leaves extinction, lighting, wind, seed,
quality, layered clouds and all non-cloud settings unchanged. A thicker occupied shell can still increase GPU
work or reduce march detail with the same sample count; compare stabilized and moving-camera frame times.
This script adjusts the global cloud defaults. Per-planet cloud overrides are independent copies and must be
edited separately; planets without an override inherit these defaults.

The original rendering file is preserved byte-for-byte in
`ProjectSettings/Rendering.before-cloud-variety.keiresettings.backup`. Existing backups prevent reapplication.
`Test-CloudVariety.ps1` passes isolated checks for the shape values, bounds, preservation and rejection paths;
it does not modify the QA project, run shaders, or establish appearance and performance acceptance.
