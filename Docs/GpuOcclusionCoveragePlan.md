# GPU Occlusion Coverage Plan

## Implemented contract

The renderer now classifies static meshes, skinned meshes, mesh VFX, local lights, reflection probes, and light-probe
volumes through one conservative visibility policy. Static meshes and compatible mesh VFX can be rejected through the
indexed-indirect consumer. Skinned meshes currently enter the same candidate/indirect path but are forced visible until
a current-pose conservative bound is available; they also cannot seed the depth pyramid. Local lights and spatial
volumes are tracked and forced visible until Forward+ and volume consumers expose same-frame visibility masks.

Custom and generated shaders use the same path. Actual rejection still requires the instance-addressing ABI plus the
`ConservativeBounds` shader declaration. Depth contribution additionally requires `DepthOnlyGeometryMatch`. Unknown,
stale, unbounded, screen-space, and ordering-sensitive work is always visible by contract.

## Correctness boundary

HZB occlusion may reject work only when the submitted bound conservatively contains every generated sample for the
frame. Non-spatial work, unbounded displacement, overlays, and effects whose ordering changes the result must remain
visible or use a system-specific conservative proxy. Treating every renderer, light, and effect as an ordinary mesh
candidate would create intermittent disappearing content.

The existing path covers static model/mesh batches, compatible mesh VFX, and order-preserving singleton transparent
draws when their shader advertises conservative bounds and the current instance ABI. Unsupported work continues
through deterministic direct drawing.

## Shared visibility contract

Add a renderer-owned `GpuVisibilityCandidate` stream with a stable owner ID, visibility class, conservative world
bound, source generation, output slot, and candidate-only versus safe-occluder flags. Each subsystem publishes
candidates; the renderer owns HZB classification, capacity fallback, diagnostics, and lifetime validation.

Shader compilation publishes one geometry policy:

- `ConservativeStaticBounds`: model bounds contain all generated geometry.
- `ConservativeDynamicBounds`: the subsystem supplies a current-frame conservative bound.
- `UnknownOrUnbounded`: HZB never rejects the work.

Only depth-writing opaque geometry with matching depth geometry may become an occluder. Other safe candidates may be
rejected against the HZB but never contribute to it.

## System stages

1. Generalize the mesh candidate buffer while retaining the indexed-indirect ABI for static meshes, models,
   compatible custom shaders, and mesh-particle VFX.
2. Add a skinned indirect ABI carrying palette/skin addressing. Animation publishes a current-pose conservative bound;
   missing or stale bounds force visible fallback.
3. Add sprite, ribbon, trail, and billboard VFX proxies. Visibility masks feed the order-preserving VFX expansion pass
   rather than reordering transparent primitives.
4. Classify point, spot, area, and probe volumes before Forward+ tile assignment. Directional and global environment
   work remains visible because it has no finite occludable volume.
5. Add conservative proxies for decals, reflection probes, influence volumes, and editor-only spatial overlays.
   Screen-space UI and non-spatial post effects remain outside HZB culling.
6. Expose per-class candidate, visible, rejected, forced-visible, overflow, stale-bound, and fallback counters.

## Rollout gates

Each class ships behind a capability bit and defaults to direct execution until producer and backend advertise the
same ABI. Capacity overflow, unsupported shaders, missing bounds, backend failure, and stale generations force visible
behavior. Asynchronous readback never controls same-frame correctness.

Rendered-output coverage must include camera cuts, near-plane intersections, animated bounds growth, displacement,
transparent ordering, VFX spawn/despawn, light-volume edges, resource exhaustion, and Direct3D 12/Vulkan parity.
