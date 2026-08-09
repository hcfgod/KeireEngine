# Generic Package Archives

New Editor, Build Support, Template, Learning Content, and Toolchain payloads use the .keirepackage container.
The legacy .keireplayersupport schema remains a separate compatibility path.

## Trust and manifest identity

The archive contains one canonical payload manifest. It includes package identity, compatibility, dependencies,
conflicts, installed size, licenses, signing-key ID, and the complete sorted file inventory. Every file record includes
its confined portable ASCII path, size, SHA-256 digest, and an allowlisted 0644 or 0755 mode.

The embedded document intentionally omits the artifact byte size and digest. Embedding the digest of the archive inside
that same archive would create a self-hash cycle. WritePackageArchive returns a complete PackageManifest with those two
catalog transport fields populated after the archive is atomically published.

Current Hubs use a compact schema-2 signed catalog record from `/v2/catalog`. It carries the complete package identity and a
content-addressed reference to the separately served full PackageManifest. The Hub downloads that manifest only when
an install or repair is queued, verifies its exact size and SHA-256 against the signed record, and requires every
summary field to match. Extraction then canonicalizes the hydrated manifest and requires exact equality with the
embedded manifest before checking the archive byte size and SHA-256 against the catalog fields. Offline
installation requires an embedded Ed25519 signature over the exact canonical embedded-manifest bytes.
CatalogTrustStore::VerifyDetached verifies that signature with the same pinned public keys used by catalog
verification.
The service retains complete schema-1 catalogs on `/v1/catalog` for previously downloaded Hubs; publishing the compact
representation never removes their online discovery path.

## Publishing an editor archive

First produce the host-native schema-2 editor distribution with `package-editor`. Build
`KeireHubPackagePublisher`, then convert that unpacked payload into a generic archive and canonical catalog manifest:

```text
KeireHubPackagePublisher create-editor \
  --payload-root <editor-distribution> \
  --output <editor.keirepackage> \
  --manifest-output <editor.manifest.json> \
  --signature-key-id <ed25519-key-id>
```

The publisher accepts Unicode paths, rehashes every file declared by `editor-package.json`, verifies size and digest,
adds the product manifest to the inventory, and refuses existing outputs. It writes the archive transactionally and
removes it if the final catalog-manifest publication fails. The returned archive length and SHA-256 are the online
artifact identity.

`Scripts/Packaging/prepare-distribution-snapshot.py` accepts that manifest and archive, verifies the artifact identity
again, and creates a compatibility `catalogs/<channel>/<platform>/<architecture>.json`, compact
`catalogs-v2/<channel>/<platform>/<architecture>.json`, exact `manifests/<sha256>.json`, and
`packages/<sha256>` in a new staging root.
The catalog is then signed, verified, and immutably published with `KeireDistributionPublisher`; the private Ed25519 key
must remain outside the repository, staging root, and online service.

## Worker integration

The worker installs a verified online archive as follows:

1. Resolve the package dependency closure and download each content-addressed artifact. Catalog-bound extraction accepts
   the cache's `<sha256>.package` name; portable offline imports retain the `.keirepackage` extension.
2. Call PlanPackagePublish(allowedParent, destination, taskId). The destination must be a direct child of the explicit
   caller-authorized parent; the returned staging, backup, and journal paths are confined siblings.
3. Call ExtractPackageArchiveToStaging with the archive, staging path, signed catalog manifest, that same parent in
   PackageArchiveVerification::AllowedStagingParent, and progress callbacks.
4. Retain PackageArchiveExtraction::Metadata. It returns the validated complete manifest, exact embedded-manifest
   bytes, optional embedded signature, archive byte size, archive digest, and staging root.
5. For an editor dependency closure, put the Editor source first and retain topological order for the remaining sources.
   `AssemblePackageTreesToStaging` validates and copies only declared files, rejects cross-package path collisions, and
   creates `.keirehub-packages.json`. The receipt binds the ordered package IDs, versions, kinds, artifact identities,
   dependencies, per-package file inventories, license references, aggregate source-manifest identity, and payload
   bytes. `FinalizePackageAssemblyMarker` adds the generated ownership marker to the exact publication manifest.
6. Persist the finalized aggregate `PackageManifest` in the worker's operation state, then call
   `PublishStagedPackage(paths, manifest, taskId)`. Publication binds the journal to the complete manifest including
   artifact size and digest, rehashes the complete no-link staging inventory, durably flushes it, and atomically renames
   directories on the same filesystem.
7. Register the installed package only after publication succeeds. The parent contains at most one active
   .keire-publish.lock/journal.json, serializing all mutations beneath that authorized installation root. After a
   worker restart, reload the exact signed-catalog or trusted offline manifest from operation state and call
   RecoverPackagePublish(allowedParent, journal, manifest, taskId) before exposing install state. The operation ID lets
   recovery promote or discard only that task's interrupted lock staging. The journal's manifest digest rejects
   substitution; the full manifest remains in bounded worker/task state rather than being duplicated in the publication
   journal.

On recovery, recreate the deterministic base with `CreatePackagePublicationManifest`, call the idempotent
`FinalizePackageAssemblyReceipt` against the surviving staging or destination root, then call
`FinalizePackageAssemblyMarker` before `RecoverPackagePublish`. `ReadPackageInstallReceipt` is the persisted source for
Components, Required By relationships, aggregate size, verification/repair inventories, and license attribution.
Every license reference is a portable relative path resolved against the aggregate installation root and must name a
file in that package's bound receipt inventory; callers must not resolve it against the cache or an archive path. The
receipt is rejected unless every dependency is present at a matching version and the complete dependency graph is
acyclic.

For an offline import, construct the trusted key store from packaged distribution configuration and pass its address as
OfflineTrustStore instead of providing a signed catalog manifest. Unsigned, untrusted, tampered, noncanonical, or
key-mismatched imports are rejected before staging is created.

Extraction never accepts a live installation root: the target must be a new absolute .keire-stage-* directory directly
inside the caller-authorized parent. Failure and cancellation remove only that newly owned staging directory.
Publication persists and revalidates the same boundary, refuses symbolic links, Windows reparse points, unexpected
backup/journal paths, concurrent writers, cross-operation path substitution, post-extraction mutation, and ambiguous
recovery states. Extracted files use exact 0644/0755 modes and owned staging directories use exact 0755 permissions on
POSIX. Journal and directory transitions use durable file and directory synchronization before the next destructive
phase. Replacement failures restore the unchanged prior installation before returning whenever publication has not
committed a validated new tree.

## Container limits and records

Schema 1 is one Zstandard frame with an eight-byte KEIRPKG1 magic, bounded manifest and optional signature blocks,
ordered file records, and an explicit terminal inventory record. Readers reject:

- absolute paths, traversal, dot components, invalid or oversized paths;
- symbolic-link or unknown record kinds;
- duplicate, case-colliding, missing, out-of-order, and undeclared files;
- per-file, total payload, manifest, signature, and archive size overflow;
- size or SHA-256 mismatches;
- incomplete frames, extra decompressed bytes, and trailing compressed frames.

Current ceilings are 64 GiB per archive and total payload, 32 GiB per file, 32,768 files, 8 MiB for the embedded
manifest, 16 KiB for signature metadata, and 1,024 UTF-8 bytes per relative path.
