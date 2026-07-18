# Asset Database And Cook Pipeline

## Source Identity

`AssetDatabase` scans the project `Assets/` directory and owns editor/tooling identity. Every source has an adjacent
`.keiremeta` JSON sidecar containing a schema version, stable 128-bit `AssetId`, type, importer identity/version,
dependencies, and reserved stable subasset IDs. A missing sidecar is created atomically; malformed metadata is reported
instead of regenerated so an identity is never silently lost. Duplicate IDs, subasset collisions, symbolic links,
case-colliding paths, unsafe relative paths, oversized metadata, and oversized sources are rejected.

Rename moves source and metadata as one rollback-capable operation. Duplicate copies bytes and creates a new identity.
Delete is represented by `MoveToTrash()`, which moves both files under `Library/Trash/<asset-id>` for recovery. Public
operations confine destinations below the configured source root.

## Import And Change Detection

`ImportAll()` hashes sources with SHA-256 and stores immutable raw objects below `Library/AssetCache/Objects`. Existing
objects are cache hits. It then publishes the development runtime directory transactionally under
`Library/AssetCache/Runtime`. `PollChangedAssets()` uses file signatures and a 250 ms default stability window; hashing
and import happen only after a stable change. The editor remounts the published catalog and requests last-good reloads
for changed IDs.

The default raw import path classifies common source/configuration/shader extensions as UTF-8 text and all remaining
files as binary. Owner-configured `AssetImporterRegistration` values claim specialized extensions, validate source
bytes, and emit deterministic canonical bytes before cache hashing. The Input registration claims `.keireinput`; both
KeireClient and `KeireAssetTool` install it explicitly. `CreateAsset()` validates through the registered importer
before transactionally publishing the source and metadata identity.

## Deterministic Cooking

`AssetCooker::Cook()` sorts entries by stable ID, compresses each payload with pinned Zstandard and the selected
versioned `AssetBuildProfile`, and shards packs at a 2 GiB default limit. Catalog entries contain relative pack paths,
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
KeireAssetTool cook --project <path> --output <path> --profile Dist
KeireAssetTool validate --catalog <path>
```

Optional cook controls are `--compression-level` and `--pack-mib`. SDK archives include this tool and the asset public
headers; they carry the private `KeireZstd` archive transitively through `Keire::Core` but do not redistribute Zstandard
headers.
