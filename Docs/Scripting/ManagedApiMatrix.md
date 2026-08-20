# Managed API Capability Matrix

This matrix tracks the supported C# gameplay surface and the remaining parity work. Kéire borrows familiar workflow
ideas from Unity and Unreal without copying their object models: scripts remain `Behaviour` components, native objects
stay behind stable value handles, and every ownership or thread boundary is explicit.

Status meanings:

- **Production** — supported by the native runtime, documented, and covered by focused tests.
- **Partial** — useful production functionality exists, with named gaps still to close.
- **Planned** — intentionally absent from the supported managed contract today.

| Area | Status | Current Kéire surface | Next parity work |
| --- | --- | --- | --- |
| Application and persistence | **Production** | `Application` identity, editor detection, persistent-data path, safe quit; typed atomic `PlayerPreferences` | Cloud/platform save providers remain game-service scope |
| Frame time | **Production** | Scaled, fixed, unscaled, and elapsed time; validated time scale and pause | Capture/replay time domains remain native tooling |
| Screen | **Production** | Logical/pixel resolution, display scale, focus/visibility/minimize state, fullscreen mode, safe area, VSync state, transactional resize | Dynamic present-mode selection and multi-display placement |
| Behaviour lifecycle | **Production** | Enable/start/update/fixed/late, physics, animation, destroy, reload, coroutines, execution order | — |
| Entities and transforms | **Production** | Identity, hierarchy, active/layer/name, clone/destroy, component handles, local/world/presentation transforms | Tags and indexed scene queries |
| Inspector serialization | **Production** | Stable fields, ranges, tooltips, groups, nested data, asset references, reload-only state | Custom managed property drawers |
| Assets and managed data | **Production** | Typed asset IDs/references, bounded async managed-data loading, `ScriptableObject` clone and hot reload | General native asset async handles for every asset class |
| Input and cursor | **Partial** | Named actions, axis/button phases, cursor visibility and capture | Rebinding, devices, touch, gamepad metadata, rumble |
| Physics | **Partial** | Nearest raycast and capsule cast, bounded sphere overlaps, rigid bodies, character controller, collision/trigger callbacks | Additional shapes, joints, batched queries |
| Animation | **Production** | State playback, transitions, parameters, layers, IK, events, procedural locomotion | Animation-rig authoring remains editor/native |
| Audio | **Production** | Clips, sources, listeners, reverb zones, mixers/buses, playback status and spatial options | DSP graph scripting and microphone input |
| VFX | **Production** | Effect playback, lifetime, events, and typed parameter ranges | Managed graph construction is not a runtime goal |
| Runtime UI | **Partial** | Scene-backed buttons, sliders, toggles, input fields and scroll views; typed polling events, focus/navigation, safe-area scaling and accessibility metadata | Localization, data binding, animated transitions, and platform screen-reader adapters |
| Scenes and prefabs | **Partial** | Transactional prefab instantiate; active/loaded scene handles; coroutine-compatible packaged-player `Single` replacement with progress, cancellation, diagnostics, and activation rollback | Unified additive worlds, unload, active-scene selection, and Editor Play replacement |
| Rendering and materials | **Partial** | Typed Camera, Mesh Renderer, directional/point/spot light handles; mesh/material/texture/shader references; bounded per-renderer numeric, color, vector, and texture property blocks consumed by Material/Shader Graph output; atomic transient render-environment settings | Dynamic material instances and managed global parameter collections |
| Jobs and async | **Production** | Managed jobs, dependencies, priorities, cancellation, synchronization context, lifetime token | Parallel-for convenience APIs |
| Diagnostics | **Production** | Structured logs, exceptions, assertions, profiler spans/counters, debug lines | In-game diagnostic overlay controls |

## Compatibility Direction

The parity target is workflow familiarity, not API-name imitation:

- Kéire `Behaviour` fills the entity-attached scripting role without exposing a native object pointer.
- `Entity` and typed handles replace Unity-style mutable object wrappers and Unreal-style raw UObject access.
- Asset and component identities remain stable values, so reload and scene replacement can invalidate them safely.
- Mutations that cross runtime systems are validated and either complete transactionally or report failure.

New managed features must update this matrix in the same change that ships their native bridge, tests, and guide.
