# Asset Database And Cook Pipeline

## Static mesh version 4

`.keiremesh` version 4 adds a separate UV1 channel for baked lighting. Version 3 introduced ordered LODs, submesh index
ranges and bounds, and named material slots with optional default material identities. Versions 1 and 2 remain readable
as their implied single LOD/submesh/slot, with missing tangent or UV1 data generated or defaulted deterministically.
Importers keep submeshes deterministic, preserve Assimp slot indices and names, and group names ending in `_LOD0`,
`_LOD1`, and so on into contiguous LOD ranges with monotonically decreasing default thresholds.

Asset cache objects, metadata, catalogs, and cooked build profiles publish through the shared atomic-file boundary.
Cooked payloads use deterministic content-addressed pack names. Publication installs new immutable packs first and
atomically switches the catalog last, so a running editor may finish reads from an older pack without blocking import.
Recently retired packs remain available for in-flight work and are reclaimed after a grace period; locked cleanup is
best-effort and never changes a successful import into a failure.

Importer registrations may declare UI-independent Boolean, Integer, Scalar, and Choice options. Normalized values are
stored in `.keiremeta`, participate in its digest, and arrive in `AssetImportContext`; editor UI is generated from the
descriptors without exposing ImGui or JSON. Existing `textureImportSettings` metadata remains readable.

Shader texture semantics constrain material authoring as well as cook validation. Base-color and emissive slots accept
color/sRGB textures, normal slots accept normal/linear textures, and metallic, roughness, occlusion, and packed ORM
slots accept data/linear textures. The Material Inspector filters incompatible choices before writing the material;
strict import and cooking retain the same validation as a defense against hand-edited or stale source files.

`AssetDatabase::ImportExternal` confines destinations to the project's source root, rejects symlinks and explicit
unsupported files, skips unsupported companion files in dropped directories, rejects external `.keiremeta` identities,
and validates the complete batch in
`Library/AssetImport/<transaction>/staging`, and publishes source plus metadata transactionally. Directory imports
preserve their relative layout. A persistent `staged`/`publishing`/`committed` journal restores replaced content or
removes new files after an interrupted publication before records are exposed at startup. Batch failure rolls back
created files and restores replaced content; explicit Replace keeps the destination `AssetId`, while Unique Name never
overwrites an existing source. Cancellation is honored through validation and until the publication boundary; once
publication begins it finishes or rolls back atomically. Successful batches retain a private
`Library/AssetImport` before/after receipt so Project undo restores replaced identities and redo republishes the exact
validated batch. Receipt replay performs a strict candidate cook and restores its previous sources and metadata if any
step fails.
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

When a registered importer is a newer compatible version, a successful import atomically updates only the
`importerVersion` field in the sidecar. JSON fields unknown to the engine, stable identity, dependencies, subassets,
and normalized import settings are preserved. Failed imports do not touch metadata. Committed sample metadata is kept
at current successful versions, so opening or reimporting the Sandbox does not dirty tracked files.

Rename moves source and metadata as one rollback-capable operation. Duplicate copies bytes and creates a new identity.
Delete is represented by `MoveToTrash()`, which moves both files under `Library/Trash/<asset-id>` for recovery. Public
operations confine destinations below the configured source root.

The Sandbox keeps its humanoid model and animation sources at `Assets/Meshes/T-Pose.fbx` and
`Assets/Meshes/Idle.fbx`. The startup scene references model AssetId
`51cd8956-a6c4-4d63-b990-7d86829f92ff`, skeleton subasset `c8bf2eaf-9146-5b53-85c8-c3e6dc9b8f08`, and skinned-mesh
subasset `78c8dbe3-2951-54b9-b34e-9221c49c506b`; the Animator Controller references Idle clip subasset
`803c0e5b-d937-521c-821e-92de5a986179`.

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
Snapshot queries use only the record-store lock and never wait for the operation boundary, so Project Browser drawing,
thumbnail lookup, and editor selection remain responsive while a scene save triggers background catalog work.

`ImportAll()` hashes sources with SHA-256 and stores immutable raw objects below `Library/AssetCache/Objects`. Existing
objects are cache hits. It then publishes content-addressed runtime packs under `Library/AssetCache/Runtime` and
atomically replaces only the catalog and small metadata documents. `PollChangedAssets()` uses file signatures and a
250 ms default stability window; hashing and import happen only after a stable change. The editor remounts the published
catalog and requests last-good reloads
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

The editor runs external import, explicit refresh, material refresh, cook, receipt replay, and Project/Inspector source
mutations in the private `KeireAssetWorker` process rather than on the UI thread. The worker mutation protocol supports
folder creation, move, duplicate, trash, restore, and permanent deletion. One prioritized service owns publication and
exchanges atomic documents below `Library/AssetOperations`. Successful work publishes
`Library/AssetCache/Runtime/source-index.json`, which the editor validates and reloads without scanning or hashing the
project again. Operation documents and captured worker logs remain available for diagnostics; the worker is packaged
with the editor but is not a supported SDK or importer plug-in API.

Command-line imports and cooks allow the worker deadline to be adjusted with
`--worker-timeout-seconds <seconds>`. The default remains 600 seconds; the value must be greater than zero. This keeps
small-project behavior bounded while allowing large production imports or slower build machines to select an explicit
deadline.
`KeireAssetTool import` and `cook` also run their import phase through the adjacent worker. This keeps private codec
backends such as FFmpeg out of the public tool boundary while allowing the cooker to restore the worker's validated,
dependency-free canonical output from the persistent object cache. Worker failures remain fatal for command-line
imports and distribution cooking; successful command-line operation documents are removed.
Packaged Linux workers resolve copied shared codecs through `$ORIGIN`; macOS workers use `@loader_path`, and the
private FFmpeg dylibs are built with `@rpath` install names so relocating the package does not retain a build prefix.
Independent editor/tool database instances serialize mutations with a project-scoped, OS-released file lock. Database
startup remains compatible with the legacy directory-swap journal and repairs an interrupted `prepared`, `backedUp`, or
`published` state before exposing records. New publications never rename a live runtime directory. Asset protocol paths
remain UTF-8 and native path suffixes are appended without locale-dependent narrowing.

## Deterministic Cooking

`AssetCooker::Cook()` walks optional stable-ID roots transitively, sorts the selected entries by stable ID, compresses
each payload with pinned Zstandard and the selected
versioned `AssetBuildProfile`, target platform, and shards packs at a 2 GiB default limit. Catalog entries contain
relative content-addressed pack paths, bounded offsets/sizes, type, SHA-256, and dependency IDs. Output is assembled in
a sibling temporary directory, immutable packs are installed, and the catalog is switched atomically. The accompanying
`build-profile.json` records schema, profile name, compression algorithm/level, shard limit, and strictness.
Transactional cook and runtime-staging names use compact 80-bit tokens so nested content-addressed filenames remain
below the legacy Windows path limit in normal package layouts. The native Windows rename boundary also accepts
extended-length paths for user-selected roots that exceed that limit.

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
`--pack-mib`. The import and cook commands require the matching `KeireAssetWorker` beside the tool in a package or in
the sibling configuration target directory of a source build. SDK archives include this tool and the asset public
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

`.keiremesh` version 2 stores a float4 tangent direction and handedness, and version 4 adds UV1. Version 1 remains
readable and receives deterministically generated tangents. Mesh import bounds are written into catalog schema 2;
`AssetSystem` exposes them through a read-only metadata query, so viewport picking never reparses OBJ/FBX/glTF/GLB
source.
Assimp sources are normalized during import from its right-handed, lower-left-UV, counter-clockwise output to Kéire's
left-handed, upper-left-UV, clockwise mesh convention. Node transforms are applied before the canonical mesh is written,
and the conversion is backend-independent across D3D12, Vulkan, and Metal.

Before a catalog is published or cooked, material overrides are checked against the referenced shader declarations.
Unknown names, incompatible value types, out-of-range values, missing/wrong asset types, and incompatible texture
semantics or color spaces fail without replacing the last-good catalog.
