# GPU Occlusion Coverage Plan

## Implemented contract

The renderer classifies static meshes, supported skinned meshes, mesh VFX, local lights, reflection probes, and
light-probe volumes through one conservative visibility policy. Static geometry uses the indexed-indirect consumer.
Skinned linear-blend geometry joins that path only when animation publishes a complete current-pose conservative bound
for the accepted frame; its safe-occluder depth draw binds the current skinned vertex stream. Mesh and sprite VFX,
Forward+ local lights, and spatial-lighting selection consume their own same-frame visibility masks. Directional lights,
camera-inside volumes, stale bounds, and unsupported work remain visible.

Custom and generated shaders use the same path. Actual rejection still requires the instance-addressing ABI plus the
`ConservativeBounds` shader declaration. Depth contribution additionally requires `DepthOnlyGeometryMatch`. Unknown,
stale, unbounded, screen-space, and ordering-sensitive work is always visible by contract.

## Correctness boundary

HZB occlusion may reject work only when the submitted bound conservatively contains every generated sample for the
frame. Non-spatial work, unbounded displacement, overlays, and effects whose ordering changes the result must remain
visible or use a system-specific conservative proxy. Treating every renderer, light, and effect as an ordinary mesh
candidate would create intermittent disappearing content.

The existing path covers static model/mesh batches, supported current-pose skinned batches, mesh and sprite VFX,
bounded local lights, reflection probes, light-probe volumes, and order-preserving singleton transparent draws when
their producer and shader satisfy the current ABI. Ribbons retain renderer-group ordering and are masked as whole
groups. Unsupported work continues through deterministic direct drawing.

## Shared visibility contract

The renderer-owned `GpuVisibilityCandidate` stream carries a stable owner ID, visibility class, conservative world
bound, source generation, output slot, and candidate-only versus safe-occluder flags. Each subsystem publishes
candidates; the renderer owns HZB classification, capacity fallback, per-class diagnostics, and exact frame, slot,
surface-epoch, and device-generation lifetime validation.

Shader compilation publishes one geometry policy:

- `ConservativeStaticBounds`: model bounds contain all generated geometry.
- `ConservativeDynamicBounds`: the subsystem supplies a current-frame conservative bound.
- `UnknownOrUnbounded`: HZB never rejects the work.

Only depth-writing opaque geometry with matching depth geometry may become an occluder. Other safe candidates may be
rejected against the HZB but never contribute to it.

## Frame stages

1. Skin vertices and prepare conservative current-pose bounds for supported linear-blend draws.
2. Render safe occluder depth, then construct the HZB.
3. Classify the unified candidate stream and publish geometry, VFX, local-light, and spatial-volume masks plus indirect
   draw and dispatch arguments.
4. Compact geometry and assign visible local lights to Forward+ tiles.
5. Select visible spatial-lighting resources and expand order-preserving VFX work.
6. Draw opaque work, then ordered transparency and VFX, followed by screen-space UI and post-processing.

Every output is owned by one in-flight frame and device generation. Device recovery invalidates those outputs and
rebuilds them while retrying the interrupted immutable frame, so an old-device mask cannot reach a new-device consumer.

## Rollout gates

Each class ships behind a capability bit and defaults to direct execution until producer and backend advertise the
same ABI. Capacity overflow, unsupported shaders, missing bounds, backend failure, and stale generations force visible
behavior. Dual-quaternion skinning, morph targets, and unknown vertex displacement remain forced visible until they
publish a conservative current-frame bound. Asynchronous readback never controls same-frame correctness.

Rendered-output coverage must include camera cuts, near-plane intersections, animated bounds growth, displacement,
transparent ordering, VFX spawn/despawn, light-volume edges, resource exhaustion, and Direct3D 12/Vulkan parity.
