# Asset Database And Cook Pipeline

Importer registrations may declare UI-independent Boolean, Integer, Scalar, and Choice options. Normalized values are
stored in `.keiremeta`, participate in its digest, and arrive in `AssetImportContext`; editor UI is generated from the
descriptors without exposing ImGui or JSON. Existing `textureImportSettings` metadata remains readable.

Shader texture semantics constrain material authoring as well as cook validation. Base-color and emissive slots accept
color/sRGB textures, normal slots accept normal/linear textures, and metallic, roughness, occlusion, and packed ORM
slots accept data/linear textures. The Material Inspector filters incompatible choices before writing the material;
strict import and cooking retain the same validation as a defense against hand-edited or stale source files.

`AssetDatabase::ImportExternal` confines destinations to the project's source root, rejects symlinks and unsupported
files, validates through the selected importer, and publishes source plus metadata transactionally. Directory imports
preserve their relative layout. Batch failure rolls back created files and restores replaced content; explicit Replace
keeps the destination `AssetId`, while Unique Name never overwrites an existing source. Cancellation is honored during
preflight, copying, validation, and before publication boundaries. Successful batches retain a private
`Library/AssetImport` before/after receipt so Project undo restores replaced identities and redo republishes the exact
validated batch.
Validated import output flows directly into object caching and development cooking instead of invoking the importer at
each stage. Mesh and Texture2D importers can reconstruct derived state from unchanged cached canonical bytes, avoiding
Assimp and image/mipmap work during unrelated imports. The editor mounts the catalog returned by this transaction and
does not immediately launch a duplicate project-wide import.
While an external import is running, the Asset Browser keeps its last published record snapshot and does not request
thumbnails for the transaction's source records. Catalog mounting also recovers any early queued or failed resolve,
and the editor invalidates fallback previews when publication completes.

## Source Identity

`AssetDatabase` scans the project `Assets/` directory and owns editor/tooling identity. Every source has an adjacent
`.keiremeta` JSON sidecar containing a schema version, stable 128-bit `AssetId`, type, importer identity/version,
dependencies, and reserved stable subasset IDs. A missing sidecar is created atomically; malformed metadata is reported
instead of regenerated so an identity is never silently lost. Duplicate IDs, subasset collisions, symbolic links,
case-colliding paths, unsafe relative paths, oversized metadata, and oversized sources are rejected.

Rename moves source and metadata as one rollback-capable operation. Duplicate copies bytes and creates a new identity.
Delete is represented by `MoveToTrash()`, which moves both files under `Library/Trash/<asset-id>` for recovery. Public
operations confine destinations below the configured source root.

`MoveAsset`, `MoveFolder`, `DuplicateFolder`, `TrashAsset`, `TrashFolder`, `RestoreTrash`, and
`PermanentlyDeleteTrash` provide the Project panel's transactional file boundary. Folder operations include metadata
recursively, reject self-descendant moves and collisions before mutation, and roll back a partially completed move.
Trash manifests persist the original relative location so editor Undo can restore the same identity across sessions.

## Import And Change Detection

Catalog-producing and source-mutating operations are serialized per `AssetDatabase`. Record queries return immutable
snapshots, while import, cook, external publication, receipt replay, rename, move, trash, and metadata refresh execute
under one operation boundary. Nested import-to-cook calls remain safe, and parallel callers cannot reset or combine
each other's prepared cook inputs. Optional stop tokens and operation progress callbacks allow tools to cancel before
publication and report scanning, importing, cooking, and publication without exposing editor implementation types.

`ImportAll()` hashes sources with SHA-256 and stores immutable raw objects below `Library/AssetCache/Objects`. Existing
objects are cache hits. It then publishes the development runtime directory transactionally under
`Library/AssetCache/Runtime`. `PollChangedAssets()` uses file signatures and a 250 ms default stability window; hashing
and import happen only after a stable change. The editor remounts the published catalog and requests last-good reloads
for changed IDs. Interactive material authoring first publishes an immutable development asset revision directly to
loaded handles, then coalesces source persistence and catalog rebuilding at the edit boundary on a background task.
This preview path is unavailable in cooked mode and does not replace import/cook validation.

The default raw import path classifies common source/configuration/shader extensions as UTF-8 text and all remaining
files as binary. Owner-configured `AssetImporterRegistration` values claim specialized extensions, validate source
bytes, and emit deterministic canonical bytes before cache hashing. The Input registration claims `.keireinput`; both
KeireClient and `KeireAssetTool` install it explicitly. `CreateAsset()` validates through the registered importer
before transactionally publishing the source and metadata identity.

Contextual importers additionally receive the project root, source path, and a bounded project-relative dependency
reader. They return canonical bytes, structured diagnostics, source dependency path/digest records, and referenced
asset IDs. This is used by shader
manifests and preserves the byte-only callback for existing importers. Dependency digests participate in the cache key,
and failed reimports never replace a last-good runtime object.

Development catalogs omit references whose source assets have been removed so editor-time mesh and texture resolution
uses deterministic error/checkerboard resources without blocking unrelated imports. Strict platform cooking continues
to reject every missing dependency.

Interactive editors may call `ImportAll(AssetImportPolicy::KeepLastGood)`. Each source receives an `AssetImportStatus`;
a failed new source stays visible but is omitted from the runtime catalog, while an existing asset keeps its last
successful runtime revision. Structured diagnostics are mirrored to the editor Console and the rotating Core/Client
logs; failed shaders expose the full bounded diagnostic list in Inspector. `Strict` remains the default and is
mandatory for cooking and release packaging.

## Deterministic Cooking

`AssetCooker::Cook()` walks optional stable-ID roots transitively, sorts the selected entries by stable ID, compresses
each payload with pinned Zstandard and the selected
versioned `AssetBuildProfile`, target platform, and shards packs at a 2 GiB default limit. Catalog entries contain relative pack paths,
bounded offsets/sizes, type, SHA-256, and dependency IDs. Output is assembled in a sibling temporary directory, then
published with a recoverable directory swap. The accompanying `build-profile.json` records schema, profile name,
compression algorithm/level, shard limit, and strictness.

`AssetCooker::Validate()` independently checks schema, identity uniqueness, dependency closure/cycles, pack headers,
ranges, decompression sizes, and SHA-256. Distribution tooling should validate immediately after cooking and ship only
the resulting catalog, build profile, and packs—not source files or `.keiremeta` documents.

## Command Line

The dedicated `KeireAssetTool` target exposes the same APIs:

```text
KeireAssetTool scan --project <path>
KeireAssetTool import --project <path>
KeireAssetTool cook --project <path> --output <path> --profile Dist --target windows
KeireAssetTool validate --catalog <path>
```

Target values are `host`, `windows`, `linux`, and `macos`; contextual cook transforms use them to strip unused shader
variants. Windows keeps DXIL and SPIR-V for its D3D12 and Vulkan backends, Linux keeps SPIR-V, and macOS keeps MSL. A
project cook roots the graph at its startup scene and default input, and writes `runtime-manifest.json`
beside the catalog with startup and rendering settings. Optional cook controls are `--compression-level` and
`--pack-mib`. SDK archives include this tool and the asset public
headers; they carry the private `KeireZstd` archive transitively through `Keire::Core` but do not redistribute Zstandard
headers.

## Mesh and texture sources

OBJ, FBX, glTF, and GLB static meshes import through the pinned private Assimp build. Import applies source node
transforms, triangulates and deterministically merges meshes, rejects animation and skinning, and intentionally ignores
source material assignment. The resulting `.keiremesh` payload is a versioned Kéire binary containing finite
position/normal/UV/color vertices, unsigned 32-bit triangle indices, and verified bounds. `KeireAssetTool convert-mesh
--input <model> [--output <asset.keiremesh>]` emits the same format for inspection or source-control workflows.

PNG, JPEG, TGA, and BMP sources decode privately through stb_image into RGBA8 `Texture2DAsset` data. Normalized import
settings select color/data/normal semantics, linear or sRGB interpretation, maximum dimensions, mip policy, filtering,
addressing, anisotropy, and optional green-channel flipping. Normal-map mips average unpacked vectors and renormalize
them; other textures use a deterministic box filter. Neither Assimp nor stb headers are part of the supported SDK
boundary.

Material texture slots are declared by the referenced shader's `Texture2D` properties. `MaterialAssetDefinition`
provides `SetTexture`, `Texture`, and `RemoveTexture` for code and tools, while `MaterialAsset::EncodeSource` and
`DecodeSource` keep JSON ownership inside the engine. Inspector resolves the shader manifest and presents a
Texture2D-filtered picker for every declared slot, so base-color, normal, emissive, packed-mask, and project-specific
maps require no hardcoded editor path. Committing an edit rewrites the material atomically and schedules a coalesced
catalog refresh without blocking the UI.

The built-in PBR surface accepts either glTF packed metallic-roughness data (G roughness, B metallic) or separate
linear Data textures through Metallic Map and Roughness Map. Separate maps use their red channel. Their neutral
fallbacks are black metallic and white roughness, so omitted optional slots do not alter a packed workflow.

.keiremesh version 2 stores a float4 tangent direction and handedness. Version 1 remains readable and receives
deterministically generated tangents. Mesh import bounds are written into catalog schema 2; AssetSystem exposes them
through a read-only metadata query, so viewport picking never reparses OBJ/FBX/glTF/GLB source.
Assimp sources are normalized during import from its right-handed, lower-left-UV, counter-clockwise output to Kéire's
left-handed, upper-left-UV, clockwise mesh convention. Node transforms are applied before the canonical mesh is written,
and the conversion is backend-independent across D3D12, Vulkan, and Metal.

Before a catalog is published or cooked, material overrides are checked against the referenced shader declarations.
Unknown names, incompatible value types, out-of-range values, missing/wrong asset types, and incompatible texture
semantics or color spaces fail without replacing the last-good catalog.
