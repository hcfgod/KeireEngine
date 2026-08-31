# Managed API Capability Matrix

This matrix tracks the supported C# gameplay surface and the remaining parity work. Kéire borrows familiar workflow
ideas from Unity and Unreal without copying their object models: scripts remain `Behaviour` components, native objects
stay behind stable value handles, and every ownership or thread boundary is explicit.

Status meanings:

- **Production** — supported by the native runtime, documented, and covered by focused tests.
- **Partial** — useful production functionality exists, with named gaps still to close.
- **Planned** — intentionally absent from the supported managed contract today.

## Presentation Gap Audit

The production presentation audit used the public declarations in `KeireManaged`, their registered native internal
calls, focused managed/native tests, and the SDK package consumers as evidence:

| Requested area | Existing public evidence | Audit decision |
| --- | --- | --- |
| Audio | `Audio`, `AudioSource`, `AudioListener`, `AudioReverbZone`, direct clip/mixer references, mixer buses and playback status | Production playback plus explicit Audio Clip/Mixer residency, priority, readiness, fallback, revision, diagnostics, and deterministic release |
| VFX | `Vfx`, `VfxEmitter`, typed Blackboard/range setters, events and playback status | Production playback plus VFX Effect/Volume residency; graph construction remains editor/native-owned |
| Materials and shaders | Camera, Mesh Renderer, light, property-block, `DynamicMaterial`, and `MaterialParameterCollectionInstance` objects | Production mutation plus typed Material, Shader, Texture, Mesh, collection, and graph residency |
| Shader/Material/VFX graphs | Immutable native graph assets and compiled runtime consumers; managed graph construction is intentionally absent | Typed graph identities and residency status ship without mutable graph objects or speculative node APIs |
| Runtime UI | `UIDocument`, retained `VisualElement` trees, controls, event propagation, binding, custom-element registration, virtualization, responsive styles, imported fonts, international shaping, panel scaling, and world/screen targets | UI Toolkit replaces Canvas authoring; localization-database authoring, image-atlas refinement, color emoji, and platform accessibility adapters remain follow-up work |
| Runtime assets | Managed data has bounded `LoadAsync`; presentation assets use `AssetLoadOperation<T>` | Typed native leases ship with priority, state, fallback, revision, structured errors, coroutine/async waiting, disposal, and hot-reload generation cleanup |

| Area | Status | Current Kéire surface | Next parity work |
| --- | --- | --- | --- |
| Application and persistence | **Production** | `Application` identity, editor detection, persistent-data path, safe quit; typed atomic `PlayerPreferences` | Cloud/platform save providers remain game-service scope |
| Frame time | **Production** | Scaled, fixed, unscaled, and elapsed time; validated time scale and pause | Capture/replay time domains remain native tooling |
| Screen | **Production** | Logical/pixel resolution, display scale, focus/visibility/minimize state, fullscreen mode, safe area, VSync state, transactional resize | Dynamic present-mode selection and multi-display placement |
| Behaviour lifecycle | **Production** | Enable/start/update/fixed/late, physics, animation, destroy, reload, coroutines, execution order | — |
| Entities and transforms | **Production** | Identity, hierarchy, active/layer/name/tags, clone/destroy, component handles, active/loaded/specific/persistent query scopes, local/world/presentation transforms | Spatial query predicates and streaming-cell filters |
| Inspector serialization | **Production** | Stable fields; custom labels, headers, groups, and tooltips; true sliders; one-sided bounds and drag steps; multiline and read-only fields; nested data, asset references, reload-only state | Arbitrary editor-code property drawers |
| Assets and managed data | **Production** | Typed asset IDs/references; bounded async managed-data loading; `ScriptableObject` clone and hot reload; explicit native presentation-asset residency handles with priority, state, fallback, revision, diagnostics, coroutine/async waiting, and generation-safe release | Streamed range access and custom project asset marker registration |
| Input and cursor | **Partial** | Named actions and phases; device snapshots; control schemes; persistent interactive rebinding; paired gamepad rumble; cursor visibility and capture | Touch, pen, sensors, XR, advanced haptics |
| Physics | **Partial** | Nearest raycast and capsule cast, bounded sphere overlaps, rigid bodies, character controller, collision/trigger callbacks | Additional shapes, joints, batched queries |
| Animation | **Production** | State playback, transitions, parameters, layers, IK, events, procedural locomotion | Animation-rig authoring remains editor/native |
| Audio | **Production** | Clips, sources, listeners, reverb zones, mixers/buses, playback status and spatial options | DSP graph scripting and microphone input |
| VFX | **Production** | Effect playback, lifetime, events, and typed parameter ranges | Managed graph construction is not a runtime goal |
| Runtime UI | **Partial** | UI Toolkit documents and v2 responsive styles; generation-safe live `UIDocument` queries; managed visual trees, controls, focus/pointer capture, event propagation, data binding, custom elements, virtualized collections, imported font families, Unicode shaping and bidirectional layout, bounded transitions/gradients, and screen/camera/render-texture/world presentation | Localization-database authoring, color emoji, and platform screen-reader adapters |
| Scenes and prefabs | **Partial** | Transactional prefab instantiate; stable active/loaded scene handles; shared packaged-player and Editor Play `Single`/`Additive` loads, unload, activation, scoped queries, persistent roots, progress, cancellation, diagnostics, and rollback | Deferred activation/preload controls, load priorities, and Edit-mode multi-scene authoring |
| Rendering and materials | **Partial** | Typed Camera, Mesh Renderer, directional/point/spot light handles; mesh/material/texture/shader references; bounded renderer-wide and per-slot dynamic overrides; hot-reload-safe global Material Parameter Collections; atomic transient render-environment settings | Render textures and custom render-pass scripting |
| Jobs and async | **Production** | Managed jobs, dependencies, priorities, cancellation, synchronization context, lifetime token | Parallel-for convenience APIs |
| Diagnostics | **Production** | Structured logs, exceptions, assertions, profiler spans/counters, debug lines | In-game diagnostic overlay controls |

## Compatibility Direction

The parity target is workflow familiarity, not API-name imitation:

- Kéire `Behaviour` fills the entity-attached scripting role without exposing a native object pointer.
- `Entity` and typed handles replace Unity-style mutable object wrappers and Unreal-style raw UObject access.
- Asset and component identities remain stable values, so reload and scene replacement can invalidate them safely.
- Mutations that cross runtime systems are validated and either complete transactionally or report failure.

New managed features must update this matrix in the same change that ships their native bridge, tests, and guide.
