# Changelog

- Added durable task and notification cleanup in the Hub, including per-item dismissal and bulk clearing of finished
  tasks. Retryable editor removals now retain their recovery identity out of sight after dismissal instead of becoming
  permanent task cards, and transient editor checks disappear when they finish. All project dialogs now follow the
  selected Hub appearance, recovery presents transaction-specific guidance, and upgraded projects refresh lock
  metadata after the launched editor exits instead of remaining falsely in use.
- Fixed managed script builds leaving the bundled .NET compiler server alive after editor shutdown, which locked the
  editor installation on Windows. Uninstall also shuts down verified bundled build servers before its commit rename,
  and a replacement removal task can safely adopt an exact matching journal instead of becoming a conflicting removal
  owner. Starting another install of an already active editor version now asks whether to download again and chooses a
  distinct managed destination when confirmed.
- Fixed Windows custom-caption clicks being consumed after hover without completing the minimize, maximize/restore, or
  close action. Caption buttons now use one native press/release transaction, cancel when released outside their
  original region, and retain Windows 11 Snap Layout discovery on the maximize button.
- Fixed editor-install, project-creation, and project-upgrade dialogs ending their popup scope before drawing their
  contents, which left the Hub dimmed while rendering the dialog controls as ordinary page content.
- Added a packaged Windows distribution-host supervisor for limited-user Task Scheduler deployments. It validates a
  bounded settings file, launches Kestrel and Caddy invisibly, prevents duplicate supervisors, checks local and public
  readiness, and restarts either process when its listening port disappears.
- Fixed online Hub catalogs being rejected when equivalent UTC expiry timestamps used different ISO-8601 spellings,
  restored pointer interaction across the complete custom-title-bar account and product control strip, and separated
  explicit offline mode from an unavailable network connection in the sidebar status.
- Fixed confirmation-required Supabase sign-ups being reported as invalid data when Auth returned the user directly,
  and replaced the generic response for repeated sign-up email rate limits with actionable inbox and retry guidance.
- Fixed the Installs catalog rejecting equivalent signed UTC expiry spellings after signature verification, which hid
  valid host-compatible editor versions. Server-revalidated HTTP 304 catalogs now also retain online status instead of
  making the sidebar report that the service is unavailable.

All notable template changes are documented here. The format follows Keep a Changelog, and releases use semantic
version tags.

## Unreleased

- Added a repeatable Podman matrix for Ubuntu 22.04/24.04, Debian 12, Fedora, Arch, openSUSE Tumbleweed, and Rocky
  Linux 9. Linux bootstrap now handles all four package-manager families, installs a pinned .NET 10 SDK, bounds native
  compilation by default, and installs verified Premake, CMake, Ninja, NASM, or patchelf fallbacks when distro
  binaries are incompatible or unavailable. Older supported distributions receive project-private GCC 12 shims
  without replacing their system compiler.
- Fixed GCC/Linux portability defects in renderer, input, logging, audio cleanup, and process-liveness code. Linux
  process checks now reject reaped-but-still-visible zombie states, Rocky GCC Toolset runtime activation follows every
  build/run/package path, and shader/compiler runtime publication no longer depends on distro-specific `patchelf`
  availability. Python tooling remains compatible with Rocky Linux 9's Python 3.9, the native Build Support downloader
  supports both old and current libcurl HTTPS-policy APIs, and headless client startup is independent from the
  warnings-as-errors CI compile policy. GCC warnings-as-errors now covers the complete engine, editor, Hub, tests, and
  client surface without compiler-specific aggregate-initialization noise.
- Fixed clean Linux and macOS bootstraps failing when Premake evaluated the repository before dependency metadata
  existed, made SDL's installed CMake package location deterministic across host toolchains, and corrected .NET 10 SDK
  root discovery for Coral and managed builds. Build and test entrypoints now fingerprint dependency and bootstrap
  infrastructure so ABI-affecting script updates regenerate project metadata and native dependency caches.
- Fixed Unix dependency and runtime publication: pinned archives now use exact `-L`/`-l:archive` linker pairs, Jolt is
  built with the RTTI ABI required by the job-system adapter, and the shader compiler and Asset Worker retain
  relocatable `$ORIGIN` loader paths with complete runtime-library aliases. Cached shader compilers are executed before
  acceptance instead of being trusted solely from their stamp.
- Hardened cross-platform compilation by using POSIX-compatible awk, direct standard-library and POSIX declarations,
  and fixed-width managed-generation identifiers across Windows LLP64 and Unix LP64 data models. Managed generation
  sequencing and Hub notice rendering were also split into focused implementation units to keep source-size budgets
  enforceable without raising their limits. The managed replacement integration test now requires the repository's
  platform-specific .NET 10 dependency instead of silently skipping behind a Windows-only executable path.
- Fixed Linux validation and distribution edge cases: intentional assertion probes no longer escape the test harness,
  AddressSanitizer retains leak detection while excluding only hosted CoreCLR process-lifetime allocations, packaged
  SDL and FFmpeg files use their real Unix layouts and materialized aliases, and editor installers materialize bundled
  .NET SDK links into self-contained regular files accepted by deterministic manifests and archives. Undefined
  Behavior Sanitizer runs now fail on their first diagnostic, and bounded Hub thumbnail resizing uses stb's scalar path
  instead of its formally invalid SIMD lookup-table pointer arithmetic.
- Applied the public security-header policy to both ordinary Caddy responses and branded site/docs error routes, with
  regression coverage for the shared policy. The rebuilt 52-guide documentation site was deployed transactionally
  without changing signed catalog bytes, trust headers, ETags, conditional requests, or package range behavior.
- Restored the Unix fast regression suite on case-sensitive Linux hosts by keeping project configuration on LF line
  endings and updating stale launcher, rendering, scene-schema, WAV-header, dirty-package, and package-inventory
  fixtures to their current production contracts.
- Normalized first-party repository structure across case-sensitive platforms: canonical documentation now lives in
  `Docs/`, C++ headers live under each project’s `Include/` tree, implementation units live under `Source/`, and the
  distribution service no longer uses lowercase `src/`. SDK and Hub packages now publish `Docs/` while Hub root
  discovery remains compatible with older packages. Added a platform regression check that prevents layout drift.
- Hardened cross-platform child-process launch: Windows now restricts inherited handles to the intended standard I/O
  pipe endpoints, while POSIX launch paths finish all allocating argument preparation before `fork`. Added regression
  coverage for unrelated inheritable handles and made confinement tests fail explicitly when CI lacks symlink support.
- Made the reusable format/static-analysis gate a release preflight, expanded its first-party coverage to every Hub
  executable and test tree, aggregated native core/editor/Hub/client coverage, and added Debug/Release managed and
  distribution-service tests to the platform test launchers. Coverage now protects separately measured 74.5% core and
  63.0% whole-product non-regression floors instead of an unattainable legacy 80% gate. NuGet and Deno restore inputs
  are now frozen by lockfiles. Ninja dependency paths now remain toolset-aware, generation fingerprints include Premake
  inputs, and the Unix launcher/package scripts pass the release workflow's ShellCheck policy.
- Hardened the public contact function with a streamed 16 KiB body limit, strict UTF-8/object JSON parsing, and a
  platform-provided client-address rate key that ignores caller-controlled forwarding headers.
- Fixed transient startup or host-network failures leaving distribution discovery permanently unavailable. The Hub now
  preserves verified catalogs while running interruptible bounded retries, periodically revalidates healthy endpoints,
  distinguishes Reconnecting from explicit Offline mode, and restarts discovery only for relevant network settings.
- Fixed the custom Hub caption strip applying ordinary content spacing between fixed-width window buttons, which
  pushed the Close hover surface through the right client edge. Rendered controls and native hit-test reservations now
  share one contiguous geometry contract, including the complete account/product command strip.

- Added the public Kéire website, packaged with the distribution service and served by Caddy without changing the
  signed `/v1/*` or health APIs. Downloads discover native Hub installers from verified catalog fields, while the new
  Hub-installer publisher and multi-package snapshot preparer keep unpublished platforms safely unavailable.
- Centered the public home-page presentation, added a private Supabase-backed contact form with bounded validation and
  abuse throttling, and made the current Windows Hub build downloadable as a clearly labeled unsigned development
  preview without weakening or modifying the signed stable catalog path.
- Versioned mutable website assets and disabled fresh caching for CSS, JavaScript, and download metadata so layout,
  form styling, and release controls activate together. Development Hub builds now use digest-suffixed immutable URLs
  to prevent one preview from reusing another preview's cached identity.
- Replaced the public Docs index with a branded Astro Starlight documentation site generated from all 52 maintained
  repository guides. It adds native guide pages, Pagefind full-text search, structured sidebars and page outlines,
  accessible mobile navigation, SEO/sitemaps, code controls, a docs-specific 404, deterministic packaging, and strict
  CSP-compatible static output while keeping repository Markdown authoritative.
- Reworked the root README and complete documentation index around the current Hub, editor, cook, runtime, scripting,
  packaging, and release workflows. Corrected project, scene, mesh, material, and runtime-manifest schema guidance;
  added exact guide-to-code authority, local-link, and schema-drift checks; and render GitHub-compatible Mermaid fences
  as responsive accessible SVG in the public static documentation build.
- Fixed manually deleted managed editors appearing as both Missing and Installed. Background health scans now persist
  atomically, missing registrations no longer suppress the matching catalog install action, Verify reports the actual
  result, and Installs offers a guarded registration-only recovery that proves the exact editor root is absent before
  enabling reinstall. The sidebar connection/version footer is also centered as one status group.
- Restyled Build Support management and removal as full Hub design-system modals with editor/component cards, semantic
  status colors, consistent spacing, and accessible actions. Task percentages now render beside their phase instead of
  clipping inside thin progress bars. Asset Tool host diagnostics expose their resolved Build Support root, and invalid
  installed entries are logged instead of disappearing silently; editor packages rebuilt from the UTF-8 directory fix
  now resolve the same canonical `Kéire` module inventory as the Hub.
- Fixed Windows Hub-managed editor installs failing before the worker status journal was created when a preference or
  install path contained `Kéire`. SDL UTF-8 directories now cross the native filesystem boundary losslessly, the worker
  consumes wide command-line paths, and existing `KÃ©ire` preference/cache/task roots migrate without discarding their
  operation journals. The account, editor-install, first-run, project, and confirmation dialogs now use a shared padded
  Hub modal system; page cards have consistent content insets; and live install progress plus durable start/completion
  events appear in the activity and notification centers.
- Fixed newly queued editor installs failing when their managed Editors root did not exist, prevented UTF-8 editor
  display names from being reinterpreted through the Windows narrow filesystem encoding, and preserved a worker's real
  terminal failure when it finishes before the coordinator's first journal poll.
- Added optional Supabase email identity and owner-scoped profiles to the Hub. The desktop client accepts only an HTTPS
  project URL and modern publishable key, runs sign-up/sign-in/rotation/profile work off the UI thread, encrypts Windows
  refresh tokens with DPAPI, preserves signed-in state across retryable failures with bounded refresh backoff, and keeps
  account identity separate from signed package authorization. The profiles migration uses explicit authenticated-only
  grants and `auth.uid() = user_id` row-level security policies.
- Added the Unicode-safe `KeireHubPackagePublisher create-editor` tool and exact distribution-snapshot preparer. The
  workflow rehashes schema-2 editor payloads, writes generic `.keirepackage` archives and canonical catalog manifests,
  verifies content-addressed package bytes before offline Ed25519 signing, and has cross-platform tamper/overwrite
  regression coverage.
- Fixed standalone Hub startup when migrating the exact `sort=last-opened` preference written by earlier Hub builds;
  the legacy file is preserved while schema-versioned JSON settings are created atomically. A missing update-resume
  token is also treated as the normal no-pending-update state instead of producing a false recovery warning on Windows.
- Fixed Windows cross-publishing of the Linux distribution service package. Its deterministic archive now preserves
  executable modes only for directories, the service/publisher entrypoints, and shell wrappers instead of inheriting
  unusable NTFS modes, with a fast regression fixture covering inventory order, metadata, and reproducible bytes.
- Moved editor discovery, inventory verification, and managed repair/removal authorization onto a single-flight
  background coordinator. Immutable results are accepted only when the registry generation, security identity, root,
  tracked process state, and targeted task activity still match; health persistence and task/notice publication remain
  owner-thread operations, and external removal no longer triggers a synchronous full-install scan.
- Hardened editor publication and restart reconciliation against replacement races. New installs use an atomic
  no-clobber policy, repair recovery reauthorizes the exact marker, receipt, package identity, and native executable
  activity before every mutation (including the same-parent backup), persisted worker mode must match task kind, and a
  completed repair is registered only from its retained identity proof without rewriting the ownership marker.
- Activated the packaged Material Symbols subset through Kéire's UI facade with ASCII fallback for consumers that do
  not configure an icon font. Hub navigation, documentation, appearance, project-view, and native caption controls now
  use the shared icon primitives, including compact-rail tooltips, maximize/restore state, and a destructive close
  hover treatment.
- Moved first-run project and editor import preparation off the Hub owner thread. Completed discovery now publishes an
  immutable, revalidated import snapshot; batched registries preserve existing paths, identities, pinning, and editor
  preferences, reject duplicate identities/roots before writing, and roll back the project registry if the paired editor
  registry commit fails.
- Collapsed project metadata refresh publication into one bounded atomic catalog update. The owner thread now validates
  every scanner result before one registry write, so missing, duplicate, or invalid results cannot leave a partially
  refreshed project list or thumbnail snapshot.
- Moved installed Build Support discovery and health verification onto a background inventory workflow with immutable
  snapshots and explicit loading/failure states, so startup and component refresh no longer scan packages on the UI
  frame.
- Added asynchronous verified-package-cache maintenance as an exclusive task-center operation. Cache clearing now
  refuses active package work, suspends the idle package coordinator while files are removed, and restores it only
  after the maintenance result is published.
- Moved Hub project-upgrade inspection, apply, interrupted recovery, and rollback behind an owner-thread-affine
  asynchronous coordinator. The confirmation modal now renders immutable state while all project filesystem and
  transactional validation work runs outside the UI frame.
- Added managed-editor **Repair** to Installs as a distinct persistent worker task. Repair requires an inactive,
  receipt-bound managed registration and the exact signed editor/component dependency closure, preserves installation
  identity and ownership nonce, reauthorizes the marker and executable immediately before atomic replacement, and
  reconciles the repaired tree through normal package registration. Ordinary installs remain unable to replace an
  existing damaged destination.
- Hardened project dispatch in the standalone Hub. Background metadata refresh now probes active editor locks and
  interrupted-upgrade journals, tracked editor processes override stale cached status, and editor selection enforces
  project schema, minimum engine, and last-saved version together. A stale preferred installation can no longer select
  an older editor, while an unavailable exact version recommends the least disruptive verified newer package.
- Removed the fabricated sibling-editor fallback and the partially interactive shell shown after runtime composition
  failures. Only manifest-validated registered editors appear in Installs, and an unrecoverable startup failure now
  presents a bounded recovery screen with logs, diagnostics, and exit actions. Manual project paths and names
  also cross the Windows filesystem boundary as UTF-8 without lossy narrow-path construction.
- Hardened macOS Hub/editor publication around one pinned 12.0 deployment target, package-time Mach-O load-command
  validation, explicit inside-out signing, preserved Microsoft .NET signatures, and managed-runtime entitlements scoped
  only to the editor host.
- Split standalone editor ownership from the Hub on Windows. Editor archives and NSIS installers now contain and launch
  only the editor and editor-specific tools, reject Hub/HubWorker/content payloads, and give the editor its own finish
  action, icon, Start Menu entry, and desktop shortcut. Schema-2 editor manifests retain their schema-1 compatibility
  fields while omitting Hub and worker entrypoints.
- Added signed, streaming `.keirepackage` archives and crash-recoverable atomic publication for Editor, Build Support,
  Template, Learning Content, and Toolchain payloads. Multi-package editor installs now persist a receipt-bound complete
  file inventory, dependency and license attribution, aggregate identity, and managed ownership marker so Components,
  verification, repair, and safe recovery remain truthful after restart. The License browser asynchronously resolves
  editor, Build Support, template, content, and toolchain notices only from receipt-bound files whose size and digest
  still match the installed package inventory.
- Added signed Hub-installer selection across online and last-known-good package catalogs. Exact semantic-version
  ordering prefers stable packages for equal releases, rejects unverified catalog snapshots, and enforces the signed
  `minimumSupportedHubVersion` policy instead of presenting an unusable or downgraded update.
- Completed the explicit Hub-update handoff: exact key/channel/platform/architecture installer identity, dedicated
  resumable task-center downloads, cache and digest revalidation, Windows Authenticode, guarded native installer launch,
  and atomic next-start recovery tokens. Windows NSIS now waits for the originating Hub and revalidates its registered
  owned root; macOS reveals the verified drag-to-Applications DMG for manual installation, and other unsupported native
  handoff environments likewise expose only the verified file.
- Added signed editor acquisition to Hub Installs: populated release-channel sections, compatible component selection,
  destination validation, and an exact dependency/disk review now hand a provenance-preserving install plan to the
  persistent package worker instead of exposing placeholder catalog or Components actions.
- Integrated legacy Build Support into each editor's **Manage Components** flow with exact-version inventory counts,
  explicit typed Asset Tool selection, target-filtered import/repair, confirmed out-of-process removal, authoritative
  final status, and bounded journal recovery for interruptions around the removal tombstone. Generic package import
  remains hidden.
- Persisted legacy Build Support operation/task history across Hub crashes with atomic bounded records for operation and
  installation identity, confined status/cancel paths, and child PID. Restart recovery observes surviving exact Asset
  Tool processes without taking ownership, restores cancellation and task-center visibility, and derives terminal
  import/repair or removal state only from authoritative status, inventory, and schema-1 removal-journal evidence.
  Successful imports and repairs now coalesce a fresh follow-up inventory scan when an older scan is still active.
- Sanitized Build Support inventory and Asset Tool status failures behind stable typed codes and user-facing messages;
  filesystem, JSON, operating-system, and exception details now remain confined to logs and explicit diagnostics.
- Added private Hub editor-installation verification with immutable health snapshots, schema-2 manifest fingerprint and
  file-inventory checks, host and entrypoint validation, registry-only external removal, and revalidatable managed
  repair/removal plans that refuse running editors, active tasks, unexpected roots, and marker mismatches. Running state
  now combines Hub-tracked launches with bounded exact-path process probes on Windows, Linux, and macOS, failing closed
  for relevant-name query failures so externally launched editors cannot be verified or mutated concurrently.
- Added guarded managed-editor uninstall from Installs through the persistent worker. Removal requires an exact healthy
  receipt-bound tree, rejects undeclared or changed files and unsafe roots, commits through a same-parent tombstone,
  resumes interrupted purges from a durable journal, and reconciles the registry only from an exact completion proof.
- Replaced the Hub's ad-hoc Show/Build Support activation text with a bounded versioned protocol and typed Show,
  Navigate, Open Project, Import Package, Install Version, and Build Support actions, including strict command/path
  validation and malformed-frame rejection. Owner-thread dispatch reuses functional project and Build Support flows and
  reports unavailable package/catalog actions without queuing mock work. Secondary-process coordination coverage now
  lives in the private Hub test target, and normal Windows and Unix test launchers run that target without coupling the
  editor test executable to Hub runtime code.
- Added private Hub project workflows for clean staged duplication with new project identities, identity-verified moved
  project location, closed-project display-name changes, and catalog-only removal. Duplicate publication rejects path
  escapes, symbolic links, portable case collisions, locked projects, bounded-size violations, and stale destinations.
  Recursive duplicate staging now runs as one cancellable background operation with immutable operation-ID progress,
  owner-thread commit, authoritative pre-publish revalidation, and shutdown cleanup. These actions are available from
  project context menus with native folder selection and removal confirmation; all Hub project mutations now use the
  runtime catalog as the sole registry writer so cached metadata survives UI refreshes.
- Added real project-card thumbnails from confined `ProjectSettings/HubThumbnail.png` files. The metadata worker now
  validates, decodes, center-crops, and bounds PNG pixels before immutable publication; the UI uploads visible images
  through a 64-entry texture LRU and retains the deterministic monogram fallback for missing or invalid thumbnails.
- Expanded Templates into a searchable, category-filtered browser for the three verified packaged templates, with
  featured ordering, manifest-backed detail, packaged PNG artwork, and per-installation editor/schema
  compatibility. Project creation now offers only compatible template/editor pairs, lets the user choose whether to
  open the result, and cancels Asset Tool validation promptly during shutdown; remote download/update actions remain
  hidden.
- Fixed moved-project location being available for live registrations, Build Support activity not blocking editor
  removal, and packaged content/license failures appearing as ordinary empty states. The Hub now revalidates the
  original project root, conservatively guards editor removal during component work, and surfaces typed catalog errors.
- Fixed Windows managed-editor uninstall and restart recovery misclassifying protected marker/receipt files when native
  directory iteration returned extended-length paths. Logical child paths now remain anchored beneath the validated
  parent while extended prefixes are confined to I/O calls.
- Added typed, scope-bound UI color and spacing overrides so product components can style surfaces without exposing
  Dear ImGui ownership. Template project creation now also rejects destinations beneath the installed Hub tree, keeping
  projects outside application directories that native updates are allowed to replace.
- Added the persistent Hub package task foundation: content-addressed verified caches, atomic resumable partial-download
  metadata with ETag/If-Range identity, bounded retry jitter, pause/cancel preservation, deterministic concurrent task
  scheduling, per-install and per-package serialization, worker-PID recovery, and a focused out-of-process Hub worker
  protocol with typed status, result, and control journals.
- Added an exact-byte Ed25519 trust boundary for signed Hub package and content catalogs, with pinned trusted-key
  identities, endpoint and expiry validation, replay protection, atomic last-known-good caching, and HTTPS-only remote
  discovery. The standalone Hub now packages its private, commit-pinned libsodium verifier and license.
- Added independently runnable `package-hub` workflows for Windows, macOS, and Linux. Standalone Hub archives exclude
  versioned editor and SDK payloads, include the private Hub task worker, and continue to reject editor-specific Asset
  Worker binaries. Editor and Hub packages now use validated schema-2 manifests with typed
  entrypoints, compatibility ranges, fingerprints, templates/toolchains/licenses, exact file inventories, and installed
  sizes. Editor manifests retain their schema-1 top-level compatibility fields for existing consumers.
- Added a separate `package-hub-installer` workflow. Windows produces a guarded per-user Hub-only NSIS installer,
  macOS produces a signed/notarizable drag-to-Applications Hub DMG, and Linux produces a desktop-integrated `keire-hub`
  Debian package with explicit C/C++ runtime and libcurl dependencies. Updates and uninstall preserve Hub preferences,
  caches, project metadata, and editor roots by default.
- Fixed the installed Project Hub using a blank system-tray icon, reporting structurally valid but incompatible Build
  Support as ready, and failing to immediately restore the existing Hub window when its shortcut was launched again.
- Fixed Windows installer creation selecting only the drive letter when NSIS was discovered in its standard install
  directory but was not available on `PATH`.
- Fixed Build Support package creation failing when the engine has no source modules, and fixed Windows verification
  and installation failing for bundled payloads beyond the legacy path-length limit. Empty module catalogs now have a
  stable non-empty identity shared by the editor, runtime, and packaging tools.
- Added a cross-platform `package-installer` workflow. Windows now produces an NSIS setup wizard with selectable
  destination, optional desktop and Start Menu shortcuts, launch-on-finish, upgrade registration, and guarded uninstall;
  macOS produces a self-contained drag-to-Applications DMG with optional signing/notarization; and Linux produces a
  desktop-integrated Debian package. All installer artifacts receive SHA-256 files.
- Windows Dist editor packages no longer open companion terminal windows. The Hub and editor use the GUI subsystem,
  disable their terminal log sinks, and retain file logging, while the editor Console now receives bounded structured
  Core and Client records from startup and worker threads, Coral managed-host diagnostics, and editor-authored messages.
- Editor packaging now leaves its validated, ready-to-run distribution under `Build/Distributions/` while retaining
  the compressed archive and checksum under `Artifacts/` for publication.
- Fixed Windows editor packaging failing three material-graph tests when the project launcher was started outside the
  repository root. Editor and GPU test processes now use the same explicit repository working directory as core tests.
- Fixed clean Visual Studio builds producing a `KeireHub.exe` that could not start because `nethost.dll` was absent.
  Every generated Windows executable now stages its load-time .NET host dependency after linking, while direct editor
  builds and repository launchers share one complete managed-host staging implementation.
- Fixed the default `clean` command leaving dependency outputs, managed builds, tools, logs, temporary files, and other
  stale content under `Build`. A full clean now removes the complete disposable build tree on Windows, macOS, and Linux,
  while the narrower build and generated scopes retain only their documented complementary state.
- Added a cross-platform `package-editor` launcher command and interactive-menu option. It always runs the Dist release
  gate and produces a native Windows, macOS, or Linux editor archive with editor-specific companion tools, the complete
  bundled .NET 10 SDK, sample project, manifests, checksums, notices, and platform launchers; macOS also includes an
  Editor `.app`.
- Fixed Windows Ninja client builds failing during managed-host staging because Premake's escaped post-build stamp
  command was interpreted as a path by `cmd.exe`; repository Ninja launchers now perform their existing staging step
  only after the native target succeeds.
- Windows archive publication now retries transient file-sharing failures while packaged runtime smoke processes finish
  releasing their staged .NET host files.
- Fixed the Lighting panel crashing when the lightmap and maximum resolutions both reached 16,384. Fixed-value integer
  slider ranges now remain valid instead of throwing during UI rendering.
- Fixed the persistent Project Hub showing a project as still open after its editor exited. Showing or reactivating the
  Hub now refreshes OS lock-derived project status before presenting the project list.
- New projects now persist the Bundled .NET 10 SDK selection, and development or packaged editors discover that SDK
  from their own installation ancestry even when the Hub launches them with the game project as the working directory.
- Starter scenes now pitch their Directional Light down toward the scene instead of casting horizontal, extremely long
  realtime shadows across the ground.

- Added linked Kéire application artwork for Windows editor, Hub, and runtime executables. Desktop player builds now
  generate fallback artwork when no icon is selected and embed the selected or generated multi-resolution icon into
  Windows player executables while retaining the existing Linux and macOS platform assets.
- Fixed duplicate and stale Project Hub tray entries by enforcing one Hub process per executable installation and
  closing surviving tray handles during window-system shutdown. Secondary editor and Build Support launches now
  activate the existing Hub, and one **Show Hub** action restores and focuses it after pending native events settle.
- Added production desktop player builds with persistent player settings and Build Profiles; Windows, Linux, and macOS
  x86_64/ARM64 Build Support packages; isolated managed-build/cook/assembly/signing execution; transactional output under
  each project's `Build` directory; packaged-runtime discovery; target branding; Hub import/repair/removal; editor
  build, run, cancel, reveal, dirty-state, and missing-support workflows; HTTPS release-catalog download with verified
  archive installation and offline import; release scripts; and CLI automation/status documents.
- Fixed packaged players presenting their offscreen game surface through Dear ImGui's fallback debug window. Runtime
  scenes now present directly to the native swapchain and authored Game UI is composited by a dedicated SDL_GPU path;
  standalone players no longer initialize, frame, dock, or submit Dear ImGui. Windows packages are patched to the GUI
  subsystem so launching a game does not allocate a console, and platform icon settings now use searchable Texture2D
  asset pickers instead of raw asset-ID text fields.
- Fixed standalone Game UI buttons stopping native click dispatch after the first frame because the player did not
  publish its elapsed frame clock to managed scripts. Players now provide the complete managed time contract and apply
  managed cursor visibility and relative-lock requests to the native game window.
- Fixed standalone scene cooking omitting managed `AssetReference<T>` values whose legacy serialized field metadata did
  not retain its managed type. Scene imports now validate projected references against the project asset index, so UI
  sounds and other managed-only asset dependencies are included in packaged players.
- Fixed quick mouse clicks disappearing from gameplay input when button-down and button-up events arrived during the
  same rendered frame. Action snapshots now retain both transitions, allowing standalone `Input.Pressed` actions such
  as Fire to observe the click while keeping the final held state released.
- Fixed standalone mouse actions being filtered when a connected controller made `Gamepad` the player's initial
  control scheme. Unlocked users now switch between gamepad and keyboard/mouse binding groups from the device that is
  actively producing input, so left-click Fire works with controllers or virtual gamepads connected.
- Fixed standalone managed VFX handles inheriting no-op service defaults even though Play Mode forwarded those calls
  to the live scene. Packaged players now route play, stop, pause, alive-state, event, and parameter operations through
  their `SceneRuntimeSession`, restoring input-driven effects such as the FPS showcase weapon fire.

- Hardened managed runtime validation by giving every integration-test process isolated assembly directories, and
  corrected the pinned Coral host patch so the configured bundled .NET root is applied before HostFXR discovery. The
  patched Coral native host now also builds warning-clean under MSVC with bounded string copying and a portable host
  sentinel. Static analysis now uses a version-stable LLVM 18 gate with documented exclusions for checks that cannot
  model the repository's lifecycle and test-macro contracts reliably. Completed the local Release package gate through
  archive extraction and direct, managed, and source-module CMake consumer builds, and recorded the post-remediation
  evidence separately from the unavailable hosted Actions matrix.

- Hardened the architecture foundation after the production-readiness audit: job dependencies now register atomically
  against completion and shutdown; replay enforces decode/rewind budgets and preserves explicit failure state; project
  upgrades use canonical link-safe paths and phase-complete crash recovery; module importers reach the editor and
  pre-1.0 caret ranges follow SemVer; streaming controls/statistics are synchronized with accurate completion latency;
  and managed-job terminal records are reclaimed under capacity pressure with executable managed state tests. Repaired
  the primary CI workflow, expanded recursive first-party format/tidy/Python gates, added workflow parsing and UTF-8
  integrity checks, enforced ratcheting non-growth budgets for oversized legacy source units, and removed duplicate
  Visual Studio external-warning flags and managed-proxy intermediate collisions from generated builds.
- Added the integrated architecture foundation: an application-owned dependency/work-stealing job system and managed
  jobs bridge; hierarchical memory tracking and arenas; string interning, generational handles, canonical asset-path
  indexing, and documented structured diagnostics; explicit budgeted streaming and fence-safe retirement; immutable
  frame-graph snapshots with editor visualization/export; fixed-tick deterministic replay/checkpoints and runtime CLI;
  schema-v2 transactional project upgrades; and dependency-resolved source modules shared by every host and packaged
  with native/managed SDK examples. Existing whole-asset loads, stable IDs, frame-graph compilation, and GPU retirement
  contracts remain source-compatible. Catalog schema 3 adds semantic texture-mip, mesh-LOD, audio-page, and animation-
  window layouts with monolithic fallback; replay restoration is transactional and captures physics, animation,
  managed behaviours, logical audio/UI presentation, and canonical CPU VFX state; application memory domains now
  account authoritative asset, transient renderer, VFX GPU, and fence-retired bytes; and editor/tool asset cooking uses
  its injected process scheduler instead of constructing a pool for each cook.
- Made Material Graph iteration non-blocking: node-position edits now stay in editor metadata and never rebuild runtime
  materials, exposed parameter values publish to the scene immediately without shader regeneration, and CPU preview
  rendering is cancelable, progressive, and coalesced off the UI thread with allocation-free per-sample graph caches.
  Live structural shader builds
  now emit only the current host's runtime format plus SPIR-V reflection data while normal asset imports retain the full
  cross-platform DXIL/SPIR-V/MSL set.
- Added production spatial lighting: oriented reflection probes with box projection and two-probe blending, static
  lightmaps, SH9 light-probe volumes, emissive-to-GI, eight-channel mixed shadow masks, realtime/baked/mixed lights,
  deterministic point/spot shadow-atlas allocation, contact-shadow refinement, packed light cookies, and an atomic
  digest-verified offline lighting cache shared by the editor worker and `bake-lighting` CLI.
- Added production Material Graph authoring on the shared VFX node-canvas foundation: Surface/Transparent/Decal/Unlit
  outputs, PBR lowering, texture and UV operations, normal/detail/parallax/emission inputs, typed parameters, bounded
  keywords and deterministic variants, live shaded sphere/plane/cube/custom-mesh previews with last-good retention,
  generated diagnostics, confined recursive custom includes, material-instance resolution/baking, source/cooked asset
  registration, and editor create/open/save with
  asset-ID-owned generated HLSL/manifests. Generated output is validated by the production DXIL/SPIR-V/MSL compiler and
  fixed resource-binding reflection contract.
- Expanded Material Graph into a layered production surface workflow with clear coat, sheen, dielectric specular,
  Fresnel, world/vertex inputs, UV rotation, procedural noise, remap/shaping/vector math, searchable categorized creation,
  typed pin defaults, texture asset/semantic editing, node duplication, adaptive exposure/environment/rotation previews,
  reachability and cost diagnostics, strict disconnected-node validation, and nine progressively advanced Sandbox graphs.
  Corrected generated alpha clipping to consume the renderer's alpha-mode/cutoff ABI in the declared order.
- Added Material Graph schema v2 and a stable 100-plus-node catalog with stage metadata, deterministic schema-v1 pin
  migration, endpoint-aware multi-output lowering, background debounced compilation, and stale-result rejection. Added
  typed Material Attributes plus Make/Break/Blend operations; composable Standard Surface, Clear Coat, Sheen, Subsurface,
  and Transmission BSDF nodes; Hair and Eye outputs; anisotropy, thickness, IOR, refraction, subsurface, world-position
  offset, and pixel-depth offset. Generated shaders now evaluate Forward+ lights, shadows, image-based lighting, layered
  surface lobes, and vertex/pixel displacement through the fixed renderer ABI. New Sandbox graphs cover anisotropic
  metal, transmission glass, procedural vertex displacement, and a layered holographic surface.
- Material Graph instances now resolve their bounded graph/instance parent chain during import and publish a stable
  ordinary runtime-material subasset with complete shader, texture, and ancestry dependencies. Instances can be dropped
  on rendered entities or selected in Mesh Renderer slots through the same renderer-safe aliasing used by graph assets.
- Fixed Material Graph Forward+ buffer registers to follow the graph's actual sampler range instead of assuming the
  spatial-lighting ABI's fixed sixteen samplers. The dense HLSL/DXIL resource layout now matches SDL's D3D12 root
  signature, preventing `CreateGraphicsPipelineState` from rejecting generated graph shaders with `E_INVALIDARG`;
  importer version 11 refreshes every affected generated shader.
- Fixed procedural vertex-displacement graphs declaring a phantom vertex material buffer when their offset expression
  only used mesh data and constants. Graphs that do use exposed vertex parameters now receive a dedicated dense
  `space1` material buffer; importer version 12 refreshes both vertex-layout cases.
- Fixed Material Graph scene integration: imports now publish deterministic compiled-shader and default runtime-material
  subassets, graph assets can be dropped onto rendered entities or into Inspector material slots, and assignments retain
  renderer-safe `MaterialAsset` IDs. Built-in cube renderers now expose their default material slot and appear explicitly
  in mesh pickers. Material Graph canvases honor scalar broadcast and Color/Vector4 coercion without rejecting valid
  saved cables while opening a graph. Graph drops now wait for an uncataloged runtime material to compile and remount
  before changing the renderer, preventing transient missing assets from replacing the previous material with pink.
  Generated graph shaders now retain the complete fixed pixel-interpolator ABI, preventing Direct3D 12 from rejecting
  simpler graphs whose optimized DXIL previously exposed sparse stage inputs; importer version 4 forces safe reimport.
- Fixed Material Graph visual authoring parity: the live mesh preview now evaluates built-in graph nodes per sample,
  the shared node canvas clips all graph drawing and drag feedback to its viewport, and generated procedural noise uses
  smooth four-octave value noise instead of a blocky nearest-cell octave. Retuned the Sandbox Procedural Emissive graph
  for finer, controlled energy detail on scaled built-in meshes; importer version 5 refreshes generated shaders.
- Fixed Material Graph scene iteration and model drops: valid parameter-default edits now publish the stable generated
  material as an immediate development revision, Save targets the parent graph and every generated runtime subasset for
  compilation/hot reload, viewport drops ray-test imported mesh bounds, and transform-only model roots apply slot zero
  across rendered descendants. Graph defaults now participate in instance resolution before overrides; importer version
  6 refreshes generated materials. Corrected the Sandbox textured pyramid's inverted OBJ winding so back-face culling no
  longer exposes interior faces under PBR graph materials.
- Material Graph scene iteration now coalesces continuous edits on a 75 ms live interval. Exposed-value changes take a
  material-only fast path, while edits that change generated code compile off the UI thread and publish every in-memory
  shader revision before the material revision, so assigned scene renderers no longer retain stale shader bytecode.
  Generated custom-node includes are emitted relative to their confined include root, and the transmission-glass sample
  exposes tint, opacity, transmission, IOR, refraction, and thickness as instant runtime parameters; importer version 13
  refreshes generated shader variants.
- Fixed asset scans racing atomic source writes: engine temporary files and editor backups are no longer indexed or
  assigned persistent metadata. Material Graph Save now stages its complete generated directory outside the asset root,
  preserves metadata for retained variants, removes stale variant sidecars, and restores the previous directory if the
  canonical graph source cannot be published. Ninja prebuild and prelink commands now create their declared outputs on
  Windows, restoring no-op incremental builds instead of recompiling KeireCore on every target invocation.
- Completed an animation production pass with import-time key compression presets and measured errors, retarget mapping
  diagnostics, state-machine subgraphs, live transition visualization, transient preview playback, immutable pose and
  trajectory debugging, state-machine profiling, serialized foot-grounding settings, transactional ground adaptation,
  and interruptible animation/ragdoll pose blending. Existing blend trees, override/additive layers, and avatar masks
  now participate in the same preview and diagnostic workflow.
- Fixed GPU-skinned meshes exploding during Play Mode and animation preview after the mesh vertex format gained a
  second UV channel. The compute shader now preserves the full six-lane vertex layout, with a regression check tying
  its storage declaration to the renderer ABI.
- Added global image-based lighting for opted-in PBR materials: deterministic second-order diffuse irradiance baking,
  RGBE-aware HDR radiance mip chains, roughness-filtered specular environment sampling, an engine-owned split-sum BRDF
  integration LUT, revision-cached environment resources, shared sky/lighting rotation, and independent diffuse and
  specular intensity controls. The fixed custom-shader ABI exposes this through `usesImageBasedLighting` without
  changing shaders that do not opt in.
- Fixed VFX graph readability at reduced zoom with deterministic detail levels: connection and Block labels collapse
  first, pin labels appear only when their scaled rows can contain them, and compact node titles remain clipped to the
  card. Corrected schema-4 Operator, Attribute, and Subgraph cards that were incorrectly labelled `Unsupported` by a
  stale editor-only node-kind switch; Split Vector 2 and its sibling Split nodes now show their actual CPU + GPU status.
- Promoted 42 additional Unity 6.3 VFX rows to executable CPU/GPU nodes: 16 live particle/strip Attribute reads plus
  Ratio Over Strip, 11 typed Inline values, Epsilon/Pi constants, four coordinate conversions, 2D/3D rotation, and
  deterministic fixed-3D Value/Perlin/Cellular noise and curl. Expanded the expression ABI from four to eight inputs
  and from 256 to 512 cooked sources across compiler folding, schema validation, CPU execution, GPU packing, renderer
  validation, and HLSL. Analytic noise gradients replace repeated finite-difference samples for materially cheaper
  curl evaluation and GPU pipeline creation. The parity ledger now records 125 enabled equivalents and 153 disabled
  rows.
- Promoted 21 Unity 6.3 utility Operators from disabled catalog entries to executable CPU/GPU graph nodes: normalized
  age, frame/system identity, inverse interpolation, modulo, discretization, reciprocal/one-minus, NAND/NOR, 64-bit
  bitwise operations, squared vector metrics, luma, and HSV/RGB conversion. The compiler, constant folder, runtime
  evaluator, packed GPU ABI, shader interpreter, renderer validation, descriptor catalog, parity manifest, generated
  capabilities, and focused edge-case tests now share the same opcode and signature contract.
- Fixed the intermittent first-Play FPS freeze by making Play entry wait for the first valid managed assembly
  generation before cloning the scene or capturing input. The queued state is visible and cancelable from the toolbar,
  native-only projects remain immediate, failed builds reject Play with their diagnostic, and an active last-good
  generation remains playable during later rebuilds. Added readiness-policy tests and a separate Kéire VFX
  beyond-parity production roadmap.
- Closed the documented managed Behaviour contract gaps: `Enabled` now synchronizes native component state,
  `RequireComponent` dependencies are reflected and enforced transactionally with cycle validation, and Animator IK
  dispatch reaches managed `OnAnimatorIk` before pose IK application. Added native/managed lifecycle and rollback tests.
- Completed the entity-layer schema-4 release surface with checked-in schema 1-4 migration fixtures, canonical
  round-trip coverage, starter-scene schema 4 output, and layer-aware low-level and managed packaged consumers.
- Added manifest-driven VFX production slices, an offline manifest reconciler, and generated capability documentation.
  Release validation now rejects stale capability output, enabled implementations without a tested slice, and catalog
  mappings that drift from runtime descriptors.
- Added renderer and GPU-VFX fence-completion latency diagnostics plus reference-hardware performance profiles and a
  capture validator that recomputes percentiles and enforces metadata, history, timestamp, renderer, and VFX budgets.
  Fence completion remains explicitly distinct from true GPU timestamps and cannot satisfy timestamp-required gates.
- Added first-class Unity-style entity layers. The Inspector entity header now exposes the project's 32 named layers,
  supports mixed multi-selection, and records one undoable edit. Scene schema v4 persists layer indices and migrates
  legacy Collider/Character Controller bit layers; duplication, prefab variants/overrides, Play Mode Changes, physics,
  the native `Entity` API, and managed `Entity.Layer` all share the same validated contract.
- Removed the first-Play GPU VFX pipeline hitch. The editor now requests all schema-4 compute pipelines through a
  low-priority background warmup while the workspace remains responsive; first-use compilation remains a compatible
  fallback for runtime clients that do not prewarm. Renderer statistics and profiler captures expose warmup pending,
  ready, and elapsed-time values, and Play startup logs now split scene clone, physics, scripts, VFX, and presentation
  costs so transition stalls are attributable.
- Optimized GPU VFX scheduling from profiler evidence. The renderer now sizes each physical particle pool from the
  summed capacities of its live systems with power-of-two growth instead of allocating and scanning the scene's fixed
  one-million-particle ceiling. Spawn Initialize/Output and strip linking consume a compact new-particle list, and
  emitters without new spawns reuse their first post-simulation render compaction. Added physical particle-capacity,
  compute-thread-group, and oldest-frame fence-wait statistics so GPU pressure and presentation pacing are separately
  visible in profiler captures. A follow-up scheduling pass now drives Update/Output from each emitter's persistent
  compacted particle view and bounds global compaction by the summed active-system capacity, reducing the six-system
  sandbox's normal no-spawn workload from 1,160 to approximately 250 compute groups without changing its 26 ordered
  dispatches.
- Reworked the FPS VFX showcase into the event-driven VX-9 Plasma Lance. Holding Fire now sends rate-limited
  `PlasmaFire` events that launch a tight, high-velocity plasma stream from the camera muzzle; releasing Fire stops new
  emission while live bolts finish naturally. Added a generated alpha plasma-core texture, transparent emissive
  material, heat-responsive Blackboard color/size control, and GPU-ready event graph content.
- Fixed GPU VFX payload validation rejecting the supported Vector2, Vector4, and Color Combine/Split signatures before
  dispatch. Renderer-side validation now mirrors the shader interpreter's typed component rules and accepts every
  production vector width while retaining output-index and SSA source checks.
- Upgraded the sandbox `VfxEffect` into the camera-mounted Aether Weapon Core showcase. Its schema-4 graph uses an
  exposed color and a Vector2 size range routed through Split Vector 2 and Random, while a dedicated FPS behaviour
  drives cyan idle energy, violet sprint response, and orange fire pulses through stable-ID managed Blackboard
  overrides. Added a visual walkthrough plus native authored-default/override lookup and managed runtime-access
  guidance.
- Added production CPU/GPU Combine and Split Operators for Vector2, Vector4, and Color alongside the existing Vector3
  pair. Each node exposes canonical typed component pins, searchable float/vector/RGBA aliases, deterministic constant
  folding, packed GPU interpreter execution, and type-filtered editor catalog metadata so Vector2 cables resolve to a
  supported Split Vector 2 node instead of the existing Vector3-only Split.
- Fixed Inspector scale editing so clearing an axis while entering a replacement value remains an uncommitted draft
  instead of publishing a singular world transform and crashing matrix inversion. Transform setters, component/scene
  loading, prefab overrides, and gizmo edits now share an explicit finite non-zero scale invariant; rejected edits leave
  the previous transform unchanged, while small valid invertible matrices remain supported.
- Fixed schema-4 VFX assets authored before the Shape Volume and Renderer Material inputs were added. Their version-one
  module layouts now upgrade deterministically in memory to version two, preserving every existing stable pin and cable
  while allocating stable IDs for only the new resource inputs. Character Controller movement now also filters
  sub-resolution smoothing tails before capsule casts, preventing valid FPS movement from faulting Play Mode after an
  input transition.
- Completed the next schema-4 VFX backend tranche across CPU, GPU, editor, assets, scenes, and managed scripting.
  GPU simulation now samples imported mesh surfaces and cooked sparse-density `.keirevfxvolume` assets through bounded
  weighted resource tables, performs swept scene-depth collision, evaluates every numeric Block input through the
  generic typed property/register ABI, and renders compatible composed Mesh materials. Sprite, Ribbon, and Volumetric
  output now compose material Tint, primary texture, alpha mode, and alpha cutoff on both backends. Repeated Blocks keep independent
  execution identities instead of aliasing one fixed payload. Effects can own multiple transactional systems behind one
  generation-safe root handle; named Event contexts route from native scene/world APIs and C#; Particle Strip identity
  participates in deterministic random sampling; and Sprite, Mesh, adjacency-connected Ribbon, and analytic Volumetric
  outputs run on CPU and GPU. GPU strip links are generation- and sequence-qualified so dead/reused pool slots break a
  ribbon safely. Added per-output statistics, resource cooking/dependency coverage, shader-layout/readback probes,
  scene-event tests, and explicit diagnostics for the remaining host-only tiers.
  Packed execution data now stays within SDL's eight-readonly-buffer compute limit. Update/Output and
  Spawn/Initialize/initial-Output expression evaluation use separate ordered kernels to avoid D3D12 register-pressure
  hangs while preserving deterministic random state and transactional particle publication. Compute-pipeline creation rolls back
  transactionally with entry-point diagnostics, and completed-frame retirement no longer recursively polls delayed GPU
  query publication. D3D12/Vulkan AddressSanitizer readback tests cover weighted Mesh/Volume spawning, sampled-depth
  collision, composed particle materials, strips/Ribbons, and Volumetric output.
- Expanded implemented VFX CPU/GPU parity: every executable core value Operator now has validated packed GPU lowering;
  nonlinear size curves and color gradients publish deterministic 64-sample GPU lookup tables; custom Sprite textures
  render on CPU and GPU; and Mesh output now uses per-emitter GPU compaction, full Euler rotation, asset-backed indexed
  indirect draws, vertex tint, and ambient/directional lighting. GPU emitters own bounded output buffers and atomically
  enforce their authored capacity instead of competing without per-effect limits in the shared particle pool. Built-in
  VFX shaders are generated for DXIL, SPIR-V, and MSL with stage-correct resource spaces, and D3D12/Vulkan readback
  coverage exercises graph execution, CPU textured sprites, and GPU mesh particles.
- Fixed Play Mode becoming globally faulted when one VFX Emitter cannot compile or activate. A GPU-incompatible effect
  that has a valid CPU program now transactionally restarts the scene VFX world on CPU, while an effect invalid on both
  backends is disabled with a stable entity/effect diagnostic and automatically retried after an asset revision.
  Scripts, physics, rendering, and other valid VFX emitters continue in either case. GPU validation also no longer
  reports Sprite-only X/Y rotation restrictions for Mesh outputs.
- Hardened the editor-owned Play Mode Escape path against runtime faults and viewport routing failures by releasing
  any native cursor capture directly from its actual window state.
- Fixed Hierarchy multi-drag so dragging a selected row preserves and moves the complete ordered selection in one
  validated undoable transaction, with selected-descendant filtering, cycle-safe parenting, stable before/after
  reordering, multi-item previews, and explicit drop-zone feedback. Editor gameplay input now uses an explicitly paired
  keyboard/mouse user instead of competing with automatic device joining, validates the Player action map before Play,
  engages from runtime capture, and releases without cursor warping. Escape has an editor-owned safety release so a
  broken project input asset or script can never trap the cursor; the sandbox FPS uses action edges for deterministic
  capture toggling.
- Fixed Inspector entity renaming so an empty or over-limit in-progress text draft remains local to the field, displays
  validation feedback, and restores the previous name when editing ends instead of throwing through the editor frame.
- Upgraded Animator Controller authoring to the shared production node canvas with zoom/pan, Bezier transition cables,
  draggable state pins, gesture-level node movement, cable selection/deletion, right-click unlink and entry-state menus,
  live playback state styling, and clip drops at the pointer position. Added a native/managed Character Controller move
  and state API, capsule casts with debug traces and self-filtering, corrected total-height capsule geometry, bounded
  sweep/slide, walkable-slope grounding, skin padding, and stair stepping. KeireSandbox now contains a parented FPS
  player with sprint, gravity, buffered jump, coyote time, air control, and world-correct camera directions. CPU mesh
  VFX now enter the material-aware scene draw path, and two schema-4 examples ship with generated emissive glTF meshes
  and imported material subassets: Ember Shard Cyclone and Arcane Sigil Orbit.
- Introduced the schema-4 VFX foundation with stable descriptor-backed node IDs, typed properties and ranges, ordered
  Context Blocks, block-pin endpoints, deterministic SSA-style CPU value evaluation, core Range/Random/Remap and
  math/logic/vector Operators, explicit backend diagnostics, and in-memory schema 1-3 migration. Added transactional
  managed `VfxRange<T>` updates for exposed Play Mode parameters and a validated Unity 6.3 LTS parity manifest whose
  unfinished catalog rows remain disabled with actionable reasons. Canonical validation rejects stale disconnected
  Operators, unsafe dynamic Burst values, malformed range overrides, and strip-scoped Random outside Particle Strip
  systems.
  The graph Inspector now exposes descriptor-constrained Operator contexts, typed Operator/Block inline values, and
  editable Random, Compare, and Remap settings instead of requiring source-level asset edits.
  Added scalar trigonometric, power/root/logarithmic, rounding, interpolation, Negate, and Sign Operators with
  deterministic constant folding and zero containment for invalid or non-finite math domains.
  The offline parity tooling now preserves Unity's dynamic `<Attribute>` labels, enforces canonical UTF-8 JSON, and
  cross-checks every claimed implementation/support tier against the build-time runtime descriptor contract.
  Executable Operator results can now feed Portable Custom HLSL inputs through typed expression registers on CPU and
  the packed GPU value interpreter. Descriptor backend badges report CPU + GPU only where packed representation and
  backend semantics are validated; the complete executable core opcode interval now meets that contract, while
  incomplete Unity parity rows remain disabled. Uniform work may still be folded or hoisted, particle-varying Portable
  inputs execute in the shader, and generic numeric Runtime Block properties now use one reflected CPU/GPU property
  ABI. Dynamic Portable execution uses the 4,096-instruction compiler safety bound; the
  fixed eight-instruction/fifteen-operation snapshot arrays are compatibility mirrors only. GPU compilation also rejects
  unsupported backend-specific collision modes and packed program safety-limit overflows at the responsible Block
  instead of silently approximating or ignoring them. Point/Box/Sphere/Cone/Mesh/Volume initialization consumes exact
  authored GPU shape data, Sprite Z-rotation is sampled on GPU, and global spawn
  identity keeps random initialization stable across dispatch batching. Schema-4 graphs may schedule multiple same-kind
  compatibility Blocks; additive emission rates and duplicate per-particle Blocks execute independently on both
  backends. CPU/GPU activation and live parameter edits now transactionally revalidate resolved backend
  capabilities, including billboard X/Y rotation. A serialized schema-4
  compatibility mode keeps migrated schemas 1-3 and explicit Runtime Module conversions warning-compatible while new
  native graphs enforce unsupported capabilities as errors.
- Replaced the GPU VFX execution snapshot limits with validated dynamic expression, custom-instruction, particle-
  operation, resource, and attribute payloads. The renderer now caches immutable uploads by program hash, accounts for
  their real buffer sizes, broadcasts scalar Portable inputs by declared operand type, and executes live Force, Size,
  Color, and Renderer uniform edits without restarting particle state. The graph compiler now enforces descriptor
  backend tiers at the producing node. Cumulative spawn work survives skipped render snapshots; exact timing across
  multiple skipped simulation steps remains a documented queued-handoff milestone.
- Upgraded the VFX graph canvas to modern direct manipulation with a searchable, categorized right-click node palette,
  bidirectional typed-pin cable dragging, live green/amber/red validation feedback, atomic occupied-input replacement,
  cable/node/pin context menus, cable selection and inspection, right-click unlink, Delete/Escape shortcuts, and
  undoable gesture-level edits. Temporarily incomplete Graph drafts now retain the last valid frozen preview, show an
  actionable diagnostic, and block Save until reconnection or Undo restores a publishable graph; the Audio Mixer's
  existing pinless read-only routing view remains compatible.
- Made schema-v3 VFX graphs executable on CPU and GPU: typed `ParticleStream` cables now schedule stable-ID Module
  payload references, Blackboard Parameter nodes bind canonical module properties, and bounded Portable Custom HLSL
  lowers to the same verified instructions on both backends. Added explicit LegacyModules-to-Graph conversion,
  per-activation and live native parameter overrides, serialized scene-emitter overrides with Edit/Play
  synchronization, a typed exposed-parameter scene inspector with reset and stale-override cleanup, deterministic
  override serialization, reload compatibility diagnostics, and cable-ordered GPU Module/Custom execution in each
  emitter's normal spawn and simulation dispatches. Collision nodes now preserve their cable-defined integration point,
  and immutable GPU snapshot replay cannot consume the same simulation step twice.
- Made the normal Windows, Linux, and macOS test entrypoints compile the complete client, repaired the relocated
  Animator Controller regression fixture, and added cross-platform checks for both safeguards.
- Replaced undocumented sandbox music with small deterministic repository-owned PCM test tones while preserving the
  scene's audio asset identities, and documented their reproducible generator and licensing provenance.
- Made AssetTool worker import deadlines configurable, separated managed SDK selection and discovery from the
  managed runtime implementation, and added focused configuration, resolution, and script regression coverage.
- Fixed edit-mode VFX duplication when an open asset draft and a matching scene emitter were previewed together. The
  live draft now replaces one selected/deterministic matching scene handle at its world position and rotation, while
  unrelated emitters remain visible. Moving a World-space emitter restarts only its editor preview so old particle
  history cannot remain beside the gizmo. GPU Local-space particles now follow emitter transforms, and
  generation-qualified retirement and per-handle simulation revisions keep stop, restart, and incompatible reload
  from resetting unrelated GPU effects; `Clear` remains world-wide. Added an extensive visual, editor, C++, and C# VFX
  guide plus public API documentation.
- Fixed edit-mode VFX preview flicker and disappearing effects by wiring `Preview In Edit Mode` to an editor-owned
  world that synchronizes enabled scene emitters by entity, effect revision, seed, simulation speed, and world
  position/rotation. Asset-authoring and scene previews now coexist, pause independently, and stop deterministically
  across panel, scene, and Play Mode transitions.
- Rebuilt the VFX Effect editor as a context-colored node graph with systems, draggable cards, typed pins and links,
  blackboard properties, runtime-module context summaries, focused inspectors, truthful compile results, and
  restart/pause/loop/backend/speed/statistics preview controls.
- Added transactional, undoable VFX graph mutations with stable-ID enforcement and incident-link cleanup, connected
  starter graphs for new effects, and correct non-looping GPU effect drainage.
- Added a comprehensive C# scripting documentation section covering managed assemblies, lifecycle and reload,
  serialization, entities and components, assets and ScriptableObjects, gameplay services, audio, animation, UI and
  events, async diagnostics, troubleshooting, and managed API lookup.
- Added typed managed audio mixer, animation clip, and Animator Controller references; stateful Audio Source
  play/pause/resume/seek/status and live properties; and Animator play/cross-fade/stop/pause/speed/state controls.
- Fixed paused audio voices retaining an audible virtualization slot and muting lower-priority voices that were still
  actively playing.
- Made `AudioSourceHandle.Time` assignments report native seek rejection instead of silently discarding it.
- Prevented invalid `AudioSourceHandle.Clip` assignments from creating missing components, matched managed bus-name
  validation to the native 128-byte UTF-8 limit, and validated Audio Source volume and pitch before native mutation.
- Routed device and headless voices through scoped, revisioned Audio Mixer bus snapshots so stable bus IDs survive
  renames, authored fader/mute/solo hierarchies hot-apply, malformed replacements retain the last valid routing, and
  cleared presentations cannot leak stale registrations. Muted routes now yield scarce audible voice slots.
- Ensured repository build launchers and generated Ninja, Make, Visual Studio, and Xcode projects refresh the managed
  runtime API before native compilation, preventing direct builds and managed integration tests from consuming a
  stale `Keire.Managed.dll`.
- Made AssetTool import and cook use the packaged private asset worker, and taught cooking to restore validated
  dependency-free outputs from the persistent cache, so FFmpeg-backed audio sources cook without exposing the private
  codec backend through public engine or SDK boundaries. Linux and macOS worker codec lookup now uses their respective
  relocatable `$ORIGIN` and `
                @loader_path`/`@rpath` conventions.
- Bounded transactional cook/staging token lengths and made Windows native renames extended-path aware, preventing
  content-addressed pack publication from failing when package staging paths cross the legacy 260-character limit.
- Removed a Windows package-smoke race by explicitly owning, waiting, reading, and disposing the short-lived
  invalid-option client process instead of querying a `Start -
        Process` object after the client had already exited.
- Removed three orphaned Sandbox metadata sidecars left behind by temporary and backup source files.
- Matched SDL's GPU presentation queue to Kéire's configured frames-in-flight policy and split command-recording
  statistics into skinning, VFX, draw preparation, shadows, Forward+, scene, depth, tone mapping, and residual overhead.
- Recorded steady-state skinning, instancing, and Forward+ uploads directly into the frame command buffer, removing the
  extra GPU submission, and prepared visible draw batches before render passes to keep allocation work out of scene
  recording.
- Removed Project-panel steady-state asset snapshot copies, thumbnail digest rebuilds, and recursive filesystem walks.
  Asset publications now advance a browser revision, while the folder tree uses a transactional cached snapshot with a
  one-second external-folder fallback refresh.
- Restored independent Scene and Game viewport camera ownership during Play Mode: Scene keeps its persistent editor
  camera and navigation, while managed gameplay input and cursor capture require the focused, hovered Game viewport.
- Fixed animated GPU skinning corruption by using renderer-private, 16-byte-aligned vertex storage layouts across
  uploads, compute deformation, shadows, fallback rendering, and material pipelines.
- Fixed animation controllers retaining obsolete generated clip handles after model or animation-source reimport.
  Catalog publication now reloads changed subassets and loaded reverse dependents, while live Animators invalidate their
  dependency caches once per controller revision so regenerated clips resume without restarting the editor.
- Fixed GPU skeletal skinning corruption by defining the palette as explicit matrix columns and rejecting out-of-range
  bone influences before palette access.
- Fixed animation previews waiting indefinitely when a controller references a missing clip, skeleton, or avatar mask;
  missing dependencies now report an actionable reassignment or reimport diagnostic.
- Fixed imported skeletal deformation by preserving Assimp's mesh-space inverse bind matrices and preferring exact bone
  names when retargeting compatible animation and model skeletons.
- Validated and enabled D3D12 compute skinning with strict buffer-layout and palette guards, avoiding the per-frame
  Debug-build CPU deformation cost while retaining bounded CPU fallback behavior.
- Cached validated skin influences by asset revision and retained fence-safe per-entity deformation buffers across
  frames, removing full-mesh influence uploads and GPU buffer allocation from steady-state animation playback.
- Fixed CPU-skinned material draws by retaining render-thread upload transfers through GPU submission and binding the
  single-instance storage record required by instancing-capable material shaders.
- Fixed Mixamo arm offsets when retargeting Assimp FBX rotation-helper tracks onto a separately imported model.

- Rebuilt imported inverse bind poses from the normalized runtime hierarchy and bounded retargeted scale ratios,
  preventing animated FBX unit conversions from expanding skinned meshes into screen-filling triangles.
- Fixed Animator playback when a source-animation skeleton was assigned beside a different target skinned mesh, and
  added live state progress plus transient Edit Mode preview controls to the Animator Controller.
- Fixed embedded skeletal imports by normalizing merged mesh vertices and inverse bind poses into one model space,
  preventing mesh-node transforms from producing explosive deformation when an Animator skin is assigned.
- Project open now invalidates development catalogs produced by older importer versions instead of silently retaining
  stale model, animation, audio, or other derived asset data.

- Fixed Animator asset fields so generated skeleton and skinned-mesh subassets can be selected directly from imported
  models, added cached bind-pose-aware retargeting for separately imported clips, rejected incompatible GPU skin data,
  and reduced audio import publication time with bounded media probing and parallel fast development cooking.

- Fixed animated-model and Animation Source drops in Animator Controllers by expanding stable generated clip subassets,
  made FFmpeg-backed imports default to fast lossless PCM streaming with bounded FLAC fallback, and made audio reimport
  stop and replay active voices instead of terminating the editor on clip replacement.
- Added schema-v2 VFX graph data, deterministic v1 publication migration, GPU compute simulation with persistent
  particle/free/alive/counter buffers, indirect sprite rendering, GPU runtime statistics, deterministic CPU fallback,
  expanded emitter authoring, managed generation-safe playback controls, and graph/fallback compilation actions.
- Added transactional importer-selected primary asset types and `AnimationSourceAsset`, allowing animation-only
  FBX/glTF containers to publish stable skeleton, rig, and clip subassets without entering mesh vertex validation.
- Pinned the signed FFmpeg `n8.1.2` tag and peeled source commit as a vendor submodule, added selectable audio-stream
  import settings, source-built worker-only shared libraries, custom in-memory AVIO, fastest-lossless conversion, and
  validated importer-cache restoration for repeated media imports.
- Added native MP3 audio import plus FFmpeg-backed AAC, Opus, WMA, AIFF, WebM, MP4, MKV, MOV, M4A, and broad
  codec/container routing with bounded lossless transcoding and actionable dependency diagnostics.
- Animation Source FBX/glTF/GLB imports now derive skeletons from animated node hierarchies when skin weights are
  absent, and the sandbox no longer retains orphaned mesh or collision-mesh references.
- Made generated asset creation use direct worker publication so VFX and other authored assets appear promptly, and
  asset operations now shut down before editor documents and the project database.
- Fixed explicit Project asset creation being rejected behind queued work and completed imports rejecting their own
  undo command while the asset worker was still publishing the result.
- Added a common lock control to every registered editor panel. Locked panels retain their placement, Inspector can pin
  either entities or assets, and Rigging Studio preserves or pins its model context while its own sections are used.
- Fixed interactive Physics Material and Animator Controller creation being discarded behind background asset work,
  allowed empty authoring layers, and repaired missing Character Controller runtime identities during scene migration.
- Accelerated generated-rig imports by reusing top-influence solver storage and publishing compact schema-v3 binary skin
  weights while retaining schema-v1/v2 decoding; Rigging Studio now recognizes current `Keire.Mesh` source records.
- Added persistent scene physics gizmos for colliders, triggers, character-controller capsules, rigid-body motion types,
  and runtime velocity vectors, with a toolbar toggle and versioned per-project editor settings.
- Fixed model and other external imports failing after successful cooking when the running Windows editor held the
  active runtime pack open. Development and cooked publications now install deterministic content-addressed immutable
  packs and atomically switch the catalog without renaming the live cache directory.
- Dropped model folders now import supported models and textures while skipping unrelated unsupported companion files;
  external identity metadata and symlinks remain rejected.
- Added a backend-owned skeletal skin cache with cross-platform SDL_GPU compute deformation, shadow/depth reuse,
  built-in-material fallback output, deterministic CPU linear-blend and dual-quaternion fallback, and focused
  deformation tests.
- Added versioned `.keirerig` semantic profiles, deterministic humanoid/biped/quadruped generation, marker overrides,
  four/eight-influence weight solving, baked semantic retargeting, and two-bone/FABRIK runtime solvers.
- Upgraded skinned-mesh assets to schema v2 with schema-v1 compatibility, explicit LBS/DQS mode, and durable
  per-vertex four/eight-influence data.
- Added explicit non-destructive model Rig Source/Profile/Influence/Skinning import controls that publish generated
  rig, skeleton, and skinned-mesh subassets while retaining embedded-rig and no-rig workflows.
- Added deterministic semantic inference for embedded Mixamo, Blender, Unreal, humanoid, biped, and quadruped
  skeletons, preserving unknown bones while publishing a retargetable rig subasset.
- Added a dockable Rigging Studio for atomic model reimport, semantic-map inspection, generated-asset navigation, and
  standalone `.keireanim` retarget baking.
- Added named runtime two-bone and FABRIK Animator IK, managed `Animator` bindings, standalone animation-clip import,
  and strict cooked-asset dependency/influence validation.
- Upgraded embedded model skinning to retain the selected four/eight influence limit and LBS/DQS method instead of
  truncating every imported vertex to the legacy four-weight schema.
- Added shared typed asset-document hosting with validation, atomic persistence, bounded undo/redo, revision-aware
  reload, live preview rollback, stable-ID graph authoring primitives, deterministic curves/gradients, and reusable
  scene shape handles.
- Added `.keiremixer` Audio Mixer assets with stable buses, effects, sends, snapshots, ducking rules, convolution
  dependencies, bounded meter snapshots, Audio Source schema-v2 migration, attenuation curves, reverb-zone authoring,
  default-mixer project settings, Project-window creation, and a dockable typed bus/routing/effect editor.
- Added scene-owned Jolt runtime synchronization, narrow-phase queries and deterministic contact phases, 32 named
  collision layers/matrix settings, physics materials, collider shape handles, character-controller and joint
  components, bounded debug snapshots/query traces, and cooked-runtime settings parity.
- Added `.keiredata` managed data assets with managed type discovery, deterministic Create menus, typed reflection
  inspectors, stable-field migration, deep supported-value cloning, identity-preserving hot reload, real cancellable
  async loads, transitive cook dependencies, and strict post-compilation semantic validation.
- Added `.keirevfx` modular CPU effects, generation-safe scene-owned pooling, deterministic rate/burst emission,
  shapes, forces, lifetime curves, color gradients, collision fallback diagnostics, bounded debug/render snapshots,
  transparent render packets, resolved-depth preservation, capability reporting, Project-window creation, and a
  dockable typed module editor.
- Hardened the new authoring slices with lifetime-safe document undo commands, eager physics before managed Play
  callbacks, transactional managed Audio Source edits, identity-preserving managed-data reload, empty Animator
  fallbacks, nullable/character managed values, validated curve interpolation, and back-to-front transparent ordering.

- Added a dockable Animator Controller workflow with Project-window creation/opening, atomic validated saves,
  undo/redo, typed parameters, layers, clip drag-and-drop, state/transition authoring, blend-tree editing, persistent
  graph layout, and managed runtime parameter/layer queries.

- Fixed Play Mode Inspector edits for managed fields with legacy name-only state so live Behaviours receive new values
  immediately while state capture canonicalizes their stable field identities.

- Moved development asset change discovery to a nonblocking database-owned monitor, cached unchanged Forward+ data,
  added managed callback/interop metrics, O(N) hierarchy traversal, Perfetto trace export, thread timelines, explicit
  GPU timing capabilities, and focused performance regression coverage.

- Replaced the editor's 100 ms full asset metadata rescan with a signature-only idle poll, preserved case-only rename
  detection, deferred metadata parsing to real changes, and throttled visible Profiler presentation refreshes to 10 Hz.

- Added bounded per-Behaviour lifecycle timing to managed scripting diagnostics and compact default row budgets to the
  Profiler's callback, hotspot, and counter tables.

- Added a sparse one-light Forward+ path, deduplicated managed native UI click draining per frame, and expanded
  retained Profiler history/export data to every CPU category for tail-spike attribution.

- Stabilized live Profiler tree identities and replaced the Hierarchy panel's full scene serialization with a
  component-free hierarchy snapshot.

- Removed Forward+ per-tile heap allocations, serialized Inspector components once per draw, and split core editor
  panels into independent Hierarchy, Inspector, and Project profiler spans.

- Cached scene lifecycle execution order, skipped empty physics worlds and idle render surfaces, suppressed empty
  managed fixed/late callbacks, and removed per-frame Profiler report construction and full-rate history analysis.

- Improved editor frame pacing with mailbox presentation, removed duplicate Play Mode UI/audio synchronization,
  eliminated native profiling-scope and render-preparation hot-path allocations, and added detailed frame-stage,
  swapchain-wait, command-recording, UI-recording, and GPU-submission diagnostics.

- Expanded profiling with bounded frame history, percentile and stutter analysis, reusable recorder storage, managed
  samples/counters, a modern copyable diagnostics workspace, a Scene/Game FPS overlay, and active-viewport cursor
  centering on capture release.

- Reduced managed-event editor and Play Mode overhead by sharing immutable callback metadata, caching persistent
  callback resolution, caching Inspector entity lists, and removing per-frame cursor bridge queries.

- Added serialized managed `KeireEvent` fields with persistent Inspector listeners and scoped cursor ownership so
  gameplay capture yields to modal runtime UI without scripts fighting over cursor state.

- Preserve managed Entity/component references when cloning an editing scene into Play Mode, and route runtime UI
  pointer clicks through both Scene and Game viewports.
- Added shared managed scene world identities, action edge-state APIs, scene-safe clone/destroy calls, collider-backed
  managed raycasts, and serializable Collider/Rigid Body components.
- Added managed ScriptableObject foundations and a production-oriented weapon framework with physical magazines,
  chambered rounds, tube-fed shells, rifle/pistol/shotgun fire modes, deterministic ballistics, penetration,
  ricochet, layered damage contracts, recoil springs, and a sandbox loadout. Removed the legacy `Testing` script.

- Added managed cursor visibility and relative-lock APIs, and isolated Play Mode input from editor input contexts.
- Added Escape cursor release and runtime-owned Player-map lookup so gameplay input is independent of editor documents.
- Rebind scene component placeholders after managed assemblies load so scripts present in startup scenes execute in Play.
- Route the Scene viewport through the runtime primary camera during Play and suspend editor-camera navigation.
- Make input-map enabling idempotent so gameplay actions remain readable after their first enabled frame.
- Give managed component instances unique world handles and make Play viewport Escape release the gameplay cursor.
- Correct first-person look direction and focus/capture the gameplay viewport without leaving editor controls hovered.
- Correct the first-person controller forward/backward movement direction.
- Make Escape toggle FPS cursor capture and smooth target camera rotation to remove raw mouse jitter.
- Redirect C# script creation from non-assembly folders into the nearest/default runtime `.keireasm` source root.
- Register `.cs` source assets with the editor, worker, and command-line databases so script creation publishes through
  the normal transactional text-asset pipeline.
- Create standalone C# scripts through the single-record asset transaction instead of running a full project import and
  catalog cook before returning.
- Generate source-checkout C# workspaces with `Keire.Managed.csproj` in the solution and direct gameplay project
  references, restoring Visual Studio semantic coloring, completion, and engine API navigation.
- Route that source-checkout reference through a generated .NET 8/C# 12 design-time facade so Visual Studio 2022 can
  load engine API symbols while Kéire runtime compilation remains on .NET 10/C# 14.
- Generate root gameplay workspace projects as .NET 8/C# 12 for Visual Studio 2022 design-time evaluation while keeping
  the separate internal gameplay compilation projects on .NET 10/C# 14.
- Keep common unused C# local and field diagnostics as visible warnings instead of promoting them to build-blocking
  errors, while retaining strict warnings-as-errors behavior for other managed diagnostics.
- Input Actions Listen now captures against the live in-memory document through a transient context, supporting any
  keyboard, mouse, or gamepad control for new and unsaved bindings.
- Script-only saves now use shorter source/debounce windows, skip asset catalog imports, and reuse the validated managed
  API assembly, substantially reducing save-to-reload latency.
- Console entries are selectable and support complete-entry clipboard copy through double-click or a context menu.
- Managed API reuse now uses a persistent source fingerprint and validated cache, preventing stale prebuilt engine
  assemblies from removing current `Cursor`, `Debug`, and `Input` APIs while retaining fast gameplay-only rebuilds.
- Input Actions hides Listen while an interactive capture is active, preventing duplicate-operation errors.
- Correct the Sandbox weapon test's inverted empty-magazine predicate, derive its fire/reload capability flags from
  current ammo state, and transfer only the available magazine space from reserve ammo during reload.

- Added .NET 10/C# 14 managed assembly schema v2, immutable incremental build generations, automatic debounced script
  compilation, transactional field-preserving Play Mode reload, per-Behaviour exception quarantine, managed component
  state persistence, lifetime-bound simulation-thread async continuations, and expanded Entity/component/transform APIs.
- Added Unity-style managed script attachment through searchable Add Component entries and `.cs` asset drops on the
  Inspector or Hierarchy, including generated stable component IDs and undo-aware scene mutations.
- Managed builds now default to the engine-bundled .NET 10 SDK, with persisted Bundled, System PATH, and Custom SDK
  choices in Project Settings; script drops also recognize metadata-backed C# asset records.
- Source-checkout editor launches now prefer the newest managed API artifact and reject stale managed state contracts
  before reload, preventing repeated Coral member-lookup failures during script serialization.
- Loaded managed Behaviours now appear under Scripts in Add Component, and supported public or `[SerializeField]`
  fields use the standard Inspector property, undo, scene, and prefab-override pipeline.
- Added Unity-style managed `Debug.Log`, warning, error, exception, and assertion output routed to the editor Console,
  plus an attachable serialized `FirstPersonCamera` sample driven by the default Move and Look input actions.
- Managed generation IDs now resume from persisted state across editor launches, preventing failed or cancelled startup
  builds from deleting the last-good gameplay DLL directory and emptying script entries from Add Component.
- Added Build > Build Scripts, the Ctrl+Shift+B shortcut, startup script compilation, and one-shot managed compiler
  diagnostics in the editor Console; the Build Settings button now routes through the same command.
- Source-checkout KeireClient builds now run the incremental Keire.Managed build explicitly before native compilation,
  preventing Visual Studio utility-project up-to-date checks from leaving the editor and gameplay compiler on stale APIs.
- Build Scripts now publishes a generation-local Keire.Managed assembly before compiling gameplay and reloads that
  immutable API/gameplay pair, eliminating stale API references in source checkouts and packaged editors.
- Play Mode now grants managed gameplay a lifetime-scoped UI-capture override for the Player input map, and Play Mode
  change rows use stable hidden checkbox IDs instead of conflicting when labels repeat.

- Interactive prefab and trash operations now preempt queued or running low-priority catalog refreshes, and project
  open no longer starts an eager full-project import solely because source timestamps are newer than the catalog.

- Prefab thumbnails now resolve built-in cube and error meshes directly instead of waiting for catalog entries that
  intentionally do not exist.

- Identity-preserving asset and folder moves now update source records directly without launching or following with a
  full-project import, removing prefab move/delete stalls caused by unrelated large mesh imports.

- The asset trash popup now uses an on-open snapshot instead of locking and reparsing manifests every frame, and trash
  mutations queue behind active asset work rather than failing or freezing the editor.

- GameObject-to-prefab drops now queue behind active asset work, development prefab thumbnails resolve directly from
  source before catalog publication, and the complete asset grid accepts drops into its current folder.

- Prefab deletion now updates the asset index without rescanning the project, folder drops cover complete cards and
  rows, and deleting prefab scene instances removes stale mappings before later unpack or save operations.

- Generated managed projects now target the Visual Studio-compatible .NET 8 baseline and stage `Keire.Managed.dll`
  locally for reliable engine API IntelliSense. Prefab creation publishes immediately to the development asset system,
  project trash mutations avoid full worker imports, restore collisions discard stale trash, and complete folder cards
  accept asset and GameObject drops.

- Fixed animated mesh corruption by retaining same-frame GPU upload transfer buffers until the consuming frame fence
  completes, with safe failure/shutdown cleanup and bounded CPU skin deformation.
- Preserved authored animation tracks for bind-compatible exact-name skeletons and restored GPU skinning on D3D12,
  avoiding unnecessary bind-space retargeting and per-frame CPU vertex deformation.
- Fixed skinned model imports by preventing Assimp vertex joining from discarding distinct bone-weight sets, and
  corrected local-space quaternion composition when retargeting different bind poses. Mesh importer v12 regenerates
  affected skins while static models retain vertex joining and cache-locality optimization.
- Fixed external model imports with explicit rig settings borrowing a destroyed JSON document, and report crashed
  asset workers without attempting to parse a missing result file.

- Preserve managed Inspector assignments when entering Play Mode by reading legacy lowercase state documents and
  publishing new component state with canonical casing; restored UI Button references now register click events.
- Added serializable scene UI Button references for managed Behaviours, including Inspector assignment, native
  component validation, event-style `Clicked` dispatch, and compatibility with existing polling UI scripts.
- Expanded the managed runtime bridge with generation-safe entity names, hierarchy traversal, active state,
  native-component handles, Behaviour lookup, fixed/unscaled/elapsed time, and configurable spatial audio playback.
- Added Inspector serialization for entity references, typed asset references, enums, and bounded nested
  `[SerializableType]` data, including range, tooltip, grouping, undo, scene/prefab persistence, and Play-world rebinding.
- Added audio metadata inspection, waveform thumbnails, Preview/Stop/Reimport actions, and full Audio Source authoring
  for resident and streaming WAV, Ogg Vorbis, and FLAC clips.
- Added scene-authored retained runtime UI and audio presentation in editor Play Mode and cooked runtimes, including
  managed audio playback, managed text updates, button click consumption, audio clip importing, and generation-safe
  teardown.
- Fixed Ninja managed-build quoting, current scene-schema regression checks, input override persistence coverage, and
  clamped weapon equip timing in the managed production harness.
- Fixed one-shot scene audio replay and Stop semantics, stale deferred UI events after teardown, interrupted-reload
  magazine loss, feedback-pool callback rollback and stale leases, zero-time ballistic advancement, editor-test
  compilation, and Windows package-fixture drift.

- Added Project-panel C# script, managed assembly, prefab-from-selection, and prefab-variant creation; source assets open
  through a persisted external-editor choice with system-default fallback. Prefabs instantiate transactionally when
  dragged into the Scene viewport and open in an isolated Prefab Mode with explicit Save/Discard boundaries.
  Apply-to-source preserves scene root placement, rebases variants, rejects nested ownership flattening, and records
  source plus scene metadata as one undoable operation. Stable-ID source replacement validates before publication and
  rolls back source and metadata on failure.
- Hierarchy GameObjects can now be dropped onto Project folders, breadcrumbs, or blank content to create uniquely named
  prefab assets. Prefab thumbnails compose variants and nested instances and render all visible world-transformed mesh
  geometry. C# source opening regenerates SDK-style assembly projects and a Visual Studio solution from `.keireasm`
  definitions, then opens `devenv.exe` with the solution and requested script.
- Scene camera orbit, pan, zoom, and fly navigation now wrap the visible cursor across opposite Scene viewport edges
  in window-local coordinates without applying the warp delta to camera movement, and remain active even when another
  editor panel has focus.
- Directional shadow maps now apply constant and slope-scaled raster depth bias, preventing large ground receivers from
  producing dense self-shadowing patterns while retaining model-cast shadows.
- Fixed imported meshes using another draw's transform on GPU backends where `first_instance` does not offset
  `SV_InstanceID`; visible geometry and shadow rendering now apply the same per-entity scale.

- Added gameplay-production foundations: scene schema v3 and nested prefab/variant composition, transactional prefab
  authoring helpers, application-owned profiling/scripting/physics/audio/navigation services, `.keireasm` assembly
  graphs with cancellable last-good managed builds, Jolt-backed rigid bodies and deterministic convex/triangle
  collision cooking, miniaudio-owned device/headless engines with offline graph rendering, Recast geometry baking with
  deterministic dependency hashes, revisioned synchronous/asynchronous navigation paths, and cooked runtime manifest
  schema v2. The pinned Coral patch now hosts .NET 10 through nethost, performs transactional collectible-context
  reloads with Behaviour type-registry validation, context-scoped unload cleanup, live Behaviour migration, and
  stable-ID scene-component lifecycle dispatch, and supports an explicit bundled runtime root. Cook now compiles
  runtime `.keireasm` graphs and publishes their DLLs; packages carry RID-specific CoreCLR/hostfxr, Coral,
  Jolt/Recast/miniaudio link closures, identities, and licenses. Added deterministic streamed pack pages, Recast-built
  Detour tile payloads, skeletal import/animation assets, audio voices/spatial mixing, prefab/build/profiler panels,
  and a managed third-person sandbox foundation.

- Made the static-scene frame graph operational with compiled transitions and transient alias slots; scene color now
  renders to RGBA16F and executes fitted ACES into double-buffered display surfaces. Added bounded renderer-thread
  submission, full Forward+ light/tile/index storage-buffer consumption, GPU instance buffers for compatible opaque
  draws, CPU preparation p95 diagnostics, and a 10,000-object batch benchmark. Surface HDR attachments now come from
  the graph-owned physical transient heap, with deterministic device-loss, resize/minimize/restore, and queue-capacity
  backend tests.

- Model import now publishes stable generated material and embedded-texture sub-assets, converts glTF PBR factors,
  alpha mode, cutoff, double-sided state, and semantic texture slots, and cooks the complete generated dependency graph.
  FBX/OBJ material limitations produce actionable diagnostics, and Project context menus can extract generated
  materials into editable `.keirematerial` sources through the isolated asset worker.
- Completed scene-document mutation ownership for Entity menu creation/deletion and viewport mesh/material drops, so
  workspace composition code no longer mutates scene objects directly.

- Replaced placeholder shadow statistics with GPU depth-map rendering and receiver sampling for directional, point,
  and spot lights. Default-material primitives now receive those shadows, local lights expose persistent quality,
  strength, and bias controls, and the portable local-light uniform path is validated on D3D12 and Vulkan. Shadow
  metadata is isolated from light color/intensity, and receiver bias prevents a surface from tinting itself when
  shadows are enabled without a separate occluder.
- Cameras now choose a serialized Skybox or Solid Color background. Scene view, Game view, camera preview, and
  standalone runtime honor that choice, the built-in sky sun matches the default directional-light convention, and
  Project Settings explains that scene shadows remain authored by Directional Lights. Shift-click range selection works in both
  Hierarchy and Project, and the single-row main toolbar keeps Play/Pause/Step/Stop visible below the menu.
- Projects now render a built-in studio sky whenever no custom environment is assigned. Project Settings replaces the
  raw environment AssetId field with the shared searchable, type-filtered asset picker used by Inspector fields, with
  drag-and-drop validation and one-click reveal in the Project panel.
- Scene Open/New/Close/Exit actions now commit through an editor-owned frame-safe transition coordinator. Scene and
  operating-system drops work on an empty viewport, failed scene decoding preserves the current document, and closed
  scene references become inert before panel helpers can query them.
- Added compact non-overlapping main toolbar and status-bar primitives, hinted search inputs, expanded editor icons,
  refreshed Kéire Dark/Light density, Hierarchy search/create, session Inspector lock, Game aspect previews, and
  Console severity/collapse controls.
- The Project Hub now defaults to a persistent sortable list with card-view fallback, keyboard/double-click/context
  actions, atomic view preferences, and a larger validated two-pane project-creation workflow.

- Scene authoring now routes `Ctrl/Cmd+D` through the shared duplicate command and supports hierarchy drag insertion
  before or after a sibling, parenting onto a row, and unparenting onto blank hierarchy space while preserving world
  transforms and undo history.
- Asset Browser thumbnails now use an untextured clay render of imported mesh geometry plus dedicated scene, shader,
  and input-action artwork. The Scene viewport adds a toggleable live main-camera preview, and the Project Hub uses a
  refreshed responsive card presentation and modern navigation palette.
- Project environments now render visible sky backgrounds from LDR or Radiance HDR equirectangular maps and common
  horizontal/vertical cubemap cross and strip atlases, with rotation, intensity, exposure, layout metadata, and
  versioned reimport support.
- Added the version-3 static mesh format with ordered LODs, submesh ranges and bounds, stable material-slot indices,
  and v1/v2 compatibility. Model import preserves Assimp slots and recognizes `_LOD0`, `_LOD1`, and later groups.
- Materials now serialize opaque, masked, or premultiplied-blend surface state, alpha cutoff, and double-sided
  rasterization. Mesh renderers support indexed material overrides and cast/receive-shadow controls.
- Static scene submission now performs per-submesh frustum culling, LOD selection, deterministic transparent sorting,
  and per-slot binding. Point/spot lights now illuminate PBR materials with bounded attenuation/cone evaluation on
  D3D12 and Vulkan, expose Scene range/cone gizmos, and retain deterministic 16x16 Forward+ CPU tile diagnostics.
- Hub builds now depend on the editor (which depends on the Asset Worker), preventing stale editor binaries from
  loading newly compiled shader assets with a mismatched graphics binding ABI.
- Added the private typed ten-pass static-scene frame graph with deterministic hazard and lifetime validation.
- Inspector and Hierarchy mutations now route through validated `SceneDocument` commands, including Play/Edit
  targeting, component properties, material slots, shadows, and point/spot-light creation.
- Consolidated durable atomic file publication for settings, thumbnails, input overrides, scene/project settings,
  asset cache objects, catalogs, and build profiles. Packaging removes stale output and verifies manifest `HEAD`.

- Windows managed clients and private tools now reconstruct the process command line with `CommandLineToArgvW` and
  normalize it to UTF-8 at one shared boundary before parsing. Asset-worker protocol and publication paths also append
  suffixes without narrowing native paths, so editor operations work from Unicode checkouts.
- Scene viewport, Hierarchy, Inspector, Input Actions, Project Settings, and Asset Browser now own their UI and
  persistent interaction state behind narrow controllers, with no workspace friendship or whole-panel draw forwarding.
  Scene and Input documents own their save/recovery lifecycles, and the workspace facade is below 1,500 lines.
- Split asset indexing/import, external transactions, mutations/trash, and cooking behind a sub-600-line facade;
  nested operations now use explicit unlocked helpers under a non-recursive transaction boundary. Rendering keeps a
  sub-700-line facade with separately compiled private device, surface/pipeline, cache, and scene-recording units.

- Added a project-settings document with validated atomic saves and coalesced undo, split script checks into timed fast
  and integration suites, and made Release/Dist packages clean-by-default with an explicitly marked local dirty
  override that CI rejects.

- Editor import, refresh, cook, and external-import receipt work now runs in the private `KeireAssetWorker` process
  through atomic versioned protocol documents. The editor coalesces material refreshes, reloads a published source
  index without rescanning, and bounds shutdown with cooperative cancellation followed by forced termination.
- New scene, material, and input-action assets are validated and published transactionally by the asset worker. A
  crash-released project-wide file lock serializes independent database owners, and recoverable directory-publication
  journals restore the last-good catalog after interruption.
- Asset-worker mutation requests now cover folder creation, asset/folder move and duplication, recoverable trash,
  restore, and permanent deletion. Project and Inspector mutations, scene Save As, and shader creation use the isolated
  publication path; multi-file creation journals recover auxiliary shader sources after interruption.
- `SceneDocument` now owns atomic scene save and recovery snapshots, while `InputActionsDocument` owns its source path
  and atomic save lifecycle; material refresh coalescing state is owned by `MaterialDocument`.
- Editor shutdown now cancels queued background catalog refreshes instead of forcing a project-wide import to finish;
  saved asset sources remain durable and refresh normally on the next launch.
- Scene view `F` frames the selected entity's imported mesh metadata and transformed descendants with aspect-aware
  camera padding, while double-`F` locks the camera to the selection like `Shift + F`.
- Project editors restore their last normal window position and size plus maximized or borderless-fullscreen state from
  project-local user settings. The Project Hub now uses a modern navigation rail and responsive recent-project cards.
- Fixed scene saves freezing the editor behind their background asset import by keeping asset record and status
  snapshot queries independent from the operation-wide publication lock.
- Fixed Stop after Play edits getting stuck when the Play Changes modal could not open on its first frame.
- Fixed clean Windows Ninja builds so generated built-in shader headers are produced before any engine source compiles.

### Fixed

- Successful imports now atomically advance compatible `.keiremeta` importer versions without rewriting stable IDs,
  settings, dependencies, or project-owned fields; failed imports leave metadata byte-identical. Sandbox validation
  now checks the current T-Pose model and Idle animation metadata against their scene and controller references.
- Asset move, folder, trash/restore, and cooked-publication renames now retry only transient sharing/permission
  failures with bounded backoff and resolved-path diagnostics. Script harnesses rely on compiled editor behavior tests
  instead of stale implementation-text probes while still proving top-level launcher failure propagation.
- Scene, Hierarchy, Inspector, Input Actions, and Project Settings panels now own their dock registration and content
  boundary; material drafts own their source/baseline lifecycle in `MaterialDocument`, document mutation storage is no
  longer public, and entity, selection, Play, save, and history actions share the command router.
- Asset diagnostics, naming actions, shader/material content, and material draft composition now have an independent
  `AssetInspectorPanel` state owner instead of sharing entity/component Inspector state.
- AssetTool now converts its UTF-8 command-line project, output, catalog, and input paths through the engine filesystem
  boundary, so packaged cooking works from non-ASCII repository and staging paths on Windows.
- Project Hub now preserves its UTF-8 executable path and resolves the editor in both packaged sibling and nested
  build-output layouts, with diagnostics listing every checked path when the companion executable is absent.
- Extracted editor panels now keep their move-only UI panel scope alive for the complete draw call, preventing Scene,
  Hierarchy, Inspector, or Input Actions content from falling into Dear ImGui's standalone `Debug##Default` window.
- GPU material entries now cache complete last-good pipelines, packed numeric slots, texture/sampler bindings, and
  dependency revision stamps. Shared-material draws reuse immutable bindings, component tint remains per draw, and a
  failed dependency rebuild cannot partially replace a working material.
- PBR tangent frames now transform normals and tangents through their correct matrices, Gram–Schmidt orthogonalize the
  basis, compensate handedness for mirrored instances, and use deterministic finite fallbacks for degenerate tangents.
- External asset imports now preflight and validate complete batches in private staging, journal publication for crash
  recovery, preserve zero partial record visibility, and replay undo/redo through the same rollback-capable cook path.
- Editor external-import work now has an explicit cancellable worker lifetime that joins during shutdown instead of
  leaving `std::async` teardown behavior to the standard library implementation.
- Noninteractive Windows repository launcher commands now preserve child-script failures and their native exit codes,
  preventing failed builds, tests, runs, or packages from being reported as successful automation steps.
- Asset pipelines now use the clockwise render-target front-face convention required by Kéire's left-handed camera
  matrices, so back-face culling preserves imported mesh exteriors instead of exposing their interiors.
- OBJ, FBX, glTF, and GLB import now converts Assimp's right-handed geometry, lower-left UVs, and counter-clockwise
  indices into Kéire's left-handed, upper-left-UV, clockwise asset convention; the importer revision invalidates stale
  mesh cache entries automatically.
- PBR materials now keep view and lighting vectors in the same world-space basis and write opaque output for opaque
  pipelines, preventing normal/specular artifacts and apparent holes on imported models. Material texture pickers also
  reject incompatible semantic/color-space assignments before they can invalidate the development catalog.
- Material creation now asks for a name before publishing, and single-asset create/rename operations update the live
  source index without synchronously hashing and cooking the entire project.
- The Sandbox monster now uses its textured material with dedicated linear Metallic and Roughness slots, leaves packed
  glTF ORM on its neutral fallback, and ships with a calibrated neutral studio environment and directional light.
- Project Browser breadcrumbs now defer navigation until path enumeration finishes and use stable scoped IDs, avoiding
  an ImGui empty-ID assertion when navigating from a nested folder to one of its parents.
- `Ctrl/Cmd+S` now routes globally to Scene save, existing-scene saves defer catalog rebuilding to a background task,
  and external imports no longer repeat import/cook work or expose IDs from a failed catalog publication.
- Newly imported meshes and textures remain hidden from thumbnail loading until their replacement catalog is mounted;
  early handles recover automatically and stale cube/checkerboard thumbnails are invalidated after publication.
- Texture imports infer normal and packed data semantics from conventional filenames, retain explicit dialog
  overrides, and report transactional catalog-validation failures without leaving fallback-only assets behind.
- Scene-view overlay controls no longer mutate ImGui layout cursors or trigger a window-boundary assertion when the
  editor opens.

### Added

- Play-mode review now tracks editor-authored entity/component/property paths and classifies later simulation output as
  Mixed, enforces created-parent and component dependencies, preserves unavailable component payloads, and validates
  the complete selected patch before replacing the edit scene.
- Asset import and cooking now expose engine-owned progress phases, typed cancellation, and stop-token-aware overloads
  while serializing every source mutation and catalog publication per database.
- Unity-style persistent Play/Pause/Step controls, nonintrusive Scene-view tool/orientation overlays, automatic
  Game-tab focus on Play and Scene-tab focus after Stop, and content-aware texture, material-sphere, and mesh previews.
- Scene and Hierarchy multi-selection with Ctrl-toggle, viewport marquee selection, primary-selection Inspector
  authoring, and batch duplicate/delete commands.
- Editable Play-mode scenes with isolated runtime undo, property-level change review, selective apply/discard, and
  guarded scene/exit transitions that leave applied changes dirty for the normal Save workflow.
- Cross-platform operating-system asset drops with Project-folder/Scene-viewport routing, transactional external
  copies, importer-owned texture options, cancellation, conflict handling, stable-ID replacement, and receipt-backed
  batch undo/redo.

- A metallic-roughness material surface with shader-declared semantic slots, neutral base/normal/ORM/emissive
  fallbacks, ranged numeric/color authoring, strict cook validation, and glTF +Y normal-map handling.
- Version-two mesh vertices with stored tangent handedness, deterministic v1 tangent reconstruction, and cataloged
  local bounds used by hitch-free scene picking.
- Cold-process rendered-output repeat harnesses and balanced render-frame cancellation/completion during shutdown.
- Correct SPIR-V entrypoint binding for Vulkan, Windows catalogs containing both DXIL and SPIR-V, and required
  GPU-runner diagnostics for D3D12, Vulkan, and Metal.
- Persisted Scene-camera ownership and typed viewport asset-drop routing, including direct mesh-to-entity creation.
- Shader-driven material texture authoring in code and Inspector, with filtered Texture2D pickers for every declared
  base-color, normal, emissive, mask, or custom texture slot and source-safe material serialization.
- Scene-view ray picking for active transform-only and rendered entities, while retaining Camera and Directional Light
  overlay selection and nearest-hit behavior.
- A renderer-owned asset resource layer for revisioned mesh, material, shader, texture, sampler, and pipeline GPU
  resources, with indexed asset geometry, fixed shader bindings, transactional fence retirement, checker/error
  fallbacks, and last-good hot reload behavior.
- MSVC AddressSanitizer builds retain first-party instrumentation while matching the unannotated STL ABI used by
  private CMake dependency archives.
- A transitive startup-scene cooker, textured Sandbox pyramid, cooked runtime manifest, and managed `KeireRuntime`
  player with finite-frame package smoke validation.
- Pinned private Assimp and stb dependencies, a strict versioned `.keiremesh` format, deterministic OBJ/FBX/glTF/GLB
  static-mesh import, RGBA8 texture import with normalized sampler settings and deterministic mip generation, and an
  `AssetTool convert-mesh` command.
- Headless editor document and command-router tests, with scene and Input Actions state moved behind dedicated document
  owners and workspace panels composed through narrow controller interfaces.
- A component-driven Inspector fallback with transactional property edits, generic drawers for every registered property
  kind, component/property overrides, filtered asset and entity pickers, and engine-owned scalar/vector UI controls.
- Kéire-owned logging levels and formatting for placeholders, integer hex/width, and floating precision; public headers
  and SDK include paths no longer expose spdlog or fmt.
- Backend-conditional D3D12, Vulkan, and Metal rendered-output tests with synchronized offscreen RGBA8 readback and
  tolerant pixel assertions for lighting, transforms, material/shader/texture revision swaps, and last-good output.

- Project-owned ambient color, intensity, and exposure settings with atomic persistence, a dockable Project Settings
  editor, and matching Scene/Game lighting submissions.
- Unity-style Scene transform gizmos with `Q/W/E/R` tool shortcuts, Local/Global space, independent position/rotation/
  scale snapping, persistent tool preferences, Camera icons/frustums, and Directional Light icons/rays.

- Advanced Input Actions authoring for action/value types, control schemes and device requirements, composites, binding
  groups, control browsing, every built-in interaction/processor, and conflict-aware interactive rebinding.
- Typed Scene-view asset drops: Material assignment uses object picking and shared scene undo, while Scene and Input
  Actions assets open through their existing guarded workflows.
- A built-in Directional Light Lambert path with transformed cube normals, linear color/intensity, optional color
  temperature, and an unlit editor grid.
- Fixed ambient and Directional Light delivery across SDL_GPU backends by using the proven per-draw vertex uniform
  block, and stopped Project Settings drags from issuing repeated atomic writes while Windows still retires a file.

- An application-owned bounded undo/redo service with isolated document contexts, owner-thread enforcement, mergeable
  commands, nested rollback-safe transactions, contextual Edit-menu shortcuts, and scene/input/theme/asset integration.
- Unity-style Project asset creation and management with current-folder templates, immediate browser synchronization,
  per-asset best-effort import diagnostics, multi-selection file commands, extension-free labels, metadata hover cards,
  drag/drop moves, and persistent recoverable trash.
- A first-party generated unlit shader path that applies each Mesh Renderer linear tint in Scene and Game views, plus a
  testable editor camera controller with framing, view locking, orthographic navigation, fly-speed control, and axis snaps.

- An application-owned SDL_GPU rendering system with coordinated Scene/Game/UI presentation, resizable sRGB/depth/MSAA
  targets, fence-based retirement, editor grid/cube rendering, Camera and Mesh Renderer components, and persistent
  Unity-style Scene camera navigation.
- A contextual shader import pipeline with pinned SDL_shadercross and recursive compiler dependencies, reproducible
  DXIL/SPIR-V/MSL compilation and reflection, immutable shader/material/mesh assets, target-platform cooking, editor
  creation commands, and packaged host compiler tooling.

- A Kéire-owned Unity-style ECS surface with stable weak entities, reference-counted components, application-owned
  registration, typed queries, deterministic lifecycle callbacks, private EnTT storage, and private GLM-backed math.
- Mandatory hierarchical Transform and authorable Directional Light components, canonical scene schema v2, schema v1
  migration, Missing Component preservation, and isolated Play/Pause/Step/Stop runtime cloning.
- A dedicated Asset Browser with folder navigation, persistent List/Grid modes, multi-selection, transactional rename,
  duplicate/trash operations, opaque UI images, and bounded asynchronous digest/version-cached thumbnails.
- Asynchronous Save As, global scene shortcuts, hierarchy context commands, component inspectors, scene play controls,
  and system-tray Hub backgrounding with a recoverable no-tray fallback.

- A project-first editor workflow with transactional Empty/Starter creation, versioned descriptors, canonical roots,
  OS-exclusive editor locks, project-local service paths, a recoverable recent-project registry, and packaged Sandbox.
- A dedicated KeireHub with searchable/pinnable recent projects, async native folder browsing, reveal/open actions,
  detached editor launch, project status diagnostics, and Hub/project smoke modes.
- Typed `.keirescene` assets, owner-thread mutable scenes with weak object handles and validated hierarchy mutation,
  plus asynchronous single/additive SceneSystem loading and frame-boundary activation events.
- Scene authoring across Project, Scene, Hierarchy, Inspector, and Console with atomic save, bounded undo/redo,
  Save/Discard/Cancel transitions, transform editing, subtree operations, and crash-recovery snapshots.
- A dockable Input Debugger with scoped UI-capture bypass, device/user inspection, transactional map subscriptions, and
  bounded searchable Console logging of action phase, processed value, scheme, device/user, duration, and timestamp.

- A reference-counted asynchronous asset runtime with typed fallbacks, priority loading, integrity-checked Zstandard packs, last-good hot reload, mount overrides, owner-thread completion events, and bounded eviction.
- A Unity-style source database with stable `.keiremeta` identities, content-addressed import cache, transactional file operations, deterministic cooker/validator, dedicated `KeireAssetTool`, and Project/Inspector editor integration.
- An application-owned action input system with typed `.keireinput` assets, keyboard/mouse/gamepad devices, local users and pairing, control schemes, frame snapshots, interactions/processors/composites, hot reload, UI capture, cursor modes, interactive rebinding, and atomic profile overrides.
- A dockable Input Actions editor with four creation templates, master-detail authoring, bounded undo/redo, canonical Save/Revert/Validate, searchable bindings, conflict-aware Listen capture, and live device/action monitoring.

- A Unity-style editor workspace with stable panel registration, factory docking, named and portable layouts, per-user atomic autosave, semantic Kéire Dark/Light themes, custom theme editing, and a polished eight-panel client shell.
- Static-first `KEIRE_API` annotations for same-toolchain shared-library preparation and a configurable assertion foundation.
- Thread-safe factory-only `Ref`/`WeakRef` ownership with polymorphic conversion and race-safe weak locking.
- Generated runtime build identity and dependency-free KeireClient help/version commands.
- A canonical SDK consumer example and validated package-only CMake imported target.
- A managed SDK consumer that links the KeireCore-owned entrypoint and validates the client factory contract.
- Identity and dependency lock manifests.
- Transactional full-template rename support.
- Doctor, LLVM coverage, SDK package, and script regression commands.
- Native x64/ARM64 selection and concrete compiler resolution.
- CI coverage, compatibility, security, dependency, and release-package automation.
- SDL 3.4.10 multi-window platform API with typed polling events, high-DPI state, owner-thread enforcement, deferred worker destruction, and inert post-shutdown handles.
- Strict nlohmann/json 3.12.0-backed `WindowSpecification` loading and a tracked `Config/Client.json`.
- Bounded `--smoke-window` and explicit `--config` client options with a real interactive event loop.
- A single-run `Application` runtime with deterministic layer/overlay lifecycle, deferred structural mutation, cancelable close handling, and exception-safe service teardown.
- A standalone typed `EventBus` with prioritized handled propagation, RAII subscriptions, allocation-free immediate dispatch, and bounded cross-thread queued delivery.
- Unity-style application-owned `Time` with scaled/unscaled clocks, smoothing, pause, fixed-step accumulation, interpolation, and backlog diagnostics.
- A Kéire-owned immediate UI API with frame-scoped RAII widgets, application/layer lifecycle integration, headless testing, docking, layout persistence, SDL3 input, and cross-platform SDL_GPU rendering backed privately by Dear ImGui 1.92.8-docking.

### Changed

- Scene view now previews the active scene Camera's clear color while retaining the nonserialized editor camera, and
  built-in Directional Light plus ambient illumination is evaluated through validated fragment-stage uniforms.

- Input Debugger action events now stay in a bounded local history, suppress idle/reset noise, coalesce meaningful
  analog changes, and forward to Console only through an explicit opt-in.
- EnTT 3.16.0 and GLM 1.0.3 are pinned private dependencies with IDE utility projects, external warning isolation,
  package attribution, and build-manifest identities without public header or source redistribution.

- Normal repository `run` launches KeireHub; direct editor launches require an explicit project. SDKs now carry the Hub,
  project/scene headers, and a complete validated sample project instead of a root-level standalone input asset.
- Generation stamps include Premake/config content and the first-party source inventory, automatically regenerating when
  translation units are added or removed. Windows Premake version checks now work from Unicode repository paths.

- First-party headers now live exclusively beneath each project's `Include` directory. Asset public headers and implementation sources are grouped under `Keire/Assets` and `Source/Assets`, while non-SDK Core headers remain isolated under `KeireInternal`.
- `Application` can own an opt-in `AssetSystem`; SDKs now include the asset APIs, asset CLI, private `KeireZstd` archive, Zstandard attribution, and transitive Core → ImGui → Zstd → SDL link closure.
- `Application` can own Input after Assets/Windowing and before UI. Windowing routes native events to multiple private sinks, and SDKs include the public Input API plus a validated Default Input source asset without exposing SDL or JSON.

- Dear ImGui now builds as the dedicated private `DearImGui` static-library project under the generated solution's
  `Dependencies` group. SDKs carry its separate archive transitively through `Keire::Core` without exposing or
  redistributing ImGui headers and sources.
- Public mutex-backed snapshot observers now accurately permit synchronization failures instead of declaring `noexcept`, and ignored return-value diagnostics cover the remaining query-style build and logging APIs.
- SDK packages now include the first-party `Keire/UiWorkspace.h` contract while keeping all Dear ImGui and JSON implementation headers private.
- Dear ImGui remains private to KeireCore's UI implementation with SDL3 and SDL_GPU backends; KeireClient uses the
  public `Keire::UiFrame` facade, and SDK packages expose `Keire/Ui.h` without redistributing Dear ImGui headers or
  sources.
- SDL dependency builds now enable SDL_GPU while keeping SDL_Renderer disabled. Docking is enabled for rendered UI; multi-viewports remain disabled pending explicit multi-window renderer ownership.
- Extracted layer ownership, overlay ordering, deferred mutations, traversal, and teardown into a dedicated public `LayerStack`; `Application` now delegates layer operations while orchestrating frame boundaries.
- Moved the executable entrypoint, informational command handling, exception boundary, application lifetime, and `Run` invocation into KeireCore; KeireClient now supplies `CreateApplication`.
- Moved client-specific help text into a static client command-line descriptor while retaining core-owned help/version handling.
- Public KeireCore headers now use the `Keire /` include prefix and the clean `KEIRE_ *` macro family.
- Logging owns reference-counted private asynchronous state, supports console suppression, and makes detached handles safely inert after shutdown.
- Build identity refreshes immediately before KeireCore compilation and includes tracked and untracked dirty state.
- Dist builds use link-time optimization and CI treats template warnings as errors.
- Release SDKs include complete dependency licenses and separate platform symbols; Dist remains stripped.
- CodeQL and Dependency Review are explicitly opt-in and strict when enabled.
- SDL is built through a compiler-keyed dependency-only CMake cache while Kéire remains Premake-driven; SDK CMake consumers receive SDL transitively.

### Fixed

- Stopping Play from the Scene UI no longer closes the runtime scene while the current render frame still references
  it; Play transitions execute at a safe update boundary and render submissions retain immutable frame-local data.

- Material property edits now publish an in-memory runtime revision immediately and persist their catalog refresh in
  the background, avoiding a synchronous project-wide import for each texture, color, or slider change.
- Scene gizmos transform every selected root around the primary pivot without double-transforming selected children,
  and globally routed editor undo/redo shortcuts work from focused Scene and Hierarchy panels.
- Current development catalogs open without an unconditional startup recook, and the Hub launches the editor before
  nonessential recent-project registry maintenance.
- UI-owned thumbnail textures remain alive until the renderer backend has released them during shutdown.
- Hierarchy Ctrl-click uses the actual pointer press, viewport marquee selection starts consistently, and gizmo handles
  no longer clear scene selection.
- Material Inspector texture edits now reload the live material revision, and viewport material drops decode source
  definitions without invoking an absent byte-only importer callback.
- Restored the declared Zstandard, EnTT, GLM, and SDL_shadercross submodule gitlinks so recursive clones reproduce the
  locked vendor tree.
- SDK packaging now stages only tracked sandbox sources and rejects generated `Library`, `Logs`, `Build`, `Temp`, and
  recovery data in the staging tree, archive, and extracted validation copy.

- Project thumbnails and labels now initiate the same asset drag, component cards collapse without losing their bordered
  presentation, horizontal Scene-camera motion follows pointer direction, and file-manager reveal opens the canonical
  project asset path instead of the process default folder.
- The Game view resolves Camera components from the active runtime scene rather than editor-camera state, and focused
  Inspector/Hierarchy edits route Undo/Redo to the scene context.
- Hub-launched editors now resolve the pinned shader compiler independently of the project working directory; failed
  imports show full Inspector diagnostics, mirror errors to the editor Console and rotating logs, and no longer trigger
  Dear ImGui's null-ID drag-source assertion when the Project panel displays an error badge.
- Asset creation now remains visible when a later best-effort import fails, blank-space Project context creation works,
  and Mesh Renderer tint changes reach GPU draw constants instead of leaving the cube at its test-shader color.

- Project Hub tray callbacks now defer native window mutations until polling completes; one Show action restores,
  raises, and focuses the Hub, minimizing hides it, and closing or choosing tray Quit performs a full process shutdown.

- Minimizing the editor during event polling no longer abandons pending fixed ticks and terminates the process on the
  following frame.
- Minimizing a restored Project Hub now hides it back to the system tray, and Show Hub reliably makes it visible,
  restores it, and raises it again.
- Asset Browser grids no longer restore invalid Dear ImGui stretch weights when their responsive column count changes,
  and narrow panels collapse the folder tree instead of crushing both browser panes together.
- Transform inspectors now use compact responsive X/Y/Z drag fields with direct numeric entry instead of nine
  full-width sliders.
- Project Hub recent-project persistence now encodes filesystem paths as UTF-8, allowing projects beneath Unicode
  directories such as `KéireEngine` to open on Windows.
- Project generation can no longer silently omit a newly added implementation file and surface as unresolved editor
  symbols at link time.

- `CommandLineError` now participates in the `KEIRE_API` boundary, script regressions cover every exported class and free function, and mandatory event, UI, and window teardown paths contain synchronization failures.
- Workspace dock splits now preserve their proportions across fullscreen, maximized, and windowed host sizes instead
  of retaining fullscreen side-panel widths and collapsing the central region.
- Dirty theme preset changes now defer their confirmation modal until the preset combo or menu has closed, so Save, Discard, and Cancel can complete the requested switch reliably.
- Release packaging now compiles and runs a standalone consumer from the extracted SDK archive.
- Nested layer traversal now defers structural mutation until the outermost callback returns, and layer operations enforce the application construction thread.
- Detaching layers cannot create automatic subscriptions, and native window registration rolls back partial resource acquisition.
- Ninja's transient lock file no longer contaminates runtime build identity or package manifests.
- Windows packaging supports Git repositories without a first commit, and vendor probes suppress expected native Git errors.
- Linux coverage resolves `llvm-profdata` and `llvm-cov` from the selected Clang major version.
- Logger handles no longer expose spdlog or standard-library ownership and lock types in the public API.
- Shutdown no longer waits for live logger-handle values, preventing same-thread handle/shutdown deadlocks.
- Security workflows expose an always-running activation check so disabled advanced-security jobs cannot look silently successful.
- Linux Premake bootstrap now accepts release archives whose executable bit is not preserved.
- ARM64 Premake source builds install the platform UUID development headers before compilation.
- Vendor bootstrap verifies the committed submodule pointer and restores detached working trees to that exact hash.
- macOS tool version checks consume complete command output, avoiding `xcodebuild` broken-pipe crashes.
- Linux Clang bootstrap installs the LLVM profiling and coverage utilities required by coverage reports.
- Lazy and explicit logger initialization now serialize under one lifecycle lock.
- macOS bootstrap accepts Command Line Tools without full Xcode for non-Xcode generators.
- Arch installation performs a full package upgrade instead of a partial database synchronization.
- Windows detects containing Git worktrees and rename updates public-header include guards.
