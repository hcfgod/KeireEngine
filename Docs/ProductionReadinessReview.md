# Kéire Production Readiness Review

Review date: 2026-08-02

Repository snapshot: `1d23614` plus the staged VFX-catalog, graph-authoring, runtime, test, sample-content, and documentation work in progress

Review target: modern commercial-engine and AAA team-production readiness, not feature-count parity alone

> Remediation update, 2026-08-04: the engineering defects identified by the follow-up audit have been corrected and
> validated locally. The 2026-08-02 table remains a historical snapshot; a separate provisional post-remediation grade
> appears below. GitHub Actions is currently unable to supply hosted evidence because the account's Actions quota is
> exhausted, which is distinct from a failing build or test.

> Current-contract update, 2026-08-07: this review intentionally preserves its dated scores, counts, performance
> captures, and remediation evidence. Since that snapshot, the current content contracts advanced to project schema 3,
> scene schema 5, mesh schema 4, VFX schema 4, material schema 3, and cooked runtime-manifest schema 4. Scene schema 5
> and mesh schema 4 carry the spatial-lighting authoring data described by the current rendering guides. The 52-guide
> public documentation build now validates its exact source inventory, implementation authorities, local links and
> fragments, key schema statements, Mermaid rendering, search index, and static output. These later facts supersede
> conflicting present-tense format or documentation-gap statements below; they do not retroactively change the frozen
> grades without a new full production-readiness review.

## Final Local Engineering Audit (2026-08-08)

The repository-wide remediation audit is complete locally. This update grades the current tree independently from the
frozen 2026-08-02 snapshot. It does not treat the account-level GitHub Actions quota exhaustion as a code failure, and
it does not substitute Windows-hosted emulation for native Linux, macOS, or Metal evidence.

| Assessment | Grade | Score |
| --- | ---: | ---: |
| Engineering foundation | A | 95/100 |
| Architecture, ownership, and lifecycle | A | 95/100 |
| Tests and local quality policy | A- | 93/100 |
| Build, CI, packaging, and SDK design | A | 95/100 |
| Current feature completeness | B+ | 87/100 |
| Unity 6.3 VFX catalog parity | C- | 70/100 |
| Cross-platform and hardware evidence | B | 84/100 |
| Release and shipping readiness | B+ | 89/100 |
| Overall production readiness | **B+** | **89/100** |

Current local evidence includes complete Debug and Release/NDEBUG matrices; AddressSanitizer; 532 core, 118 editor,
and 303 Hub tests; 18 rendered-output cases on both Direct3D 12 and Vulkan; client, managed, distribution-service, and
packaged-project smoke tests; and a Release package validated by low-level, managed, and source-module CMake consumers.
The final measured line coverage is 74.81% for core and 63.14% across core/editor/Hub/client, above the enforced 74.5%
and 63.0% non-regression floors. The compile database covers all 448 Premake-owned translation units, and all 166
changed workflow-scope C++ translation units pass local LLVM 22 clang-tidy with warnings treated as errors.

The remaining release-evidence gates are a clean hosted matrix after Actions quota is restored, native Linux/macOS and
Metal runs, privileged symbolic-link confinement probes, and broader named-hardware/long-duration performance evidence.
Feature closure also remains material: the frozen 278-row Unity VFX manifest still records 153 disabled rows. These
limits prevent an A-range overall production-readiness claim even though the locally observable engineering and release
machinery are now A-range.

The remediation includes atomic job dependency registration and worker-safe close, bounded replay decoding and rewind,
explicit replay failure diagnostics, crash-safe/link-safe project upgrades, editor composition of module importers,
correct pre-1.0 caret ranges, synchronized streaming lifecycle/statistics, bounded managed-job record retention, a
valid primary workflow, recursive quality coverage, parsed-workflow/UTF-8 gates, and enforced source-unit non-growth
budgets. Focused tests and local validation are recorded below. VFX catalog closure, Metal/Linux evidence, sustained
named-hardware baselines, and a clean hosted release matrix remain release gates rather than being relabeled complete.

## Executive Assessment

Kéire is a substantial, thoughtfully partitioned engine and editor foundation. It has unusually strong ownership,
determinism, asset validation, migration, profiling, and failure-isolation contracts for a project of this size. It is
well beyond a prototype. It is not yet ready to claim complete Unity-class or AAA production parity: the frozen Unity
6.3 VFX manifest remains mostly disabled, portable GPU timestamp queries are not available through the current SDL GPU
boundary, rendering still has material milestones, and the review snapshot found malformed primary-workflow YAML. The
workflow syntax has since been repaired and is parsed by both local harnesses; a hosted rerun awaits Actions quota.

| Assessment | Grade | Score |
| --- | ---: | ---: |
| Engineering foundation | A- | 91/100 |
| Current feature completeness | B | 84/100 |
| Unity 6.3 VFX catalog parity | C- | 70/100 |
| Observed sandbox performance | A | 94/100 |
| Performance-evidence maturity | B | 83/100 |
| Release and shipping readiness | C+ | 76/100 |
| Overall | **B** | **84/100** |

The correct description today is **production-oriented pre-release engine**, not **finished AAA production engine**.
The largest difference is closure: every supported path needs sustained cross-platform, GPU, migration, package, and
content-team validation, while every advertised parity catalog must have no disabled rows.

## Provisional Post-Remediation Assessment

This grade reflects the corrected local tree and the completed Windows validation on 2026-08-04. It is deliberately
provisional: Windows 10 on an NVIDIA GeForce RTX 3060 (driver 32.0.15.9597) is named local evidence, but it does not
replace the unavailable hosted Windows/Linux/macOS matrix or required Metal validation.

| Assessment | Grade | Score |
| --- | ---: | ---: |
| Engineering foundation | A | 94/100 |
| Current feature completeness | B | 84/100 |
| Unity 6.3 VFX catalog parity | C- | 70/100 |
| Observed sandbox performance | A | 94/100 |
| Performance-evidence maturity | B+ | 86/100 |
| Build and CI design | A- | 92/100 |
| Release and shipping readiness | B+ | 88/100 |
| Overall | **B+** | **89/100** |

The build/CI design is graded independently from hosted execution evidence. Its workflows parse, recursively cover the
first-party source surface, validate their compilation database, pin quality tools, enforce text and source-size
policy, and describe broad platform, sanitizer, coverage, security, package, and GPU lanes. Hosted status is
**not observed** while Actions quota is exhausted; it is neither counted as a pass nor misreported as a code failure.

### Local remediation evidence

- Debug: 492/492 core tests (30,089 assertions), 117/117 editor tests (1,596 assertions), and 17/17 rendered-output
  tests on each of D3D12 and Vulkan.
- Release/NDEBUG: 493/493 core tests (30,090 assertions), 117/117 editor tests (1,596 assertions), complete client
  compilation, and 17/17 rendered-output tests on each of D3D12 and Vulkan.
- AddressSanitizer: the rebuilt job-system group passed 7/7 tests and the transactional managed-reload lifecycle passed
  1/1 with 118 assertions; the wider focused ownership, replay, upgrade, handle, and streaming groups also passed.
- Managed production contracts passed 14/14 executable tests. Performance tooling passed 3/3, VFX tooling passed 8/8,
  and the reconciled VFX manifest validated all 278 entries.
- The Windows script harness passed workflow parsing, UTF-8 integrity, source budgets for all 440 governed files,
  managed API freshness, and integration fixtures. The compile database covers all 242 Premake-owned translation units,
  and the recursive C++ formatting gate covers the complete first-party surface.
- Two dirty-development Release packages completed end to end; the final exact-tree run took 10 minutes 24 seconds
  (the preceding run took 11 minutes 31 seconds). They validated the shipping hosts/tools, client and packaged-project
  smoke, archive/extraction, and direct, managed, and source-module CMake SDK consumers. The dirty marker prevents these
  local validation artifacts from being mistaken for official releases.

## Evidence Base

- 124,434 first-party production C++/C# source and header lines across 298 files in the seven native/managed product
  roots; 25,392 test lines across 69 files in the three native test roots.
- 525 unique native doctest cases: 404 core, 106 editor, and 15 backend-rendering cases. The 2026-08-02 Debug matrix
  passed 27,559 core assertions, 1,380 editor assertions, 2,753 D3D12 rendering assertions, and 2,786 Vulkan rendering
  assertions with no skipped cases in those runs.
- 32 top-level subsystem/reference documents plus a 326 KiB machine-readable VFX parity manifest.
- Five GitHub workflow files describing Windows, Linux, macOS, x64/ARM64, sanitizer, coverage, package, security, and
  GPU validation lanes.
- The latest supplied 240-frame history reported a 3.342 ms average, 4.001 ms P95, 4.888 ms P99, 6.352 ms maximum,
  no stutters, no VFX fence wait, and no dropped VFX particles. This is excellent observed behavior, but it is not a
  formal gate result: the reference profile requires 300 frames, matching metadata, and true GPU timestamps.
- An earlier dirty-checkout packaged smoke exposed 17.1 seconds of cold first-use GPU VFX pipeline creation. Async
  warmup is now implemented, and the current Vulkan backend test reported a 135.52 ms warmup, but a new packaged
  named-hardware cold/warm capture has not yet replaced the earlier evidence.
- The Unity 6.3 parity manifest contains 278 rows: 153 `Disabled`, 125 `Kéire Equivalent`, and 126 rows with some named
  Kéire implementation. A named implementation is not counted as supported while its manifest row remains disabled.

Counts measure review surface and test investment; they are not substitutes for defect rates, coverage reports,
platform runs, or shipped-game evidence.

## System Grades

| System | Grade | Evidence and judgment |
| --- | ---: | --- |
| Core architecture and lifecycle | A- | Public/private dependency boundaries, owner-thread rules, RAII, stable handles, deferred structural mutation, deterministic shutdown, and explicit service lifetimes are coherent and documented. The static library avoids presenting a false stable cross-compiler ABI. |
| ECS, scenes, prefabs, and undo | A | Stable entity/component IDs, missing-component retention, transactional hierarchy edits, isolated Play clones, selective Play Mode Changes, nested prefab composition, recovery files, and schema migration are strong. Schema 4 makes entity layers first-class and checked-in schemas 1-4 fixtures prove migration and canonical round trips. Future shipped schemas must extend that fixture policy. |
| Editor authoring and UX | B+ | The editor has mature scene navigation, multi-selection, gizmos, hierarchy ordering, asset operations, component cards, graph canvases, document histories, background work, and a visible/cancelable managed-readiness Play transition. VFX graph zoom now uses semantic detail levels and keeps pin labels from colliding at reduced scale. It still needs broader automated interaction coverage, accessibility/keyboard audits, localization/text-scaling checks, and sustained large-project usability testing. |
| Rendering | B- | SDL_GPU backend isolation, DXIL/SPIR-V/MSL assets, RGBA16F/ACES, global image-based lighting, shadows, PBR materials, Forward+, skinning, instancing, GPU VFX, frame-graph ownership, and detailed counters form a serious renderer. Documented gaps include spatial reflection probes, mixed/static lighting, and custom raw GPU passes; production claims also require repeated D3D12/Vulkan/Metal image and validation-layer runs on representative hardware. |
| VFX runtime and authoring | B- | Schema 4, typed cables, ordered Blocks, deterministic CPU/GPU value execution, events, strips, mesh/ribbon/volume outputs, resource-backed shapes, diagnostics, and bounded GPU pools are impressive. Per-step GPU timing can still collapse to the latest timing tuple when render consumption skips simulation snapshots; arbitrary resources and unrestricted Custom HLSL are absent. |
| Unity 6.3 VFX catalog parity | C- | 125 of 278 frozen manifest rows are documented Kéire equivalents, leaving 153 rows (55.0%) disabled. The latest vertical slice adds live Attribute reads, typed Inline values, constants, coordinate/rotation math, and deterministic fixed-3D procedural noise/curl across CPU and GPU. Enabled implementations must belong to a tested production slice, and the generated capability reference is reconciled offline against the runtime catalog. The lower grade reflects the remaining catalog closure objectively; strong foundations do not substitute for enabled parity rows. |
| Animation and rigging | B+ | Import compression presets and metrics, retarget diagnostics, blend trees, subgraphs, transitions, override/additive masked layers, preview/debug/profiling, grounding IK, ragdoll pose blending, LBS/DQS, and GPU skinning cover a broad production slice. More character/content diversity, deformation image tests, per-bone ragdoll authoring, and long-session transition/retarget soak data are still needed. |
| Physics and navigation | B+ | Jolt/Recast/Detour are hidden behind engine-owned contracts. Primitive/convex/static-triangle collision, character movement, queries, contacts, deterministic cooking, async path cancellation, and scene-owned worlds are sound. Complex gameplay needs broader stress tests for stacked bodies, high-speed contacts, crowds, streamed nav tiles, and platform differentials. |
| Managed scripting | A- | Generation-safe handles, collectible reload contexts, last-good assembly retention, serialized fields, managed data assets, gameplay services, native lifecycle, and bundled cooked runtime support are strong. `Behaviour.Enabled` synchronizes native state, `RequireComponent` is enforced transactionally, and `OnAnimatorIk` reaches managed code. Play now waits for the first active managed generation, preventing a scene clone from preserving gameplay scripts only as unresolved components; failed builds reject Play, while later builds can use the active last-good generation. Continued cross-platform reload and long-session evidence remains necessary. |
| Assets, import, and cooking | A- | Stable metadata identities, process-isolated workers, immutable content-addressed packs, dependency tracking, atomic publication, recovery/rollback, deterministic cooking, streaming handles, and package validation are among the strongest areas. Large-depot throughput, importer fuzzing, distributed cache behavior, and corrupt-pack recovery need ongoing production evidence. |
| Audio | B+ | Versioned clips/mixers, bounded voices, virtualization, spatial state, effects, snapshots, meters, deterministic offline rendering, and device-thread isolation are production-minded. Platform/device matrices, hot-plug behavior, long-run underrun testing, and content-scale mixing benchmarks remain important. |
| Input, gameplay, and runtime UI | B | Paired users, action phases, cursor ownership, Play-mode capture safety, FPS sample behavior, serialized UI, event dispatch, and gameplay service wrappers form a usable slice. The FPS controller can no longer enter a frozen scriptless first Play while startup compilation is pending. Rebinding/accessibility, multiple-local-player workflows, IME/controller navigation, and dense UI layout coverage need expansion. |
| Performance and profiling | B+ | Typed spans/counters, worker-thread instrumentation, trace export, renderer attribution, VFX capacity/group/fence metrics, callback timing, stutter statistics, capture validation, and machine-readable reference profiles are strong. The supplied history is A-grade for observed frame pacing, but the evidence system remains B-grade because the capture is short, metadata is absent, exports do not identify the same frame, and the current SDL GPU backend reports `GPU timing supported = 0`. Completion latency is diagnostic queue latency, not GPU execution time. |
| Tests and quality policy | B+ | 532 native core tests plus editor, Hub, and rendered-backend suites, sanitizer configurations, measured 74.5% core and 63.0% aggregate line-coverage floors, script harnesses, package consumers, and hygiene rules show strong intent. Test investment remains uneven, and the coverage floors should rise as editor, Hub, and client gaps close. Current results must be trusted only when the actual workflows parse and run. |
| Build, CI, release, and cross-platform | D+ | Historical 2026-08-02 grade: the intended matrix was broad, but malformed workflow YAML and an incomplete package run blocked continuously verified release health. The YAML defect is now corrected locally; this score remains frozen until the hosted matrix and complete package validation provide replacement evidence. |
| Documentation and samples | A- | The VFX guide is extensive and examples cover many workflows. Capability tables are generated from authoritative catalogs, manifest reconciliation and production-slice coverage are release-validated, and stale multi-system/event/strip milestone prose is corrected. The Kéire-specific production backlog is now explicitly separated from Unity catalog parity. Broader link/schema validation and content-team usability evidence should continue to grow. |

## Latest Performance Capture Audit

The supplied history covers frames 19,095 through 19,334. The accompanying trace snapshot reports frame 34,593,
whereas the pasted summary reports frame 19,334. They appear to be later samples of the same general workload, but
they are not synchronized artifacts and must not be combined as one exact frame. The history remains useful for
steady-state CPU pacing; the snapshots remain useful for qualitative counter inspection.

| Area | Grade | Measured evidence and interpretation |
| --- | ---: | --- |
| Frame pacing | A | 3.342 ms mean, 3.240 ms median, 4.001 ms P95, 4.888 ms P99, and 6.352 ms maximum across 240 frames. Only 12 frames exceeded 4 ms, two exceeded 5 ms, none exceeded 8 ms, and no stutters were reported. |
| Rendering CPU path | A- | Rendering averaged 1.711 ms, reached 2.222 ms at P95 and 3.660 ms maximum, and had the strongest correlation with total frame time (`r = 0.92`). Present dominated the sampled frame. Zero GPU fence wait indicates no observed frames-in-flight back-pressure, but Present and completion latency are not GPU execution measurements. |
| VFX runtime cost | A | The summary reports 0.164 ms preparation, 31 compute dispatches, 284 thread groups, six indirect draws, 16,384-particle capacity, 3.92 MB of buffers, no drops, and no pending warmup. The 87.506 ms persistent warmup is below the checked-in 250 ms VFX budget. The VFX workload shape is effectively unchanged from the preceding capture and is not the source of the higher frame average. |
| Managed scripting | B+ | Play update averaged approximately 0.634 ms over the history, with 0.687 ms P95 and 1.608 ms maximum. The snapshot's callback totals are session cumulative: 1,541.38 ms over 86,972 invocations is approximately 17.7 microseconds per invocation, so the lifetime maximum does not identify a current-frame regression. Per-callback-type attribution is the next requirement. |
| Editor UI | B+ | Editor UI averaged 0.666 ms, reached 0.782 ms at P95 and 1.781 ms maximum. The active Project panel accounts for roughly 0.30 ms in both supplied snapshots and explains much of the difference from the earlier capture where it was nearly idle. Large-folder and thumbnail-cache scaling should be measured separately. |
| Assets, physics, and audio | A | Asset work averaged 0.003 ms with a 0.048 ms maximum; physics averaged 0.030 ms with a 0.212 ms maximum; audio averaged 0.001 ms. There were no failed loads, queued work, underruns, scripting faults, or scripting diagnostics in the snapshots. |
| Capture certification | C+ | The history has 240 of the required 300 frames, no metadata artifact, mismatched snapshot/history frame IDs, and `GPU timing supported = 0`. Build configuration, exact GPU, driver, workload ID, and cache state are therefore unverified. This capture cannot pass either checked-in reference profile even though its observed CPU frame budgets are excellent. |

Compared with the earlier 2.11 ms average capture, the current mean is 58.4% higher, P95 is 56.3% higher, and P99
is 71.2% higher. That is not sufficient evidence of an engine regression because the active editor panels and capture
frame ranges differ. The major visible changes are managed Play work, Project-panel work, and Present; VFX preparation
increased by only about 0.019 ms and its dispatch/capacity profile stayed stable.

The next optimization pass should therefore prioritize measurement fidelity before low-level tuning: export one
atomic snapshot/history/metadata bundle, add backend GPU timestamp support, attribute managed time by script type and
callback, split Present into submit/pacing/swapchain components, and profile Project-panel enumeration, sorting, and
thumbnail/cache misses. A 3.34 ms average leaves substantial headroom, so speculative micro-optimization would be a
lower-value action than finding the two observed 5 ms-plus frames and validating cold/warm behavior in Release.

## Highest-Priority Findings

### P0 remediation complete locally: restore trustworthy CI

The malformed indentation at the former `.github/workflows/ci.yml:229` is fixed. Local harnesses now parse all workflow
files and reject duplicate YAML keys; the quality lane also runs `actionlint` and `yamllint`. A clean hosted
Windows/Linux/macOS matrix is still required before treating a release badge or compatibility claim as current evidence,
and cannot be obtained until the repository's GitHub Actions quota is available again.

### Closed in this pass: scene-schema release surface

Entity layers now have golden v1/v2/v3 migrations, a schema-4 fixture and canonical round trip, starter-scene schema-4
output, existing prefab/Play Mode coverage, and layer-aware direct and managed SDK consumers. The production matrix is
the release authority for Debug, Release, ASan, project smoke, and clean package execution.

### P1: Make the VFX manifest match the product claim

The authoritative manifest is 55.0% disabled. Prioritize coherent production effect families over isolated catalog
checkboxes, but do not advertise Unity 6.3 parity until every row is Supported, GPU Required, or a documented Kéire
Equivalent with executable tests. Production slices and generated capabilities now enforce this direction; keep
disabled nodes visible with reasons.

### Closed in this pass: managed behavioral contracts

`Behaviour.Enabled`, `RequireComponent`, and `OnAnimatorIk` now have native/managed implementations and focused tests,
including transactional dependency-cycle rejection and automatic required-component attachment.

### Closed in this pass: deterministic first-Play script readiness

Play no longer clones a managed scene while its first build/reload generation is unavailable. The editor queues the
transition, keeps cursor/input capture inactive, enters Play automatically after the safe-boundary reload installs and
rehydrates the editing scene, and exposes a Stop action to cancel the wait. Native-only projects start immediately;
failed first generations produce an actionable diagnostic; an active last-good generation remains usable during later
builds. Focused policy tests cover all of those decisions.

### P1: Require GPU and content-scale proof

Run repeated D3D12, Vulkan, and Metal image/readback/validation tests, implement portable backend timestamp collection,
and enforce the checked-in budgets for a representative PBR scene, skinned crowd, VFX-heavy scene, UI-heavy scene, and
asset-streaming traversal. Include
shader-cache cold/warm behavior and first-Play latency. Reference profiles and capture validation are now present, but
the timestamp-required profiles correctly fail until a backend can publish real device measurements. The last packaged
cold-start result remains the historical 17.1-second failure and must be replaced by a post-warmup named-hardware run.

### Closed locally: package validation end to end

The final 2026-08-04 dirty-development Release package completed in 10 minutes 24 seconds, following an independent
11-minute 31-second pass. It passed Release core/editor and D3D12/Vulkan rendering tests, project smoke, asset
tool/worker/runtime/hub compile gates, shader-compiler validation, staging/archive/extraction, and direct, managed, and
source-module CMake SDK consumer builds. The remaining release gate is a clean hosted/package run from the intended
release revision, not missing local package functionality.

### Closed in this pass: reconcile VFX capability documentation

The generated capability reference is derived from the runtime descriptor export and frozen manifest. Offline
reconciliation, generation checks, production-slice completeness, and parity validation now run in production scripts.

## Release Gates

A production release should require all of the following:

1. Valid workflows and a green clean-tree platform matrix.
2. Debug, Release, and applicable sanitizer suites passing without skips hidden as successes.
3. Required D3D12/Vulkan/Metal probes and validation layers passing on named hardware/driver baselines.
4. Scene schemas 1-5 and every other supported asset schema covered by checked-in golden migration fixtures.
5. Clean package validation for runtime, direct SDK consumers, and CMake SDK consumers.
6. No `Missing` parity rows and no user-creatable disabled/no-op feature.
7. Automated performance budgets with captured hardware, driver, resolution, workload, and percentile metadata.
8. Documentation consistency checks and release notes describing compatibility boundaries.
9. A recoverable upgrade rehearsal on at least one realistically sized project copy.
10. Content-team soak sessions covering authoring, Play transitions, hot reload, undo/redo, import churn, and crash recovery.

## Recommended Order Of Work

1. Rerun and prove the repaired CI matrix when Actions quota is restored, then lock it with branch protection.
2. Implement real device timestamp collection behind the existing renderer statistics contract, then establish named
   D3D12, Vulkan, and Metal reference baselines.
3. Continue VFX closure one complete production slice at a time until the manifest has no disabled rows.
4. Expand managed reload, scene-clone, and destruction differential tests across supported platforms.
5. Run a multi-week sample-game production soak before changing the product description to production ready.

The [VFX Beyond-Parity Roadmap](VfxBeyondParityRoadmap.md) separately tracks runtime scalability, deterministic
checkpoint/replay, networking, frame-exact GPU stepping, GPU inspection/timing, streaming, XR, telemetry,
collaboration, extension, distributed-build, and visual-certification work. None of those items changes the frozen
Unity parity score.
