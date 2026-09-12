# Material migration publication lane — September 12, 2026

This lane extends the supplied uncommitted baseline. Existing graph extraction, deterministic shader identities,
source-only migration, and journal recovery were already present; they are not new work claimed by this lane.

## Added implementation

The importer-aware `ApplyShaderGraphMigration(root, reviewed, specification, cancellation)` overload prepares an
isolated project snapshot, checks the reviewed conversion there, imports the converted source identities with
fail-fast dependency validation, and validates the resulting catalog's dependency closure and packed payload integrity.
It checks that existing runtime material identities and converted source identities remain available. Published source
index metadata paths are rebased to the actual project before publication.
The reviewed fingerprint now covers the bounded authoring input inventory plus the current source index/catalog,
so a shader include changed after review requires another review before preparation begins.

Preparation holds the standard asset-operation process lock. Assets, Packages, ProjectSettings, and the previous runtime
catalog/pack files are copied through the anchored filesystem, then checked again against their original hashes before
publication. Links and non-regular files are rejected. Preparation is bounded to 4,096 files, 32 MiB per file, and 512 MiB
of snapshot contents. Changed source bytes, metadata, source index, runtime packs, and final catalog share one prepared
recovery journal; the catalog is written last. Unchanged runtime packs remain available. Interrupted publication and
interrupted rollback are recoverable through the existing project-open recovery path.

This is a recoverable multi-file transaction with an explicit caller requirement to suspend asset readers/adoption and
reload the published database afterward. It does not provide a single filesystem generation switch to arbitrary
concurrent readers. The editor must use the importer-aware overload. The original overloads remain source-only APIs
for compatibility and do not establish runtime validation.

Warm projects retain unrelated last-good runtime entries through targeted import. A project without a runtime catalog
uses the existing full-import bootstrap; any failed asset then blocks migration with its import diagnostic. Preparation
does not publish partial results. Compilation toolchain configuration is supplied by the caller's importer registrations;
the migration overload does not invent compiler settings or execute another platform's toolchain.

## Exact lane file manifest

- Modified `KeireCore/Include/Keire/Project/ShaderGraphMigration.h`: importer-aware overload and lifecycle contract.
- Modified `KeireCore/Source/Project/ShaderGraphMigration.cpp`: complete authoring and publication-input review hash.
- Modified `KeireCore/Source/Project/ProjectFileTransaction.cpp`: journal destination validation now admits only the
  runtime publication subtree beneath `Library/AssetCache/Runtime`, in addition to existing authoring roots.
- Added `KeireCore/Source/Project/ShaderGraphMigrationPublication.cpp`: bounded snapshot/import/validation/publication.
- Added `KeireTests/Source/Project/MaterialMigrationPublicationTests.cpp`: focused publication and failure regressions.
- Added this evidence document.

The public header, transaction publisher, and new publication implementation were initially edited in the isolated
c2fd worktree. The lead transferred those deltas into
`C:/Users/keith/Desktop/KéireEngine` after the user authorized the canonical-checkout transition. All subsequent lane
edits use the canonical checkout. No lane Git staging, commit, reset, push, submodule update, or vendor edit occurred.

## Focused coverage and evidence

Eight new doctest cases cover a warm published catalog preserving runtime material IDs; source-index metadata path
rebasing; shader dependency import failure preserving all original source/metadata bytes; pre-cancelled migration;
source/index/catalog publication failure and interrupted rollback followed by idempotent recovery; rejected library
destinations; dependency edits during validation; dependency changes after review; and missing shader rejection through
the production material importer without a compiler invocation. The publication rollback case
has two subcases.

The importer fixtures intentionally avoid invoking a platform shader compiler. They use the real asset database,
dependency/import pipeline, catalog packing, integrity validation, and journal publisher. They establish transaction
behavior, not executable shader visual parity. Existing OpenPBR extraction tests remain relevant separately.
Prepared derived-import object caches are not published; future imports may refill them, so this lane does not prove
zero subsequent shader recompilation or value-edit performance.

Validation performed by this lane: clang-format applied to the five C++ files, and scoped `git diff --check` passed.
Native test execution, Release, ASan, real compiler execution, editor review interaction, SDK consumers, cooked players,
Linux/Vulkan, and macOS/Metal are pending lead-orchestrated validation; none are claimed here.

## Proposed common documentation additions for integration

README: “Reviewed material migration validates conversions in an isolated project snapshot before publishing source,
metadata, source index, and runtime catalog through a recoverable transaction. Failed imports and cancellation preserve
the original project; project opening recovers interrupted publication before assets are loaded.”

Architecture: “Importer-aware material migration holds the asset-operation lock throughout preparation and publication.
The editor suspends asset readers/adoption, then reloads the database after success. The catalog is published last within
the source/index/catalog recovery journal. Original source-only migration entrypoints remain compatibility APIs.”

Changelog: “Added validated, cancellable material migration publication with source-index/runtime identity checks and
joint source/catalog recovery, including interruption during rollback.”

Remaining acceptance gates include a real shader compiler integration run, editor review/apply/failure/reopen exercises,
graph visual parity, larger-project resource characterization, and cross-platform/package validation. The full material
and shader replacement milestone remains open.
