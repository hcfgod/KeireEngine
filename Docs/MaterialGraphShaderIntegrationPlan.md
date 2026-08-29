# Material and Shader Graph Overhaul Contract

## Implemented foundation

Surface materials and target programs are separate authoring products that share the same typed graph model and shader
publication boundary:

- Material Graph schema 6 owns the authoritative OpenPBR/slab surface graph. It supports OpenPBR Surface, Mix Slabs,
  Add Slabs, Coat, and Fuzz nodes in addition to the existing typed attributes and BSDF nodes.
- Shader Graph schema 6 owns target programs. The serialized target declares `Ui`, `Fullscreen`, `Vfx`,
  `CustomGraphics`, `Compute`, or the temporary `LegacySurface` compatibility target, legal stages, fullscreen
  injection point, and compute thread-group size.
- Generated graphics shaders use contract 7 and schema-2 shader manifests. Manifests publish the target, stages,
  fullscreen/compute settings, stable properties, resources, render state, occlusion capabilities, and displacement
  bounds.
- Schemas 1–5 remain readable. Historical fullscreen graphs infer the Fullscreen target; other historical Shader
  Graphs infer Legacy Surface. Historical Material Graph expressions are promoted into the schema-6 `surfaceGraph`
  when saved.
- UI, Fullscreen, VFX, and Custom Graphics targets publish shader variants without manufacturing a user material.
  Compute validates and serializes now but fails compilation explicitly until the compute-program artifact ABI ships.

The current `.keirematerialgraph` extension remains the schema-6 surface-authoring container during transactional
migration. Promotion into `.keirematerial`, Material Instance schema 3, and redirector cleanup are later migration
steps; documentation and UI must not claim those transitions are complete.

## Target architecture

```text
.keirematerialgraph / future .keirematerial -- OpenPBR closures --\
                                                                +--> typed graph compiler --> HLSL --> shader artifacts
.keireshadergraph ---------------------------- target program --/
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
| Shared typed compiler | Partial | Both authoring paths use the typed graph compiler and publication boundary; the planned SSA optimizer/artifact ABI extraction remains. |
| Target integrations | Partial | Graphics variants publish; bounded UI/fullscreen/VFX runtime request APIs and compute dispatch remain. |
| Deferred renderer | Planned | Current runtime remains Forward+; no setting or documentation may imply a working deferred path. |
| Material promotion and instance schema 3 | Planned | `.keirematerialgraph` schema 6 and instance schema 2 remain authoritative. |
| Certified interchange adapters | Planned | Assimp remains the current legacy FBX/OBJ/glTF adapter; USD, Alembic, MaterialX, and process plug-ins are not certified. |
| IK Rig, Retargeter, and Control Rig assets | Planned | Existing arbitrary skeletons, Rig Definition, retargeting, and IK solvers remain supported. |

The detailed [Material Ecosystem capability matrix](MaterialParityMatrix.md) remains the acceptance authority.
Unsupported rows must fail explicitly rather than silently downgrade.

## Migration sequence

1. Preserve current IDs, generated subasset identities, raw-shader references, and schema readers.
2. Preview conversions and their before/after artifact hashes without writing.
3. Convert compatible Legacy Surface Shader Graphs to schema-6 Material Graphs at the same identity.
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
- Schema-1–5 projects open without source mutation and save as canonical schema 6.
- The Starter 3D schema-2 raw shader retains vertex layout 3, instance ABI 2, and occlusion metadata through import.
