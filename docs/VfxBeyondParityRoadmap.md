# Kéire VFX Beyond-Parity Roadmap

Review date: 2026-08-02
Scope: production capabilities that are not rows in the frozen Unity 6.3 Operator, Block, Context, and Output manifest

The [parity manifest](VfxParityManifest.json) measures node-catalog compatibility. It does not measure whether Kéire
can operate a VFX-heavy shipped game for years. The work below is intentionally separate: closing one of these items
must not change a manifest row or inflate the Unity parity score.

## Priority Backlog

| Priority | Kéire production feature | Current evidence | Production acceptance |
| --- | --- | --- | --- |
| P0 | Runtime scalability manager | `VfxEmitterComponent` persists Quality Tier, Culling Mode, and bounds, but runtime policy does not consume them. Worlds have hard effect/particle budgets and report drops. | Per-camera visibility, distance and fixed-bounds culling; priority/budget groups; deterministic tier selection; sleep/wake hysteresis; configurable CPU/GPU degradation; counters and tests proving that overload reduces quality without random effect loss. |
| P0 | Deterministic checkpoints, simulation cache, and replay | Seeds and simulation steps are deterministic, but no versioned world-state checkpoint or seekable cache is public. | Capture/restore every live system, particle, strip, event queue, parameter, RNG input, and generation; frame seeking and editor scrubbing; CPU/GPU replay validation; migration, memory bounds, and corrupt-cache rejection. |
| P0 | Network replication and rollback contract | Stable effect/parameter IDs and deterministic events exist, but there is no wire protocol or reconciliation API. | Versioned spawn/event/parameter command stream, tick and seed agreement, state checksums, prediction/rollback hooks, late-join snapshots, bandwidth budgets, and differential tests under loss/reordering. |
| P0 | Frame-exact queued GPU simulation handoff | Spawn work accumulates safely when render snapshots are skipped, but coalesced work uses the latest Time, Delta Time, and Simulation Step tuple. | Bounded per-step queue or mathematically equivalent batching that preserves fixed-step timing and random identity; explicit overflow policy; no simulation-path allocations; CPU/GPU differential tests during render stalls. |
| P0 | GPU particle and expression inspection | CPU debug snapshots expose bounded particle samples. GPU per-particle samples and intermediate register/attribute inspection are unavailable. | Asynchronous bounded readback, particle/strip selection, node-register and attribute history, NaN/non-finite provenance, source-node correlation, zero forced synchronization in normal frames, and editor visualization. |
| P0 | Per-effect device timing and enforceable budgets | Renderer/VFX completion latency and CPU preparation are measured, but the active SDL GPU boundary cannot publish true device timestamps. | Backend timestamp queries for D3D12, Vulkan, and Metal; stable effect/system attribution; overlap-aware totals; named-hardware baselines; automated per-effect GPU-time regression gates. |
| P1 | Warm-start, pre-roll, pooling, and seekable one-shots | GPU pipelines can warm asynchronously and world storage is pooled, but authored simulation pre-roll and reusable live-effect instance pools are not first-class. | Pre-roll without visible intermediate frames, deterministic warm-start caches, lifecycle-safe instance pools, burst preallocation, cancelable async preparation, memory telemetry, and no first-use hitch. |
| P1 | World streaming, large-world rebasing, and activation zones | Effects follow ordinary scene transforms, but no VFX-specific world-partition or origin-rebase contract exists. | Streaming-cell ownership, unload policy for world/local particles, rebasing without trails jumping, activation volumes, dependency prefetch, and deterministic save/restore across cell boundaries. |
| P1 | Multi-camera, split-screen, stereo, and XR policy | Outputs consume the active render scene/camera path, without an explicit multi-view simulation/sorting contract. | Per-camera culling and sorting, camera-relative data, shared-versus-per-view simulation rules, stereo-safe ribbons/billboards/depth collision, multiview cost accounting, and image tests. |
| P1 | Asynchronous gameplay readback channels | CPU callbacks and named input events exist; GPU particle collisions/deaths cannot publish bounded gameplay data without a backend-specific stall. | Typed asynchronous output channels with frame latency, quotas, overflow counters, stable particle identity, scene-safe delivery, server fallback, and deterministic tests. |
| P1 | Production telemetry and remote capture | Local profiler counters cover worlds, buffers, dispatches, drops, warmup, and completion latency. There is no shipping telemetry schema keyed by effect content. | Privacy-safe sampled metrics, stable asset/system IDs, drop-reason and memory attribution, remote capture triggers, build/content revision correlation, bounded overhead, and offline triage tooling. |
| P1 | Source-control-aware graph diff, merge, and collaboration | Stable IDs and canonical serialization make textual review possible, but there is no semantic three-way graph merge or collaborative ownership model. | Node/block-aware diff, move-versus-edit classification, stable-ID collision repair, deterministic three-way merge, conflict UI, comments/ownership metadata, and undoable resolution. |
| P2 | VFX extension SDK and sandbox policy | Built-in descriptors and Portable Custom HLSL are engine-owned. Third-party native Operators, Blocks, resources, and compiler passes have no supported ABI. | Versioned registration API, capability declarations, deterministic lowering contract, resource lifetime isolation, package signing/trust policy, crash containment, compatibility tests, and documentation. |
| P2 | Distributed shader/PSO build cache | Import/cook caching and cooked shader binaries exist, but VFX compilation has no shared farm/cache protocol. | Content-addressed cross-machine cache, compiler/toolchain fingerprinting, signed artifacts, negative-result caching, cancellation, cache observability, and cold-depot build benchmarks. |
| P2 | Automated visual approval and content certification | Runtime, schema, GPU readback, and production-slice tests exist, but artists lack a standard effect-turntable approval artifact. | Deterministic camera/lighting fixtures, CPU/GPU image sequences, perceptual thresholds, motion/flicker checks, performance envelope, reviewer metadata, and release-blocking certification reports. |

## Recommended Delivery Order

1. Consume the already-persisted quality, culling, and bounds data in a deterministic scalability manager.
2. Fix the queued GPU step contract, then build checkpoints/cache/replay on the resulting exact timeline.
3. Add true device timing and GPU inspection so later optimization work has attributable evidence.
4. Define networking and asynchronous gameplay channels against the checkpoint and identity contracts.
5. Add streaming, multi-view/XR, warm-start, and shipping telemetry as complete game-facing vertical slices.
6. Finish collaboration, extension, distributed-build, and visual-certification tooling for larger teams.

Each item needs native and managed API decisions, editor workflow, schema/versioning impact, CPU/GPU behavior,
failure diagnostics, package consumers, documentation, and named performance gates. A persisted field or visible control
does not count as support until runtime behavior and tests consume it.
