# Changelog

- Reduced managed-event editor and Play Mode overhead by sharing immutable callback metadata, caching persistent
  callback resolution, caching Inspector entity lists, and removing per-frame cursor bridge queries.

- Added serialized managed `KeireEvent` fields with persistent Inspector listeners and scoped cursor ownership so
  gameplay capture yields to modal runtime UI without scripts fighting over cursor state.

- Preserve managed Entity/component references when cloning an editing scene into Play Mode, and route runtime UI
  pointer clicks through both Scene and Game viewports.
- Added shared managed scene world identities, action edge-state APIs, scene-safe clone/destroy calls, collider-backed
  managed raycasts, and serializable Collider/Rigid Body components.
- Added managed ScriptableObject foundations and a production-oriented weapon framework with physical magazines,
  chambered rounds, tube-fed shells, rifle/pistol/shotgun fire modes, deterministic ballistics, penetration,
  ricochet, layered damage contracts, recoil springs, and a sandbox loadout. Removed the legacy `Testing` script.

- Added managed cursor visibility and relative-lock APIs, and isolated Play Mode input from editor input contexts.
- Added Escape cursor release and runtime-owned Player-map lookup so gameplay input is independent of editor documents.
- Rebind scene component placeholders after managed assemblies load so scripts present in startup scenes execute in Play.
- Route the Scene viewport through the runtime primary camera during Play and suspend editor-camera navigation.
- Make input-map enabling idempotent so gameplay actions remain readable after their first enabled frame.
- Give managed component instances unique world handles and make Play viewport Escape release the gameplay cursor.
- Correct first-person look direction and focus/capture the gameplay viewport without leaving editor controls hovered.
- Correct the first-person controller forward/backward movement direction.
- Make Escape toggle FPS cursor capture and smooth target camera rotation to remove raw mouse jitter.
- Redirect C# script creation from non-assembly folders into the nearest/default runtime `.keireasm` source root.
- Register `.cs` source assets with the editor, worker, and command-line databases so script creation publishes through
  the normal transactional text-asset pipeline.
- Create standalone C# scripts through the single-record asset transaction instead of running a full project import and
  catalog cook before returning.
- Generate source-checkout C# workspaces with `Keire.Managed.csproj` in the solution and direct gameplay project
  references, restoring Visual Studio semantic coloring, completion, and engine API navigation.
- Route that source-checkout reference through a generated .NET 8/C# 12 design-time facade so Visual Studio 2022 can
  load engine API symbols while Kéire runtime compilation remains on .NET 10/C# 14.
- Generate root gameplay workspace projects as .NET 8/C# 12 for Visual Studio 2022 design-time evaluation while keeping
  the separate internal gameplay compilation projects on .NET 10/C# 14.
- Keep common unused C# local and field diagnostics as visible warnings instead of promoting them to build-blocking
  errors, while retaining strict warnings-as-errors behavior for other managed diagnostics.
- Input Actions Listen now captures against the live in-memory document through a transient context, supporting any
  keyboard, mouse, or gamepad control for new and unsaved bindings.
- Script-only saves now use shorter source/debounce windows, skip asset catalog imports, and reuse the validated managed
  API assembly, substantially reducing save-to-reload latency.
- Console entries are selectable and support complete-entry clipboard copy through double-click or a context menu.
- Managed API reuse now uses a persistent source fingerprint and validated cache, preventing stale prebuilt engine
  assemblies from removing current `Cursor`, `Debug`, and `Input` APIs while retaining fast gameplay-only rebuilds.
- Input Actions hides Listen while an interactive capture is active, preventing duplicate-operation errors.
- Correct the Sandbox weapon test's inverted empty-magazine predicate, derive its fire/reload capability flags from
  current ammo state, and transfer only the available magazine space from reserve ammo during reload.

- Added .NET 10/C# 14 managed assembly schema v2, immutable incremental build generations, automatic debounced script
  compilation, transactional field-preserving Play Mode reload, per-Behaviour exception quarantine, managed component
  state persistence, lifetime-bound simulation-thread async continuations, and expanded Entity/component/transform APIs.
- Added Unity-style managed script attachment through searchable Add Component entries and `.cs` asset drops on the
  Inspector or Hierarchy, including generated stable component IDs and undo-aware scene mutations.
- Managed builds now default to the engine-bundled .NET 10 SDK, with persisted Bundled, System PATH, and Custom SDK
  choices in Project Settings; script drops also recognize metadata-backed C# asset records.
- Source-checkout editor launches now prefer the newest managed API artifact and reject stale managed state contracts
  before reload, preventing repeated Coral member-lookup failures during script serialization.
- Loaded managed Behaviours now appear under Scripts in Add Component, and supported public or `[SerializeField]`
  fields use the standard Inspector property, undo, scene, and prefab-override pipeline.
- Added Unity-style managed `Debug.Log`, warning, error, exception, and assertion output routed to the editor Console,
  plus an attachable serialized `FirstPersonCamera` sample driven by the default Move and Look input actions.
- Managed generation IDs now resume from persisted state across editor launches, preventing failed or cancelled startup
  builds from deleting the last-good gameplay DLL directory and emptying script entries from Add Component.
- Added Build > Build Scripts, the Ctrl+Shift+B shortcut, startup script compilation, and one-shot managed compiler
  diagnostics in the editor Console; the Build Settings button now routes through the same command.
- Source-checkout KeireClient builds now run the incremental Keire.Managed build explicitly before native compilation,
  preventing Visual Studio utility-project up-to-date checks from leaving the editor and gameplay compiler on stale APIs.
- Build Scripts now publishes a generation-local Keire.Managed assembly before compiling gameplay and reloads that
  immutable API/gameplay pair, eliminating stale API references in source checkouts and packaged editors.
- Play Mode now grants managed gameplay a lifetime-scoped UI-capture override for the Player input map, and Play Mode
  change rows use stable hidden checkbox IDs instead of conflicting when labels repeat.

- Interactive prefab and trash operations now preempt queued or running low-priority catalog refreshes, and project
  open no longer starts an eager full-project import solely because source timestamps are newer than the catalog.

- Prefab thumbnails now resolve built-in cube and error meshes directly instead of waiting for catalog entries that
  intentionally do not exist.

- Identity-preserving asset and folder moves now update source records directly without launching or following with a
  full-project import, removing prefab move/delete stalls caused by unrelated large mesh imports.

- The asset trash popup now uses an on-open snapshot instead of locking and reparsing manifests every frame, and trash
  mutations queue behind active asset work rather than failing or freezing the editor.

- GameObject-to-prefab drops now queue behind active asset work, development prefab thumbnails resolve directly from
  source before catalog publication, and the complete asset grid accepts drops into its current folder.

- Prefab deletion now updates the asset index without rescanning the project, folder drops cover complete cards and
  rows, and deleting prefab scene instances removes stale mappings before later unpack or save operations.

- Generated managed projects now target the Visual Studio-compatible .NET 8 baseline and stage `Keire.Managed.dll`
  locally for reliable engine API IntelliSense. Prefab creation publishes immediately to the development asset system,
  project trash mutations avoid full worker imports, restore collisions discard stale trash, and complete folder cards
  accept asset and GameObject drops.

All notable template changes are documented here. The format follows Keep a Changelog, and releases use semantic version tags.

## Unreleased

- Preserve managed Inspector assignments when entering Play Mode by reading legacy lowercase state documents and
  publishing new component state with canonical casing; restored UI Button references now register click events.
- Added serializable scene UI Button references for managed Behaviours, including Inspector assignment, native
  component validation, event-style `Clicked` dispatch, and compatibility with existing polling UI scripts.
- Expanded the managed runtime bridge with generation-safe entity names, hierarchy traversal, active state,
  native-component handles, Behaviour lookup, fixed/unscaled/elapsed time, and configurable spatial audio playback.
- Added Inspector serialization for entity references, typed asset references, enums, and bounded nested
  `[SerializableType]` data, including range, tooltip, grouping, undo, scene/prefab persistence, and Play-world rebinding.
- Added audio metadata inspection, waveform thumbnails, Preview/Stop/Reimport actions, and full Audio Source authoring
  for resident and streaming WAV, Ogg Vorbis, and FLAC clips.
- Added scene-authored retained runtime UI and audio presentation in editor Play Mode and cooked runtimes, including
  managed audio playback, managed text updates, button click consumption, audio clip importing, and generation-safe
  teardown.
- Fixed Ninja managed-build quoting, current scene-schema regression checks, input override persistence coverage, and
  clamped weapon equip timing in the managed production harness.
- Fixed one-shot scene audio replay and Stop semantics, stale deferred UI events after teardown, interrupted-reload
  magazine loss, feedback-pool callback rollback and stale leases, zero-time ballistic advancement, editor-test
  compilation, and Windows package-fixture drift.

- Added Project-panel C# script, managed assembly, prefab-from-selection, and prefab-variant creation; source assets open
  through a persisted external-editor choice with system-default fallback. Prefabs instantiate transactionally when
  dragged into the Scene viewport and open in an isolated Prefab Mode with explicit Save/Discard boundaries.
  Apply-to-source preserves scene root placement, rebases variants, rejects nested ownership flattening, and records
  source plus scene metadata as one undoable operation. Stable-ID source replacement validates before publication and
  rolls back source and metadata on failure.
- Hierarchy GameObjects can now be dropped onto Project folders, breadcrumbs, or blank content to create uniquely named
  prefab assets. Prefab thumbnails compose variants and nested instances and render all visible world-transformed mesh
  geometry. C# source opening regenerates SDK-style assembly projects and a Visual Studio solution from `.keireasm`
  definitions, then opens `devenv.exe` with the solution and requested script.
- Scene camera orbit, pan, zoom, and fly navigation now wrap the visible cursor across opposite Scene viewport edges
  in window-local coordinates without applying the warp delta to camera movement, and remain active even when another
  editor panel has focus.
- Directional shadow maps now apply constant and slope-scaled raster depth bias, preventing large ground receivers from
  producing dense self-shadowing patterns while retaining model-cast shadows.
- Fixed imported meshes using another draw's transform on GPU backends where `first_instance` does not offset
  `SV_InstanceID`; visible geometry and shadow rendering now apply the same per-entity scale.

- Added gameplay-production foundations: scene schema v3 and nested prefab/variant composition, transactional prefab
  authoring helpers, application-owned profiling/scripting/physics/audio/navigation services, `.keireasm` assembly
  graphs with cancellable last-good managed builds, Jolt-backed rigid bodies and deterministic convex/triangle
  collision cooking, miniaudio-owned device/headless engines with offline graph rendering, Recast geometry baking with
  deterministic dependency hashes, revisioned synchronous/asynchronous navigation paths, and cooked runtime manifest
  schema v2. The pinned Coral patch now hosts .NET 10 through nethost, performs transactional collectible-context
  reloads with Behaviour type-registry validation, context-scoped unload cleanup, live Behaviour migration, and
  stable-ID scene-component lifecycle dispatch, and supports an explicit bundled runtime root. Cook now compiles
  runtime `.keireasm` graphs and publishes their DLLs; packages carry RID-specific CoreCLR/hostfxr, Coral,
  Jolt/Recast/miniaudio link closures, identities, and licenses. Added deterministic streamed pack pages, Recast-built
  Detour tile payloads, skeletal import/animation assets, audio voices/spatial mixing, prefab/build/profiler panels,
  and a managed third-person sandbox foundation.

- Made the static-scene frame graph operational with compiled transitions and transient alias slots; scene color now
  renders to RGBA16F and executes fitted ACES into double-buffered display surfaces. Added bounded renderer-thread
  submission, full Forward+ light/tile/index storage-buffer consumption, GPU instance buffers for compatible opaque
  draws, CPU preparation p95 diagnostics, and a 10,000-object batch benchmark. Surface HDR attachments now come from
  the graph-owned physical transient heap, with deterministic device-loss, resize/minimize/restore, and queue-capacity
  backend tests.

- Model import now publishes stable generated material and embedded-texture sub-assets, converts glTF PBR factors,
  alpha mode, cutoff, double-sided state, and semantic texture slots, and cooks the complete generated dependency graph.
  FBX/OBJ material limitations produce actionable diagnostics, and Project context menus can extract generated
  materials into editable `.keirematerial` sources through the isolated asset worker.
- Completed scene-document mutation ownership for Entity menu creation/deletion and viewport mesh/material drops, so
  workspace composition code no longer mutates scene objects directly.

- Replaced placeholder shadow statistics with GPU depth-map rendering and receiver sampling for directional, point,
  and spot lights. Default-material primitives now receive those shadows, local lights expose persistent quality,
  strength, and bias controls, and the portable local-light uniform path is validated on D3D12 and Vulkan. Shadow
  metadata is isolated from light color/intensity, and receiver bias prevents a surface from tinting itself when
  shadows are enabled without a separate occluder.
- Cameras now choose a serialized Skybox or Solid Color background. Scene view, Game view, camera preview, and
  standalone runtime honor that choice, the built-in sky sun matches the default directional-light convention, and
  Project Settings explains that scene shadows remain authored by Directional Lights. Shift-click range selection works in both
  Hierarchy and Project, and the single-row main toolbar keeps Play/Pause/Step/Stop visible below the menu.
- Projects now render a built-in studio sky whenever no custom environment is assigned. Project Settings replaces the
  raw environment AssetId field with the shared searchable, type-filtered asset picker used by Inspector fields, with
  drag-and-drop validation and one-click reveal in the Project panel.
- Scene Open/New/Close/Exit actions now commit through an editor-owned frame-safe transition coordinator. Scene and
  operating-system drops work on an empty viewport, failed scene decoding preserves the current document, and closed
  scene references become inert before panel helpers can query them.
- Added compact non-overlapping main toolbar and status-bar primitives, hinted search inputs, expanded editor icons,
  refreshed Kéire Dark/Light density, Hierarchy search/create, session Inspector lock, Game aspect previews, and
  Console severity/collapse controls.
- The Project Hub now defaults to a persistent sortable list with card-view fallback, keyboard/double-click/context
  actions, atomic view preferences, and a larger validated two-pane project-creation workflow.

- Scene authoring now routes `Ctrl/Cmd+D` through the shared duplicate command and supports hierarchy drag insertion
  before or after a sibling, parenting onto a row, and unparenting onto blank hierarchy space while preserving world
  transforms and undo history.
- Asset Browser thumbnails now use an untextured clay render of imported mesh geometry plus dedicated scene, shader,
  and input-action artwork. The Scene viewport adds a toggleable live main-camera preview, and the Project Hub uses a
  refreshed responsive card presentation and modern navigation palette.
- Project environments now render visible sky backgrounds from LDR or Radiance HDR equirectangular maps and common
  horizontal/vertical cubemap cross and strip atlases, with rotation, intensity, exposure, layout metadata, and
  versioned reimport support.
- Added the version-3 static mesh format with ordered LODs, submesh ranges and bounds, stable material-slot indices,
  and v1/v2 compatibility. Model import preserves Assimp slots and recognizes `_LOD0`, `_LOD1`, and later groups.
- Materials now serialize opaque, masked, or premultiplied-blend surface state, alpha cutoff, and double-sided
  rasterization. Mesh renderers support indexed material overrides and cast/receive-shadow controls.
- Static scene submission now performs per-submesh frustum culling, LOD selection, deterministic transparent sorting,
  and per-slot binding. Point/spot lights now illuminate PBR materials with bounded attenuation/cone evaluation on
  D3D12 and Vulkan, expose Scene range/cone gizmos, and retain deterministic 16x16 Forward+ CPU tile diagnostics.
- Hub builds now depend on the editor (which depends on the Asset Worker), preventing stale editor binaries from
  loading newly compiled shader assets with a mismatched graphics binding ABI.
- Added the private typed ten-pass static-scene frame graph with deterministic hazard and lifetime validation.
- Inspector and Hierarchy mutations now route through validated `SceneDocument` commands, including Play/Edit
  targeting, component properties, material slots, shadows, and point/spot-light creation.
- Consolidated durable atomic file publication for settings, thumbnails, input overrides, scene/project settings,
  asset cache objects, catalogs, and build profiles. Packaging removes stale output and verifies manifest `HEAD`.

- Windows managed clients and private tools now reconstruct the process command line with `CommandLineToArgvW` and
  normalize it to UTF-8 at one shared boundary before parsing. Asset-worker protocol and publication paths also append
  suffixes without narrowing native paths, so editor operations work from Unicode checkouts.
- Scene viewport, Hierarchy, Inspector, Input Actions, Project Settings, and Asset Browser now own their UI and
  persistent interaction state behind narrow controllers, with no workspace friendship or whole-panel draw forwarding.
  Scene and Input documents own their save/recovery lifecycles, and the workspace facade is below 1,500 lines.
- Split asset indexing/import, external transactions, mutations/trash, and cooking behind a sub-600-line facade;
  nested operations now use explicit unlocked helpers under a non-recursive transaction boundary. Rendering keeps a
  sub-700-line facade with separately compiled private device, surface/pipeline, cache, and scene-recording units.

- Added a project-settings document with validated atomic saves and coalesced undo, split script checks into timed fast
  and integration suites, and made Release/Dist packages clean-by-default with an explicitly marked local dirty
  override that CI rejects.

- Editor import, refresh, cook, and external-import receipt work now runs in the private `KeireAssetWorker` process
  through atomic versioned protocol documents. The editor coalesces material refreshes, reloads a published source
  index without rescanning, and bounds shutdown with cooperative cancellation followed by forced termination.
- New scene, material, and input-action assets are validated and published transactionally by the asset worker. A
  crash-released project-wide file lock serializes independent database owners, and recoverable directory-publication
  journals restore the last-good catalog after interruption.
- Asset-worker mutation requests now cover folder creation, asset/folder move and duplication, recoverable trash,
  restore, and permanent deletion. Project and Inspector mutations, scene Save As, and shader creation use the isolated
  publication path; multi-file creation journals recover auxiliary shader sources after interruption.
- `SceneDocument` now owns atomic scene save and recovery snapshots, while `InputActionsDocument` owns its source path
  and atomic save lifecycle; material refresh coalescing state is owned by `MaterialDocument`.
- Editor shutdown now cancels queued background catalog refreshes instead of forcing a project-wide import to finish;
  saved asset sources remain durable and refresh normally on the next launch.
- Scene view `F` frames the selected entity's imported mesh metadata and transformed descendants with aspect-aware
  camera padding, while double-`F` locks the camera to the selection like `Shift+F`.
- Project editors restore their last normal window position and size plus maximized or borderless-fullscreen state from
  project-local user settings. The Project Hub now uses a modern navigation rail and responsive recent-project cards.
- Fixed scene saves freezing the editor behind their background asset import by keeping asset record and status
  snapshot queries independent from the operation-wide publication lock.
- Fixed Stop after Play edits getting stuck when the Play Changes modal could not open on its first frame.
- Fixed clean Windows Ninja builds so generated built-in shader headers are produced before any engine source compiles.

### Fixed

- Successful imports now atomically advance compatible `.keiremeta` importer versions without rewriting stable IDs,
  settings, dependencies, or project-owned fields; failed imports leave metadata byte-identical. The Sandbox contains
  one organized monster FBX at `Assets/Meshes/Monster/base.fbx` with its active scene identity preserved.
- Asset move, folder, trash/restore, and cooked-publication renames now retry only transient sharing/permission
  failures with bounded backoff and resolved-path diagnostics. Script harnesses rely on compiled editor behavior tests
  instead of stale implementation-text probes while still proving top-level launcher failure propagation.
- Scene, Hierarchy, Inspector, Input Actions, and Project Settings panels now own their dock registration and content
  boundary; material drafts own their source/baseline lifecycle in `MaterialDocument`, document mutation storage is no
  longer public, and entity, selection, Play, save, and history actions share the command router.
- Asset diagnostics, naming actions, shader/material content, and material draft composition now have an independent
  `AssetInspectorPanel` state owner instead of sharing entity/component Inspector state.
- AssetTool now converts its UTF-8 command-line project, output, catalog, and input paths through the engine filesystem
  boundary, so packaged cooking works from non-ASCII repository and staging paths on Windows.
- Project Hub now preserves its UTF-8 executable path and resolves the editor in both packaged sibling and nested
  build-output layouts, with diagnostics listing every checked path when the companion executable is absent.
- Extracted editor panels now keep their move-only UI panel scope alive for the complete draw call, preventing Scene,
  Hierarchy, Inspector, or Input Actions content from falling into Dear ImGui's standalone `Debug##Default` window.
- GPU material entries now cache complete last-good pipelines, packed numeric slots, texture/sampler bindings, and
  dependency revision stamps. Shared-material draws reuse immutable bindings, component tint remains per draw, and a
  failed dependency rebuild cannot partially replace a working material.
- PBR tangent frames now transform normals and tangents through their correct matrices, Gram–Schmidt orthogonalize the
  basis, compensate handedness for mirrored instances, and use deterministic finite fallbacks for degenerate tangents.
- External asset imports now preflight and validate complete batches in private staging, journal publication for crash
  recovery, preserve zero partial record visibility, and replay undo/redo through the same rollback-capable cook path.
- Editor external-import work now has an explicit cancellable worker lifetime that joins during shutdown instead of
  leaving `std::async` teardown behavior to the standard library implementation.
- Noninteractive Windows repository launcher commands now preserve child-script failures and their native exit codes,
  preventing failed builds, tests, runs, or packages from being reported as successful automation steps.
- Asset pipelines now use the clockwise render-target front-face convention required by Kéire's left-handed camera
  matrices, so back-face culling preserves imported mesh exteriors instead of exposing their interiors.
- OBJ, FBX, glTF, and GLB import now converts Assimp's right-handed geometry, lower-left UVs, and counter-clockwise
  indices into Kéire's left-handed, upper-left-UV, clockwise asset convention; the importer revision invalidates stale
  mesh cache entries automatically.
- PBR materials now keep view and lighting vectors in the same world-space basis and write opaque output for opaque
  pipelines, preventing normal/specular artifacts and apparent holes on imported models. Material texture pickers also
  reject incompatible semantic/color-space assignments before they can invalidate the development catalog.
- Material creation now asks for a name before publishing, and single-asset create/rename operations update the live
  source index without synchronously hashing and cooking the entire project.
- The Sandbox monster now uses its textured material with dedicated linear Metallic and Roughness slots, leaves packed
  glTF ORM on its neutral fallback, and ships with a calibrated neutral studio environment and directional light.
- Project Browser breadcrumbs now defer navigation until path enumeration finishes and use stable scoped IDs, avoiding
  an ImGui empty-ID assertion when navigating from a nested folder to one of its parents.
- `Ctrl/Cmd+S` now routes globally to Scene save, existing-scene saves defer catalog rebuilding to a background task,
  and external imports no longer repeat import/cook work or expose IDs from a failed catalog publication.
- Newly imported meshes and textures remain hidden from thumbnail loading until their replacement catalog is mounted;
  early handles recover automatically and stale cube/checkerboard thumbnails are invalidated after publication.
- Texture imports infer normal and packed data semantics from conventional filenames, retain explicit dialog
  overrides, and report transactional catalog-validation failures without leaving fallback-only assets behind.
- Scene-view overlay controls no longer mutate ImGui layout cursors or trigger a window-boundary assertion when the
  editor opens.

### Added

- Play-mode review now tracks editor-authored entity/component/property paths and classifies later simulation output as
  Mixed, enforces created-parent and component dependencies, preserves unavailable component payloads, and validates
  the complete selected patch before replacing the edit scene.
- Asset import and cooking now expose engine-owned progress phases, typed cancellation, and stop-token-aware overloads
  while serializing every source mutation and catalog publication per database.
- Unity-style persistent Play/Pause/Step controls, nonintrusive Scene-view tool/orientation overlays, automatic
  Game-tab focus on Play and Scene-tab focus after Stop, and content-aware texture, material-sphere, and mesh previews.
- Scene and Hierarchy multi-selection with Ctrl-toggle, viewport marquee selection, primary-selection Inspector
  authoring, and batch duplicate/delete commands.
- Editable Play-mode scenes with isolated runtime undo, property-level change review, selective apply/discard, and
  guarded scene/exit transitions that leave applied changes dirty for the normal Save workflow.
- Cross-platform operating-system asset drops with Project-folder/Scene-viewport routing, transactional external
  copies, importer-owned texture options, cancellation, conflict handling, stable-ID replacement, and receipt-backed
  batch undo/redo.

- A metallic-roughness material surface with shader-declared semantic slots, neutral base/normal/ORM/emissive
  fallbacks, ranged numeric/color authoring, strict cook validation, and glTF +Y normal-map handling.
- Version-two mesh vertices with stored tangent handedness, deterministic v1 tangent reconstruction, and cataloged
  local bounds used by hitch-free scene picking.
- Cold-process rendered-output repeat harnesses and balanced render-frame cancellation/completion during shutdown.
- Correct SPIR-V entrypoint binding for Vulkan, Windows catalogs containing both DXIL and SPIR-V, and required
  GPU-runner diagnostics for D3D12, Vulkan, and Metal.
- Persisted Scene-camera ownership and typed viewport asset-drop routing, including direct mesh-to-entity creation.
- Shader-driven material texture authoring in code and Inspector, with filtered Texture2D pickers for every declared
  base-color, normal, emissive, mask, or custom texture slot and source-safe material serialization.
- Scene-view ray picking for active transform-only and rendered entities, while retaining Camera and Directional Light
  overlay selection and nearest-hit behavior.
- A renderer-owned asset resource layer for revisioned mesh, material, shader, texture, sampler, and pipeline GPU
  resources, with indexed asset geometry, fixed shader bindings, transactional fence retirement, checker/error
  fallbacks, and last-good hot reload behavior.
- MSVC AddressSanitizer builds retain first-party instrumentation while matching the unannotated STL ABI used by
  private CMake dependency archives.
- A transitive startup-scene cooker, textured Sandbox pyramid, cooked runtime manifest, and managed `KeireRuntime`
  player with finite-frame package smoke validation.
- Pinned private Assimp and stb dependencies, a strict versioned `.keiremesh` format, deterministic OBJ/FBX/glTF/GLB
  static-mesh import, RGBA8 texture import with normalized sampler settings and deterministic mip generation, and an
  `AssetTool convert-mesh` command.
- Headless editor document and command-router tests, with scene and Input Actions state moved behind dedicated document
  owners and workspace panels composed through narrow controller interfaces.
- A component-driven Inspector fallback with transactional property edits, generic drawers for every registered property
  kind, component/property overrides, filtered asset and entity pickers, and engine-owned scalar/vector UI controls.
- Kéire-owned logging levels and formatting for placeholders, integer hex/width, and floating precision; public headers
  and SDK include paths no longer expose spdlog or fmt.
- Backend-conditional D3D12, Vulkan, and Metal rendered-output tests with synchronized offscreen RGBA8 readback and
  tolerant pixel assertions for lighting, transforms, material/shader/texture revision swaps, and last-good output.

- Project-owned ambient color, intensity, and exposure settings with atomic persistence, a dockable Project Settings
  editor, and matching Scene/Game lighting submissions.
- Unity-style Scene transform gizmos with `Q/W/E/R` tool shortcuts, Local/Global space, independent position/rotation/
  scale snapping, persistent tool preferences, Camera icons/frustums, and Directional Light icons/rays.

- Advanced Input Actions authoring for action/value types, control schemes and device requirements, composites, binding
  groups, control browsing, every built-in interaction/processor, and conflict-aware interactive rebinding.
- Typed Scene-view asset drops: Material assignment uses object picking and shared scene undo, while Scene and Input
  Actions assets open through their existing guarded workflows.
- A built-in Directional Light Lambert path with transformed cube normals, linear color/intensity, optional color
  temperature, and an unlit editor grid.
- Fixed ambient and Directional Light delivery across SDL_GPU backends by using the proven per-draw vertex uniform
  block, and stopped Project Settings drags from issuing repeated atomic writes while Windows still retires a file.

- An application-owned bounded undo/redo service with isolated document contexts, owner-thread enforcement, mergeable
  commands, nested rollback-safe transactions, contextual Edit-menu shortcuts, and scene/input/theme/asset integration.
- Unity-style Project asset creation and management with current-folder templates, immediate browser synchronization,
  per-asset best-effort import diagnostics, multi-selection file commands, extension-free labels, metadata hover cards,
  drag/drop moves, and persistent recoverable trash.
- A first-party generated unlit shader path that applies each Mesh Renderer linear tint in Scene and Game views, plus a
  testable editor camera controller with framing, view locking, orthographic navigation, fly-speed control, and axis snaps.

- An application-owned SDL_GPU rendering system with coordinated Scene/Game/UI presentation, resizable sRGB/depth/MSAA
  targets, fence-based retirement, editor grid/cube rendering, Camera and Mesh Renderer components, and persistent
  Unity-style Scene camera navigation.
- A contextual shader import pipeline with pinned SDL_shadercross and recursive compiler dependencies, reproducible
  DXIL/SPIR-V/MSL compilation and reflection, immutable shader/material/mesh assets, target-platform cooking, editor
  creation commands, and packaged host compiler tooling.

- A Kéire-owned Unity-style ECS surface with stable weak entities, reference-counted components, application-owned
  registration, typed queries, deterministic lifecycle callbacks, private EnTT storage, and private GLM-backed math.
- Mandatory hierarchical Transform and authorable Directional Light components, canonical scene schema v2, schema v1
  migration, Missing Component preservation, and isolated Play/Pause/Step/Stop runtime cloning.
- A dedicated Asset Browser with folder navigation, persistent List/Grid modes, multi-selection, transactional rename,
  duplicate/trash operations, opaque UI images, and bounded asynchronous digest/version-cached thumbnails.
- Asynchronous Save As, global scene shortcuts, hierarchy context commands, component inspectors, scene play controls,
  and system-tray Hub backgrounding with a recoverable no-tray fallback.

- A project-first editor workflow with transactional Empty/Starter creation, versioned descriptors, canonical roots,
  OS-exclusive editor locks, project-local service paths, a recoverable recent-project registry, and packaged Sandbox.
- A dedicated KeireHub with searchable/pinnable recent projects, async native folder browsing, reveal/open actions,
  detached editor launch, project status diagnostics, and Hub/project smoke modes.
- Typed `.keirescene` assets, owner-thread mutable scenes with weak object handles and validated hierarchy mutation,
  plus asynchronous single/additive SceneSystem loading and frame-boundary activation events.
- Scene authoring across Project, Scene, Hierarchy, Inspector, and Console with atomic save, bounded undo/redo,
  Save/Discard/Cancel transitions, transform editing, subtree operations, and crash-recovery snapshots.
- A dockable Input Debugger with scoped UI-capture bypass, device/user inspection, transactional map subscriptions, and
  bounded searchable Console logging of action phase, processed value, scheme, device/user, duration, and timestamp.

- A reference-counted asynchronous asset runtime with typed fallbacks, priority loading, integrity-checked Zstandard packs, last-good hot reload, mount overrides, owner-thread completion events, and bounded eviction.
- A Unity-style source database with stable `.keiremeta` identities, content-addressed import cache, transactional file operations, deterministic cooker/validator, dedicated `KeireAssetTool`, and Project/Inspector editor integration.
- An application-owned action input system with typed `.keireinput` assets, keyboard/mouse/gamepad devices, local users and pairing, control schemes, frame snapshots, interactions/processors/composites, hot reload, UI capture, cursor modes, interactive rebinding, and atomic profile overrides.
- A dockable Input Actions editor with four creation templates, master-detail authoring, bounded undo/redo, canonical Save/Revert/Validate, searchable bindings, conflict-aware Listen capture, and live device/action monitoring.

- A Unity-style editor workspace with stable panel registration, factory docking, named and portable layouts, per-user atomic autosave, semantic Kéire Dark/Light themes, custom theme editing, and a polished eight-panel client shell.
- Static-first `KEIRE_API` annotations for same-toolchain shared-library preparation and a configurable assertion foundation.
- Thread-safe factory-only `Ref`/`WeakRef` ownership with polymorphic conversion and race-safe weak locking.
- Generated runtime build identity and dependency-free KeireClient help/version commands.
- A canonical SDK consumer example and validated package-only CMake imported target.
- A managed SDK consumer that links the KeireCore-owned entrypoint and validates the client factory contract.
- Identity and dependency lock manifests.
- Transactional full-template rename support.
- Doctor, LLVM coverage, SDK package, and script regression commands.
- Native x64/ARM64 selection and concrete compiler resolution.
- CI coverage, compatibility, security, dependency, and release-package automation.
- SDL 3.4.10 multi-window platform API with typed polling events, high-DPI state, owner-thread enforcement, deferred worker destruction, and inert post-shutdown handles.
- Strict nlohmann/json 3.12.0-backed `WindowSpecification` loading and a tracked `Config/Client.json`.
- Bounded `--smoke-window` and explicit `--config` client options with a real interactive event loop.
- A single-run `Application` runtime with deterministic layer/overlay lifecycle, deferred structural mutation, cancelable close handling, and exception-safe service teardown.
- A standalone typed `EventBus` with prioritized handled propagation, RAII subscriptions, allocation-free immediate dispatch, and bounded cross-thread queued delivery.
- Unity-style application-owned `Time` with scaled/unscaled clocks, smoothing, pause, fixed-step accumulation, interpolation, and backlog diagnostics.
- A Kéire-owned immediate UI API with frame-scoped RAII widgets, application/layer lifecycle integration, headless testing, docking, layout persistence, SDL3 input, and cross-platform SDL_GPU rendering backed privately by Dear ImGui 1.92.8-docking.

### Changed

- Scene view now previews the active scene Camera's clear color while retaining the nonserialized editor camera, and
  built-in Directional Light plus ambient illumination is evaluated through validated fragment-stage uniforms.

- Input Debugger action events now stay in a bounded local history, suppress idle/reset noise, coalesce meaningful
  analog changes, and forward to Console only through an explicit opt-in.
- EnTT 3.16.0 and GLM 1.0.3 are pinned private dependencies with IDE utility projects, external warning isolation,
  package attribution, and build-manifest identities without public header or source redistribution.

- Normal repository `run` launches KeireHub; direct editor launches require an explicit project. SDKs now carry the Hub,
  project/scene headers, and a complete validated sample project instead of a root-level standalone input asset.
- Generation stamps include Premake/config content and the first-party source inventory, automatically regenerating when
  translation units are added or removed. Windows Premake version checks now work from Unicode repository paths.

- First-party headers now live exclusively beneath each project's `Include` directory. Asset public headers and implementation sources are grouped under `Keire/Assets` and `Source/Assets`, while non-SDK Core headers remain isolated under `KeireInternal`.
- `Application` can own an opt-in `AssetSystem`; SDKs now include the asset APIs, asset CLI, private `KeireZstd` archive, Zstandard attribution, and transitive Core → ImGui → Zstd → SDL link closure.
- `Application` can own Input after Assets/Windowing and before UI. Windowing routes native events to multiple private sinks, and SDKs include the public Input API plus a validated Default Input source asset without exposing SDL or JSON.

- Dear ImGui now builds as the dedicated private `DearImGui` static-library project under the generated solution's
  `Dependencies` group. SDKs carry its separate archive transitively through `Keire::Core` without exposing or
  redistributing ImGui headers and sources.
- Public mutex-backed snapshot observers now accurately permit synchronization failures instead of declaring `noexcept`, and ignored return-value diagnostics cover the remaining query-style build and logging APIs.
- SDK packages now include the first-party `Keire/UiWorkspace.h` contract while keeping all Dear ImGui and JSON implementation headers private.
- Dear ImGui remains private to KeireCore's UI implementation with SDL3 and SDL_GPU backends; KeireClient uses the
  public `Keire::UiFrame` facade, and SDK packages expose `Keire/Ui.h` without redistributing Dear ImGui headers or
  sources.
- SDL dependency builds now enable SDL_GPU while keeping SDL_Renderer disabled. Docking is enabled for rendered UI; multi-viewports remain disabled pending explicit multi-window renderer ownership.
- Extracted layer ownership, overlay ordering, deferred mutations, traversal, and teardown into a dedicated public `LayerStack`; `Application` now delegates layer operations while orchestrating frame boundaries.
- Moved the executable entrypoint, informational command handling, exception boundary, application lifetime, and `Run` invocation into KeireCore; KeireClient now supplies `CreateApplication`.
- Moved client-specific help text into a static client command-line descriptor while retaining core-owned help/version handling.
- Public KeireCore headers now use the `Keire/` include prefix and the clean `KEIRE_*` macro family.
- Logging owns reference-counted private asynchronous state, supports console suppression, and makes detached handles safely inert after shutdown.
- Build identity refreshes immediately before KeireCore compilation and includes tracked and untracked dirty state.
- Dist builds use link-time optimization and CI treats template warnings as errors.
- Release SDKs include complete dependency licenses and separate platform symbols; Dist remains stripped.
- CodeQL and Dependency Review are explicitly opt-in and strict when enabled.
- SDL is built through a compiler-keyed dependency-only CMake cache while Kéire remains Premake-driven; SDK CMake consumers receive SDL transitively.

### Fixed

- Stopping Play from the Scene UI no longer closes the runtime scene while the current render frame still references
  it; Play transitions execute at a safe update boundary and render submissions retain immutable frame-local data.

- Material property edits now publish an in-memory runtime revision immediately and persist their catalog refresh in
  the background, avoiding a synchronous project-wide import for each texture, color, or slider change.
- Scene gizmos transform every selected root around the primary pivot without double-transforming selected children,
  and globally routed editor undo/redo shortcuts work from focused Scene and Hierarchy panels.
- Current development catalogs open without an unconditional startup recook, and the Hub launches the editor before
  nonessential recent-project registry maintenance.
- UI-owned thumbnail textures remain alive until the renderer backend has released them during shutdown.
- Hierarchy Ctrl-click uses the actual pointer press, viewport marquee selection starts consistently, and gizmo handles
  no longer clear scene selection.
- Material Inspector texture edits now reload the live material revision, and viewport material drops decode source
  definitions without invoking an absent byte-only importer callback.
- Restored the declared Zstandard, EnTT, GLM, and SDL_shadercross submodule gitlinks so recursive clones reproduce the
  locked vendor tree.
- SDK packaging now stages only tracked sandbox sources and rejects generated `Library`, `Logs`, `Build`, `Temp`, and
  recovery data in the staging tree, archive, and extracted validation copy.

- Project thumbnails and labels now initiate the same asset drag, component cards collapse without losing their bordered
  presentation, horizontal Scene-camera motion follows pointer direction, and file-manager reveal opens the canonical
  project asset path instead of the process default folder.
- The Game view resolves Camera components from the active runtime scene rather than editor-camera state, and focused
  Inspector/Hierarchy edits route Undo/Redo to the scene context.
- Hub-launched editors now resolve the pinned shader compiler independently of the project working directory; failed
  imports show full Inspector diagnostics, mirror errors to the editor Console and rotating logs, and no longer trigger
  Dear ImGui's null-ID drag-source assertion when the Project panel displays an error badge.
- Asset creation now remains visible when a later best-effort import fails, blank-space Project context creation works,
  and Mesh Renderer tint changes reach GPU draw constants instead of leaving the cube at its test-shader color.

- Project Hub tray callbacks now defer native window mutations until polling completes; one Show action restores,
  raises, and focuses the Hub, minimizing hides it, and closing or choosing tray Quit performs a full process shutdown.

- Minimizing the editor during event polling no longer abandons pending fixed ticks and terminates the process on the
  following frame.
- Minimizing a restored Project Hub now hides it back to the system tray, and Show Hub reliably makes it visible,
  restores it, and raises it again.
- Asset Browser grids no longer restore invalid Dear ImGui stretch weights when their responsive column count changes,
  and narrow panels collapse the folder tree instead of crushing both browser panes together.
- Transform inspectors now use compact responsive X/Y/Z drag fields with direct numeric entry instead of nine
  full-width sliders.
- Project Hub recent-project persistence now encodes filesystem paths as UTF-8, allowing projects beneath Unicode
  directories such as `KéireEngine` to open on Windows.
- Project generation can no longer silently omit a newly added implementation file and surface as unresolved editor
  symbols at link time.

- `CommandLineError` now participates in the `KEIRE_API` boundary, script regressions cover every exported class and free function, and mandatory event, UI, and window teardown paths contain synchronization failures.
- Workspace dock splits now preserve their proportions across fullscreen, maximized, and windowed host sizes instead
  of retaining fullscreen side-panel widths and collapsing the central region.
- Dirty theme preset changes now defer their confirmation modal until the preset combo or menu has closed, so Save, Discard, and Cancel can complete the requested switch reliably.
- Release packaging now compiles and runs a standalone consumer from the extracted SDK archive.
- Nested layer traversal now defers structural mutation until the outermost callback returns, and layer operations enforce the application construction thread.
- Detaching layers cannot create automatic subscriptions, and native window registration rolls back partial resource acquisition.
- Ninja's transient lock file no longer contaminates runtime build identity or package manifests.
- Windows packaging supports Git repositories without a first commit, and vendor probes suppress expected native Git errors.
- Linux coverage resolves `llvm-profdata` and `llvm-cov` from the selected Clang major version.
- Logger handles no longer expose spdlog or standard-library ownership and lock types in the public API.
- Shutdown no longer waits for live logger-handle values, preventing same-thread handle/shutdown deadlocks.
- Security workflows expose an always-running activation check so disabled advanced-security jobs cannot look silently successful.
- Linux Premake bootstrap now accepts release archives whose executable bit is not preserved.
- ARM64 Premake source builds install the platform UUID development headers before compilation.
- Vendor bootstrap verifies the committed submodule pointer and restores detached working trees to that exact hash.
- macOS tool version checks consume complete command output, avoiding `xcodebuild` broken-pipe crashes.
- Linux Clang bootstrap installs the LLVM profiling and coverage utilities required by coverage reports.
- Lazy and explicit logger initialization now serialize under one lifecycle lock.
- macOS bootstrap accepts Command Line Tools without full Xcode for non-Xcode generators.
- Arch installation performs a full package upgrade instead of a partial database synchronization.
- Windows detects containing Git worktrees and rename updates public-header include guards.
