# Kéire

[![CI](https://github.com/hcfgod/KeireEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/hcfgod/KeireEngine/actions/workflows/ci.yml)
[![Security](https://github.com/hcfgod/KeireEngine/actions/workflows/security.yml/badge.svg)](https://github.com/hcfgod/KeireEngine/actions/workflows/security.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE.txt)

A reproducible C++20 foundation with a static KeireCore library, a project-first editor Hub, typed scene and input assets,
asynchronous reference-counted loading, deterministic cooked packs, a production application/layer runtime, typed events,
Unity-style frame timing, application-owned transactional undo/redo, an SDL3 multi-window and SDL_GPU platform layer,
private Dear ImGui UI, reusable strong/weak
references, private asynchronous logging, Premake generation, sanitizers, SDK packaging, and Windows/Linux/macOS
automation.

The editor now renders resizable Scene and Game views through an application-owned SDL_GPU pipeline. The starter scene
contains a primary Camera, a tintable cube, and a downward-angled Directional Light; Scene view adds a depth-tested grid
plus Unity-style framing, locking,
orbit, camera-local pan, dolly, fly, orthographic, and axis-snap navigation. The Project panel provides extension-free
List/Grid views, contextual creation, multi-item file operations, delayed metadata cards, and persistent recoverable trash.
Pinned host-side shader tooling imports HLSL into DXIL, SPIR-V, and MSL while runtime/public APIs remain backend-free.

KeireCore is static by default. Its export annotations prepare for a possible same-toolchain C++ shared-library build; they are not a stable cross-compiler ABI. Generated build identity, development assertions, and a core-owned entrypoint keep startup policy consistent while the client supplies only its application factory.

## Quick Start

Clone submodules with the repository:

```sh
git clone --recurse-submodules https://github.com/hcfgod/KeireEngine.git
cd KeireEngine
```

Windows PowerShell:

```powershell
./Scripts/project.ps1 bootstrap -Generator vs2022
./Scripts/project.ps1 test -Generator vs2022 -Configuration Debug
./Scripts/project.ps1 run -Generator vs2022 -Configuration Debug
```

Linux:

```sh
bash Scripts/project.sh bootstrap --generator ninja --toolset gcc
bash Scripts/project.sh test --generator ninja --configuration Debug --toolset gcc
bash Scripts/project.sh run --generator ninja --configuration Debug --toolset gcc
```

macOS:

```sh
bash Scripts/project.sh bootstrap --generator ninja --toolset clang
bash Scripts/project.sh test --generator ninja --configuration Debug --toolset clang
bash Scripts/project.sh run --generator ninja --configuration Debug --toolset clang
```

Run `Scripts/project.bat` on Windows or `Scripts/project.sh` on Unix without a command for an interactive menu. Commands work from the repository root or from `Scripts`.

## Commands

| Command | Purpose |
| --- | --- |
| `bootstrap` | Verify/install tools and restore locked vendor dependencies |
| `generate` | Generate IDE, Ninja, Make, or compile-database files |
| `build` | Build a selected target and configuration |
| `test` | Build and run the doctest executable |
| `run` | Build and run the Project Hub; optional flags open a project editor directly |
| `coverage` | Run Clang source coverage and enforce 80% line coverage |
| `package` | Test, smoke-run, and create runtime/SDK archives and checksums |
| `package-editor` | Build, validate, and archive the native Dist editor distribution |
| `package-hub` | Build, validate, and archive the standalone Dist Hub distribution |
| `package-installer` | Build the Dist editor and create the native Windows, macOS, or Linux installer |
| `package-hub-installer` | Build the standalone Dist Hub and create its native installer |
| `doctor` | Report detected tools, versions, identity, and environment |
| `clean` | Remove build, generated, or all disposable outputs |
| `vendor-update` | Intentionally update one dependency lock and working tree |
| `rename` | Transactionally rename the complete template |
| `help` | Show launcher syntax and supported values |

Common examples:

```powershell
./Scripts/project.ps1 generate -Generator ninja -Architecture ARM64 -Toolset clang -Force
./Scripts/project.ps1 clean
./Scripts/project.ps1 clean -CleanScope generated
./Scripts/project.ps1 package -Generator vs2022 -Configuration Dist
./Scripts/project.ps1 package-editor -Generator vs2022
./Scripts/project.ps1 package-hub -Generator vs2022
./Scripts/project.ps1 package-installer -Generator vs2022 -Toolset msc
./Scripts/project.ps1 package-hub-installer -Generator vs2022 -Toolset msc
#Local diagnostics only; rejected in CI and marked as a development artifact.
./Scripts/project.ps1 package -Generator ninja -Configuration Release -AllowDirty
```

```sh
bash Scripts/project.sh generate --generator ninja --architecture ARM64 --toolset clang --force
bash Scripts/project.sh clean
bash Scripts/project.sh clean --clean-scope generated
bash Scripts/project.sh package --generator ninja --configuration Dist --toolset clang
bash Scripts/project.sh package-editor --generator ninja --toolset clang
bash Scripts/project.sh package-hub --generator ninja --toolset clang
bash Scripts/project.sh package-installer --generator ninja --toolset clang
bash Scripts/project.sh package-hub-installer --generator ninja --toolset clang
#Local diagnostics only; rejected in CI and marked as a development artifact.
bash Scripts/project.sh package --generator ninja --configuration Release --allow-dirty
```

`default` resolves before Premake runs: MSVC for Visual Studio and Windows Ninja, GCC for Windows GNU Make and Linux, and Clang for macOS. Generation stamps record the concrete toolset.

`clean` defaults to the `full` scope, which removes the entire disposable `Build` directory, package artifacts, and
generated project files. Use the narrower `build` or `generated` scope only when retaining the other group's state is
intentional.

`package-editor` always uses Dist. It leaves an unpacked, ready-to-run editor under `Build/Distributions/` and writes
the host-native archive plus SHA-256 file under `Artifacts/`. Run it on Windows, macOS, and Linux to produce the three
distributable builds; native executables, SDKs, and system frameworks must be packaged on their target OS. Each
distribution contains the editor and editor-specific companion tools, the complete bundled .NET 10 SDK, sample
project, notices, and a platform launcher. Editor packages do not contain the Hub executable, its private HubWorker,
or Hub-owned fonts, catalogs, templates, learning content, launchers, or desktop integration. macOS packages include an
Editor `.app`, while Linux packages expose `keire-editor`.
The Unix editor launcher accepts `--project <path>`; normal project selection and editor-version dispatch remain in the
separately installed Hub.
Its `editor-package.json` uses schema 2 and records stable channel/host identity, typed entrypoints, the supported
project schema range, source-module and canonical manifest fingerprints, optional packaged templates, bundled
toolchains, license and release-note references, a SHA-256 file inventory, and total installed size. The schema-1
top-level identity, launcher, bundled .NET SDK, and build-manifest fields remain present for existing installer and
discovery consumers.
Editor manifests expose only the editor and editor-specific tool entrypoints; Hub and worker entrypoints belong to
`hub-package.json`.

For Hub-managed online installs, build the `KeireHubPackagePublisher` target and run its `create-editor` command against
the unpacked schema-2 editor distribution. It rehashes the complete payload and writes a `.keirepackage` plus canonical
catalog manifest. `Scripts/Packaging/prepare-distribution-snapshot.py` verifies that pair and creates the immutable
catalog/package staging layout consumed by the offline Ed25519 publisher and read-only distribution service. See
[Generic Package Archives](docs/PackageArchives.md) and the
[Distribution Service guide](Services/KeireDistributionService/README.md). A public Hub package must be generated with
the deployed HTTPS service URL and its trusted release public key; without both, online discovery remains intentionally
disabled while installed editors and cached content continue to work.

Editor installs are reviewed in a version-aware Hub dialog before they enter the persistent task queue. The title-bar
Tasks and Notifications controls show the same live download, verification, extraction, and installation progress,
including pause, resume, cancel, and retry actions. On Windows, Hub-owned paths and worker command lines remain UTF-8
safe; installations created by earlier builds beneath a `KÃ©ire` storage component are migrated to `Kéire` on startup
with their cached catalogs and operation journals preserved.

`package-hub` also always uses Dist, but builds and stages only the Hub lifecycle, its private `KeireHubWorker` task
process, runtime files, branding,
licensed fonts, packaged documentation, sample content, the validated template catalog/payloads, and licenses. It does
not copy the versioned editor, editor-specific Asset Tool or Asset Worker, SDK
headers, static libraries, or consumer examples. The unpacked product remains under `Build/Distributions/`; a native
archive, checksum, and schema-2 `hub-package.json` are written under `Artifacts/`. Run this command independently on
Windows, macOS, and Linux; macOS output includes a native `.app` launcher.
Test and package launchers run repository-dependent executables from the repository root, regardless of the directory
from which the launcher was invoked.

The Hub optionally uses Supabase Auth for email accounts and the owner-scoped profile display name. Only the public
desktop project URL and `sb_publishable_...` key are packaged; software downloads remain authorized solely by signed
distribution catalogs and are available without signing in. Windows stores a rotated refresh token through DPAPI;
platforms without an implemented native secure store use session-only authentication. When email confirmation is
enabled, account creation asks the user to check their inbox instead of inventing a signed-in session; immediate repeat
attempts report the confirmation-email cooldown directly.

`package-hub-installer` builds only the validated standalone Hub stage. Windows uses a Hub-only NSIS 3 template;
macOS creates a self-contained Hub `.app` in a drag-to-Applications `.dmg`; and Linux creates a `keire-hub` `.deb`
under `/opt` with an explicit `/usr/bin` Hub wrapper and desktop entry. Updates and uninstall remove only the verified
application payload;
per-user preferences, caches, project metadata, and managed or external editor roots are preserved by default.

`package-installer` builds on the validated editor distribution. Windows uses NSIS 3 to create a per-user setup wizard
with selectable installation directory, Start Menu shortcuts, an optional desktop shortcut, launch-on-finish, upgrade
registration, and a guarded uninstaller. Its icons, finish action, and shortcuts launch the editor executable; it does
not install or claim Hub files or shortcuts. Install NSIS with `winget install NSIS.NSIS`. macOS creates a
self-contained Editor `.app` in a drag-to-Applications `.dmg` using the system `hdiutil`, `sips`, and `iconutil` tools.
Linux creates a desktop-integrated `keire-editor` `.deb` using `dpkg-deb`; the `.tar.gz` from `package-editor` remains
the distribution for other Linux families. Installers and SHA-256 files are written under `Artifacts/` and must be
produced on their target OS. The Hub and editor DEBs use regular `/usr/bin` wrappers that execute their explicit
`/opt/.../bin` target; the editor package also declares the baseline native runtime and curl dependencies.
macOS first-party and source-built native dependencies share the `12.0` deployment target pinned in
`Config/Dependencies.lock`; packaging confirms Mach-O load commands with `vtool` or `otool`. Signing proceeds
inside-out without `--deep`, preserves and verifies bundled Microsoft .NET signatures byte-for-byte, and applies the
reviewed managed-runtime entitlements only to the editor host. The native Hub receives no managed-host entitlements.

## Architecture workflows

The engine now has application-owned jobs, tracked memory domains and arenas, typed generational handles, structured
diagnostics, per-class streaming budgets, frame-safe GPU retirement, deterministic replay, transactional project
upgrades, and source-level modules. The editor's **Window > Architecture** and **Window > Render Graph** panels inspect
these systems without exposing renderer or platform implementation types.

Project upgrade preview is the default; mutation requires an explicit operation:

```powershell
KeireAssetTool upgrade-project --project C:\Projects\Game
KeireAssetTool upgrade-project --project C:\Projects\Game --apply
KeireAssetTool upgrade-project --project C:\Projects\Game --recover
```

Runtime replay automation supports certified strict verification and non-certified performance capture:

```powershell
KeireRuntime --content Cooked --record Captures\run.keirereplay --profile strict --tick-limit 3600 --headless
KeireRuntime --content Cooked --verify Captures\run.keirereplay --headless --output Captures\result.json
```

Standalone desktop players use saved Build Profiles and separately installed Build Support. In the editor, open
**Build > Build Profiles**, choose Windows/Linux/macOS and x86_64/ARM64, then use **Build** or **Build & Run**. Successful
players are published to `<Project>/Build/<profile-output-slug>/`; foreign targets can be assembled on any host, while
Build & Run requires a matching host. Missing support opens the Hub's **Build Support** page for the selected target.
Player icons are selected from imported image assets, Windows game executables launch without a console, and packaged
runtime scenes plus authored Game UI present directly through the native renderer without initializing editor ImGui.

Automation uses the same isolated pipeline:

```powershell
KeireAssetTool build-player --project C:\Projects\Game --profile "Windows Dist" --status build-status.json
```

See [Desktop Player Builds](docs/PlayerBuilds.md) for profile/settings files, offline support installation, layouts,
signing-hook JSON, release scripts, and the native platform matrix.

SDK source modules are compiled with the application. See `Examples/SourceModule`: CMake's
`keire_define_source_module_pack` creates the shared static pack and `keire_link_source_module_pack` links it into each
host. `Config/SourceModules.premake.lua` provides the equivalent Premake helpers. This is a source contract, not a
runtime-loadable binary plugin ABI.

## Build Matrix

Configurations are `Debug`, `Release`, `Dist`, `DebugASan`, `DebugUBSan`, `DebugTSan`, and `Coverage`. Release and Dist define `NDEBUG`; Dist also enables link-time optimization. CI adds fatal warnings.

| Platform | Generators | Toolsets | Architectures |
| --- | --- | --- | --- |
| Windows | VS2019, VS2022, VS2026, Ninja, GNU Make, compile commands | MSVC, GCC, Clang where compatible | x86_64, ARM64 |
| Linux | Ninja, GNU Make, compile commands | GCC, Clang | x86_64, ARM64 |
| macOS | Ninja, Xcode, GNU Make, compile commands | Clang | x86_64, ARM64 |

ASan is supported on all three operating systems. UBSan and TSan are supported with GCC/Clang on Linux and Clang on macOS, but rejected with MSVC. Coverage requires Ninja and Clang and writes LCOV plus HTML under `Build/Coverage/<system-architecture>`.

## Project Layout

```text
Config/                 Project identity, client JSON, and immutable dependency lock
SourceModules/           Shared source-level module pack linked into every engine host
KeireCore/              Static library and public Keire/<header> API
KeireClient/            Console application
KeireHub/               Project discovery, creation, and editor launcher
AssetTool/              Source scan, import, cook, and package validation CLI
KeireAssetWorker/       Private process-isolated editor import and cook worker
KeireTests/             Independent doctest cases
Examples/SourceModule/  Packaged source-module SDK consumer
Samples/KeireSandbox/   Validated starter project packaged with the SDK
Vendor/                 Pinned runtime, test, UI, compression, ECS, and math submodules
Scripts/Premake/        Shared policy and tracked private dependency project definitions
Scripts/Unix/           Shared macOS/Linux implementation
Scripts/<platform>/     Platform bootstrap and wrappers
Tools/<platform>/       Ignored, checksum-verified local tools
docs/                   Architecture and focused subsystem guides
.github/workflows/      CI, compatibility, security, and packaging
```

The root Premake file owns first-party targets plus dependency projects. `KeireCore`, `KeireClient`, `KeireHub`,
`KeireAssetTool`, and `KeireTests` own first-party definitions; private `DearImGui` and `Zstd` static libraries plus
header-only EnTT and GLM utility projects are grouped under `Dependencies`.
Generated dependency metadata remains below ignored `Build/Projects`. Public consumers include `Keire/Core.h` and link
only the supported `Keire::Core` package target.

## Documentation

The [documentation index](docs/README.md) links focused guides for projects, Project Settings, the Hub, scene
runtime/authoring, rendering, shaders/materials, undo/redo, input debugging, architecture, runtime lifecycle, the UI
workspace, testing/release workflows, and the comprehensive [C# scripting guide set](docs/Scripting/README.md).

The Scene view uses the selected runtime Camera's clear color while retaining its independent editor viewpoint. Its
`Q/W/E/R` View/Move/Rotate/Scale tools support Local/Global handles, configurable position/rotation/scale snapping, and
camera/light gizmos. Project ambient color, intensity, and exposure are edited through **Edit > Project Settings...**
and light both Scene and Game views together. A built-in studio sky renders by default; the same panel provides a
searchable asset picker for custom HDR, equirectangular, cross-atlas, and strip-atlas environments. The sandbox PBR
material uses those environments for cached spherical-harmonic diffuse irradiance and split-sum specular response;
sky rotation and the independent diffuse/specular intensity controls affect both the background and material lighting.
Scene and Game surfaces render through the private frame graph into RGBA16F, resolve with fitted ACES, upload bounded
Forward+ tile lists, and instance compatible opaque objects on a dedicated renderer submission thread. Renderer
statistics expose graph transitions, transient allocation slots, instance batches, queue high-water mark, and CPU
preparation p95 for scalability captures. Steady-state skinning, instance, and Forward+ transfers share the frame
command buffer; the upload-submission counter identifies fallback resource-publication work. Command-recording
statistics separate skinning, VFX, draw preparation, shadow, Forward+, scene, sampled-depth, tone-map, and residual
costs. The configured frames-in-flight value is applied to SDL's bounded GPU presentation queue and reported alongside
swapchain wait time; higher applied values favor throughput at the cost of additional presentation latency.

Spatial lighting is authored from **Window > Lighting**. Add Reflection Probe and Light Probe Volume entities from the
Entity or Hierarchy menus, mark contributing Mesh Renderers static, choose Realtime/Baked/Mixed on lights, and enable
emissive GI on contributing materials. Realtime shadows update without a bake. **Bake Lighting** runs in the isolated
asset worker and publishes lightmaps,
eight-channel mixed shadow masks, reflection cubemaps, and SH9 probe volumes as one cached lighting set. The equivalent
headless workflow is:

```powershell
.\Build\Bin\Debug-windows-x86_64\KeireAssetTool\KeireAssetTool.exe bake-lighting --project <project-path> --input Assets/Scenes/Main.keirescene
```

Omit `--input` to bake the startup scene, or add `--force` to bypass the digest-verified cache.

## Gameplay-Production Foundations

`ApplicationSpecification` can independently enable application-owned scripting, physics, audio, navigation, and
profiling services. Scene-owned physics and navigation worlds remain isolated and become inert when their application
service closes. Public contracts contain only Kéire handles and value types; Jolt, Recast/Detour, and miniaudio remain
private implementation dependencies.

Physics worlds now simulate static, dynamic, kinematic, and trigger bodies through pinned Jolt 5.6.0. Box, sphere,
capsule, deterministic convex, and static triangle collision are available. `CookCollisionMesh` validates finite input,
canonicalizes convex points, produces a stable content hash, honors cancellation, and rejects dynamic triangle bodies
before registration. Queries and contact events retain deterministic Kéire ordering.

The patched Coral host targets .NET 10, resolves hostfxr through nethost, accepts UTF-8 paths, and owns shutdown
idempotently. Managed builds reference the staged `Keire.Managed.dll`; successful reloads validate Behaviour type
registries in a candidate collectible load context, transactionally recreate live Behaviour objects, and replace
stable-ID script component registrations at an explicit boundary while failed loads retain the active context and
caller-supplied migration payload. Managed components then receive the same Awake/enable/start/fixed/update/disable/destroy scene
lifecycle as native components. Cooked games carry RID-specific hostfxr/CoreCLR files and load published gameplay DLLs
without requiring a system .NET installation. The .NET 10 SDK is required only for project compilation and cook.
Managed Behaviours can write retained editor Console entries through `Debug.Log`, `Debug.Warn`, `Debug.Error`,
`Debug.LogException`, and `Debug.Assert`. The sample `FirstPersonCamera` Behaviour uses the project `Player/Move` and
`Player/Look` input actions and exposes movement, look, pitch, and smoothing settings in the Inspector.
During Play Mode, gameplay temporarily overrides editor UI capture for the validated `Player` action map and releases
that override on exit. The editor owns an explicitly paired keyboard/mouse input user, a runtime capture request can
engage the Game view while its dock-focus request is still settling, and clicking the Game image re-engages a released
capture. Escape always has an editor-level safety path that restores a visible, unlocked cursor when project input or
scripts fail. The Scene viewport retains its independent editor camera and navigation throughout Play Mode.
GPU-incompatible VFX effects that compile for CPU automatically move the scene VFX world to CPU; effects invalid on
both backends are isolated and reported with their entity/effect identity. Neither case faults the whole Play session,
so gameplay and other valid effects continue, and publishing a corrected effect revision retries an isolated emitter.
Use **Build > Build Scripts** or `Ctrl+Shift+B` to compile gameplay assemblies immediately. The editor also schedules
an initial script build when opening a project and writes compiler diagnostics to the Console.
Source checkouts compile `Keire.Managed` into the same immutable script generation before gameplay assemblies; packaged
editors copy their bundled API into that generation. Repository build launchers also rebuild the managed runtime API
before native targets. Generated Ninja, Make, Visual Studio, and Xcode projects carry the same input-aware dependency,
so direct project builds cannot consume a stale API DLL either. Reload therefore never combines new scripts with a
stale API DLL.

Generated Windows executable projects also copy `nethost.dll` beside their output as part of linking, and direct editor
builds stage the complete managed host. Starting the Hub or editor from Visual Studio therefore does not depend on a
previous launcher having populated that particular target directory.

Managed gameplay now receives one shared opaque world identity per runtime scene instead of a Behaviour-instance ID.
`Input.Held`, `Input.Pressed`, and `Input.Released` expose action phases, scene-safe entity cloning and deferred
destruction use validated handles, and managed raycasts resolve collider-backed hits to ordinary scene entities.
The default component registry includes Collider and Rigid Body authoring components.

Entity and asset references can be authored directly on Behaviours. Public fields and private `[SerializeField]`
fields appear in Inspector; `[SerializableType]` values can be nested, grouped, ranged, and documented with tooltips.
References retain stable entity or asset identity in scenes and prefabs and are rebound to the active Play world when
the managed instance is restored:

```csharp
[SerializeField, Tooltip("Target damaged by this Behaviour.")]
private Entity _target;

[SerializeField]
private AssetReference<AudioClip> _fireSound;

protected override void Start()
{
    if (_target.TryGetBehaviour<Health>(out var health))
        Debug.Log($ "Target health: {health.Current}");

    if (!_target.HasComponent<ColliderComponent>())
        _target.AddComponent<ColliderComponent>();
}
```

Managed audio and animation are stateful as well as reference-safe. `AssetReference<AudioClip>`,
`AssetReference<AudioMixer>`, `AssetReference<AnimationClip>`, and `AssetReference<AnimatorController>` fields serialize
through the normal asset pipeline. `Entity.AudioSource` supports play, pause, resume, seek, stop, live volume/pitch and
playback status; `Entity.Animator` supports play, cross-fade, pause, resume, stop, speed, and current-state inspection.
See [C# Scripting](docs/Scripting/README.md) and [Animation And Rigging](docs/AnimationRigging.md) for examples.

Managed entity, hierarchy, component, and Behaviour operations use generation-checked value handles. Stale worlds,
destroyed entities, incompatible component types, and retired script generations are rejected at the native boundary;
managed code never receives a native pointer.

Scene-authored UI Buttons may be assigned directly to `[SerializeField] UiButton?` fields. The managed wrapper retains
the owning entity, validates that it has a UI Button component, and dispatches native click events before script
`Update`. `RuntimeUi.WasClicked` remains available for polling workflows:

```csharp
[SerializeField] private Entity uiPanel;
[SerializeField] private UiButton? uiButton;

protected override void Start()
{
    if (uiButton is not null)
        uiButton.Clicked += () = > uiPanel.Active = !uiPanel.Active;
}
```

`ScriptableObject` provides Unity-style transient runtime data instances, stable managed asset-type metadata, typed
`AssetReference<T>` access, validation callbacks, cloning, and asynchronous load syntax. The sandbox weapon framework
uses physical magazine instances, chambered rounds, tube-fed loose shells, deterministic fire modes and shot IDs,
fixed-step ballistic projectiles, penetration and ricochet energy, layered damage contracts, and a spring-driven
first-person recoil rig. The sample camera carries a rifle, pistol, and shotgun loadout; use Fire, Aim, Reload,
Fire Mode, Next/Previous Weapon, or weapon slots 1-3 through the project Input Actions asset.

Concrete managed data types declare `[StableAssetTypeId]` and `[CreateAssetMenu]`; serialized members declare
`[StableFieldId]`. After a successful managed build, their deterministic entries appear under **Create > Managed
Data**. `.keiredata` sources retain typed asset references and dependency closure, expose reflection-driven Inspector
editing with undo/save/reload, and hot-apply as project assets even while Play Mode uses an isolated scene clone.
Malformed or missing managed types keep their raw serialized state visible, while strict cooking compiles and
discovers runtime types before rejecting incompatible data.

Headless audio owns the same pinned miniaudio 0.11.25 engine as device-backed runtime audio. It provides bounded
resident voices, priority virtualization, listener/source spatial state, doppler and attenuation, snapshot
interpolation, immutable DSP graphs, meters, and deterministic offline interleaved-PCM rendering without physical
hardware. Invalid graph replacement remains transactional. `.keiremixer` assets define a stable Master-rooted bus
hierarchy, ordered effect racks, pre/post-fader sends, snapshots, sidechain ducking, and convolution dependencies.
Create mixers and Audio Reverb Zones from the normal Project and component menus. Double-click a mixer to open its
dockable bus, routing, effects, sends, snapshots, and ducking editor. Scene playback resolves mixer revisions into
scoped transactional routing snapshots; stable bus IDs drive fader, mute, solo, and parent-bus gain on both device
and headless paths, with bus names retained as a compatibility fallback. Project settings select the default mixer that
becomes a cook root. Mixer effect/send graph execution, selected-bus audition, and reverb-zone evaluation remain the
next Audio runtime slice, so preview controls stay disabled until preview can match that complete runtime path.

Physics Play sessions and cooked runtimes create their scene-owned Jolt world eagerly. Fixed simulation runs gameplay
`FixedUpdate`, pushes authored and kinematic state, steps Jolt, pulls dynamic transforms, then dispatches ordered
Enter/Stay/Exit contacts. Collider masks use the project’s named 32-layer collision matrix for simulation and queries.
The Scene view draws selected collider wireframes and box/sphere/capsule resize handles with undo. Physics Materials,
mesh-collider dependencies, joint and kinematic capsule character-controller components, narrow-phase ray/overlap
queries, and opt-in bounded debug snapshots use first-party public types. Joint constraints and collision-resolving
character sweeps are still follow-up runtime work; the current controller slice supplies bounded movement commands and
ground-state filtering.

`BakeNavigationMesh` rasterizes explicit triangle geometry with pinned Recast 1.6.0, emits deterministic Detour tile
data plus an inspection graph, and integrates with revisioned synchronous/asynchronous queries and crowd agents.
Published meshes, async cancellation/stale rejection, dynamic obstacle invalidation, and deterministic dependency
hashes remain middleware-free public contracts.

FBX/glTF/GLB import can be marked **Animation Source** in the Import Assets dialog and emits stable skeleton,
semantic-rig, skinned-mesh, and animation-clip subassets with normalized four/eight-influence weights. Per-import
None/Light/Balanced/Aggressive animation compression presets reduce redundant keys within measured transform-error
tolerances. Embedded Mixamo,
Blender, Unreal, humanoid, biped, and quadruped naming is mapped
deterministically; models without a rig can generate one from a non-destructive import preset. Open **Window > Rigging
Studio** to change the rig source/profile/skinning method, inspect semantic mappings and generated subassets, and bake
retargeted `.keireanim` clips. See [Animation and Rigging](docs/AnimationRigging.md) for the complete workflow.

Animation graph assets, animator sampling, transitions, root motion, events, skin palettes, two-bone IK, and FABRIK are
exposed through first-party types. Create an **Animator Controller** from the Project panel, double-click it to open the
dockable state-machine editor, then drag imported clips, Animation Sources, or animated models onto the graph. Source
and model drops expand their generated clip subassets into states. Parameters, override/additive layers, entry states,
transitions, conditions, blend trees, state-machine subgraphs, avatar masks, node layout, validation, and undo/redo are
authored without editing JSON. State nodes use the production graph canvas: drag their Transition pin to another state's Enter pin, select or
Delete a Bezier cable, right-click nodes/pins/cables to set entry state or unlink, middle-drag to pan, and use the wheel
to zoom. Managed scripts can set or query typed parameters and layer weights and submit named IK goals through `Animator`.
Select a scene object using the controller to inspect live state, transition progress, the final pose, root-motion
trajectory, and state-machine profiling counters. Outside Play Mode, the selection-backed animation preview scene can
preview, pause, restart, stop, and scrub the animation directly on that object. Preview poses are transient; the
assigned skinned mesh determines the compatible target skeleton for playback and retargeting. Rigging Studio exposes
the retarget mapping and scale/root-motion diagnostics before bake. Animator ground-adaptation settings apply
physics-query-driven foot IK, while the public ragdoll transition utility blends compatible animation and physics poses.
The sample project includes a
`.keireasm` gameplay assembly, reload-aware third-person and navigation scripts, and base/variant prefab assets;
`AssetTool cook` compiles and publishes those DLLs before writing the runtime manifest.

`.keirevfx` assets use an explicit schema-4 execution source. New Graph assets connect Spawn, Initialize, Update, and
Output Contexts, order executable Blocks inside those Contexts, and cable descriptor-backed Operators and stable-ID
Blackboard Parameters into typed Block inputs. Schemas 1-3 migrate in memory and are written as schema 4 only on
explicit Save; historical module-stack effects retain `LegacyModules` execution until an explicit conversion. Range,
Random/Random Range, Remap, core arithmetic/logic/vector Operators, bitwise math, HSV/RGB conversion, normalized age,
frame/system identity, and bounded CPU/GPU value interpreters form the current value release. Unsupported or
GPU-required work reports an explicit diagnostic instead of acting as a no-op;
compiled backend-limit diagnostics retain the responsible stable node ID. CPU + GPU catalog badges require validated
packed representation and backend semantics; every currently executable packed core Operator now satisfies that
contract on both interpreters.
A scene-owned `VfxWorld` supplies generation-safe handles, fixed global/effect budgets, pooled steady-state simulation,
revision-aware reload, diagnostics, and immutable debug/render snapshots.
GPU Depth collision samples the scene depth buffer directly; Scene Physics collision remains an explicit CPU query.
Mesh surfaces and sparse `.keirevfxvolume` density cells are sampled on both backends through deterministic weighted
tables, with explicit runtime diagnostics while an asset is unavailable. GPU compute simulation and per-system indirect
Sprite, Mesh, adjacency-connected Ribbon, and analytic Volumetric output are active. Sprite textures and standardized
particle materials work on both backends: particle tint composes with material Tint, primary texture, alpha mode, and
alpha cutoff for Sprite, Ribbon, and Volumetric output. GPU Mesh output uses the ordinary material-shader composition
path when the assigned shader is instancing-compatible, with deterministic last-good fallback and diagnostics
otherwise. Ribbon links are sequence- and generation-qualified so dead or recycled particles break the strip safely.
A single root handle can own several compiled systems, including named Event sources and
Particle Strip identity; named events are available from C++, scene runtime, and C#. Duplicate Context Blocks retain
independent execution IDs and every numeric Block property can consume per-particle expression registers through the
typed CPU/GPU property ABI. CPU mesh output is material-aware and resolves each imported mesh's default material slots.
The sample VFX folder includes Ember Shard Cyclone and Arcane Sigil Orbit with repository-owned
emissive glTF geometry and generated material subassets. Double-click a VFX asset to open the dockable Graph, Runtime
Modules, Blackboard, and Effect Settings workflows. The authoring preview defaults to the stable CPU backend and can
switch to the runtime GPU backend. In a scene, enable **Preview In Edit Mode** on a VFX Emitter to synchronize its
assigned effect, revision, compatible parameter overrides, seed, simulation speed, enabled state, and world
position/rotation without entering Play Mode or dirtying the scene.
The [VFX Authoring And Runtime guide](docs/Vfx.md) covers Context Blocks, value Operators, visual workflows, scene
setup, native and managed range control, backend differences, recipes, migration, and troubleshooting. The
[Unity 6.3 LTS parity manifest](docs/VfxParityManifest.json) is the machine-readable catalog and support ledger; its
checked-in generator and validator live under [`Scripts/Vfx`](Scripts/Vfx). The generated
[VFX capability reference](docs/generated/VfxCapabilities.md) is release-checked against both the frozen manifest and
the runtime descriptor catalog, and production slices require every enabled implementation to have executable coverage.
The current 125-row executable parity surface includes typed Inline values, particle/strip Attribute reads, constants,
coordinate conversion and rotation, and deterministic fixed-3D Value/Perlin/Cellular noise and curl on CPU and GPU.
Kéire-specific shipping work that is deliberately outside the Unity node catalog is tracked separately in the
[VFX Beyond-Parity Roadmap](docs/VfxBeyondParityRoadmap.md), so engine-production features never inflate parity counts.

## Windowing And Configuration

### GPU VFX runtime

VFX effects publish schema-4 documents with explicit `legacyModules` or `graph` execution. Schemas 1-3 migrate in
memory without changing their execution source; explicit Save publishes schema 4, while the undoable conversion command
replaces a compatibility module stack with one deterministic Context-and-Block particle graph. Graph compilation
validates registered descriptor IDs and canonical pins/settings, ordered Block references and endpoints, same-typed
single-driver value cables, acyclic forward Spawn-to-Output flow, stable-ID references, and an enabled
emission/renderer path.

Render-capable scene sessions use persistent structure-of-arrays GPU buffers, free/alive lists, compute
spawn/update/compaction, Local-space transform following, generation-qualified per-handle retirement, per-emitter
capacity enforcement, deterministic resource-shape sampling, scene-depth collision, and indirect textured-Sprite,
indexed-Mesh, Ribbon, or analytic Volumetric draws. Portable Custom HLSL accepts up to the
explicit 4,096-instruction compiler safety bound and publishes its
dynamic records for CPU and GPU execution. The fixed eight-instruction and fifteen-operation snapshot arrays are
compatibility mirrors, not execution limits. The GPU executes ordered Context Block and Portable Custom HLSL operations
together in the relevant per-emitter spawn or handle-filtered, capacity-wide simulation dispatch. Stopping or restarting
one GPU effect preserves unrelated emitters, and `VfxWorld::Clear` remains the explicit world-wide reset.
The editor prewarms the complete GPU VFX compute-pipeline set on a low-priority worker when the workspace opens, keeping
the first Play transition responsive. Standalone clients can call `RenderSystem::RequestGpuVfxPipelineWarmup()` during
their loading flow; warmup progress and elapsed time are reported through `RenderStatistics` and profiler counters.

The VFX Effect panel authors draggable Contexts and ordered Blocks, descriptor-backed value nodes and cables, typed
Blackboard defaults, portable custom statements, stable IDs, undo/redo, automatic incident-link cleanup, and CPU/GPU
compile diagnostics. The toolbar provides restart, pause/resume, looping, backend, speed, active-particle, and
dropped-particle controls.
`VfxEmitterComponent` provides quality, culling, bounds, seed, speed, Play On Awake, Edit Mode preview, auto-destroy,
and serialized stable-ID parameter overrides. The scene Inspector resolves the assigned effect and provides typed
controls for exposed parameters, default reset, and stale-override cleanup. Native `VfxActivation`, `VfxWorld::SetParameter`,
`SetParameterOverrides`, and `ResetParameter` provide typed per-handle control. Managed scripts can control playback
through `Vfx.Play`, `Vfx.Pause`, `Vfx.Resume`, `Vfx.Stop`, and `Vfx.SendEvent`, and can transactionally update exposed
scalar, integer, vector, and color ranges through `VfxRange<T>`. Subgraph assets, decal outputs, unrestricted
Unity-style Custom HLSL, and arbitrary custom GPU resources remain explicit later compiler/runtime milestones.

Animation-only FBX/glTF imports now publish `AnimationSourceAsset` as their effective primary type and retain stable
skeleton, rig, and clip subasset IDs. They never pass through mesh vertex validation. Reimport changes metadata,
catalog type, dependencies, and generated subassets as one publication, retaining the last-good asset on failure.

## Runtime UI And Audio

Create a Canvas, Text, Button, Audio Source, or Audio Listener from the Hierarchy context menu. Canvas children are
serialized with the scene and rendered during Play Mode and in cooked runtime builds. Audio clips support WAV, Ogg
Vorbis, and FLAC imports; assign the resulting asset to an Audio Source and enable Play On Awake, or start it from C#:

```csharp
Audio.Play(Entity, clip.Id, new AudioPlaybackOptions
{
    Spatial = true,
    Bus = "SFX",
    Gain = 0.9f,
    MinimumDistance = 1.0f,
    MaximumDistance = 40.0f
});
RuntimeUi.SetText(ammoLabel, $"{roundsInMagazine} / {reserveRounds}");
if (RuntimeUi.WasClicked(resumeButton))
    ResumeGame();
```

Runtime UI uses retained scene components, reference-resolution scaling, safe-area layout, clipping, pointer hit
testing, focus, and bounded event queues. Managed calls validate the active world and entity generation before
mutating authored UI or audio components.

Selecting an audio asset shows codec, channel, sample-rate, duration, frame-count, and streaming metadata in Inspector,
with Preview, Stop, and Reimport controls. Project thumbnails display a waveform for resident and streaming clips.
The sandbox's two PCM examples are deterministic repository-owned test tones; regenerate them with
`Scripts/Assets/generate-sample-audio.ps1`.
Audio Source schema 2 exposes clip, mixer, stable bus identity with the legacy bus-name fallback, gain, pitch, priority,
looping, spatialization, min/max distance, a reusable attenuation curve, and Play On Awake through the normal component
Inspector, scene serialization, undo, and prefab-override workflow. Existing schema-1 sources migrate without changing
their audible legacy attenuation.

`Keire/Window.h` exposes SDL-free `WindowSystem`, `Window`, opaque `WindowId`, logical/pixel extents, and a typed ordered event variant. One system is active per process and any number of windows may be created. SDL video initialization, window creation, mutation, polling, and shutdown are creating-thread-affine; releasing the final `Ref<Window>` from a worker is safe because native destruction is deferred to the owner thread. Shutdown destroys all native windows and makes surviving handles inert.

Logical sizes describe UI coordinates while pixel sizes describe the high-DPI drawable density. `DisplayScale()` bridges the two; pixel-size and scale changes arrive independently through events. The platform boundary deliberately creates no graphics context and translates no input; rendering and input remain separate concerns.

`LoadWindowSpecification` parses `Config/Client.json` without exposing nlohmann/json. The root `window` object accepts `title`, `width`, `height`, `resizable`, `highPixelDensity`, `visible`, `maximized`, and `mode` (`windowed` or `borderlessFullscreen`). Missing optional fields retain API defaults; unknown/duplicate keys, malformed UTF-8, wrong types, invalid dimensions, oversized titles/files, and incompatible fullscreen/maximized state are errors with file and JSON-pointer-style locations.

Interactive project editors additionally remember their last normal window position and size together with maximized or
borderless-fullscreen state under `Library/UserSettings/Workspace/editor-window.state`. Restoration happens before the
window is shown, and minimized state is intentionally not restored.

KeireClient accepts `--project <path>`, `--config <path>`, `--smoke-window`, `--smoke-ui`, and `--smoke-project`.
Interactive editor startup requires a validated project; the normal launcher opens KeireHub first. The default
`Config/Client.json` is optional when implicit, while an explicitly named missing file is an error. Window, Hub UI, and
full project editor smoke modes are bounded; the latter exercises project locks, assets, scenes, input, workspace, and
clean shutdown.

Windows editor, Hub, and runtime executables carry the repository Kéire icon in both Visual Studio and Ninja builds.
Desktop player icon settings remain optional: packaging uses the selected Texture2D when present and otherwise
generates the built-in Kéire artwork for the Windows executable/ICO, Linux desktop entry, or macOS application bundle.
The Hub is single-instance per installed executable. Its secondary-activation protocol accepts requests to show the
existing window, navigate to a product page, open a project, import a located package, request an editor version, or
select Build Support. Use
`--show`, `--navigate <page>`, `--open-project <absolute-path>`, `--import-package <absolute-path>` (or its
`--locate-package` alias), `--install-version <id>`, and `--build-support <platform> <architecture>`. Activation uses a
strictly validated, versioned, length-prefixed 512-byte protocol. Show, navigation, and compatible project opening use
their normal owner-thread workflows. Local Build Support imports run only when the required editor Asset Tool exists;
unsupported package types and unavailable editor catalog IDs produce notifications without creating placeholder tasks.
Build Support import, repair, and removal tasks are journaled before the Asset Tool starts, remain visible after a Hub
restart, and reconcile from the tool's atomic status or a fresh installed-component scan. A restarted Hub observes a
surviving Asset Tool without taking ownership of or terminating it; non-removal tasks retain their confined cancel
action.
Shutdown closes the tray handle before SDL exits.

## Application, Layers, Events, And Time

`Keire::Application` owns logging, a standalone `EventBus`, `Time`, `WindowSystem`, the primary window, and a dedicated `LayerStack`. KeireCore supplies `main`, handles dependency-free help/version commands, owns the top-level exception boundary and application lifetime, and calls `Run()`. A managed client supplies a static command-line description plus `CreateApplication(const ApplicationCommandLineArguments&)`; custom help remains client-owned without initializing engine services. The stack owns layer lifetimes, overlay partitioning, attachment, detachment, deferred structural changes, and traversal. Access it through `Application::Layers()`; the `PushLayer`, `PushOverlay`, and `RemoveLayer` application helpers remain as convenient delegates. The application construction thread owns `Run` and all layer mutations, while `RequestExit` remains safe from workers. Layers update bottom-to-top, receive events top-to-bottom, and may safely request structural changes during nested callbacks; those changes apply at the next frame boundary. Automatic layer subscriptions cannot be created during `OnDetach` and never survive detachment.

`Keire::UiFrame` is a first-party, frame-scoped immediate UI facade. Set `ApplicationSpecification::Ui.Mode` to
`UiMode::Rendered` for SDL_GPU output or `UiMode::Headless` for deterministic tests and SDK validation. `Layer::OnUi`
runs after variable update, bottom-to-top with overlays last. Window, menu, tab, tree, disabled, child, ID, font, color,
and scalar/vector style scopes are move-only RAII values, so callback exceptions cannot leave the backend stack
unbalanced. Calls outside `OnUi` or from another thread are rejected. Docking is enabled by default; opaque ref-counted
RGBA images are supported, while detachable native viewports, renderer handles, raw textures, and custom draw lists
remain intentionally unavailable.

Enable `ApplicationSpecification::Ui.Workspace` for the editor-grade layout system. `Application::GetUiWorkspace()` provides named layouts, immutable Default reset, registered panels, Kéire Dark/Light/Classic themes, editable semantic theme tokens, and portable `.keirelayout`/`.keiretheme` import and export. Live layout changes autosave atomically to SDL's per-user preference directory; `DirectoryOverride` supports deterministic tools and tests, while `Ephemeral` keeps smoke runs off disk. A backend-independent `BuildFactoryLayout` callback declares the default dock recipe using stable panel IDs. The legacy single-file `LayoutPath` remains available for simple clients, but it is mutually exclusive with the workspace.

The [UI Workspace Guide](docs/UiWorkspace.md) documents configuration, panel ownership, factory docking, layout and theme workflows, persistence, recovery, and troubleshooting.

```cpp
void EditorLayer::OnUi(Keire::UiFrame& ui)
{
    if (auto window = ui.BeginWindow("Inspector"); window)
    {
        ui.Text("Kéire owns the UI lifecycle and renderer.");
        (void)ui.Checkbox("Visible", m_Visible);
    }
}
```

```cpp
constexpr Keire::ApplicationCommandLineOption Options[]{
    {"--profile <name>", "Select a runtime profile."},
};

Keire::ApplicationCommandLineDescription
Keire::GetApplicationCommandLineDescription() noexcept
{
    return {"[--profile <name>]", Options};
}

std::unique_ptr<Keire::Application>
Keire::CreateApplication(const Keire::ApplicationCommandLineArguments& arguments)
{
    return std::make_unique<ClientApplication>(BuildSpecification(arguments));
}
```

Events are ordinary C++ value types. `Subscribe<T>` and `SubscribeAny` return move-only RAII tokens, while `Dispatch` delivers synchronously on the bus owner thread. Worker threads use bounded `TryEnqueue`; the application drains one stable snapshot per frame. Higher priorities run first, equal priorities retain registration order, and `EventFlow::Handled` stops propagation. An unhandled quit or primary-window close exits cleanly, so overlays can veto close requests when necessary.

```cpp
struct AssetReady {
    std::uint64_t Id; };

auto events = application.Events();
auto subscription = events->Subscribe<AssetReady>([](const AssetReady& event) {
    UseAsset(event.Id);
    return Keire::EventFlow::Continue;
});

events->Dispatch(AssetReady{42});  // application thread
events->TryEnqueue(AssetReady{43}); // any thread
```

`Keire::Time` provides `RawDeltaTime`, scaled and unscaled delta/time, smoothed delta, fixed time, frame/tick counts, interpolation alpha, pause, time scale, and dropped-backlog diagnostics. Fixed simulation defaults to 60 Hz, clamps a frame to 250 ms, and runs at most eight ticks per frame. The application-owned service is passed read-only to layer hooks; code that intentionally changes pause or scale uses `Application::GetTime()` on the application thread.

## Logging

`Keire::Log` owns a private asynchronous backend and never changes its backend's global registry or default logger.
KeireCore and KeireClient use separate rotating-file loggers. Public code formats through Kéire-owned `{}` placeholders,
including integer hex/width and floating precision specifications; spdlog and fmt remain implementation details.
`LogConfig::EnableConsole` controls the color console sink and defaults to true. The default files are `Logs/Core.log`
and `Logs/Client.log`.

Logs default to `Logs` relative to the process working directory. IDE debug directories and scripts use the repository root. Initialization is idempotent only for identical configuration; conflicting configuration throws. `LoggerHandle` is a copyable value backed by `Ref`; each call takes a short operation lock. Shutdown may wait for an active write, but never for a handle's lifetime, and old handles safely become inert. Disabled macro levels evaluate neither logger acquisition nor message arguments.

The editor drains a bounded, structured feed of native Core and Client records into its Console panel, including early
startup, worker-thread, and Coral managed-host messages. Editor-authored Console entries still flow through the Client
logger and rotating
file sink. Windows Dist Hub/editor executables use the GUI subsystem and disable the terminal sink, so packaged launches
do not open a second console window; `Logs/Core.log` and `Logs/Client.log` remain available. Asset import and runtime-load
failures include stable asset ID, importer/type, source path, line/column when available, and the original diagnostic.
Selecting a failed shader shows the same compiler diagnostics in Inspector; successful hot-reload data remains active
until a corrected import completes.

## References

`Keire/Ref.h` provides thread-safe `Ref<T>` strong ownership, `WeakRef<T>` observation, and the factory-only `CreateRef<T>(...)` API for types derived from `RefCounted`. The external control block keeps weak locking safe while the last strong owner is released, supports polymorphic and incomplete object types, and exposes `UseCount()` for inspection. Reference-counted graphs must use at least one `WeakRef` in every cycle.

## Build Identity And Assertions

`Keire::GetBuildInfo()` reports the project version, Git commit and tracked/untracked dirty state, configuration, compiler, platform, and architecture compiled into the binary. Identity is regenerated immediately before KeireCore compiles and only rewrites its header when values change. `KeireClient --version` prints that identity; `KeireClient --help` documents the intentionally small command line. Informational commands do not initialize logging.

`KEIRE_ASSERT(condition)` and `KEIRE_ASSERT(condition, "message")` diagnose and abort in Debug and sanitizer configurations. They compile out without evaluating arguments in Release, Dist, and Coverage. Logging uses `KEIRE_CORE_*` and `KEIRE_CLIENT_*` for every severity.

`KEIRE_API` marks KeireCore-owned public symbols and cross-boundary exception types for same-toolchain shared-library preparation. Header-only values and templates remain unannotated, while the managed-client factory hooks remain executable-defined reverse imports. The SDK is still distributed as a static library and does not promise a compiler-independent C++ ABI.

## Asset Workflow

Set `ApplicationSpecification::Assets.Mode` to `Development` for editor catalogs or `Cooked` with explicit mounts for a
distribution runtime. `AssetHandle<T>::Get()` immediately returns a typed fallback, workers load and validate content,
and frame-boundary completion swaps in immutable data. Initial failure is explicit; failed reload keeps the last
known-good revision. Built-in `BinaryAsset` and UTF-8 `TextAsset` types establish the lifecycle without prematurely
introducing renderer, audio, model, or scene ownership.

The editor discovers the opened project's `Assets/`, creates stable `.keiremeta` sidecars, caches immutable imports under
`Library/AssetCache`, and exposes Project/Inspector browsing plus import and Dist cook actions. The same workflow is
available without the editor:

Files and folders may be dragged from the operating system into a Project folder or the Scene viewport. Configurable
textures open an import-options dialog; simple files import directly. External sources are copied into `Assets`, and
viewport imports reuse the normal typed Scene drop behavior.

```powershell
./Build/Bin/Debug-windows-x86_64/KeireAssetTool/KeireAssetTool.exe import --project Samples/KeireSandbox
./Build/Bin/Debug-windows-x86_64/KeireAssetTool/KeireAssetTool.exe cook --project Samples/KeireSandbox --output Build/Assets --profile Dist
./Build/Bin/Debug-windows-x86_64/KeireRuntime/KeireRuntime.exe --content Samples/KeireSandbox/Build/Assets --frames 12
```

See [Asset Runtime](docs/AssetRuntime.md) and [Asset Pipeline](docs/AssetPipeline.md) for threading, mount, metadata,
integrity, file-operation, cook, and packaging contracts.

Asset APIs are organized beneath `Keire/Assets` (for example, `#include "Keire/Assets/AssetSystem.h"`). The umbrella
`Keire/Core.h` continues to include the supported asset surface for consumers that prefer the aggregate header.

Static meshes can be imported from OBJ, FBX, glTF, or GLB and converted explicitly with `KeireAssetTool convert-mesh
--input <model>`. PNG, JPEG, TGA, BMP, and Radiance HDR textures import as validated assets with deterministic mip
generation and sampler settings stored in source metadata. HDR environment mip generation preserves decoded radiance
for roughness-filtered specular sampling. Environment textures support equirectangular panoramas and horizontal/vertical
cubemap cross or strip atlases. Model-folder drops preserve supported models and textures while ignoring unrelated
exporter/readme files. Live imports publish immutable content-addressed packs, so active Scene/Game loads do not block
catalog replacement on Windows. Assimp and stb remain private implementation dependencies; their headers are not
required by engine or SDK consumers.

Audio clips import from WAV, Ogg Vorbis, FLAC, and MP3 natively. AAC, Opus, WMA, AIFF, WebM, MP4, MKV, MOV, M4A, and
other registered codec/container sources are transcoded losslessly through the engine's private FFmpeg libraries.
The default `fast` mode streams PCM WAV and avoids compression work; `compressed` emits FLAC, and oversized sources
automatically use FLAC to stay within bounded memory and asset sizes.
Project generation source-builds the locked LGPL FFmpeg revision; no system FFmpeg executable or temporary transcode
file is used. Native sources keep the miniaudio fast path, while converted results are restored from the importer cache.

Model import publishes referenced materials and embedded images as generated sub-assets with stable IDs, so mesh slots,
PBR factors, alpha/double-sided state, and texture dependencies cook as one graph. Right-click an imported model in the
Project panel and choose **Extract Materials** to create editable `.keirematerial` copies beside the model. External
FBX/OBJ texture channels that cannot be converted are reported as import diagnostics instead of being silently dropped.

Selecting a `.keirematerial` in Inspector exposes every shader-declared numeric, color, and texture property. The
production surface uses base-color, +Y normal, packed metallic-roughness, separate metallic/roughness, occlusion, and
emissive semantics with neutral fallbacks. Edits are range-checked, previewed through live immutable revisions, saved
atomically at the edit boundary, persisted to the catalog in the background, and recorded in project-asset undo.

Create a **Material Graph** from the Project panel to author Surface PBR, Transparent, Decal, Unlit, Hair PBR, or Eye
PBR shaders on the same production node canvas used by VFX. The schema-v2 catalog provides more than 100 typed nodes
across texture/UV, scene inputs, math/vector/color, procedural generation, normals, parallax, Material Attributes, and
composable Standard/Clear Coat/Sheen/Subsurface/Transmission BSDF layers. It also includes keyword/static-switch
variants and confined custom functions. Stable cables, typed input defaults, texture asset/semantic picking, node
duplication, undo/redo, and cost-aware generated diagnostics update as the graph changes.
The adaptive shaded sphere/plane/cube/custom-mesh preview executes the graph's built-in nodes per pixel, including UV,
procedural, shaping, emission, normal, attributes, and BSDF paths, while exposing lighting, exposure, and rotation
controls. Revisioned background compilation is debounced, discards stale completions, and retains the last-good shader.
The shared graph canvas clips all cards, pins, cables, labels, and drag feedback to its viewport.
Valid parameter-default edits publish an immutable development-material revision immediately, so entities using the
graph update while it is authored; invalid edits leave the last-good scene material intact. Save queues a targeted
compile and hot reload of deterministic HLSL and `.keireshader` variants under the graph's generated asset directory,
where the production compiler validates DXIL, SPIR-V, and MSL. Import exposes a stable compiled material beneath the
graph, so the graph can be selected in Mesh Renderer material slots or dropped directly onto a rendered Scene entity.
Drop picking uses imported mesh bounds, and dropping on a model root applies slot zero to all rendered descendants.
Built-in cube renderers expose the same material-slot workflow. Nine progressive examples live
under `Samples/KeireSandbox/Assets/Materials/MaterialGraphs`. Typed `.keirematerialinstance` assets support
bounded parent chains, property overrides, and keyword overrides, and import as assignable runtime material subassets.
They work in Mesh Renderer slots and viewport drops without duplicating their parent graph. See
[Shaders And Materials](docs/ShadersAndMaterials.md) for the contracts and safe-include rules.

The Sandbox startup scene uses an imported humanoid model from `Assets/Meshes/T-Pose.fbx`, an Idle animation source,
and a UV-mapped pyramid through the same renderer-owned resource caches used by the editor. Dist cooking follows
scene-to-controller-to-clip and scene-to-material-to-shader/texture dependencies and emits a runtime manifest; omit
`--frames` to run the cooked scene as the normal standalone player.

## Projects And Scenes

During Play mode, Hierarchy, Inspector, gizmo, and viewport edits target the isolated runtime clone. Stop presents the
runtime differences by entity/component/property; selected changes can be applied as one undoable dirty scene edit,
discarded, or left running with Cancel. Entering Play focuses the Game tab; review and Cancel retain Game, while a
completed Stop returns focus to Scene. Play/Pause/Step remain centered in the persistent editor bar, and compact Scene
tool/orientation overlays consume input only within their visible controls.

Project-grid previews use imported content: textures preserve aspect and alpha, materials appear on a shaded sphere,
and mesh/model assets use framed imported geometry. Scene, shader, input, folder, missing, and unknown content use
distinct generated type icons; all preview cache data remains under `Library/Thumbnails`.

`Ctrl/Cmd+D` duplicates selected scene roots as one undoable operation. Hierarchy rows accept drops in three zones: the
upper and lower edges insert before or after a sibling, while the center parents the selection; dropping on blank
Hierarchy space moves it to the scene root. Dragging any selected row moves the complete selection in hierarchy order,
keeps the selection active, filters selected descendants whose parent is already moving, and records one undo step.
Drop highlights and labels distinguish reordering from parenting before release. The Scene toolbar camera button toggles
a live main-camera preview in the lower-right corner without intercepting input elsewhere in the viewport.

`ProjectSettings/Project.keireproject` is the fixed marker for an isolated Kéire project. `Project::Create` produces
transactional Empty or Starter roots; `Project::Open` validates schema/version and the editor holds an OS-exclusive lock
for its lifetime. Assets, import caches, input profiles, workspace state, recovery, logs, and cooked output are rebased
under that root. The Project Hub manages a per-user recent registry without deleting projects and launches each editor as
an independent process. Its Home, Projects, Installs, Templates, Learn, Resources, Licenses, and Settings areas show
only live local or signed-catalog data. Project dispatch uses each registered editor manifest's schema range together
with the project's minimum and last-saved engine versions; an unavailable editor routes to the real install/locate
workflow, and the Hub never invents an installation from a nearby executable.
The integrated title bar and responsive navigation rail use the packaged icon set while retaining native drag, resize,
Snap/maximize, and close behavior. Installation refresh, verification, and managed repair/uninstall preparation run in
the task center without blocking the Hub window; stale results are discarded if the editor registration, process state,
or active tasks change.

`.keirescene` is the first scene asset. `SceneSystem` loads it asynchronously and commits single/additive activation only
at application frame boundaries; failures preserve the last-good scene set. Mutable `Scene` instances support stable
weak object handles, hierarchy edits, transforms, subtree duplication/deletion, cycle-safe reparenting, and dirty state.
The editor adds atomic Save, bounded undo/redo, Save/Discard/Cancel transitions, and project-local crash recovery.
Scene replacements requested by menus, Project actions, imports, or viewport drops are decoded first and committed at
the next editor update boundary. The Scene canvas remains available with no open document, so dropping a scene is the
same safe operation from an empty or populated workspace.

Schema v2 scenes use stable `Entity` handles and application-registered, reference-counted Components. Transform is
mandatory; Directional Light supplies built-in Lambert lighting and realtime cascaded shadows. Play clones authored
state, Pause freezes component
updates, Step advances one fixed tick, and Stop discards runtime changes. EnTT and GLM implement ECS/math privately;
SDK code sees only Kéire IDs, components, queries, vectors, quaternions, matrices, and colors.

See [Project System](docs/ProjectSystem.md), [Project Hub](docs/ProjectHub.md), [Scene System](docs/SceneSystem.md), and
[Scene Authoring](docs/SceneAuthoring.md), plus [ECS And Components](docs/ECSAndComponents.md).

## Input Actions

Input is disabled by default. Enable `ApplicationSpecification::Assets` and set
`ApplicationSpecification::Input.Mode = InputMode::Enabled`; Kéire rejects Input without Assets. The application then
publishes `Application::Input()` after window events and asset completions once per outer frame. Action contexts expose
stable map/action lookup, polling, RAII phase callbacks, users and exclusive/shared pairing, control schemes, hot
reload, interactive rebinding, and atomic profile overrides without exposing SDL.

`Samples/KeireSandbox/Assets/Input/DefaultInput.keireinput` provides Player Move/Look/Jump/Sprint/Fire and UI
Navigate/Submit/Cancel/Point/Click/Scroll maps
for keyboard/mouse and gamepad. KeireClient enables Development Assets and Input for its editor. Create or select a
`.keireinput` asset, then open the dockable Input Actions panel from Inspector, Project double-click/context menu, or
Window. The editor provides four templates, searchable master-detail authoring, bounded undo/redo, Save/Revert,
validation, conflict-aware Listen rebinding, and a live monitor entirely through `Keire::UiFrame`.

The Input Debugger can enable the project maps with a scoped UI-capture override and records filtered phase/value,
user/device, scheme, duration, and timestamp data in its bounded local history. Idle values and synthetic resets are
discarded; optional Console forwarding is off by default. See [Input System](docs/InputSystem.md),
[Input Actions Editor](docs/InputActionsEditor.md), and [Input Debugger](docs/InputDebugger.md).

## Dependencies

`Config/Dependencies.lock` is the source of truth for tool URLs, archive hashes, installer pins, and submodule commits. Normal bootstrap verifies immutable state and never stages files or advances dependency pointers. SDL 3.4.10 is built as cached Debug and Release static archives by a dependency-only CMake step; Kéire itself remains Premake-driven. Debug, sanitizer, and Coverage configurations select Debug SDL, while Release and Dist select Release SDL. nlohmann/json 3.12.0 remains a private header-only implementation dependency.

Dear ImGui 1.92.8 is pinned to the released `v1.92.8-docking` tag. Its core, demo, SDL3 platform, SDL_GPU renderer,
and standard-string adapter translation units build in the private `DearImGui` project as `KeireImGui.lib` on Windows
or `libKeireImGui.a` on Unix. Kéire owns context, event forwarding, frame, docking, layout, GPU, swapchain, and shutdown
lifecycles; clients use only `Keire::UiFrame`. SDL is built with GPU support and without SDL_Renderer. Dear ImGui types
and headers never cross the public API, and its headers and sources are not redistributed. Multi-viewports remain
disabled until Kéire has explicit multi-window renderer ownership.

Zstandard 1.5.7 is pinned and compiled as the private `KeireZstd` archive. Asset packs use it with deterministic build
profiles and SHA-256 payload verification. Zstandard headers never cross the public API and are not redistributed.

EnTT 3.16.0 and GLM 1.0.3 are pinned header-only implementation dependencies for entities/components and math. Their
headers never cross Kéire's API, third-party warnings are isolated, SDKs contain their attribution and manifest commits,
and their source trees are not redistributed.

KeireClient demonstrates the workspace as a Unity-style project editor. Project and Inspector are backed by the asset
database; Scene and Hierarchy author typed scene assets, Console captures editor/input diagnostics, and Game remains a
deliberate renderer-owned preview boundary.

Intentional updates are explicit:

```powershell
./Scripts/project.ps1 vendor-update -Dependency spdlog -Tag v1.18.0
./Scripts/project.ps1 vendor-update -Dependency imgui -Tag v1.92.8-docking
./Scripts/project.ps1 vendor-update -Dependency zstd -Tag v1.5.7
./Scripts/project.ps1 vendor-update -Dependency entt -Tag v3.16.0
./Scripts/project.ps1 vendor-update -Dependency glm -Tag 1.0.3
```

```sh
bash Scripts/project.sh vendor-update --dependency spdlog --tag v1.18.0
bash Scripts/project.sh vendor-update --dependency imgui --tag v1.92.8-docking
bash Scripts/project.sh vendor-update --dependency entt --tag v3.16.0
bash Scripts/project.sh vendor-update --dependency glm --tag 1.0.3
```

Review the diff and upstream changes before running the Git commands printed by the updater.

## Rename The Template

Rename must run in a clean Git worktree; non-Git copies are also supported. The identifier must be PascalCase. Changes are transactional and are never staged or committed.

```powershell
./Scripts/project.ps1 rename -Name MyGame -DisplayName "My Game" -Repository owner/my-game
```

```sh
bash Scripts/project.sh rename --name MyGame --display-name "My Game" --repository owner/my-game
```

This derives `MyGameCore`, `MyGameClient`, `MyGameTests`, namespace `MyGame`, matching folders, package names, workflow URLs, and identity manifest values. Build the renamed copy before committing it.

## CI And Packages

Required CI covers Windows/VS2022, Linux/GCC, macOS/Clang, x64 and ARM64 smoke builds, sanitizers, KeireClient execution, script regression tests, 80% LLVM line coverage, formatting, clang-tidy, ShellCheck, PSScriptAnalyzer, actionlint, yamllint, and Python validation. Extended Compatibility runs weekly and manually. Dependabot checks Actions and submodules weekly.

CodeQL and Dependency Review are an explicit repository opt-in. Enable Dependency Graph and code scanning in GitHub, create the repository variable `ENABLE_ADVANCED_SECURITY=true`, and require `Security activation status` plus the resulting checks in the `master` branch protection rules. Once enabled, the status job fails if any eligible security job is skipped or unsuccessful; CodeQL findings are governed by the repository's code-scanning rules. Privileged CodeQL uploads are skipped for pull requests from forks.

Version tags and manual release workflow runs create SDK archives without publishing a GitHub Release. Archives contain
KeireClient, KeireCore, the private KeireImGui archive, public headers including `Keire/Ui.h`, SDL's
static archive/headers/official CMake configuration, complete licenses, two consumers, and a validated JSON build
manifest containing SDL, JSON, Dear ImGui, and Zstandard commits. Packages also include the canonical Default Input
source/metadata pair and validate it with the packaged asset tool. Every package extracts and validates both a low-level consumer
with its own `main` and a managed headless-UI consumer whose `main` comes from KeireCore, using direct compiler invocation
and `find_package(Keire CONFIG REQUIRED)`. `Keire::Core` transitively links Core → ImGui → Zstd → SDL, so CMake
consumers still name only `Keire::Core`. nlohmann/json, Dear ImGui, and Zstandard headers are intentionally absent
because those dependencies do not cross the public API. The asset CLI is included under `bin`. SHA-256 files and separate symbol archives are
included where available; Dist packages are stripped.

## Troubleshooting

- Run `doctor` first when a compiler, SDK, or package manager is not found.
- Use `--update` or `-Update` only when installed system tools should be upgraded.
- Run forced generation after changing a compiler installation or toolchain: `--force` or `-Force`.
- Do not bypass a checksum failure. Verify the lock entry against the upstream release.
- Windows MSVC builds require the Desktop development with C++ workload for the requested Visual Studio major version.
- Apple's ASan runtime does not support leak detection; the macOS scripts disable that check.

## Production Performance Validation

The editor profiler exposes a sortable CPU-hotspot view, chronological per-thread lanes, copyable counters, and a
Perfetto-compatible JSON trace through `Copy Perfetto Trace`. Renderer pass values are labeled as CPU measurements
unless the active backend explicitly reports GPU timestamp support. Managed scripting counters include callback calls,
skips, native/managed interop calls, cumulative callback time, and the maximum callback duration. A copyable managed
callback table aggregates active instances by Behaviour type and lifecycle method at the profiler's throttled refresh
rate. Default-open profiler tables use compact row budgets; `Show all` remains available for deep inspection.
For GPU VFX captures, compare physical particle capacity and compute thread groups with buffer bytes and dispatches.
Use `GPU fence wait (ms)` to identify frames blocked at the configured frames-in-flight boundary; it is distinct from
swapchain acquisition and CPU command-recording time.

`GPU completion latency (ms)` and `VFX GPU completion latency (ms)` measure submit-to-observed-fence-completion delay;
they are useful back-pressure signals, not GPU execution timestamps. Reference-hardware profiles in
`Config/PerformanceGates.json` require real backend timestamps, capture metadata, a minimum history length, and explicit
renderer/VFX budgets. See [Performance Gates](docs/PerformanceGates.md) for capture and validation commands.

Development asset discovery runs on a database-owned monitor. `PollChangedAssets()` only consumes an immutable
published snapshot and never walks the project tree on the application frame. `ChangeMonitorInterval` controls
background reconciliation while `ChangeDebounce` controls publication.

Run the repeatable Debug, Release, sanitizer, and regression matrix with:

```powershell
./Scripts/Windows/validate-production.ps1
```

```sh
bash Scripts/Unix/validate-production.sh
```

Add `-IncludeGraphicsSmokes` on Windows or `--include-graphics-smokes` on Unix on a graphics-capable worker to run the
project-aware Release smoke. Add `-IncludePackage` or `--include-package` to package the SDK and validate both consumers.

See [Architecture](docs/Architecture.md), [Contributing](CONTRIBUTING.md), [Security](SECURITY.md), and [Changelog](CHANGELOG.md).
