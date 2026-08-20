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
Windows maps the caption regions to native non-client roles, retains `HTMAXBUTTON` for Snap Layouts, and completes each
button action only when the pointer is released over the same region; macOS retains native traffic lights. Linux uses
SDL client-side hit testing and falls back to native decorations when the compositor cannot support it. The default
logical size is 1280x800, the minimum is 960x640, and the 224-pixel navigation rail collapses below 1080 pixels.

## Product areas

The production navigation contains Home, Projects, Installs, Templates, Learn, Resources, Licenses, and Settings. The
title bar exposes an optional account/profile menu, appearance, tasks/downloads, notifications, documentation, and
native window controls. Accounts never gate projects, editor downloads, or local Hub use. There are no entitlement,
marketplace, organization, billing, or cloud-project controls.

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

On Windows, project launch also rejects an elevated Hub process. Explorer cannot deliver file-drop messages to an
Editor at a higher integrity level, so launching anyway would make Project-panel asset imports fail silently. Reopen
the Hub normally and launch the same project; no project migration or repair is required.

An older project schema is not itself a reason to force an upgrade. When the exact previous Editor or another
schema-compatible Editor remains installed, **Open** launches it directly and leaves the project unchanged until that
Editor performs an ordinary save. Upgrade review is used only when no installed Editor can open the older schema;
interrupted upgrade journals always enter the dedicated recovery workflow.

Project metadata and thumbnails are scanned asynchronously with bounded filesystem work. Once a scan is ready, all
cached project metadata is validated together and persisted in one atomic registry write. If any project disappeared or
any result is invalid or duplicated, no project receives a partial metadata refresh and the prior thumbnail snapshot is
retained. Opening a project after an upgrade does not publish an intermediate pre-launch scan, and every tracked editor
exit requests a coalesced metadata refresh so a closed project cannot retain a stale **Open in another editor** state.

Schema upgrades and interrupted-upgrade recovery use a confirmation modal backed by an asynchronous coordinator.
Planning, apply, recovery, rollback, staged validation, and backup work stay off the UI owner thread; the modal exposes
only immutable progress/result state and never performs filesystem mutation inside an ImGui frame. Upgrade and recovery
copy, semantic colors, and actions reflect the actual transaction state. This modal and every project action dialog use
the selected Hub appearance instead of inheriting a conflicting operating-system theme.

Installs lists managed and externally located editor installations with version, channel, platform/architecture, path,
installed size, bundled .NET SDK, project/component counts, activity, and verified health. Locating an editor validates
its schema-2 `editor-package.json`, typed editor and Asset Tool entrypoints, host identity, file inventory, and manifest
fingerprint. If an external package was intentionally rebuilt or replaced at the same location, a registration-mismatch
card exposes **Refresh registration**. The single-flight worker validates the new manifest's self-fingerprint, host,
entrypoints, and complete declared inventory before the owner thread atomically replaces the saved metadata while
preserving the registration ID and root. Ordinary refresh and verification never accept changed metadata silently.
External removal deletes only the Hub registration. Managed repair or removal is never authorized without
the matching unforgeable install marker and manifest fingerprint. Managed uninstall is exposed only for a healthy,
receipt-bound installation with a complete verified inventory. Verification, repair, and removal also require both the
Hub process tracker and the native executable-path probe to report the editor inactive. A tracked launch retains the
operating system's process-creation identity, so PID reuse cannot keep the wrong process active even if it runs from the
same Editor binary; relevant process-query failures fail closed. This covers editors launched outside the Hub or left
running across a Hub restart. The worker revalidates
the exact marker, receipt, manifest, declared bytes, and absence of undeclared files immediately before atomically
renaming the root to a same-parent tombstone. A durable journal resumes an interrupted purge, and the Hub removes the
registry entry only after the root is absent and every persisted identity field still matches. Repeating Remove after
a retryable failure resumes the task identity that owns that journal instead of creating a conflicting removal owner;
if that task record was lost, a new task may adopt only a journal whose complete ownership proof and derived paths
still match. A retryable recovery task can be dismissed from visible history without deleting its durable recovery
identity, and it becomes visible again if Remove or Retry resumes it. Before the Windows commit rename, the worker asks
the verified bundled .NET SDK to stop its compiler/build servers so an editor that has already closed does not remain
locked by an idle `VBCSCompiler` process. Damaged receipt-bound managed
installations expose **Repair** when no editor or installation task is active. Repair resolves the exact signed editor
and component versions recorded by the registry, downloads and verifies that complete dependency closure, and
atomically replaces the damaged tree while preserving its installation ID and ownership nonce. An ordinary install
cannot use this replacement path. Legacy registrations without a receipt-bound package set retain recovery guidance
instead of an unsafe repair action.
Install discovery, full inventory refresh, verification, and repair/removal preparation run through an immutable
single-flight background workflow. A result is discarded if its registration, root, ownership proof, tracked running
state, or targeted task activity changed before the owner thread receives it; only that owner thread persists health
and queues a prepared package task. The complete refresh health set is persisted atomically so a missing managed editor
cannot remain Installed in the signed catalog view. A missing editor card offers a separate **Remove from Hub** recovery:
the runtime matches the exact managed registration and root, proves no filesystem object remains there, and removes only
the stale registration. It never deletes editor files and immediately exposes the matching version for reinstall.
Editor removal is also disabled while any Build Support operation is active, so its selected Asset Tool and
version-scoped component storage cannot disappear beneath an import, repair, or removal worker.

When an install of the same catalog package and version is already active, choosing **Install again** opens an explicit
confirmation instead of silently queuing duplicate work. Continuing selects a distinct available managed destination;
declining leaves the existing download or installation untouched. Editor inventory refresh, verification, and
authorization checks appear in the task center only while they are running; their result is reported through the
calling workflow rather than left behind as an undismissable synthetic task.

Verified editor catalogs populate Stable, Pre-release, and Nightly sections only when an enabled channel contains a
host-compatible editor. An install review selects a confined destination and compatible optional components, then shows
the dependency resolver's exact topological package closure, download size, disk requirement, and `Required By`
relationships before any task is queued. Versions with missing or conflicting dependencies remain visible with their
actionable catalog error but cannot be installed.

The destination remains manually editable and has a native **Browse** action. Folder-picker results pass through the
same absolute, non-root, writable-location validation as typed paths before the install task can begin.

Each healthy editor card exposes **Manage Components** through that installation's typed Asset Tool. Component counts
come from the installed Build Support inventory and are matched to the editor's exact engine version; the modal then
applies any requested platform/architecture filter. Signed catalog `.keirepackage` Build Support participates in the
editor install dependency closure, while offline import, repair, and removal continue through the selected editor's
typed Asset Tool using `.keireplayersupport` archives. Those operations validate the requested pack/target, publish
atomic status, and retain the existing bounded removal journal so a tombstone is never exposed as installed. Generic
offline `.keirepackage` import remains hidden until its product task flow is connected.

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

Terminal package tasks can be dismissed individually, or cleared together with **Clear finished**; active tasks remain
untouched. Notifications preserve unread state, then expose **Dismiss** after they have been read. These actions update
the durable stores, so cleared history does not return on the next Hub launch.

Installed component discovery and inventory validation run on a background workflow. Home, first run, and the component
modal consume immutable snapshots and show a deliberate checking state while that scan is active; package enumeration,
manifest parsing, and inventory hashing never run inside a UI frame.

Templates are data-driven. The browser searches and filters the three verified templates packaged with this release:
Empty Project, 3D Starter, and Kéire Sandbox. The 3D Starter payload opens a ready-to-run scene with an active camera
and directional light; Empty Project deliberately has no startup scene. Sandbox packages the canonical sample's clean
authoring content, including its paired Shader/Material Graph library, gallery and gameplay scenes, VFX, scripts,
meshes, textures, audio, and applicable project settings. Caches, builds, generated graph output, and project-specific
identity files are deliberately regenerated in the created project. Featured entries are ordered first, and the
detail panel reports tags, packaged thumbnail previews, editor-version constraints, project schema, platform target,
starter content, package requirements, and license references from the manifest. Compatibility is evaluated against
each healthy installed editor before the creation dialog offers a template/editor pair. Remote template download and
update controls remain absent until template packages are connected to the persistent package-task pipeline.

Creation stages only declared payload files, writes schema-3 project metadata, validates the staged project
out-of-process with the selected editor's Asset Tool, and publishes atomically. Failures remove staging and leave an
existing destination untouched. The creation dialog explicitly chooses whether to open the result in the selected
editor. Validation observes Hub shutdown, terminates its child process promptly, and removes staging instead of making
application shutdown wait for the full validation timeout. Creation progress appears in the task center and never
blocks the UI thread.

`Scripts/Packaging/sync-sandbox-template.py` owns the deterministic Sandbox projection and manifest hashes. Run it
after changing canonical Sandbox authoring content; `--check` is part of the Windows and Unix fast regression suites
and fails when payload bytes, file inventory, startup references, or catalog metadata drift.

Learn and Resources load real packaged or signed catalog entries only. Packaged entries include the documentation and
Kéire Sandbox; remote entries require HTTPS and a valid signed content catalog. Licenses loads the MIT license and real
third-party/package license files, grouped by source, with search, copy, and reveal actions. Installed-package notices
are resolved off the UI thread from receipt-bound paths and are shown only while their current size and digest still
match the verified inventory. Missing or malformed packaged content/license catalogs produce explicit page warnings and
typed error notifications rather than ordinary empty states.

Packaged and development Hubs resolve this local content from the executable's distribution ancestry. Development
binaries therefore load repository-owned templates, documentation, licenses, branding, and fonts consistently when
started from an IDE, the command line, File Explorer, or another process with an unrelated working directory.

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

## Accounts and identity

`Config/Supabase.json` enables optional identity with a project HTTPS URL and modern `sb_publishable_...` key. Schema 2
can independently enable the public-client browser OAuth adapter with a registered client ID and the exact HTTPS
website callback. The recommended flow uses authorization-code PKCE, state, and nonce; the browser forwards only the
single-use code through `keirehub://oauth/callback`, and the Hub performs the token exchange. Email authentication
remains available as a staged fallback. The Hub never accepts or packages a client secret or Supabase service-role key.
OAuth refresh tokens are treated as opaque, non-empty, bounded values; the Hub does not assume a provider-specific
minimum length and rotates every accepted token through the platform secure store.
The Windows installer owns the current user's `keirehub` protocol registration and removes it only when it still points
to that exact installation. Linux packages declare `x-scheme-handler/keirehub` and pass a single `%u` activation to the
verified Hub wrapper. The callback page never displays or copies the authorization code: it offers a user-initiated app
handoff, reports focus/visibility feedback, and directs a missing handler to the catalog-verified Hub download. A
pending request can be cancelled in Hub before starting another flow; callbacks from cancelled or unrelated flows
remain rejected by the state check.
Sign-up correctly
accepts both direct user and user-envelope response forms, represents email-confirmation-required responses without
inventing a session, and reports confirmation-email cooldowns directly. Sign-in, refresh-token rotation, local sign-out,
and profile display-name updates run on the account worker rather than the UI thread. Retryable refresh and profile
failures preserve the authenticated UI snapshot and refresh attempts use bounded backoff.

Marketplace profiles, organizations, publisher membership, entitlements, reviews, and device sessions are protected by
forced row-level security and protected membership tables. Platform roles are evaluated from the database-authoritative
`platform_staff_members` relation on every operation, so revocation does not wait for JWT refresh. Browser staff reads
remain RLS-constrained; MFA-protected Edge Functions and service-role-only transactional functions own staff and
moderation writes.
Windows refresh tokens use DPAPI, Linux uses Secret Service when `secret-tool` is available, and macOS uses Keychain.
When secure persistence is unavailable, the Hub keeps the session in memory and shows a warning instead of writing a
plaintext credential. Website cookies and browser refresh tokens never transfer to Hub; Hub sessions can be revoked
independently. Account tokens, proxy credentials, signing material, and unredacted user paths never enter diagnostics.

Identity and software distribution are separate trust domains. A Supabase session is not an editor-package
entitlement, cannot authorize an install or uninstall, and is never used to bypass Ed25519 catalog signatures,
SHA-256 package identity, managed-install ownership markers, or editor project locks.

Marketplace HTTP uses canonical trailing-slash `/marketplace/v1/.../` routes because the unified Astro deployment
enforces trailing-slash routing. Catalog, library, claims, OAuth device registration, and download-grant calls remain
bounded and versioned. The first verified Hub OAuth token may reach only the device-registration route before its
session record exists; every later Hub bearer request must match an active, non-revoked device session. Marketplace
feature flags remain the operational kill switch: catalog, claims, downloads, reviews, and publishing can be disabled
independently without weakening authorization or exposing unpublished content.

Marketplace product activation uses the same per-user `keirehub` protocol registration as desktop OAuth but a distinct,
strict action: `keirehub://marketplace/product/<UUID>`. Query strings, fragments, traversal, extra path segments, invalid
UUIDs, oversized frames, and unexpected fields are rejected before dispatch. A secondary Hub process forwards the typed
request to the existing primary process, so website clicks do not create competing account sessions or caches.
On Windows, starting browser sign-in first refreshes the current-user protocol command to the exact running Hub
executable. A Hub launched directly from a build or portable directory therefore receives its callback without an
installer or administrator access. Registration is transactional: a failed update restores the previous values before
the browser flow begins.

If the user is signed out, Hub retains the requested product ID in memory and opens the account dialog. After sign-in,
Hub synchronizes My Assets and prepares the package without exposing the access token to the Editor. If an Editor is
already open, its Package Manager observes the atomic token-free cache update. Hub renews a short-lived, token-free
account lease beside that cache; the Editor displays My Assets only when the lease is current and belongs to the same
account. Signing out clears the lease immediately, while a crashed or closed Hub expires within 15 seconds. Otherwise
Hub remains on Projects so the user can choose the target project; the next Editor reads the same requested-product
marker and focuses the Package Manager after the matching Hub session is available. Registry and Asset Import packages
are project-scoped Editor operations. Complete Project packages remain Hub creation operations.

Minimizing or manually closing the Hub to its tray suspends rendering and fixed simulation but keeps its low-rate
background layer update active. Account refresh and the short-lived Marketplace lease therefore continue while the Hub
window is hidden, so the Editor Package Manager does not mistake a running, signed-in tray Hub for a closed process.

## Distribution and trust

The checked-in `Config/Distribution.json` is the shared Windows, Linux, and macOS release-trust authority. Hub packaging
validates and copies it into the package so online discovery does not depend on host-specific environment variables.
The packaged file contains the initial service URL and trusted Ed25519 public-key documents.
If either is absent, online discovery is disabled and the Hub continues with installed content, imported packages, and a
valid last-known-good cache. HTTPS is required except for explicit loopback development mode; there is no certificate
bypass.

Catalog signatures cover the exact received bytes. The Hub validates key ID, sequence, expiry, channel, platform, and
architecture before parsing package records. Online packages are immutable SHA-256 resources. Resumable downloads use
content-addressed cache files, `.partial` data, ETag/If-Range metadata, bounded retry, and digest verification. The task
store and worker protocol use atomic files beneath the preference directory so interrupted work can be reconciled after
a restart. Equivalent ISO-8601 UTC expiry spellings compare as instants at both the parsing and Installs indexing gates.
An HTTP 304 response keeps the source online because the service revalidated the cached signed bytes; only an offline or
failed-transport cache fallback is labeled last-known-good. A package catalog may carry a signed
`minimumSupportedHubVersion`; update selection considers only verified
online or last-known-good snapshots, chooses the newest semantic Hub-installer version, prefers Stable for equal
versions, and treats a minimum-version policy without a suitable installer as a catalog error.

Hub-installer records must match the verified catalog's key, channel, platform, and architecture exactly; wildcard or
cross-endpoint installers fail closed. **Download update** creates a dedicated resumable `HubUpdate` task and publishes
the native artifact only after its signed-catalog SHA-256 and size match. Download completion does not install anything.
The separately enabled **Install update…** action rechecks the digest, verifies Authenticode on Windows, writes an
atomic resume token, and then opens the native installer before the Hub exits normally. Windows NSIS waits for the
recorded Hub PID and revalidates the registered install root and ownership marker before replacing files. Linux uses an
explicit `pkexec` handoff to `dpkg` for DEB hosts or `dnf`/`zypper` for RPM hosts when those tools exist; its
signed-catalog digest remains the package trust boundary.
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
displace the last valid snapshot. `KeireHubPackagePublisher create-editor` converts a validated schema-2 editor
distribution into the generic archive/catalog manifest. `create-hub-installer` creates the corresponding catalog
manifest only for a clean Hub package and platform-native `.exe`, `.dmg`, `.deb`, or `.rpm`. Catalog signing and
SHA-256 binding are mandatory. Native signing/notarization remains mandatory for a production platform claim; an
unsigned preview must be disclosed and may have a narrower installation path. Linux Hub records carry an explicit
`packageFormat`. This lets one signed Linux
catalog retain DEB and RPM artifacts for the same Hub version while each host and the downloads site select the correct
format. `prepare-distribution-snapshot.py` accepts repeated manifest/artifact pairs, rechecks every length and digest,
rejects duplicate identities, and groups records into their host catalog before offline signing. A
new Editor release carries every retained Editor manifest and its original content-addressed package into that input;
publishing a newer version must not silently retire an older downloadable Editor. Retirement is an explicit release
decision, while ordinary publication is additive across supported versions and preserves each artifact's exact digest.
A production Hub enables online Installs from the checked-in real HTTPS service URL and trusted release public key.
Explicit packaging environment variables may replace that public configuration for another deployment. Caddy serves
the package-local public website for every other route while preserving `/v1/*` and
`/health/*` as exact backend interfaces.

Signing-key rotation uses an overlap window: updated Hubs package both the retiring and replacement public keys before
any catalog is signed by the replacement identity. Only public keys enter the repository or online host. The raw
private key remains outside both, and any cloud backup must be independently encrypted with its recovery secret stored
separately. After the replacement Hub has been distributed and the transition policy is complete, retiring an old
public key is a separate release decision. Packaging overrides may supply an operating-system path-separated
`KEIRE_DISTRIBUTION_TRUSTED_KEYS` list during an overlap; the legacy singular key variable remains supported.

The active stable snapshot reviewed on 2026-08-16 is
`release-0.3.2-sequence-14-4b96626`. Its Windows x86-64 catalog contains `keire.editor@0.3.2` and
`keire.hub@0.3.2`; its Linux x86-64 catalog contains the Editor archive plus distinct DEB and RPM Hub records. The
Windows EXE and RPM are catalog/hash verified but do not carry Authenticode or RPM GPG signatures. Earlier versions
were retired as an explicit release decision because they contain known major defects; ordinary future publication
remains additive unless another documented retirement is approved.

## Launch and activation

Opening a project launches the selected editor's typed entrypoint as:

```text
KeireClient --project <canonical-root>
```

The editor independently validates the descriptor and acquires the exclusive project lock. After a successful launch,
the Hub records the project, installation ID, and last-opened time. Depending on settings it remains alive while hidden
to the tray/taskbar, or exits normally. A Hub hidden automatically for a tracked editor restores its window when the
final tracked editor exits; a manual close-to-tray remains hidden until the user explicitly shows it.

A non-distribution Hub launched from a source build uses the sibling editor from the same configuration for ordinary
project opens. The Hub build depends on that editor, which in turn depends on the Asset Worker, so building or starting
the Hub from Visual Studio first updates the complete development launch chain. This prevents a project's persisted
packaged-editor preference from silently launching an older distribution. An explicit **Open with** choice still honors
the selected registered installation. Packaged Hubs continue to use only verified registered editors.

Only one Hub process owns a window and tray entry for each canonical installed executable. Secondary processes send a
bounded, versioned activation request for show, open-project, install-version, import-package, navigation, or legacy
Build Support actions, authorize the primary process to take foreground focus, then exit without initializing window or
rendering services. The primary restores and raises its own window at the owner-thread safe boundary.

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

On Linux, the standalone Hub installer selects DEB for Ubuntu/Debian and RPM for Rocky/Fedora/openSUSE. Release
builders can make that choice explicit with `--linux-installer-format deb` or `--linux-installer-format rpm`; the two
formats remain separate native artifacts with independent checksums and website identities.

`--smoke-ui` validates bounded Hub startup and rendering. `--smoke-project` opens the sample through the real project,
asset, input, scene, workspace, and editor lifecycle before exiting cleanly. The focused private runtime suite is the
`KeireHubTests` target. Standalone Hub package and native-installer regression harnesses live under `Scripts/Tests/`.
