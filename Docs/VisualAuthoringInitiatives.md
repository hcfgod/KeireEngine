# VFX And Shader Graph Initiatives

Review date: 2026-08-09  
Scope: KE-021 and the first major VFX parity milestone

Kéire measures these initiatives by stable capability contracts and production scenarios. Node totals are supporting
evidence, not the acceptance test, and visual similarity to another editor is not a completion criterion. Shader Graph
owns executable shader logic. Material Graph owns shader selection and material bindings. They publish through the
same immutable shader/material runtime boundary and remain independently authorable assets.

## Executive Baseline

| Initiative | Current validated baseline | First decision |
| --- | --- | --- |
| VFX parity | 278 frozen Unity 6.3 rows, 248 enabled Kéire-equivalent rows, 30 disabled rows, and 241 runtime descriptors | Deliver the remaining event/behavior and renderer-specific rows by production slice; never enable a catalog row without runtime, test, documentation, and scenario evidence. |
| Shader Graph and Materials | 120 stable node types, seven output models, reusable functions/layers, Material Parameter Collections, dynamic instances, generated DXIL/SPIR-V/MSL, Material Graph composition, live previews, and transactional publication | Complete renderer-wide collection binding, sampler/resource breadth, node previews, and enforceable performance gates before expanding specialized shading models. |

The VFX first-major-milestone target is 50 enabled parity rows. The next portable expansion targets 120 additional rows
above the validated 125-row baseline. The checked-in ledger closes that expansion plus three Kill Shape P0 rows, for
248 total. This satisfies both numerical gates, but does not close the initiative: the generated matrix keeps all 30 remaining renderer/GPU-resource
items visible and prioritized. The authoritative counts, priority assignments, evidence paths, and remaining rows are in the
[generated VFX capability matrix](generated/VfxCapabilities.md) and its
[machine-readable manifest](VfxParityManifest.json).

## Priority Contract

| Priority | Meaning |
| --- | --- |
| P0 | Required for representative production simulation, attribute, event, geometry, or Shader Graph output workflows. |
| P1 | Common authoring, sampling, presentation, preview, and iteration capability. |
| P2 | Specialized production depth after the main runtime and authoring contracts are stable. |
| Deferred | Pipeline-specific or low-portability behavior that needs an explicit Kéire design rather than a direct clone. |

Enabled VFX rows are marked `Complete`. Disabled rows receive one of the remaining priorities from the checked-in
policy. The offline validator rejects missing or drifted priorities, stale milestone counts, missing evidence, invalid
runtime mappings, and enabled implementations that do not belong to a tested production slice.

## Shader Graph Capability Matrix

| ID | Capability | Status | Priority | Current evidence and remaining acceptance |
| --- | --- | --- | --- | --- |
| SG-001 | Create, rename, duplicate, move, and recover assets | Shipped | Complete | Project creation and generic stable-ID asset mutations cover `.keireshadergraph`; deletion uses recoverable project trash. |
| SG-002 | Deterministic source, cooking, packaging, and transactional publication | Shipped | Complete | Schema-v2 JSON, bounded decoding, stable subassets, staged directory replacement, rollback, and target-specific shader cooking are tested. |
| SG-003 | Assign generated shaders through ordinary materials | Shipped | Complete | Direct Materials and Material Graphs store tagged raw-Shader or Shader-Graph references and import to ordinary `MaterialAsset` data; Mesh Renderer assignment uses the normal renderer boundary. |
| SG-004 | Searchable, organized node library | Shipped | Complete | The catalog exposes 120 stable type IDs grouped by authoring category and both graph editors filter names, categories, reusable functions, and layers. |
| SG-005 | Right-click creation and keyboard-first palette navigation | Shipped | Complete | The top-bar chooser and canvas-positioned right-click menu share focused live search, wrapping keyboard selection, Enter-to-create, recent/common entries, category browsing, and click-position placement. |
| SG-006 | Exposed scalars, vectors, colors, and textures | Shipped | Complete | Typed parameters include metadata, ranges, step, category, stable symbols, texture semantics, reflected Material Output pins, and Material Instance overrides. |
| SG-007 | Explicit samplers and broader resource types | Partial | P0 | Texture2D sampling, level sampling, and triplanar sampling ship; sampler-state authoring, arrays, cubes, 3D textures, and buffers remain designed work. |
| SG-008 | Math, UV, normal, lighting, attributes, BSDF, and output nodes | Shipped | Complete | The catalog includes scalar/vector math, branching, boolean logic, coordinate and derivative nodes, procedural tools, layered attributes, five BSDF operations, and seven output models including Fullscreen. |
| SG-009 | Live material and mesh preview | Shipped | Complete | Last-good asynchronous sphere, plane, cube, and custom-mesh previews expose environment and exposure controls. |
| SG-010 | Per-node previews | Partial | P1 | Built-in nodes are evaluated by the preview renderer, but persistent per-node preview tiles and selective preview compilation are not exposed. |
| SG-011 | Graph-, node-, pin-, and generated-line diagnostics | Shipped | Complete | Validation and compilation diagnostics carry stable `MG` codes, node/pin IDs, and generated line metadata while last-good output remains active. |
| SG-012 | Graph-format and generated-shader compatibility versions | Shipped | Complete | Public source-schema, generator, and vertex-layout versions are embedded in generated HLSL/manifests; future source schemas fail early with a specific recoverable error. |
| SG-013 | Renderer boundary and cross-platform shader compilation | Shipped | Complete | Generated source uses the renderer-neutral material contract and production importer for DXIL, SPIR-V, and MSL. |
| SG-014 | Consistent Shader and Material Graph thumbnails and badges | Shipped | Complete | Asset Browser resolves generated runtime materials, renders ordinary material previews asynchronously, and distinguishes `SG`, `MG`, and instance sources with type-specific fallbacks. |
| SG-015 | VFX outputs driven by Shader/Material Graph assets | Partial | P0 | Material-backed particle mesh output is enabled and tested. Dedicated quad and strip authoring contracts, exposed-property binding, and strip tiling modes remain disabled VFX parity rows. |
| SG-016 | Performance statistics and enforceable budgets | Partial | P0 | Compilation reports node, connection, reachability, texture, ALU, and variant counts. Reference-hardware compile, preview, and runtime gates remain required. |
| SG-017 | Examples, focused tests, and user documentation | Shipped | Complete | Nine paired Sandbox Shader/Material Graph examples, engine/editor/render tests, migration coverage, and the Shaders and Materials guide cover the supported workflow. |
| MAT-001 | Reusable Material and Shader Functions | Shipped | Complete | Distinct versioned assets expose typed inputs/outputs, searchable call nodes, deterministic recursive expansion, dependency extraction, cycle/depth rejection, tests, and recoverable editor validation. |
| MAT-002 | Material Layers and Layer Blends | Shipped | Complete | Layer and blend assets use Material Attributes interfaces, open in reusable graph mode, and can be called from Shader Graph or Material Graph without becoming standalone materials. |
| MAT-003 | Material Parameter Collection authoring and runtime state | Partial | P0 | Versioned assets, stable parameters, Inspector add/edit/remove/save/revert, deterministic serialization, and revisioned thread-safe runtime overrides ship. Renderer-wide buffers and world scoping remain. |
| MAT-004 | Dynamic Material Instances | Partial | P0 | The public runtime type provides typed transient overrides, reset, snapshots, revisions, and deterministic close. Render-thread upload/coalescing and managed API exposure remain. |

`Partial` never means an unsupported choice silently degrades. Unsupported graph resources or future formats are
rejected, and failed edits retain the last-good preview and runtime assets.

## Production Scenarios

| Scenario | Acceptance | Current state |
| --- | --- | --- |
| VFX-01 Hero weapon effect | Event-driven multi-system effect, deterministic random identity, material-aware output, live parameters, CPU/GPU execution, diagnostics, and packaged assets | Covered by existing value/context production slices and the VX-9 example; remaining specialized output rows stay visible. |
| VFX-02 Dense combat scene | Per-effect CPU/GPU attribution, visibility and distance culling, deterministic tier degradation, bounded queue behavior, and no random effect loss under pressure | Planned P0 beyond-parity work; hard world budgets and drop counters exist, but the scalability manager is not complete. |
| VFX-03 Recoverable graph failure | Broken link, future schema, unavailable resource, or compile error reports the responsible graph element, preserves last-good output, and can be repaired without asset loss | Authoring drafts, last-good publication, bounded decoders, and diagnostics ship; GPU particle inspection remains P0. |
| VFX-04 Deterministic replay and reload | Same version, seed, event stream, and fixed steps reproduce state across reload and CPU/GPU; incompatible revisions fail or migrate explicitly | Deterministic execution and revision-aware reload ship; checkpoints, seekable caches, and rollback are P0. |
| SG-01 Layered hero material | Artist builds a textured, displaced, clear-coated layered surface, exposes controls, previews it, assigns it, and receives actionable compile costs | Supported by the 103-node library, layered attributes/BSDF nodes, live preview, statistics, examples, and production shader importer. |
| SG-02 Transparent VFX material | Artist authors a transparent graph, binds it to quad/strip/mesh particle outputs, previews properties, and packages identical renderer contracts | Mesh is supported; dedicated quad/strip graph-output authoring and binding remain P0. |
| SG-03 Cross-platform package | One graph produces deterministic target shader assets for Windows, Linux, and macOS with versioned compatibility metadata | Source generation and DXIL/SPIR-V/MSL import ship; platform release validation remains part of the production gate. |
| SG-04 Safe upgrade and recovery | Historical assets migrate deterministically; future assets fail before mutation; a failed generated shader never replaces the last-good material | Schema-v1 migration, deterministic pins, explicit version contracts, transactional publication, and last-good runtime behavior ship. |

## Delivery Order

1. Close P0 resource/output boundaries: Shader Graph sampler/resource model, VFX quad/strip graph outputs, and
   property binding.
2. Close P0 production behavior: VFX scalability, deterministic checkpoints, frame-exact GPU handoff, GPU inspection,
   and enforceable reference-hardware budgets for both graph systems.
3. Deliver P1 iteration depth: per-node previews, broader sampling, cache/scrubbing, and authoring
   collaboration diagnostics.
4. Add P2 specialized nodes and outputs only through complete production slices with examples and CPU/GPU or
   renderer-backend evidence.
5. Keep pipeline-specific parity rows deferred until Kéire has a portable product requirement and an intentional
   engine-native design.

## Validation

From the repository root:

```powershell
python Scripts/Vfx/reconcile_vfx_manifest.py --check
python Scripts/Vfx/validate_vfx_parity_manifest.py
python Scripts/Vfx/generate_vfx_capabilities.py --check
python Scripts/Vfx/test_vfx_parity_tooling.py
./Scripts/project.ps1 test -Generator ninja -Configuration Debug -Toolset msc
```

The VFX tooling checks the frozen external catalog and live runtime contract. Shader Graph and Material Graph engine,
editor, rendering, asset, and package tests remain the executable authority for graph rows; prose does not upgrade a
row.
