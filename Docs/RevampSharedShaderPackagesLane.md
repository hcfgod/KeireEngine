# Shared shader package input acceptance

This lane extends the restored September 12 seed. The existing deterministic six-shader installation, immutable
source checks, and material identities belong to the seed. This work does not claim a new rendered visual baseline.

## Added behavior

`ReviewSharedShaderInputs` captures the current shared-library lock, graph importer version, and SHA-256 hashes of
project-relative compiler identity, include, visual fixture, and package-lock files. All four input categories are
required. Missing files, duplicate paths (including case-only differences), escaping paths, and oversized input
sets are rejected before publication. Caller-supplied hashes are replaced with hashes of actual file bytes.

`ApplySharedShaderInputs` recomputes the review and rejects changed inputs, compiler contract, or library lock.
It upgrades only the shared-library lock to schema 2 using the existing journaled file transaction. Shader source
bytes, version 1.0.0 source paths, source IDs, parameter IDs, and runtime identities are retained. The caller must
hold exclusive project access and stop source mutations throughout review/application. Schema-1 projects remain
readable and are not silently upgraded on open or material creation.

Schema-2 reads verify every input digest and the graph importer version. A changed pinned dependency fails clearly;
an explicit fresh review can adopt the changed dependency. `SharedShaderLibrary::Inputs` exposes the verified
dependency closure for import/cache consumers.

The Shader Graph importer now registers all verified input paths and hashes as source dependencies and rechecks
the bytes through its import-context reader. Changed inputs therefore participate in refresh/cache invalidation.
The existing importer version 23 and compute-graphics-import rejection from the compute lane are preserved.

`DefaultSharedShaderInputs()` provides `ProjectSettings/SharedShaderInputs/Compiler.json`, `Includes.json`,
`VisualFixtures.json`, and `Packages/packages-lock.keirejson`. The one-argument review overload uses those paths
and names missing files in its error. It never fabricates compiler identity or visual evidence. Integrators may
pass actual include files individually through the explicit input-list overload.

## Change manifest relative to seed

- `KeireCore/Include/Keire/Project/SharedShaderLibrary.h`: input categories, reviewed-upgrade value types/functions,
  verified library input list.
- `KeireCore/Source/Project/SharedShaderLibrary.cpp`: schema-2 verification, bounded input review and transactional
  reviewed publication, with schema-1 compatibility.
- `KeireTests/Source/Project/SharedShaderLibraryTests.cpp`: focused review/publication/stale-input/validation tests.
- `KeireCore/Source/Rendering/ShaderGraphImporters.cpp`: verified pin dependency registration and deduplication.
- `Docs/RevampSharedShaderPackagesLane.md`: this evidence and proposed integration text.

These C++ files were already present as untracked seed files; their presence as untracked files is not a new addition
from this lane.

## Validation and remaining gates

Ran LLVM clang-format on the three changed C++ files and its `--dry-run --Werror` check: passed.
Ran `git diff --check`: passed. Inspected scoped `git status --short`: only the three expected seed files appeared.
After transfer to the canonical checkout, repeated formatting/dry-run on all four C++ files and diff checking passed.
No native build was launched by this lane; the lead coordinates native validation under the shared build lock.
The focused doctest filter is `*Shared shader*`. Runtime test results must be recorded after execution.

This delivers reviewed dependency pin adoption, not shader-source version replacement. The editor review surface,
automatic construction of actual compiler identity manifests, package manager install/upgrade integration, generated
visual reference images, cross-platform visual comparisons, and packaged SDK acceptance remain integration gates.
A visual-fixture digest proves file identity, not visual quality or a passing GPU comparison.

## Exact proposed documentation additions for integration

README: Shared shader projects can explicitly review and adopt compiler identity, include, visual-fixture, and
package-lock inputs through `ReviewSharedShaderInputs` / `ApplySharedShaderInputs`. Changed reviewed bytes reject
publication; previously pinned projects remain on their existing source identities.

Architecture: Shared shader schema-2 locks own the dependency input digests and graph importer version. Review
captures actual project-relative file bytes; apply revalidates the snapshot under caller-owned exclusive project
access and publishes through the material migration file transaction. Shader sources and IDs are unchanged.

Changelog: Added explicit reviewed adoption of shared shader compiler/include/fixture/package input pins, with
stale-review rejection and deterministic verification while retaining schema-1 compatibility.

## Priority follow-up: created asset scroll reveal

Validation reproduced a newly created shader selected below the visible Project grid after the existing search
filter was cleared. `DrawAsset` previously discarded `RevealAsset` after rendering without requesting scrolling.
Both list rows and grid cards now consume the reveal only when the matching widget is submitted and call the new
`UiFrame::ScrollLastItemIntoView()` wrapper immediately after that widget. This uses actual layout geometry,
including preceding folders and variable-height expanded groups; no estimated row or grid index is used. Hidden
targets retain the pending request. Project close clears it, and ordinary subsequent frames do not force scrolling.

Additional changed files:

- `KeireCore/Include/Keire/Ui.h` and `KeireCore/Source/UiLayout.cpp`: encapsulated next-frame item-scroll request.
- `KeireClient/Include/KeireClient/Editor/AssetBrowserUtilities.h` and
  `KeireClient/Source/Editor/AssetBrowserUtilities.cpp`: one-shot matching reveal consumption.
- `KeireClient/Include/KeireClientInternal/Editor/AssetBrowserPanelInternal.h`: grid/list calls and close reset.
- `KeireEditorTests/Source/EditorUserWorkflowTests.cpp`: hidden/unrelated targets retain requests; matching target
  consumes once and an explicit repeat can request again.
- `KeireTests/Source/UiRevealScrollTests.cpp`: headless real UI list and three-column grid, preceding folders and
  variable-height rows, offscreen target becoming visible on the following frame, and subsequent manual scrolling.

All seven follow-up C++ files were formatted and passed clang-format dry-run. `git diff --check` passed (line-ending
warnings concern existing working-tree files). Native/editor tests and the real Windows reproduction await the
lead's serialized build and validation lane. Test filters: `*UI reveal scroll*`, `*asset browser reveal*`.

Proposed changelog addition: Created and explicitly revealed assets now scroll into view in Project list/grid views,
including after clearing a search filter and below expanded generated-asset groups.

## Deferred texture transform contract proposal

Append `AssetId TextureTransformProperty` (empty means absent) to `ShaderGraphParameterMetadata` and
`ShaderPropertyDefinition`. A Texture2D declaration explicitly links an existing stable Vector4 parameter whose
components are tiling.xy and offset.zw; conventional defaults are (1,1,0,0). Validate texture-only links to existing
Vector4 parameters. Preserve the link through graph source codec, reflection, shader manifest/runtime codec, and
material property reflection. Inspector edits reuse the linked property's existing stable-ID override and undo
paths. Shader lowering and CPU preview must apply the same transform at sampling; code shaders explicitly consume
the uniform. This proposal was inspected and communicated, not implemented by this lane.
