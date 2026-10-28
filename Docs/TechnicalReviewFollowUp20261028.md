# October 28 Technical Review Follow-up

## Scheduling Gate

User-directed order, clarified October 28, 2026:

1. Finish the current environment rendering, night appearance, authoring and performance work.
2. Finish fog implementation and rendered workflow validation.
3. Finish the large-world implementation and travel validation in
   [Large World and Environment Plan](LargeWorldAndEnvironmentPlan.md), including terrestrial worlds,
   planetary transitions and seamless space travel.
4. Only then begin this review follow-up audit and its fixes.

This is a deferred backlog, not permission to interrupt those milestones. Existing performance and correctness
work continues under the current milestones. Recording an overlapping review finding here does not expand that
work into the deferred audit. Do not mark the gate closed merely because a build or a narrow test passes.

## Evidence Policy

Source: user-supplied “Keire Engine — Extensive Technical Review”, October 28, 2026.
The original attachment is retained by the conversation. The findings below preserve its actionable scope.
All findings start **unverified/deferred**. Reproduce against the then-current revision before changing code:
some observations may describe an older state. Grades and illustrative timing budgets are not acceptance evidence.
Record each disposition, revision, reproduction, fix and validation here when this phase begins.

## Deferred Work Register

| ID | Finding and investigation | Required evidence before closure |
| --- | --- | --- |
| TR01 | Historical D3D12 access violation in `nvwgf2umx.dll`, near descriptor binding/ImGui. Audit texture descriptor ownership, in-flight retirement, resize, device generations and asynchronous publication. Do not assume a driver defect. | Reproduction or documented inability to reproduce; lifetime regression coverage and extended editor sessions across resize, asset reload and recovery. |
| TR02 | Moving-camera cloud cost and temporal stability. Measure trace, cache, resolve and composite separately; correlate history rejection with camera speed. | Fixed scenes/settings and camera paths; GPU pass timings, frame percentiles and silhouette/ghosting captures. |
| TR03 | Full cloud trace fallback in the composite when low-resolution depth samples are unusable. | Fallback pixel counts and GPU cost in foliage, railings and thin geometry; any bounded alternative preserves depth and radiance correctness. |
| TR04 | Long pipeline preparation and startup asset work. Historical multi-second figures require current reproduction. Audit background preparation, cancellation, revision/device-generation publication and cache behavior. | Cold/warm startup and shader-upgrade measurements; responsive editor; stale results never publish. |
| TR05 | Hardcoded cloud density frequency filtering thresholds. | Resolution, grazing-angle and step-budget comparisons; use a footprint-based model only when evidence supports it. |
| TR06 | Approximate multiple scattering may drift in energy or color. | Bounded radiance across extreme settings, documented artistic controls and measured quality/cost tradeoffs. |
| TR07 | Cloud early ray termination at very low residual transmittance. | Actual steps saved in thin/dense/horizon cases and a defensible radiance error bound. |
| TR08 | Temporal history weight, nine-sample neighborhood cost and exact parameter compatibility checks. | Resolution scaling and animated-environment tests; no ghosting; complete compatibility-field coverage before considering a versioned key or relaxed rejection. |
| TR09 | Occlusion hierarchy reduction and depth convention consistency. | Joint construction/classification tests, including reversed-depth and boundary cases where supported. |
| TR10 | Fourteen HZB texture bindings and selection switch may create binding pressure. | Descriptor/update and GPU measurements; benchmark any mipmapped-resource alternative across backends before adopting it. |
| TR11 | Visibility atomic contention and diagnostic overhead. | Separate required scheduling from optional counters; measured group reduction or diagnostic gating without lost work. |
| TR12 | Forward+ per-tile limit of 128 lights and overflow handling. | Dense local-light scenes demonstrate explicit bounded behavior with no silent missing lights. |
| TR13 | Deferred sampler/resource pressure and shader variants. | Portable limits, variant counts, descriptor update costs and supported-backend validation. |
| TR14 | Lighting scalability beyond simple scenes. | Directional outdoor, many-light outdoor, clustered indoor, and probes/shadows/transparency benchmarks with repeatable budgets. |
| TR15 | Large-world completeness and precision claims. Reassess the completed preceding milestone rather than duplicate its implementation. | Authoritative high-precision positions, relative rendering, hierarchy, serialization/recovery/prefabs, native/C# ABI, physics, navigation, audio and interpolation evidence; region/HLOD/residency transitions. Asset streaming alone does not establish world partition. |
| TR16 | Atmosphere acceleration, multiple scattering, planetary precision and energy consistency. | Day/dusk/night, sun/cloud/exposure consistency and backend appearance comparisons; measure proposed LUT or scattering changes. |
| TR17 | Complexity and coupling in large implementation files and rendering APIs. | Identify concrete ownership, rebuild or maintenance problems; cohesive refactors with behavior preserved. File length alone is not a defect. |
| TR18 | Representative performance and long-session confidence. | Record hardware, settings, CPU/GPU/presentation/memory, p95/p99/max and session duration. Reviewer sample budgets are illustrative; retain the user's approximately 10 ms frame-time target and report misses honestly. |
| TR19 | Reviewer could not access website documentation. | Independently verify live availability and relevant links when this phase starts; do not infer a current outage. |
| TR20 | Cloud/atmosphere integration edges. | Transparent surfaces, particles, overlapping geometry, camera cuts, long-session sampling and backend parity tests; narrow unit passes do not establish production acceptance. |

## Delivery Rules

Batch related confirmed fixes before focused tests and DevDist UI validation. Preserve the existing rendering
baseline and capture regressions explicitly. Keep all work on master, avoid unrelated edits, and follow repository
rules for API documentation, compatibility and formatting. This backlog does not authorize deployments, dependency
changes or additional Git operations. Update actual status rather than promising production readiness in advance.
