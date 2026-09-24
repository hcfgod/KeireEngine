# Material and Shader Graph Overhaul Contract

## Implemented foundation

The contract below records the graph-overhaul foundation. The current creation workflow is property-first:
new schema-5 Materials select a shared or project shader and edit values in the Inspector, while existing schema-7
graph-owned Materials retain surface graph editing. See [Shaders and Materials](ShadersAndMaterials.md) for current
user workflows and [remaining work](MaterialShaderReplacement.md#current-remaining-work) for reconciled gaps.

Surface materials and target programs are separate authoring products that share the same typed graph model and shader
publication boundary:

- Material schema 7 owns the authoritative OpenPBR surface graph, domain, shading model, authoring mode, and bounded
  closure budget. It supports OpenPBR Surface, Mix Slabs, Add Slabs, Coat, and Fuzz nodes in addition to typed
  attributes and BSDF nodes.
- Shader Graph schema 6 owns target programs. The serialized target declares `Ui`, `Fullscreen`, `Vfx`,
  `Material`, `CustomGraphics`, or `Compute`, legal stages, fullscreen
  injection point, and compute thread-group size.
- Generated graphics shaders use contract 16 and schema-3 shader manifests. Manifests publish the target, stages,
  fullscreen/compute settings, stable properties, resources, render state, occlusion capabilities, and displacement
  bounds.
- Schemas 1–6 remain readable. Historical fullscreen graphs infer the Fullscreen target; other historical Shader
  Graphs infer Material. Historical Material Graph expressions are promoted into the schema-7 `surfaceGraph` when
  saved.
- UI, Fullscreen, VFX, and Custom Graphics targets publish shader variants without manufacturing a user material.
  Compute uses the separate ProgramArtifact compiler/runtime path; it is rejected by the graphics ShaderAsset
  importer and remains disabled in the editor's creation menu. See [compute acceptance](RevampComputeCompilerLane.md).

The `.keirematerial` extension contains new schema-5 shader-selected materials and existing schema-7 surface graphs.
Historical flat material
sources use the explicit `.keiremateriallegacy` compatibility extension; `.keirematerialgraph` is no longer
registered for new assets. Material Instance schema 3 stores stable property-ID overrides; redirector cleanup remains
a separate migration step.

## Target architecture

```text
.keirematerial ---------------- OpenPBR closures --\
                                                   +--> typed graph compiler --> ProgramArtifact --> runtime/cook
.keireshadergraph ---------------- target program --/
```

The common compiler boundary owns typed values, stage legality, stable properties, deterministic variants, source
mapping, resource reflection, and last-good publication. Material-only value changes remain binding updates. Static
topology, target, or resource changes require compilation.

Material targets describe surface closures; program targets describe stage outputs and render integration. Neither
public asset exposes SDL, native GPU handles, backend compiler objects, or third-party importer types.

## Rollout state

| Initiative | State | Current boundary |
| --- | --- | --- |
| Schema-6 target contracts and compatibility readers | Complete | Deterministic encoding, validation, manifests, templates, and tests. |
| OpenPBR/slab graph foundation | Partial | Core surface, bounded mix/add, coat, fuzz, HLSL lowering, and editor preview ship; complex deferred closure payloads remain. |
| Shared typed compiler | Complete | Both authoring paths emit the same versioned ProgramArtifact, reflection, diagnostics, dependencies, and variants. |
| Target integrations | Partial | Graphics variants and bounded compute dispatch exist; complete target-specific previews, shared scheduling, and platform acceptance remain. |
| Deferred renderer | Complete | Single-sample Deferred Hybrid, exact backend probing, standard/extended GBuffer lanes, decals, forward escape, and spatial-lighting parity are live. |
| Irradyn GI | Partial | Requested/effective modes, quality intent, and the live Deferred Hybrid spatial-lighting path ship; a dedicated multi-bounce scene cache remains future work. |
| Material promotion | Complete | `.keirematerial` supports schema-5 property sources and schema-7 surface graphs; legacy custom-shader assets retain compatibility readers. |
| Material Instance schema 3 | Complete | Stable property-ID overrides, static keyword selection, compatibility readers, importer, and Inspector are implemented. |
| Certified interchange adapters | Planned | Assimp remains the current legacy FBX/OBJ/glTF adapter; USD, Alembic, MaterialX, and process plug-ins are not certified. |
| IK Rig, Retargeter, and Control Rig assets | Planned | Existing arbitrary skeletons, Rig Definition, retargeting, and IK solvers remain supported. |

The detailed [Material Ecosystem capability matrix](MaterialParityMatrix.md) remains the acceptance authority.
Unsupported rows must fail explicitly rather than silently downgrade.

## Migration sequence

1. Preserve current IDs, generated subasset identities, raw-shader references, and schema readers.
2. Preview conversions and their before/after artifact hashes without writing.
3. Convert compatible historical surface Shader Graphs to schema-7 Materials at the same identity.
4. Convert binding-only legacy material graphs to Material Instances; inline genuine expressions only when a unique
   material is required.
5. Convert non-surface Shader Graphs to explicit targets and retain legacy paths as redirectors.
6. Publish atomically, validate the whole project, and roll back every source and metadata pair on failure.

Legacy readers remain until the documented two-minor-release window closes. Writers emit only current schemas, and
legacy payloads and redirectors never enter final cooked packages.

## Acceptance criteria

- Encoding, lowering, variant ordering, and generated manifests are byte-deterministic.
- Broken revisions retain the complete last-good preview and runtime artifact.
- OpenPBR additive composition cannot create an unbounded closure weight.
- Target/stage/resource mismatches fail with actionable diagnostics before publication.
- Material value edits do not invoke shader compilation.
- Historical sources open without source mutation; property-only sources retain schema 5 and graph-owned surface
  sources save as schema 7. Explicit reviewed migration preserves source and generated runtime identities.
- The Starter 3D schema-2 raw shader retains vertex layout 3, instance ABI 2, and occlusion metadata through import.
