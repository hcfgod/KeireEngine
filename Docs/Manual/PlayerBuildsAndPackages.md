# Player Builds And Packages

Player builds cook one project for a target platform and assemble a standalone runtime. Asset packages move project
content. Editor/Hub distribution packages and Build Support components are different formats and ownership domains.

## Configure A Player

Open **Build > Build Settings**. Create or select a profile and configure:

- target platform, architecture, and configuration;
- product identity, version, output location, and branding;
- ordered included scenes and startup scene;
- renderer and cooking choices exposed by the profile;
- Development Player and symbol policy;
- signing policy and hook only when deliberately configured.

Build validates project state, scene ordering, target support, assets, managed generations, content dependencies, and
the selected profile before publishing output. A failure must not replace a previously completed build.

## Build Support

A foreign or optional target may require an exact-version Build Support component. In Hub, open **Installs**, select a
healthy Editor, and choose **Manage Components**. Missing components keep Build disabled and link back to this filtered
workflow. Do not advertise a platform merely because a package can be assembled; matching-host execution and release
validation are separate requirements.

## Test The Output

Run the main executable beside its generated `PlayerBuild.json` and content/managed payload. Test startup scene,
ordered scene changes, input focus and rebinding, audio output, UI navigation, VFX, diagnostics, shutdown, and any
Development Player features used by the team. Foreign output can be structurally validated on the assembly host but
must be executed on the target OS/architecture.

## Package Types

| Format | Purpose |
| --- | --- |
| Player output | One cooked game's standalone runtime. |
| `.keireassetpackage` | Project content for Registry, Asset Import, or Complete Project workflows. |
| `.keirepackage` | Hub/Editor software distribution, not project content. |
| `.keireplayersupport` | Versioned target Build Support imported through Hub. |

## Create An Asset Package

In Project, select assets or a folder and choose **Create Asset Package...**. Enter stable package/publisher IDs,
semantic version, summary, and minimum Kéire version. The Editor adds declared dependencies, stages source plus
`.keiremeta` sidecars, and refuses to overwrite an existing archive.

For an import package, preflight shows new, identical, locally modified, and conflicting files. Selective imports expand
to dependency closure. Executable C# requires explicit consent, and a later package whose code fingerprint changes
requires consent again. Receipts make update/removal transactional and retain locally modified files rather than
silently deleting them.

Registry packages are mounted read-only through the project manifest/lock contract. Embedding creates a writable
project copy and intentionally stops normal registry updates until reverted.

## Publication Status Matters

Local package creation produces an archive; it does not publish, sign, moderate, or activate a Marketplace listing.
Kéire 0.4.4 is the current source and Windows publication target. Signed 0.4.2 catalog sequence 17 remains active for
Windows and Linux x86-64 and retains the immutable 0.4.1 records; a local build or quarantine package is still not a
downloadable release unless it is present in the active catalog.

Marketplace installation requires an entitled, compatible, published artifact and the Hub/Editor verification flow.
Unpublished, withdrawn, revoked, unsigned, incompatible, or unentitled releases remain unavailable.

See [Desktop Player Builds](../PlayerBuilds.md), [Asset Packages](../AssetPackages.md), and
[Package Archives](../PackageArchives.md) for target layouts, signing hooks, deterministic archive limits, recovery,
and automation.
