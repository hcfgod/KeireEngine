# Player Builds And Packages

Player builds cook one project for a target platform and assemble a standalone runtime. Asset packages move project
content. Editor/Hub distribution packages and Build Support components are different formats and ownership domains.

## Configure A Player

Open **Build > Build Settings...**. Save the scenes to ship and resolve asset and managed compile errors. Create or
select a **Build Profile** and configure:

- target platform and architecture;
- product identity, version, output location, and branding;
- ordered included scenes and startup scene;
- Development, Release, or Dist configuration and **Include Symbols**;
- signing policy and hook only when deliberately configured.

In **Scenes In Build**, use **Add Open Scene**, or choose a **Scene Asset** and **Add Selected Scene**. Enable the
required scenes and use **Set as Startup** on the intended first scene. The first enabled scene starts the player;
merely opening a scene does not add it to the build. Include every scene the game transitions to.

**Output Folder** is a profile folder name below the project's `Build/`, not an arbitrary absolute installation path.
**Player Identity & Branding** controls product name, version, application identifier, window title, and platform icons.
Settings edits are persisted automatically.

Choose **Build** to assemble output or **Build & Run** for a matching host platform/architecture. For unsaved state,
**Save All** builds your saved changes, **Build Saved State** deliberately uses the previous on-disk state, and
**Cancel** returns without building. Do not build saved state while expecting unsaved Inspector edits to appear.

Build validates project state, scene ordering, target support, assets, managed generations, content dependencies, and
the selected profile before publishing output. A failure must not replace a previously completed build.

## Build Support

A foreign or optional target may require an exact-version Build Support component. In Hub, open **Installs**, select a
healthy Editor, and choose **Manage Components**. Build Settings reports missing support and provides **Open Build
Support**. The handoff identifies the requesting Editor executable. Verify that installation in Hub, save and close the
Editor before maintenance, and reopen it afterward. Do not substitute another installation solely because its version
label matches. Support must match the engine version, target, configuration, and native-module compatibility. A Windows
Editor may already include its host bootstrap; foreign targets require their own support.

Do not advertise a platform merely because a package can be assembled; matching-host execution and release
validation are separate requirements.

## Test The Output

Wait for completion, inspect Console if it fails, and use **Reveal Build**. **Cancel** stops an in-progress build; it is
not uninstall. A running previous player can prevent replacement: close it and retry instead of deleting active staging.

Run the main executable beside its generated `PlayerBuild.json` and content/managed payload. Distribute the whole output,
not only its executable. In a fresh player process, play through success, failure, restart, pause/resume, and every scene
transition your game implements. Confirm physics and timers pause correctly and that keyboard controls still work after
clicking UI buttons. Close and relaunch to verify repeatable startup. Also test input rebinding, audio output,
UI navigation, VFX, diagnostics, shutdown, and any
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
A local build or quarantine package is not a downloadable release unless it is present in the active signed catalog.
Consult Downloads and the catalog for current platform versions; platform releases can differ. Catalog signatures and
SHA-256 verification do not imply native OS code signing. Player signing hooks are an explicit build-profile policy:
configure the executable, arguments, required environment, and timeout only for a signing process you control.

Marketplace installation requires an entitled, compatible, published artifact and the Hub/Editor verification flow.
Unpublished, withdrawn, revoked, unsigned, incompatible, or unentitled releases remain unavailable.

See [Desktop Player Builds](../PlayerBuilds.md), [Asset Packages](../AssetPackages.md), and
[Package Archives](../PackageArchives.md) for target layouts, signing hooks, deterministic archive limits, recovery,
and automation.
