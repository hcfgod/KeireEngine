# Kéire Hub

`KeireHub` is the normal product entrypoint and is packaged independently from versioned editor installations. The Hub
owns project discovery, editor selection, templates, signed distribution catalogs, package tasks, local learning
content, licenses, notifications, and user preferences. It does not own project files and never bypasses an editor's
project lock or compatibility checks.

The Hub is split into three private product targets:

- `KeireHubRuntime` contains the testable stores, catalogs, dependency resolver, downloads, package validation, and
  task orchestration.
- `KeireHub` composes the runtime with Kéire's window and UI services.
- `KeireHubWorker` performs resumable package transfers outside the UI process through confined operation journals.

The UI uses Kéire's public window and UI abstractions. Dear ImGui and SDL are not forked. A 40-pixel client-rendered
title bar supplies drag, resize, system-menu, minimize, maximize/restore, and close hit regions to the window layer.
Typed font, color, spacing, wrapping, table, focus, positioning, and bounded-image primitives remain behind that public
facade; Hub code does not own Dear ImGui style or texture state. Sidebar navigation, documentation, appearance,
project-view, and caption controls use the packaged licensed icon subset through that facade, with selected/hover
states, compact-rail tooltips, and a fallback for clients that do not configure the icon font.
Windows additionally maps the maximize region to `HTMAXBUTTON` for Snap Layouts; macOS retains native traffic lights;
Linux uses SDL client-side hit testing and falls back to native decorations when the compositor cannot support it. The
default logical size is 1280x800, the minimum is 960x640, and the 224-pixel navigation rail collapses below 1080 pixels.

## Product areas

The production navigation contains Home, Projects, Installs, Templates, Learn, Resources, Licenses, and Settings. The
title bar exposes appearance, tasks/downloads, notifications, documentation, and native window controls. There are no
account, entitlement, marketplace, or cloud controls.

Home is a live summary of registered projects, installed editors, component health, active tasks, updates, and verified
featured content. Empty catalog sections stay hidden or show an intentional offline/empty state.

Only installations backed by a validated editor manifest and registry record appear in product state; finding a
sibling executable never fabricates a healthy editor. If the private runtime cannot compose, the normal navigation and
mutation controls are not drawn. A dedicated recovery screen offers logs, redacted diagnostics, and exit.

Projects provides a sortable table and responsive card view. Search covers names and paths; filters cover status,
favorites, editor availability, and editor version. Cached descriptor metadata is displayed immediately, while a bounded
background scanner refreshes created/modified times, last-saved editor, project size, and the optional confined
`ProjectSettings/HubThumbnail.png`. Valid PNGs are decoded and normalized off the UI thread, published as immutable
pixels, and uploaded lazily through a 64-entry owner-thread texture LRU. Missing, invalid, or failed uploads use a
deterministic project-name monogram instead. The same worker probes the real editor lock and interrupted-upgrade
journal; a live process tracked by the Hub overrides cached status until it exits.
Project actions include:

- Open with the exact or least-disruptive compatible installed editor.
- Open with another explicitly selected compatible editor.
- Pin, reveal, or copy the project path.
- Rename the display name through a closed-project workflow.
- Duplicate through a confined staging directory with a new project identity and atomic publication.
- Locate a moved project only while its registered root is missing and the replacement's persistent project identity
  matches. The runtime rechecks that the original is unavailable before changing the catalog.
- Remove the registration from the Hub without deleting project files.

Missing, malformed, locked, recovery-required, upgradeable, and unsupported-schema projects remain distinct states. A
folder drop adds up to 32 validated project roots. The Hub tracks the preferred editor installation but the editor lock
and live process remain authoritative. Compatibility requires the editor manifest's project-schema range, the
project's minimum engine version, and an editor no older than the last-saved version. The exact last-saved version wins
when installed; otherwise the least newer compatible version is recommended and labeled with migration risk. A stale
preferred installation never bypasses these checks. Projects whose exact editor is unavailable link to the real
Installs/Locate flow rather than a placeholder action.

Project metadata and thumbnails are scanned asynchronously with bounded filesystem work. Once a scan is ready, all
cached project metadata is validated together and persisted in one atomic registry write. If any project disappeared or
any result is invalid or duplicated, no project receives a partial metadata refresh and the prior thumbnail snapshot is
retained.

Schema upgrades and interrupted-upgrade recovery use a confirmation modal backed by an asynchronous coordinator.
Planning, apply, recovery, rollback, staged validation, and backup work stay off the UI owner thread; the modal exposes
only immutable progress/result state and never performs filesystem mutation inside an ImGui frame.

Installs lists managed and externally located editor installations with version, channel, platform/architecture, path,
installed size, bundled .NET SDK, project/component counts, activity, and verified health. Locating an editor validates
its schema-2 `editor-package.json`, typed editor and Asset Tool entrypoints, host identity, file inventory, and manifest
fingerprint. External removal deletes only the Hub registration. Managed repair or removal is never authorized without
the matching unforgeable install marker and manifest fingerprint. Managed uninstall is exposed only for a healthy,
receipt-bound installation with a complete verified inventory. Verification, repair, and removal also require both the
Hub process tracker and the native executable-path probe to report the editor inactive; relevant process-query failures
fail closed. This covers editors launched outside the Hub or left running across a Hub restart. The worker revalidates
the exact marker, receipt, manifest, declared bytes, and absence of undeclared files immediately before atomically
renaming the root to a same-parent tombstone. A durable journal resumes an interrupted purge, and the Hub removes the
registry entry only after the root is absent and every persisted identity field still matches. Damaged receipt-bound
managed installations expose **Repair** when no editor or installation task is active. Repair resolves the exact signed
editor and component versions recorded by the registry, downloads and verifies that complete dependency closure, and
atomically replaces the damaged tree while preserving its installation ID and ownership nonce. An ordinary install
cannot use this replacement path. Legacy registrations without a receipt-bound package set retain recovery guidance
instead of an unsafe repair action.
Install discovery, full inventory refresh, verification, and repair/removal preparation run through an immutable
single-flight background workflow. A result is discarded if its registration, root, ownership proof, tracked running
state, or targeted task activity changed before the owner thread receives it; only that owner thread persists health
and queues a prepared package task.
Editor removal is also disabled while any Build Support operation is active, so its selected Asset Tool and
version-scoped component storage cannot disappear beneath an import, repair, or removal worker.

Verified editor catalogs populate Stable, Pre-release, and Nightly sections only when an enabled channel contains a
host-compatible editor. An install review selects a confined destination and compatible optional components, then shows
the dependency resolver's exact topological package closure, download size, disk requirement, and `Required By`
relationships before any task is queued. Versions with missing or conflicting dependencies remain visible with their
actionable catalog error but cannot be installed.

Each healthy editor card exposes **Manage Components** through that installation's typed Asset Tool. Component counts
come from the installed legacy Build Support inventory and are matched to the editor's exact engine version; the modal
then applies any requested platform/architecture filter. Import and repair accept only `.keireplayersupport` archives,
validate the requested pack/target inside the selected editor process, and publish status through an atomic status
document. Removal requires confirmation and also runs in that selected Asset Tool process. Its bounded atomic journal
completes a post-rename interruption or clears a pre-rename interruption on the next inventory reconciliation, so a
tombstone is never exposed as installed. Generic `.keirepackage` import remains hidden until its product task flow is
connected.

The Hub also keeps a bounded atomic operation journal for these legacy Asset Tool tasks. It records the exact operation,
editor installation, typed Asset Tool, confined status/cancel paths, and child PID before presenting the task as active.
After a Hub crash, a surviving child remains independently owned: the Hub observes its recorded PID together with a
bounded exact-executable probe and never attaches to or terminates it. Import and repair complete only from the Asset
Tool's terminal atomic status. Removal completes only after a fresh inventory pass has recovered any schema-1 removal
journal and confirms that both the component and its tombstone journal are gone. Malformed task journals fail closed;
active and recent terminal tasks remain visible in the task center, and recovered non-removal tasks retain their
confined cancellation action. A successful import or repair always requests a post-completion inventory refresh; if an
older scan is still running, refresh requests coalesce into exactly one follow-up scan instead of publishing stale
pre-install component state as final.

Installed component discovery and inventory validation run on a background workflow. Home, first run, and the component
modal consume immutable snapshots and show a deliberate checking state while that scan is active; package enumeration,
manifest parsing, and inventory hashing never run inside a UI frame.

Templates are data-driven. The browser searches and filters the three verified templates packaged with this release:
Empty, 3D Starter, and Kéire Sandbox. Featured entries are ordered first, and the detail panel reports tags, packaged
thumbnail previews, editor-version constraints, project schema, platform target, starter content, package
requirements, and license references from the manifest. Compatibility is evaluated against each healthy installed
editor before the creation dialog offers a template/editor pair. Remote template download and update controls remain
absent until template packages are connected to the persistent package-task pipeline.

Creation stages only declared payload files, writes schema-3 project metadata, validates the staged project
out-of-process with the selected editor's Asset Tool, and publishes atomically. Failures remove staging and leave an
existing destination untouched. The creation dialog explicitly chooses whether to open the result in the selected
editor. Validation observes Hub shutdown, terminates its child process promptly, and removes staging instead of making
application shutdown wait for the full validation timeout. Creation progress appears in the task center and never
blocks the UI thread.

Learn and Resources load real packaged or signed catalog entries only. Packaged entries include the documentation and
Kéire Sandbox; remote entries require HTTPS and a valid signed content catalog. Licenses loads the MIT license and real
third-party/package license files, grouped by source, with search, copy, and reveal actions. Installed-package notices
are resolved off the UI thread from receipt-bound paths and are shown only while their current size and digest still
match the verified inventory. Missing or malformed packaged content/license catalogs produce explicit page warnings and
typed error notifications rather than ordinary empty states.

## First run and settings

First run appears only until a completed settings document exists. It selects project/editor roots, performs bounded
discovery only beneath explicitly selected roots and the packaged installation ancestry, imports the existing recent
project registry, reviews detected editors/projects/components, and can skip optional discovery. Finishing after a
discovery imports every item shown in that review; users can instead skip discovery and add items individually later.
It never scans an entire disk or network share automatically. The default project root is the platform user Documents
directory followed by `Kéire Projects`, never the Hub's current working or installation directory.

Discovery also performs its project and editor revalidation away from the UI thread. A completed review holds an
immutable prepared import, so finishing first run performs only bounded batched registry commits on the owner thread.
Duplicate identities or roots reject the complete import before any write, and a later editor-registry write failure
rolls the project registry back to its exact prior persisted state.

Settings are schema-versioned JSON written by atomic replacement. Current visible settings cover startup page,
Hub/tray behavior, System/Dark/Light appearance, update checks, project defaults and cleanup, editor/cache/temp roots,
download concurrency, enabled channels, offline mode, system/custom proxy, bandwidth limit, restart-scoped logging,
diagnostics, verified-cache clearing, and reset. Development builds may supply a service URL only together with a
separately trusted public key; packaged builds reject that override. Compatibility fields retained from older settings
documents are not presented as controls until the product honors them.

Clearing the verified package cache is an asynchronous, exclusive maintenance operation represented in the task center.
It refuses to start while package work is active, temporarily closes the idle package coordinator before deleting cache
content, and recreates that coordinator after the terminal result so downloads cannot repopulate the cache mid-clear.

Hub logs live beneath the per-user preference directory. **Copy diagnostics** creates a redacted report containing build,
platform, configured roots, catalog state, task state, and recent failures. Proxy credentials, signing material, tokens,
and sensitive user-path segments are excluded.

## Distribution and trust

`Config/Distribution.json` in a Hub package contains the initial service URL and trusted Ed25519 public-key documents.
If either is absent, online discovery is disabled and the Hub continues with installed content, imported packages, and a
valid last-known-good cache. HTTPS is required except for explicit loopback development mode; there is no certificate
bypass.

Catalog signatures cover the exact received bytes. The Hub validates key ID, sequence, expiry, channel, platform, and
architecture before parsing package records. Online packages are immutable SHA-256 resources. Resumable downloads use
content-addressed cache files, `.partial` data, ETag/If-Range metadata, bounded retry, and digest verification. The task
store and worker protocol use atomic files beneath the preference directory so interrupted work can be reconciled after
a restart. A package catalog may carry a signed `minimumSupportedHubVersion`; update selection considers only verified
online or last-known-good snapshots, chooses the newest semantic Hub-installer version, prefers Stable for equal
versions, and treats a minimum-version policy without a suitable installer as a catalog error.

Hub-installer records must match the verified catalog's key, channel, platform, and architecture exactly; wildcard or
cross-endpoint installers fail closed. **Download update** creates a dedicated resumable `HubUpdate` task and publishes
the native artifact only after its signed-catalog SHA-256 and size match. Download completion does not install anything.
The separately enabled **Install update…** action rechecks the digest, verifies Authenticode on Windows, writes an
atomic resume token, and then opens the native installer before the Hub exits normally. Windows NSIS waits for the
recorded Hub PID and revalidates the registered install root and ownership marker before replacing files. Linux uses an
explicit `pkexec`/`dpkg` handoff when those tools exist; its signed-catalog digest remains the package trust boundary.
The macOS drag-to-Applications DMG is revealed for manual installation rather than treated as an automatic replacement.
If a safe native handoff is unavailable, the Hub offers only to reveal the verified installer. On the next launch the Hub
removes the token only when the installed semantic version reached or exceeded the target; otherwise it keeps a recovery
notification. No update silently elevates or replaces a running Hub.

The runtime's generic `.keirepackage` extraction rejects absolute or traversing paths, links, non-canonical paths, case
collisions, undeclared files, unsupported modes, oversized records, and file digest mismatches. Offline archives require
a trusted embedded signature; online archives must match the already verified catalog manifest. Publication uses a
staged root, durable journal, unchanged prior-install backup, and atomic no-clobber rename. The new tree is fully
validated before it replaces that backup, and a failed replacement restores the prior tree rather than exposing
staging. Legacy `.keireplayersupport` schema-1 Build Support imports remain supported by the existing Asset Tool adapter
and are the only offline package type currently exposed by the Hub UI and activation protocol.

The companion `KeireDistributionService` is a stateless .NET 10 service behind Caddy. It serves exact signed catalog and
content bytes, immutable packages with conditional and range requests, and liveness/readiness endpoints. The publisher
signs offline, validates a complete staging snapshot, and atomically advances `current`; an invalid replacement does not
displace the last valid snapshot.

## Launch and activation

Opening a project launches the selected editor's typed entrypoint as:

```text
KeireClient --project <canonical-root>
```

The editor independently validates the descriptor and acquires the exclusive project lock. After a successful launch,
the Hub records the project, installation ID, and last-opened time. Depending on settings it remains alive while hidden
to the tray/taskbar, or exits normally.

Only one Hub process owns a window and tray entry for each canonical installed executable. Secondary processes send a
bounded, versioned activation request for show, open-project, install-version, import-package, navigation, or legacy
Build Support actions, then exit before creating UI. Native callbacks enqueue work until the owner-thread safe boundary.

## Launcher and validation workflows

Windows:

```powershell
./Scripts/project.ps1 run -Generator ninja -Configuration Debug -Toolset msc
./Scripts/project.ps1 run -Editor -ProjectPath C:\Projects\MyGame
./Scripts/project.ps1 run -SmokeProject
./Scripts/project.ps1 package-hub
./Scripts/project.ps1 package-hub-installer
```

Linux and macOS:

```sh
bash Scripts/project.sh run --generator ninja --configuration Debug --toolset clang
bash Scripts/project.sh run --editor --project /projects/MyGame
bash Scripts/project.sh run --smoke-project
bash Scripts/project.sh package-hub
bash Scripts/project.sh package-hub-installer
```

`--smoke-ui` validates bounded Hub startup and rendering. `--smoke-project` opens the sample through the real project,
asset, input, scene, workspace, and editor lifecycle before exiting cleanly. The focused private runtime suite is the
`KeireHubTests` target. Standalone Hub package and native-installer regression harnesses live under `Scripts/Tests/`.
