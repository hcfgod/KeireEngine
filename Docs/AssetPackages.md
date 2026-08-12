# Asset Packages and Project Package Manager

Kéire 0.3.1 introduces a project-content package boundary that is separate from Editor and Build Support
distribution. Asset packages use `.keireassetpackage`; Hub and Editor software continues to use `.keirepackage`.

The marketplace, Hub, and Editor workflow is feature-gated while the end-to-end signing, validation, legal, recovery,
and native-platform launch gates are completed. The archive and project contracts described here are implemented engine
foundations and can be tested locally without enabling the public marketplace.

## Archive contract

Schema 1 archives begin with the exact `KEIRASPK1` magic and contain a canonical UTF-8 manifest, an optional detached
marketplace signature, and a Zstandard-compressed payload in manifest order. Archive creation rejects an existing output
instead of silently overwriting it.

The manifest binds:

- stable package, publisher, and semantic-version identities;
- `Registry`, `AssetImport`, or `CompleteProject` installation kind;
- minimum and optional maximum engine versions;
- platform, architecture, renderer capability, and managed API compatibility;
- package dependencies and conflicts;
- canonical portable paths, byte sizes, modes, and SHA-256 values;
- stable asset identities, types, metadata paths, dependencies, samples, entry points, and managed assembly scopes;
- licenses, validator identity, and marketplace signature key identity.

Paths are relative, normalized, ASCII-portable paths in schema 1. Traversal, links, device paths, Windows reserved names,
duplicate paths, case-fold collisions, undeclared files, oversized records, malformed manifests, archive-size mismatches,
hash mismatches, signature failures, and trailing compressed data fail before publication.

## Deterministic authoring

Use the public archive functions in `Keire/Assets/AssetPackage.h` to inventory a payload, encode or decode a canonical
manifest, create an archive, inspect trusted metadata, and extract exact bytes to a new owned staging directory.

Identical payload bytes and manifest input produce identical archive bytes when the same schema and compression level are
used. Output paths and timestamps do not enter the archive. Creation re-hashes every source file after inventory so a
payload changed during publication cannot produce a mismatched artifact.

Signature verification is injected through `AssetPackageSignatureVerifier`. Kéire Core does not own marketplace public
keys or expose an implementation-specific cryptography type in its public API. Marketplace signing uses a trust root
separate from Editor distribution signing. The current public trust document lives at
`Config/Marketplace/trusted-marketplace-key.json`; its private key remains outside the repository and online services.

## Project registry packages

The first package operation creates two source-controlled project files:

| Path | Purpose |
| --- | --- |
| `Packages/manifest.keirepackages` | Direct package requirements and requested version constraints. |
| `Packages/packages-lock.keirejson` | Exact versions, archive size and SHA-256, source, dependency graph, signature key, and embedded state. |

Projects without these files remain valid. The first successful package publication raises `minimumEngineVersion` to
0.3.1 in the same transaction, ensuring an older Editor refuses the project safely instead of ignoring mounted content.

`ProjectPackageManager` performs compatible dependency-closure resolution, reports missing or incompatible dependencies,
rejects cycles and package conflicts, verifies trusted catalog size/hash/signature data, and extracts immutable content
into a per-user content-addressed cache. The project lockfile is not published until every required cache root validates.

## Mounting and embedding

Registry cache roots are mounted read-only and use the archive SHA-256 as their global cache identity. A lock entry with a
marketplace signature key reports marketplace-signature trust; an unsigned local registry entry can report catalog-hash
trust only when the caller deliberately permits it.

Embedding copies a resolved package into `Packages/<package-id>`, makes the copy writable, records it as embedded, and
prevents normal registry updates until it is reverted. Reverting requires the verified cache entry to remain available.
Removing a direct dependency also removes transitive lock entries no longer reachable from another direct dependency.

## Transactions and recovery

Manifest, lockfile, and project-version changes publish through a recovery journal under
`Library/PackageTransactions/`. The journal snapshots pre-operation files and records project paths created by the
operation. A failed publication restores the previous state. On startup, `RecoverInterruptedOperations()` restores every
non-committed transaction and reports malformed journals without deleting their evidence.

The global content cache is not source controlled and is safe to reuse offline only after its complete file inventory is
revalidated. Entitlement controls access to a marketplace download; the catalog hash and detached signature establish
the artifact's integrity independently.

## Asset imports and executable code

`ProjectAssetPackageImporter` performs selective import under `Assets` and records a source-controlled receipt beneath
`ProjectSettings/Packages/Imported/`. Selected assets expand to a complete declared dependency closure. Preflight
classifies new, identical, locally modified, and conflicting paths before staging any bytes. An update uses the previous
receipt as the common ancestor: unchanged local files update automatically, local-only changes remain, and divergent
local/incoming changes require an explicit replace or keep-local decision. Removal deletes only receipt-owned files
whose hashes remain unchanged and reports modified files that were retained.

The Editor's **Window -> Package Management -> Package Manager** surface exposes My Assets, Kéire Registry, In Project,
Updates, Local Packages, and Built-in views. The local workflow can inspect Registry, Asset Import, and Complete Project
archives, search their inventory, install registry content, import asset content, embed packages, and recover interrupted
transactions. Catalog-backed library/download presentation, per-file selection controls, sample import, and complete-
project creation remain gated until the authenticated Hub broker and production catalog are enabled.

Package C# must be explicitly declared as runtime, Editor, or test code. Import requires explicit executable-code
consent. The receipt binds that consent to the package's executable-code fingerprint, so a version that changes code
requires renewed approval. Marketplace validation may compile declared C#
with the pinned Kéire SDK in a generated no-network build, but it must never execute package code, MSBuild hooks,
analyzers, source generators, native binaries, or install scripts. Native plugins are prohibited in 0.3.1.

## Automation

`KéireAssetTool create-asset-package`, `inspect-asset-package`, `verify-asset-package`, and
`extract-asset-package` use the same Core parser and canonical encoder rather than implementing a second archive
format. Inspection and extraction report the SHA-256 of the exact canonical manifest bytes. Extraction accepts only a
new staging directory directly beneath an existing authorized parent. Automation must always provide a trusted
expected archive size and SHA-256; marketplace artifacts also require an approved signature key and verifier.

Kéire's five first-party launch products are prepared with
`python Scripts/Marketplace/create-official-marketplace-packages.py`. The builder copies only reviewed Sandbox source
roots, proves that selected asset dependencies remain closed, explicitly declares managed assemblies, rejects links and
nonportable paths, refuses to overwrite an output release set, and inspects every generated archive through the
authoritative Asset Tool. The resulting packages are still unsigned quarantine inputs: official content must pass the
same Publisher upload, isolated validation, staff moderation, and offline publication boundary as any other product.

The marketplace validator is split into a networked broker and a local-only worker. A dedicated Edge boundary holds
Supabase service privileges; the broker holds only a scoped queue secret and receives a short-lived URL for its one
leased object. It verifies the object's exact size and digest while downloading and exchanges the bytes through a
private directory. It never invokes package code. The offline worker uses the Core
parser through `KéireAssetTool`, rejects links, executable modes, native signatures, scripts, publisher MSBuild files,
malware, and secret indicators, then compiles only explicitly declared C# from validator-generated projects. Those
projects pin the SDK, clear NuGet sources, disable analyzers and source generators, and ignore publisher build imports.
The worker must run behind OS-enforced outbound denial; its environment marker is an additional launch assertion, not
a substitute for that sandbox.

After staff approval, `prepare-marketplace-publication.ps1` creates an exact, independently verified Ed25519 release
envelope on the offline signing workstation. The administrator uploads only that bounded JSON envelope. The publication
Edge boundary verifies its signature against the active public trust root, rechecks the validator and moderation hashes,
copies the same quarantine object into a content-addressed path in private release Storage, and then commits the
publication, product/version state, and audit event through one service-only transaction. If that transaction fails,
the promoted object is removed. Neither the private signing key nor an unsigned replacement package enters the online
publication path.

See [Package Archives](PackageArchives.md) for Editor distribution packages and [Asset Pipeline](AssetPipeline.md) for
asset import, cache, and cooking behavior.
