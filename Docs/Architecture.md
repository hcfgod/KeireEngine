# Architecture

## Shared architecture foundations

`Application` owns the scheduler, memory tracker, string interner, diagnostic catalog/sink, source-module registry,
streaming manager, and replay service. They are constructed before services that consume them. Shutdown proceeds in
the opposite direction: managed generations and subsystem scopes cancel and drain, renderer and streaming references
retire, modules stop in reverse dependency order, and the scheduler closes last. Asynchronous work captures reference-
counted or weak state; reusable slots use typed generational handles so an old completion cannot address a replacement.

`JobSystem` has bounded priority-aware injection queues, local LIFO worker deques, FIFO stealing, configurable compute
workers, and a separately bounded two-worker blocking lane. Dependencies are immutable and scheduler-qualified.
Failure and cancellation propagate without hiding the originating exception; continuations can explicitly observe a
terminal result. A worker wait cooperatively executes ready work. `JobScope` supplies the cancellation-and-drain
boundary used by asset loading/cooking, navigation, managed builds and managed callbacks, thumbnails, material work,
VFX warmup, and physics. Jolt worlds share the application scheduler through a barrier adapter. Renderer submission,
filesystem monitoring, process reaping, audio callbacks, and the logger retain their required dedicated threads.
Strict simulation domains use submission order and deterministic partitions instead of stealing. Editor and tool asset
databases inject the same process-owned scheduler into catalog cooking; the small internal scheduler remains only as a
source-compatible fallback for standalone database consumers that do not provide one.
Dependency registration is serialized against dependency completion, so a dependent cannot become runnable until its
entire immutable dependency set is attached. Submission and close share one lifecycle gate, worker-initiated close
never self-joins, and overflow from each job's fixed scratch arena uses the configured upstream memory resource.
Stable-handle slot creation is transactional for both new and reused indices, so a throwing value constructor cannot
strand an unreachable slot or consume configured capacity.

## Hub product runtime and package workers

`KeireHubRuntime` is a private, engine-independent product library. Its owner-thread stores publish immutable settings,
project, installation, task, and notification snapshots through atomic JSON replacements. `HubTaskManager` derives a
deterministic dispatch set from those snapshots: downloads are bounded by the configured concurrency (two by default),
tasks that overlap a package are not dispatched together, and mutations sharing an installation ID are serialized.
Claiming a task records its worker PID and first phase in one store commit. Restart reconciliation preserves queued and
paused work, requeues resumable downloads, and turns interrupted mutation phases into retryable typed failures.

Optional Hub identity is a separate adapter boundary. `SupabaseAccountClient` owns bounded Auth/PostgREST request and
response contracts over the private native HTTP transport; `HubAccountWorkflow` owns asynchronous session rotation and
publishes immutable account snapshots. `DesktopOAuthClient` is a provider adapter for a public authorization-code PKCE
client: the website consent callback forwards only a single-use code and state through a typed Hub activation, the Hub
verifies state and ID-token nonce, and the Hub exchanges the code itself. Website cookies, passwords, browser refresh
tokens, and service-role credentials never enter the desktop process. Browser SSO is configuration- and feature-gated,
with the existing email flow retained as a staged fallback. Only publishable desktop configuration enters packages.
Refresh-token persistence records whether the session came from browser OAuth or direct Supabase Auth inside the
protected payload. Rotation therefore returns browser sessions to the public-client OAuth token endpoint with the
configured `client_id`, while direct email sessions retain the Auth endpoint. Persistence is delegated to DPAPI on
Windows, Secret Service on Linux, and Keychain on macOS; an unavailable Linux secret store produces a visible
memory-only session instead of plaintext disk storage. No account state participates in package trust, editor
ownership, task authorization, or project locking.

`EditorInstallationManager` verifies schema-2 editor manifests, their canonical fingerprints, host identity, complete
declared file inventory, and confined entrypoints outside the UI layer, then publishes immutable health snapshots.
Managed repair and removal remain two-phase: the manager checks the exact registered root and unforgeable marker,
rejects running editors and installations with active tasks, and emits a typed plan that must be revalidated immediately
before a worker mutation. Running state combines Hub-tracked processes with a bounded native probe of the exact
executable path and operating-system process-creation identity on Windows, Linux, and macOS. The creation identity
prevents a recycled PID from retaining or transferring a tracked launch, even when the new process uses the same Editor
binary. An operating-system query failure for a process with the relevant executable name fails
closed, so an editor launched outside the current Hub session cannot be verified, repaired, or removed concurrently.
Native process handles remain confined to the private probe. The manager never deletes an installation itself. External
removal deletes only the atomic Hub registry entry and leaves every editor file untouched. A changed marker, health
result, or repair inventory invalidates a prepared plan instead of widening its authority.

The Hub's editor-management coordinator captures value-owned registrations and activity before sending manifest,
receipt, and file-inventory work to a bounded background operation. Its owner-thread poll rejects results after any
registry-generation, security-identity, root, tracked-process, or targeted-task change. Only that poll persists verified
health or hands a still-current repair/removal plan to the package task system; UI components consume immutable busy,
result, and failure snapshots and never hash an editor installation inside a frame.
Refresh health is committed as one bounded registry update so installed cards and catalog availability share a single
state generation. An explicit external-registration refresh captures the old ID, root, ownership, activity, and
registry generation; a worker validates and inventories the replacement manifest, and the owner thread rejects the
result if any captured identity or activity changed before atomically adopting the new external metadata. This keeps
ordinary health refresh fail-closed while supporting intentional in-place development rebuilds. A separate
missing-managed-registration operation checks the exact registered identity and root, then
requires the filesystem object to be definitively absent before removing only the registry entry; it never authorizes
or performs filesystem deletion.

Managed-editor repair is a distinct persistent task kind and worker protocol mode. Catalog planning requires every
signed package manifest to reproduce the exact receipt-bound editor/component versions, artifact digests, dependency
metadata, and file inventory already registered for that installation. The worker permits an existing destination
only in repair mode, rechecks its marker and executable activity before mutation and again at the atomic publication
boundary, and requires the newly staged receipt and ownership marker to reproduce the authorized aggregate identity.
New installs publish with an explicit no-clobber policy even if a destination appears after preparation. Repair
publication and recovery reauthorize both the registered destination and its same-parent backup under the publication
lock before proceeding; persisted worker mode must agree with task kind, and the completion snapshot retains the exact
fingerprint, tree identity, receipt digest, and nonce needed for non-mutating reconciliation.
This permits recovery of a missing or malformed on-disk receipt without treating an ordinary install as repair, while
leaving the old tree intact on cancellation, authorization drift, or package mismatch. Completion reuses the normal
managed-package registration path, preserving installation identity and refreshing its verified health snapshot.

`ProjectWorkflowManager` owns Hub-only project mutations while `Project` and the editor lock remain authoritative. A
duplicate is copied into a same-parent staging directory with generated output omitted, bounded file and byte counts,
portable case-collision checks, no symbolic links, a fresh schema-4 identity, and optional selected-editor validation;
only a fully re-inspected tree is atomically published and registered. Duplicate preparation captures an owner-thread
plan, bounded recursive copy and validation run in one cancellable background operation, and owner-thread commit
rechecks the catalog, source lock, staging identity, and destination immediately before publication. The coordinator
publishes immutable operation-ID snapshots and joins and discards unpublished staging during shutdown. Locate verifies
the descriptor's original project identity before replacing the catalog path. Display-name changes require a closed,
supported-schema project and update the descriptor and catalog transactionally. Removing a project affects only the Hub
catalog and never project files. The Hub routes pinning and last-opened updates through that same catalog, making it the
sole `projects.json` writer. Its legacy Core `ProjectRegistry` projection is reconstructed as a read-only presentation
snapshot after each mutation, so refreshing visible lock/status data cannot strip the runtime catalog's cached metadata.
Metadata scanning performs bounded lock and interrupted-upgrade probes on its worker before publishing status. The
owner thread validates the complete result set and persists every cached-metadata replacement with one atomic registry
write; a missing, duplicate, or invalid result leaves all prior metadata intact. The UI then overlays live
tracked-process state and derives missing-editor state through the same semantic editor selector used for launch. That
selector rejects unhealthy or entrypoint-less installations, out-of-range project schemas, versions
below the project minimum, and versions older than the last save; preferred-installation metadata is only a preference
inside that compatible set. Launch uses version-neutral descriptor inspection so the Hub can safely dispatch a newer
project to a validated newer editor without attempting to open it through the Hub build itself.
Upgrade-and-reopen dispatch skips a pre-launch metadata scan that could race the new editor's lock. A tracked process
exit requests a coalesced follow-up scan, ensuring the persisted lock state is refreshed without performing filesystem
work on the UI thread. Hub modal styling is centralized around semantic appearance tokens, including frame, button,
header, border, text, and status colors, so every project and recovery dialog follows the selected Hub appearance.
Editor discovery, installed versions, and installation configuration are separate views rather than one unbounded
page. Task and notification centers are anchored overlay popovers; their open state never changes surrounding layout.
Every progress and completion label is derived from the persisted task kind, so removal cannot be presented as an
installation operation.

Package transfer runs in `KeireHubWorker`, not in UI components. The Hub creates one confined operation directory with
atomic request, status, result, and control documents; the worker rejects aliased or escaping protocol paths. The
runtime's transport interface streams bounded chunks into a SHA-256-addressed cache. A sibling `.partial` file and
atomic metadata document bind resumable bytes to package ID, URL, expected size and digest, and ETag; an If-Range
mismatch discards stale bytes before restart. Pause and cancellation leave valid partial data, while publication occurs
only after exact size and SHA-256 verification. Retry delays use bounded deterministic jitter and remain responsive to
worker control. The initial concrete worker transport handles offline `file://` imports; authenticated HTTPS package
streaming is supplied through the same interface rather than changing cache or task semantics.

Windows launches that worker with `CreateProcessW`, and the worker consumes `wmain` arguments directly so operation
roots containing non-ASCII characters never pass through the active ANSI code page. SDL-provided user directories are
decoded as UTF-8 before becoming `std::filesystem::path` values. Startup migrates the exact legacy `KÃ©ire` component
produced by older builds, moving Hub-owned preference, cache, temporary, and task-journal trees without merging
unrelated non-empty roots. The task-to-notification tracker observes state transitions on the owner thread: first
observation is silent, new/retried work emits one start event, and successful or cancelled terminal transitions emit
durable activity history while progress remains an immutable task snapshot.
Terminal package tasks are removed only by the coordinator's serialized control queue; bulk removal atomically erases
terminal records while preserving active work. Read notifications use the notification store's atomic removal path.

The general process boundary prepares owned POSIX argument storage before `fork`; the child performs only descriptor
redirection, directory change, `execv`, and `_exit`. Windows capture launches use an explicit process-thread handle
list containing only the standard-input null handle and output pipe writer, preventing an unrelated inheritable engine
handle from escaping into a child. Native handles and partial launch setup remain under immediate RAII ownership.

Local Build Support inventory enumeration and verification use a separate owner-thread coordinator whose worker
publishes immutable component snapshots. Verified-cache clearing likewise runs through an exclusive maintenance
coordinator: it validates that no package task is active, stops the idle package workflow before deletion, projects its
state into the task center, and recreates package coordination only after completion. Neither operation performs
filesystem traversal or hashing in a UI frame.

Legacy Build Support Asset Tool processes use a separate schema-1 `BuildSupportOperationStore`, preserving schema-1
`.keireplayersupport` package compatibility without pretending those operations use the generic package worker. A
launching record is atomically committed before spawn and immediately amended with the child PID; failure to persist
that PID terminates the still-owned child. Restart recovery validates the exact target installation, Asset Tool,
operation root, status path, and cancel path. It combines PID liveness with the bounded exact-executable probe, but takes
no destructive ownership of a surviving child. Import/repair terminal state comes only from the atomic Asset Tool status;
removal terminal state comes from a fresh background inventory pass plus a bounded schema-1 root-journal inspection.
An indeterminate process probe stays conservatively busy. A crash in the narrow interval between spawn and PID commit is
recovered through the exact executable path; PID reuse can delay reconciliation but cannot authorize mutation or
termination. Recent terminal history is bounded and task-center visibility is reconstructed from the durable records.
Inventory refresh requests made while a scan is active coalesce into one owner-thread-scheduled follow-up worker, so a
pre-install scan cannot become the final snapshot after a successful import or repair.

`MemorySystem` exposes hierarchical domains, tracked PMR resources, immutable current/peak/allocation/external-byte
snapshots, an owner-thread frame arena, per-job scratch arenas, and fence-owned renderer upload storage. It does not
replace global allocation. The editor Architecture dashboard reports scheduler queues, memory domains, streaming
budgets, active and retired renderer bytes, module order, replay state, and structured diagnostic counts.
Application domains cover assets, renderer residency and fence retirement, physics, audio, animation, scripting,
navigation, editor, replay, streaming, jobs, and registered source modules. Asset CPU residency and renderer transient,
VFX GPU, and fence-retired bytes are refreshed from their authoritative subsystem counters.

`DiagnosticId` values use `KEIRE-<DOMAIN>-NNNN`. Definitions are registered transactionally before the catalog freezes;
reports carry severity, optional source location, and stable context. The Diagnostics panel resolves a packaged Markdown
page first and the configured repository documentation URL second. `StringInterner` IDs remain process-local and are
never serialized. Persistent assets continue to store canonical UTF-8 paths or UUIDs, while `AssetDatabase` maintains
transactional identity and canonical-path indexes and reports portable case-fold collisions.

## Streaming and frame-safe resources

Cooked catalog schema 3 retains schema-1/2 monolithic readability and adds semantic segment tables over independently
compressed, SHA-256-verified pages. New texture cooks publish metadata plus tail-first mip ranges, meshes publish
metadata plus per-LOD vertex/index ranges, audio publishes metadata plus time-windowed pages, and animation publishes
independently addressable time windows. Old catalogs surface one whole-resident compatibility segment.
Whole-asset `LoadAsync` remains compatible, while `ReadRangeAsync` and `StreamingSystem` expose explicit texture-mip,
mesh-LOD, audio-page, animation-window, and general residency requests. Priority follows metadata, reads, decode, and
publication; bounded aging in the shared scheduler prevents starvation. Each class has independent CPU/GPU soft budgets,
100/90 percent eviction hysteresis, pins, stale-handle cancellation, latency/miss/eviction counters, and retired-byte
accounting. Audio underruns are reported atomically; the callback emits silence without waiting, allocating, decoding,
locking a completion, or performing file I/O.

Request creation and `Pump` remain construction-thread operations because they coordinate with the application frame
boundary. Cancellation, release, pin/touch, snapshots, statistics, retirement reports, and idempotent close are
synchronized and may be issued by worker consumers. Handle/free-list storage is reserved to the configured request
capacity, so `noexcept` release and close do not allocate. Successful completion count is the denominator for reported
load latency; failures and cancellations have separate counters. Aggregate streaming bytes, failures, and evictions
are also published to the Assets profiler category.

Renderer resource replacement uses the fence retirement queue. GPU bytes remain charged until their submission fence
signals, and CPU pages remain owned by snapshots, jobs, or consumers until their last reference releases. The compiled
frame graph publishes an immutable inspection snapshot containing deterministic pass order, resource lifetimes,
transitions, estimated bytes, and physical alias slots. The Render Graph panel reads that snapshot rather than compiling
a second graph and performs JSON or Graphviz export only after an explicit atomic-save action.

Each render surface retains grow-only dynamic upload storage for scene instances and CPU VFX vertices. Instance data is
mapped once into a cyclic transfer arena and copied to reusable per-batch storage in one copy pass; CPU VFX uses the same
cyclic transfer/GPU-buffer contract and combines only adjacent, depth-ordered particles whose texture and surface state
match. Replaced buffers enter the normal submission-fence retirement queue, and surface close releases retained storage
only after renderer shutdown has made GPU access inert. `RenderStatistics` reports dynamic bytes, buffer reallocations,
and CPU VFX draw batches so a warmed-up scene can distinguish expected payload traffic from resource churn.

GPU occlusion is a same-frame, per-surface extension of the private frame graph. Explicitly safe opaque geometry writes
the occlusion depth pass, compute reductions build a hierarchical depth pyramid, and conservative bounds classification
produces compact instance data plus indexed indirect arguments before the main opaque pass. Each dependent reduction is
recorded with the synchronization required by SDL_GPU; no CPU visibility decision waits for the GPU. Transparent
geometry never contributes occluder depth, though an eligible depth-tested singleton can be rejected without disturbing
transparent ordering. Always-visible instances stay in their compacted batch with a force-visible bit. Unsafe deforming
geometry, legacy instance-addressing shaders, and materials without conservative bounds remain on deterministic direct
draws; only opaque geometry with matching depth-only behavior may become an occluder. Generated Shader Graph materials
publish the eligibility metadata only when their vertex and depth behavior satisfies that contract.

Imported linear-blend skins retain conservative bind-space influence bounds for every contributing bone and submesh.
Skinning preparation transforms each bound by the current palette, unions the transformed bounds per submesh, and
publishes them only after the corresponding deformed vertex stream is ready. A skinned draw may be rejected by occlusion
only when the bound's pose generation matches the captured Animator pose, its frame index matches the immutable frame
packet, and the draw can bind that frame's `SkinnedAssetVertices` stream for occluder depth. A missing or stale bound,
dual-quaternion skinning, morph targets, unbounded Shader Graph world-position displacement, and other unknown vertex
displacement remain force-visible. These fallbacks are correctness contracts, not profitability choices, and Forced
occlusion mode does not override them.

`Disabled`, `Automatic`, and `Forced` are persisted in rendering settings. Forced mode bypasses profitability
thresholds, never safety checks. Allocation, pipeline, backend, content-eligibility, resize, and unavailable-surface
failures preserve complete direct rendering and publish a typed reason instead of dropping geometry. Each surface epoch
owns one occlusion workset per in-flight frame slot. Its pyramid, indirect arguments, `GeometryVisibility`,
`VfxVisibilityMask`, `LocalLightVisibilityMask`, and `SpatialVolumeVisibilityMask` resources are qualified by the frame
slot, surface epoch, and device generation. Resize, minimize, recovery, and close therefore cannot expose an
old-epoch or old-device mask to a newer frame; device recovery releases the lost generation and rebuilds the interrupted
frame's resources during retry. Geometry currently consumes `GeometryVisibility` through scan/scatter compaction and
indexed indirect draws. Forward+ consumes `LocalLightVisibilityMask` in the same frame after classification and compacts
each tile's light indices on the GPU. Both the mask and destination Forward+ workset must match the captured frame ID,
frame slot, surface epoch, and device generation, and the mask count must match the packet's local-light count. Missing
or mismatched ownership, buffers, counts, pipeline support, or dispatch bounds leaves the original CPU-built tile lists
intact, so the path fails visible instead of accepting stale data or removing a light. Its capability flag is true only
when GPU occlusion is available.

VFX begins with a deterministic, bounded visibility-planning contract. It reserves stable candidate ranges for
supported GPU sprite, mesh, and whole-ribbon groups after simulation has produced that frame's dynamic bounds. Mesh
bounds are transformed into world space, and ribbons are classified as whole renderer groups with bounds that cover
both segment endpoints and their conservative widths. After unified classification, the VFX expansion pass can compact
visible instances into output-index, indirect-argument, and instance buffers owned by the active frame workset. Drawing
uses those buffers only when frame ID, slot, surface epoch, device generation, and expected range all match; otherwise it
uses the persistent source buffers and remains visible. CPU VFX, volumetric or unbounded effects, stale/nonfinite bounds,
unsupported renderers, invalid ranges, and candidate-limit overflow remain force-visible. The VFX mask capability is
advertised only while rendered GPU occlusion is active and the device is running; ownership or recovery mismatches fail
visible instead of consuming stale output.

Spatial-lighting ABI v3 adds a frame-owned selection buffer after the fixed Forward+ fragment-buffer prefix. The
post-classification selection pass writes one record per prepared ABI-v3 draw, retaining deterministic contribution and
stable-asset ordering while choosing up to two visible reflection probes and one visible light-probe volume. A draw
indexes that record only when its selection workset and visibility mask exactly match the captured frame, slot, surface
epoch, device generation, and counts. `UINT_MAX` selects the embedded ABI-v2 values, so stale ownership, an unavailable
pipeline, invalid descriptors, or an unsafe candidate fails visible without a GPU-to-CPU readback. ABI v2 remains
supported, and ABI-v3 draws retain one selection index per prepared draw instead of merging instances that may require
different spatial records. The spatial-volume capability is likewise advertised only while rendered GPU occlusion is
active and the device is running; invalid selection ownership retains the embedded CPU values.

The readback ring and value-only `GpuOcclusionSurfaceDiagnostics` remain surface-owned. Aggregate statistics expose
current recording work while visibility totals arrive asynchronously with their source frame and age.
Occlusion depth remains presentation-resolution because directly rasterizing occluders at a lower resolution can close
pixel-sized gaps and produce false occlusion. The separate R32 hierarchy begins at half resolution; at 3840x2160, with
a 32-bit depth format, it and the depth texture consume about 42 MiB per frame slot (about 127 MiB for three slots),
within a 256 MiB per-surface texture budget. Checked admission accounts for the full depth hierarchy and configured
frames in flight before any texture allocation; over-budget or arithmetic-overflowing configurations retain direct
draws with a typed resource fallback, so the admitted 16,384-pixel dimension cannot trigger multi-gigabyte attempts.
Persistent allocation failure uses independent capped exponential backoff per frame slot and sparse warnings while
retaining direct draws; resize, minimize/restore, and other surface-resource resets retry immediately.
Automatic mode remains inactive without advancing activation hysteresis while any slot is in allocation backoff. HZB
and bounds visualization pipelines are optional, independently released on failure, and cannot disable the core depth,
reduction, classification, scan, or scatter pipelines.
Pending readbacks also carry the surface generation, an internal submission epoch, and requested mode. Resource resets
and every mode transition advance that epoch, so a late result from a pre-resize frame or a Forced-Disabled-Forced ABA
sequence cannot revalidate stale visibility counters.
Execution-produced aggregate counters reset on the render owner immediately before frame execution, not at the earlier
application `BeginFrame` boundary. Editor UI built between those boundaries therefore observes one coherent finalized
workload; after execution, profiling and telemetry observe the newly finalized frame.
The active and terminal-fallback surface counts cover only surfaces submitted in that completed frame; partial-fallback
surfaces remain active and form a subset of the active count. A completed frame without a request idles that surface,
invalidates its pending-readback epoch, and prevents old visibility results from repopulating terminal diagnostics.
The editor's session-transient visualization request is a value-only per-surface contract. The renderer composites
visibility bounds or a selected hierarchical-depth mip in its overlay pass and keeps the debug texture private; the
separate metadata overlay reports mode, fallback, mip availability, source frame, and readback age. External GPU capture
remains the authoritative workflow for inspecting every pyramid resource and synchronization barrier.

## Fixed-tick replay and deterministic profiles

Gameplay input is latched at fixed-tick boundaries. Digital edges remain pending until a tick consumes them, analog
absolute values use the latest sample, and relative mouse/scroll values accumulate across render frames and are consumed
once. Actions are sorted by stable context/map/action/user identity and replay encoding stores exact floating-point bit
patterns plus the input-map fingerprint. Playback replaces only gameplay input; editor-control contexts remain live.

`.keirereplay` is a versioned chunk stream with build/project/module/content/deterministic fingerprints, SHA-256 chunk
and footer integrity, zstd checkpoints, an initial checkpoint, and a configurable 300-tick checkpoint interval. Seeking
restores the closest checkpoint and deterministically simulates intervening ticks. `StrictVerified` enables ordered
simulation scheduling and rejects any simulation-affecting module or serializer without a deterministic path.
`PerformanceCapture` retains complete input/checkpoints and divergence reporting without promising identical
intermediate GPU results. Runtime flags support record, play, verify, headless execution, startup-scene override, tick
limits, profile selection, and atomic JSON result reports. Checkpoint restoration validates every serializer before
mutation and rolls all restored serializers back if a later one fails. Runtime checkpoints include the scene graph,
Jolt transforms/velocities/sleep state, animator layers and transitions, managed behaviour state, presentation focus and
queued UI events, logical audio voice frames/pause state, and VFX emitter/random/spawn state. Strict replay selects the
canonical CPU VFX path and stores CPU particles; performance captures retain GPU emitter progress without claiming
cross-device bit identity. Networking and rollback transport remain outside this layer.
Replay input and checkpoints are decoded under the configured total-size and rewind budgets before allocation, and
variable-count fields are bounded before reservation. Recording charges the exact metadata, input, compressed
checkpoint, header, and footer sizes as data is captured, so it fails before retained tick data can exceed the eventual
file limit. Playback advances on the recorded logical tick rather than the host frame's tick argument. While playback
is paused, host-produced fixed steps are discarded without advancing `Time::FixedTime` or `FixedTickCount`; Step commits
exactly one replay and clock tick before the remaining host backlog is discarded. Recording, decoding, restore,
verification, and finalization faults transition the session to `Failed` and emit
`KEIRE-REPLAY-0002`; `Close` remains non-throwing without hiding that terminal state.

## Project upgrades and source modules

Project descriptors are schema 4 and include creation time, created-with and last-saved editor versions, optional
template provenance, and a sorted required source-module catalog. Schema-1, schema-2, and schema-3 projects inspect as
`UpgradeAvailable`; they are not treated as corrupt, while version-neutral inspection can still dispatch a newer
schema to a compatible installed editor. `ProjectUpgradeService` produces a pure ordered plan, applies under the
project lock through path-confined staging and before-images, durably journals publication, validates each step and the
staged project, can recover or roll back an interrupted transaction, and retains the three newest successful backups
under `Library/ProjectUpgrades`. The Hub's upgrade coordinator performs inspection, apply, recovery, and rollback on a
worker and publishes immutable state; the owner thread only renders confirmation and consumes the terminal action.
`upgrade-project` is dry-run unless `--apply` is explicit and also exposes recovery and rollback.

Upgrade journals are written before snapshot work begins and use explicit initializing, snapshotted, prepared, ready,
publishing, and completed phases. Recovery rebuilds staging from the immutable before-image before rerunning a step,
so callbacks need not tolerate a half-written prior attempt. Journal schema, step continuity, affected-file sets, and
transaction IDs are validated before use. Canonical confinement rejects symbolic links and Windows reparse points;
the same check covers the editor lock, journal, snapshots, staging, and backup storage below `Library`, not only affected
project files. An orphaned `Active` directory is recognized as interrupted state and can be safely cleared.

`EngineModule` is a source-level interface. The registry deterministically resolves versions and dependencies, rejects
duplicates/cycles/mismatches before service startup, collects components, importers, decoders, replay serializers,
diagnostics, upgrade steps, and memory domains through one transactional registration context, freezes afterward, and
stops modules in reverse order. The same static source-module pack links into editor/client, runtime, AssetTool, and the
asset worker. Its ordered catalog is embedded in cooked manifests and replay fingerprints. There is deliberately no
`LoadLibrary`, `dlopen`, runtime unloading, or public binary plugin ABI.
Simulation-affecting modules must explicitly declare their replay state as stateless or stateful. The registry creates
an empty identity marker only for a declared stateless module; a stateful module must register `module.<module-id>` with
real capture/restore callbacks whose determinism matches its descriptor during that module's own registration callback.
Registration provenance prevents one module from supplying another module's checkpoint contract. This prevents strict
replay certification from silently substituting an empty checkpoint for unregistered module state.
The editor composes built-in asset importers with the registry's ordered module importers before constructing its asset
database; existing duplicate-name and duplicate-extension validation rejects collisions. Caret version ranges follow
SemVer's pre-1.0 compatibility rules and reject an unrepresentable overflowing exclusive bound.

## Persistence boundary

First-party settings, scene/project documents, thumbnails, input overrides, asset metadata/cache objects, catalogs,
and package manifests use one private atomic-publication service. It creates same-volume temporaries, durably flushes
data and containing directories where supported, publishes with bounded transient retry, and removes abandoned
transaction files during recovery. Callers do not implement private rename/write protocols.

Development and cooked asset payloads are immutable content-addressed packs. A publication installs packs before
atomically switching the small catalog, so open or queued reads retain valid generation paths across an editor reimport.
Retired packs use delayed best-effort collection rather than participating in the success boundary.

## Ownership

`KeireCore` is a static C++20 library and owns reusable application behavior, including projects, scenes,
reference-counted ownership, logging, and the Kéire UI runtime. `KeireClient` is the project editor, `KeireHub` owns
project discovery/creation and editor process launch, `KeireAssetTool` owns headless import/cook validation, and
`KeireTests` is independent. `DearImGui` is a private static-library build dependency
grouped under `Dependencies`; its reviewed definition lives in `Scripts/Premake/DearImGui.lua`, while generated project
metadata lives below ignored `Build/Projects/DearImGui`. First-party targets retain local `premake5.lua` files, and
the root file defines workspace identity, dependency grouping, and project load order.

KeireCore's source-build implementation is partitioned into private Foundation, Assets, Build, World, Rendering,
Scenes, Scripting, UI, and VFX archives. They share the same public headers, compile contract, generated-content
dependency, and final-binary link closure, but keep ordinary edits from recreating one monolithic archive. GNU-family
links group the archives to preserve cross-subsystem cyclic resolution. Packaging transactionally recombines them into
one platform KeireCore archive, so the supported SDK boundary and consumer link name do not expose the partitioning.

First-run discovery and import preparation are bounded Hub worker-thread operations. The worker re-inspects every
discovered project descriptor and editor manifest, then publishes immutable prepared project/editor records. The owner
thread only preflights and commits those records through the runtime's batched stores. Both store plans are validated
before persistence; if the editor-registry write fails after the project-registry write, the controller restores the
exact prior project snapshot and durable file contents before returning the typed failure.

`Config/Project.conf` defines names and folders. `Config/Dependencies.lock` defines immutable external inputs. Premake and launchers read these files so renaming and dependency verification have one source of truth.

`Application` creates `RenderSystem` after Windowing and before UI. RenderSystem exclusively owns the SDL_GPU device,
window claim, swapchain, bounded submission thread, command recording, fences, viewport front/back resources, and
deferred retirement. Owner-thread scene snapshots cross the private queue by value; GPU recording and submission run
on the renderer thread and complete before the next owner-thread frame boundary. UI records through a private renderer
bridge rather than owning presentation. Scene/Game panels exchange only Kéire `RenderView` and `RenderSurface` handles;
backend resources remain private. Dynamic skinning, instance, and Forward+ transfers are recorded before their consumers
on the frame command buffer; the fallback upload queue is reserved for resource publication that cannot join that
ordered recording path. The renderer applies the configured frames-in-flight policy to SDL's 1–3 frame presentation
queue while retaining the full configured bound for fences and transient-resource retirement. Higher applied queue depths
favor throughput at the cost of additional presentation latency. Profiling attributes command recording to skinning, VFX,
draw preparation, frame-graph passes, and residual orchestration overhead. Each offscreen surface owns an ordered
command-buffer sequence. Material-heavy opaque and transparent passes continue in bounded batches that store and reload
their color/depth attachments between submissions, preventing backend-local descriptor heaps from becoming an
unbounded per-frame resource while preserving the final fence as the retirement boundary for the full sequence.

Draw preparation extracts one conservative frustum per object, rejects the selected LOD bounds before testing its
submeshes, and applies the same plane representation to local-light ranges and non-mesh CPU VFX bounds. Shadow recording
retains off-camera casters, selects one highest-detail LOD, and rejects only static caster bounds outside each light
frustum; skinned and `Always Visible` casters bypass that rejection. Sampled scene depth is redrawn only while a GPU
depth-collision operation needs it, MSAA HDR resolves once after transparency, and a scene with no visible local lights
uses a constant-size Forward+ grid independent of viewport and camera changes.

The public `RenderSystem.cpp` PImpl facade delegates to separately compiled private backend units for device/frame
lifecycle, resource caches, surface/pipeline management, and scene recording. Frame execution, skinning, sampled-depth
and shadow recording, scene surfaces, GPU VFX preparation, VFX pipeline ownership, and VFX drawing are separate
translation units coordinated by `RenderBackendInternal.h`. Shared frustum and projected-size math lives in a private
header with no resource ownership. Mesh Renderers use imported submesh bounds by default; the versioned `Always Visible`
authoring flag explicitly bypasses camera-frustum bounds rejection for camera-relative or heavily deforming geometry
whose runtime pose cannot be represented by those static bounds. The flag does not bypass component visibility,
activation, material resolution, skinning, or draw submission. SDL handles and backend state remain absent from
supported headers.

Offline spatial lighting follows the same ownership boundary. `LightingBaker` consumes an immutable scene definition,
validated public asset values, transitive content digests, and explicit bake settings; it does not own an editor, scene,
or GPU device. The asset worker owns process isolation and cancellation, atomically replaces the scene's lighting-set
reference only after every artifact is published, and returns progress through the existing worker protocol. The editor
Lighting panel queues that operation but never writes cache or scene files directly. Runtime rendering loads only typed
lighting assets, publishes replacements transactionally, and fence-retires superseded GPU arrays. Cache manifests are
content-addressed, digest-verified, and disposable; source scenes retain only stable asset identities.

`Application` also owns one `UndoService` before layer attachment. Editors create bounded contexts per document rather
than retaining process-global history. Commands own forward/inverse behavior and availability checks; nested
transactions preserve all-or-nothing semantics. Contexts close during layer teardown, and the service closes before
scene and asset services so no history callback can observe a partially destroyed document service.

Prefab source editing swaps in a dedicated `SceneDocument` and retains the active scene document, including its undo
context, until Prefab Mode closes. Stable-ID source replacement validates imported bytes before atomic publication and
rolls back source plus metadata on failure. Apply-to-source computes a canonical source-ID view of the selected instance,
preserves scene-owned root placement, and records the source replacement and scene metadata update in one scene undo
command. Variant saves regenerate overrides against the composed base; unsupported nested-owner flattening fails before
publication.
Hierarchy-to-Project drops cross the Asset Browser controller as an object ID plus destination folder and reuse the
same prefab extraction transaction as named creation. Prefab thumbnails compose source assets on the UI owner thread,
resolve asynchronous mesh handles, then transfer immutable mesh references and world matrices to the bounded CPU
thumbnail worker.

Managed IDE generation consumes the same validated `.keireasm` graph and C# project generator as managed builds.
Persistent solution/project files are conveniences derived from canonical assembly assets and source roots; they do
not become build authority or expose runtime-host implementation state.
Each collectible managed load context receives its own Coral internal-call table. Calls cross an application-owned
`IScriptRuntimeServices` boundary, retain only value handles, validate the currently executing script generation, and
route gameplay logging, frame time, input actions, and transform access back to owner-thread engine services.
Coral's patched reflection registry assigns monotonic opaque IDs with reference-equality lookup, so hash collisions and
retired metadata cannot alias a later type, method, field, property, or attribute. Context, assembly, and reflected-method
caches are concurrent across independent runtime hosts; diagnostic load status is thread-local.
Managed build generations contain both the engine API and gameplay outputs. Source checkouts incrementally compile the
API project into generation-local storage, while packaged editors copy their bundled API; candidate reloads consume
that immutable pair transactionally rather than resolving a process-global API artifact.
Before Coral sees an API or gameplay assembly, the owner thread captures a bounded snapshot, verifies that size and
modification time stayed stable across the read, records its SHA-256 identity, and loads the retained bytes from memory.
This removes memory-mapped access to a live publisher path. A rejected candidate reports the symbolic Coral status,
source snapshot metadata, and the captured managed exception, then unloads only the candidate context. Windows package
entrypoints also share the repository workspace lock; cooked runtime smoke tests execute against an invocation-unique
content copy that is removed after validation, so parallel direct invocations cannot share mutable validation state.

## GPU VFX And Media Import Boundaries

Play Mode treats VFX backend rejection as a recoverable failure. When a GPU-incompatible effect compiles for CPU,
`SceneRuntimeSession` transactionally rebuilds its scene-owned VFX world on CPU and restarts the emitters. Effects
invalid on both backends are isolated: the runtime logs the entity/effect identity once, suppresses repeated activation
attempts for the same asset revision and Blackboard override set, and retries after publication of a new revision. The
scene remains `Playing` in both cases; only a failure of the shared VFX world update remains a session-level fault.

`VfxWorld` remains the backend-neutral scene facade. Render-capable scene sessions select the GPU backend; headless
tests and explicit compatibility policy select deterministic CPU simulation. Effect-asset construction and residency
accounting live in `VfxEffectAsset.cpp`, while
the larger authoring/compiler unit retains schema, lowering, and import responsibilities. Checkpoint byte encoding is a
separate internal value codec shared by capture and restore; it owns no world state.

GPU snapshots contain only immutable
per-emitter work descriptors, cumulative spawn sequences, per-handle simulation revisions, a world simulation-step
revision, a world reset revision, and aggregate limits, including resolved bounded custom instructions. They never
contain per-particle CPU snapshots. The renderer owns persistent particle, free-list, alive-list, counter, event,
dispatch, and indirect-draw buffers and mutates them only inside an active render frame. Each emitter additionally owns
a bounded filtered-index buffer and an indexed-compatible indirect-argument buffer. A handle-filtering pass rebuilds
that view every frame; spawn allocation uses its atomic count to enforce the effect's authored capacity before
committing a shared-pool particle.
The renderer treats the world's maximum as a logical safety ceiling rather than an eager allocation request. It sums
the capacities of immutable live-system descriptors, selects an amortized power-of-two physical pool with a bounded
minimum, and only grows that pool during the world's lifetime. This keeps capacity diagnostics and million-particle
effects intact without making small scenes allocate or scan a million 160-byte particle records. Newly accepted spawn
indices are compacted through the per-emitter output buffer, so the ordered Initialize, first Output, and strip-link
kernels dispatch by spawn count. Post-simulation compaction is also reused when an emitter has no new particles.
Physical growth is an explicit simulation-layout restart and produces a capacity diagnostic; pools never shrink while
the world remains resident, avoiding allocation churn when emitters stop.
The prior frame's per-emitter filtered indices are the next frame's simulation work list. Update and Output therefore
dispatch by bounded emitter capacity and reject threads beyond the prior live count, while all emitters finish reading
their views before compaction overwrites them. The global alive list cannot exceed the saturating sum of active emitter
capacities, so handle-filtering dispatches use that aggregate bound rather than the larger amortized physical pool.
Generation-qualified kill passes retire particles for one stopped, restarted, naturally completed, or incompatibly
reloaded handle without clearing unrelated emitters. A transform pass applies Local-space emitter position/rotation
deltas to that handle's existing position, velocity, and acceleration state. Only `VfxWorld::Clear` advances the
world-wide reset revision. Compute world initialization, handle retirement, Local transforms, alive-list reset,
handle-filtered per-system simulation, bounded spawn, compaction, and indirect argument finalization precede one
indirect output draw per system. Sprite and Ribbon output bind either the authored texture or the procedural fallback;
Ribbon segments connect consecutive live identities inside each bounded strip, and generation-qualified links prevent
recycled pool slots from joining unrelated strips. Volumetric uses an analytic density impostor. Sprite, Ribbon, and
Volumetric output compose particle tint with the assigned material Tint, primary texture, and alpha state through the
built-in particle surface contract. Mesh output binds asset vertex/index buffers and compatible composed material
shaders with per-particle size, full Euler rotation, tint, and Forward+ lighting. Mesh-surface and sparse-volume
spawn resources are uploaded as weighted tables, and GPU Depth collision receives sampled scene depth plus camera
matrices. Immutable constants and live parameters share one value table, while shape headers and samples share one
fixed-stride table, keeping execution within SDL's eight-readonly-storage-buffer compute contract. Cable-ordered Module
and Custom operations execute together in the relevant spawn or simulation dispatch. Update and Output evaluation use
separate compute kernels so large schema-4 programs retain ordered semantics without exceeding practical D3D12 register
pressure. Spawn, Initialize, and initial Output evaluation likewise run as three ordered kernels; deterministic module
random state is carried in particle state until the particle is committed to the alive list, so a rejected stage returns
the allocation exactly once without publishing a partially initialized particle.
Each active system adds one filtered dispatch over the world particle capacity; this explicit cost avoids
effect-specific pipeline creation while retaining deterministic cross-backend semantics. Shutdown releases pipelines
before the device and treats repeated close as inert.

The editor requests GPU VFX pipeline warmup when its rendered workspace attaches. A low-priority compiler thread creates
the complete backend pipeline set and publishes it atomically, so render recording never observes a partial set and Play
Mode does not wait on driver pipeline creation. Shutdown joins that worker before releasing the GPU device. Clients that
own a loading screen can request the same non-blocking work through `RenderSystem::RequestGpuVfxPipelineWarmup`; clients
that do not request it retain synchronous first-use creation for source compatibility. `RenderStatistics` exposes the
pending/ready state and elapsed warmup time. While an explicitly requested warmup is pending, GPU VFX presentation is
deferred without blocking the frame; simulation and unrelated rendering remain live.

Schema-5 `.keirevfx` documents make execution explicit with `LegacyModules` or `Graph`. The graph compiler accepts
multiple particle systems with Spawn or named Event sources and canonical Initialize, Update, and Output contexts;
stable-ID Block, Operator, and Parameter references; typed, single-driver, forward `ParticleStream` flow; and a directed
acyclic topology. Context stacks lower every Block occurrence to its own execution ID even when payload references
repeat. Blackboard parameters and SSA-style value expressions lower into typed slots/registers, and a generic property
ABI connects defaults, literals, parameters, or particle-varying registers to CPU and GPU Block execution. Activation,
component, live-world overrides, reload, transform, Stop, and events are transactional through one root handle that
owns bounded internal system slots.

Portable Custom HLSL is a bounded backend-neutral instruction language, not arbitrary shader compilation. The compiler
accepts up to 4,096 verified assignments to Position, Velocity, billboard Rotation, Tint, or Size using `=`, `+=`, or
`*=`, a literal or typed expression operand, and optional trailing `* DeltaTime`. Both simulation backends consume the
dynamic lowered instruction stream. Unrestricted Unity-style HLSL, arbitrary resources, branches, loops, decals, and
froxel injection remain explicit capability tiers.

Schema-1 `.keirevfxsubgraph` assets provide typed Operator, ordered Block, and complete System bodies. Import publishes
their sorted dependencies; compilation resolves immutable assets through an injected callback, validates purpose and
boundary ports, rejects direct or indirect recursion, and expands each instance with regenerated stable identities
inside bounded depth and size limits. Expansion completes before CPU or GPU activation and reload, so a missing asset,
purpose drift, cycle, or invalid expanded graph rejects the candidate transactionally. Subgraph support does not enable
disabled parity rows: the manifest retains 30 disabled entries, including 23 P0/P1 rows.

CPU texture, mesh, buffer, and attribute-map expressions cross a separate renderer-neutral `ResourceQuery` callback on
`VfxWorldSpecification`. Requests carry only a stable `AssetId`, operation kind, coordinate, integer index, and level;
results use bounded value lanes, dimensions, count, and transform data. Renderer handles, SDL types, and asset-system
ownership never enter the public graph or compiled-program ABI. A missing provider, rejected query, callback exception,
or invalid returned value fails the affected expression and records `SimulationValueInvalid`. These descriptors remain
explicitly CPU-only until the renderer exposes an equivalent cross-platform resource-table contract.

Schemas 1-4 remain readable. Historical module documents decode as `LegacyModules`, and explicit Save publishes schema
5 without changing their execution source; the explicit deterministic conversion operation replaces previous
presentation systems with one canonical graph while preserving emitter, payload, and Blackboard stable IDs.
Authoring annotations, comments, and subgraph declarations migrate in memory before canonical publication. Future
schemas fail before the live document changes. CPU-incompatible
features produce diagnostics rather than implicit substitutions. Managed VFX calls cross `IScriptRuntimeServices`,
validate entity/world/script generations, and enqueue component state for the scene-safe render boundary.

Asset import output may declare an effective primary type, but only from the importer's registered compatible type set.
The database validates that declaration before atomically publishing metadata, catalog records, dependencies, generated
subassets, and cache entries. Animation-only model containers use this path to become `AnimationSourceAsset` records
without mesh validation while retaining the parent asset ID and stable generated clip IDs.

Assimp receives primary model bytes from memory and a private `IOSystem` adapter for ordinary sidecars. The adapter
normalizes model-relative URI paths, confines them beneath the source root, and delegates every successful read to the
asset context's bounded project-file callback. Assimp never owns a native file handle or project root. Cached MTL,
buffer, and external texture bytes remain import-local; their SHA-256 records cross back only as source dependencies,
while Assimp stream and texture types remain below `KeireInternal`.

Native WAV, Ogg Vorbis, FLAC, and MP3 probing remains on miniaudio's in-process fast path. Broad media conversion is
keyed by source digest, stream selection, importer version, and codec configuration, and cached worker output is
validated before restoration. FFmpeg is pinned as the signed `n8.1.2` source submodule and source-built as private
shared libraries linked only by `KeireAssetWorker`. Custom AVIO reads the staged source and writes bounded FLAC output
in-process; FFmpeg types never enter public headers or runtime/editor processes.

## Public Binary Boundary

Public classes and free functions with KeireCore-owned out-of-line symbols use `KEIRE_API`. Exception types that cross the managed-client boundary are annotated as well so their type identity remains consistent in a same-toolchain shared-library build. Header-only value types, templates, IDs, and aggregates do not own exportable symbols and remain unannotated. `GetApplicationCommandLineDescription` and `CreateApplication` are the deliberate reverse boundary: the managed executable defines them for KeireCore, so they must not be marked as library imports. Script regressions keep this policy explicit as the API grows.

`noexcept` is reserved for operations whose complete implementation is non-throwing. Snapshot observers that acquire a standard mutex allow `std::system_error` to propagate; destructors, shutdown helpers, and other mandatory cleanup paths instead contain synchronization failures and preserve any exception already in flight.

## Automation Flow

```mermaid
flowchart LR
    Launcher["Platform launcher"] --> Identity["Identity and dependency locks"]
    Launcher --> Bootstrap["Tool and vendor verification"]
    Launcher --> Generate["Premake generation"]
    Generate --> Build["Compiler or IDE build"]
    Build --> KeireTests["doctest and KeireClient smoke run"]
    KeireTests --> Coverage["LLVM coverage"]
    KeireTests --> Package["Runtime and SDK archive"]
```

The scripts resolve `default` to a concrete compiler before generation. Architecture defaults to the native host and may
be overridden with x86_64 or ARM64. A generation stamp covers generator, architecture, toolset, CI warning policy,
Premake/config content, and the first-party source inventory, so adding or removing translation units regenerates stale
IDE/Ninja metadata automatically.

## Project Ownership

`.keireassetpackage` is a project-content boundary separate from the signed Editor/Build Support `.keirepackage`
distribution format. Kéire Core owns the deterministic `KEIRASPK1` parser, canonical manifest, bounded extraction, and
project transaction engines without owning marketplace HTTP, credentials, or signing keys. `ProjectPackageManager`
resolves registry dependency closure into an exact source-controlled lockfile, verifies immutable content in the Hub-
owned global cache, mounts it read-only, and supports transactional embed/revert/remove. `ProjectAssetPackageImporter`
owns dependency-aware selective imports, three-way update decisions, executable-code consent fingerprints,
source-controlled receipts, safe removal, rollback journals, and interrupted-operation recovery.
The parser's deterministic mutation corpus distributes truncations and bit flips across a valid archive and appends
trailing-data cases; every rejection must leave the authorized staging parent without a partial extraction tree.

The Editor Package Manager is a presentation/controller layer over those public contracts. Marketplace Asset Import
first verifies the publication and archive, computes a complete preflight plan, and opens an explicit review modal. The
modal exposes planned install/replace/reuse/keep-local actions and unresolved conflicts; project writes remain disabled
until the plan is valid and the user confirms it. The Editor never receives Hub OAuth tokens. Website product links use
a strictly parsed `keirehub://marketplace/product/<UUID>` activation that is forwarded
to the already-running primary Hub through the existing single-instance channel. Hub alone reads the account session,
registers the current OAuth device session, walks bounded catalog/library pages, and obtains the short-lived grant. The
  grant carries the immutable signed publication envelope already accepted at release publication. Hub verifies
  that envelope against every key in the packaged, bounded Marketplace trust bundle and requires its product UUID,
  version UUID, archive size,
archive SHA-256, manifest SHA-256, storage path, sequence, and expiry to agree before downloading the content-addressed
archive. It then verifies the exact bytes and package identity before the item becomes ready.

Hub publishes catalog, entitlement, requested-product, progress, failure, verified-cache identity, and the owning
account ID beneath the per-user Hub cache. Current Editors prefer the bounded, atomically replaced schema-3
`marketplace-cache-v3.json` snapshot. It contains the public signed publication envelope but no bearer token, refresh
token, signed URL, proxy credential, service credential, or private signing material. While Hub is running it also
renews a token-free, 15-second account-session lease every five seconds and writes a signed-out lease immediately on
sign-out or orderly shutdown. The Editor exposes or consumes My Assets only while that lease is current and its account
ID exactly matches the cache. Missing, expired, signed-out, corrupt, and cross-account states fail closed.

Hub atomically replaces the schema-1 and schema-2 compatibility files with empty projections whenever it publishes
schema 3, preventing an older Editor from disclosing stale entitlement names after sign-out. Current Editors retain
migration support for existing schema-1 and schema-2 files but require a new account-bound Hub synchronization before
displaying them. They derive the archive path from the trusted digest rather than accepting a stored arbitrary path,
independently verify the exact signed publication document, and repeat archive size, SHA-256, manifest, and payload
verification before a project transaction. The durable snapshot remains the production package handoff; the short
lease proves only that the matching local Hub session is live. Entitlement authorizes download access, while the
independently Ed25519-signed publication and its bound archive digest establish integrity independently of the online
grant service.
Projects without package files remain valid, while the first successful package transaction raises
`minimumEngineVersion` to 0.3.1 atomically.

Project identity is separate from repository/template identity. `Project` validates the fixed marker, owns the canonical
root and exclusive editor lock, and supplies derived paths for Assets, catalogs, workspace state, input overrides, scene
recovery, logs, and builds. `Application` opens a project before logging and all project-backed services, then releases it
after layers, Input, Scenes, and Assets stop. No service consults the process working directory as an implicit project.

`ProjectRegistry` is Hub-owned per-user discovery state. `KeireHub` may inspect and launch projects but never owns editor
assets or the exclusive lock. Each detached KeireClient process revalidates and locks its requested project. The packaged
`samples/KeireSandbox` is a complete project and is validated through the same asset tool contract as user projects.
KeireClient keeps native window placement below project-local `Library/UserSettings`: normal bounds remain separate from
maximized/fullscreen state, restore occurs before the window becomes visible, and minimized state is never persisted.
The Hub coordinates one primary process per canonical executable identity. Secondary launches send one typed Show,
Navigate, Open Project, Import Package, Install Version, Build Support, or OAuth Callback action and exit without
creating a window or tray handle. The binary activation frame has an explicit magic value, protocol version, total
length, action, field
count, and length-prefixed UTF-8 fields, with a 512-byte limit chosen to preserve atomic FIFO writes on Unix. Windows
shares the explicit frame length under the existing named-mutex channel. Decoding rejects unknown versions/actions,
truncation, trailing bytes, invalid UTF-8/control text, relative or traversing paths, invalid identifiers, and surplus
fields. The primary polls validated actions on its owner thread. `WindowSystem` tracks weak tray ownership and closes
every surviving native tray before SDL shutdown. Activation dispatch reuses normal page navigation, project opening,
and compatible Build Support import paths. An unavailable editor catalog ID, unsupported offline package type, or
missing version-specific Asset Tool records a warning notification and queues nothing. Layer teardown, explicit Quit,
and exceptional application shutdown therefore converge on the same idempotent cleanup path.

## Scene Ownership

`SceneAsset` is immutable imported data. `Scene` is an owner-thread mutable instance with weak object handles, validated
transactional hierarchy mutation, and explicit dirty/open lifecycle. `SceneSystem` owns loaded runtime instances and
pending operations. Asset workers decode immutable data; `Application` pumps completions, then Scenes commit loads,
unloads, and active changes at a safe frame boundary before Input and layer updates. Failed loads never replace the
last-good loaded set.

Mutable scenes retain deterministic hierarchy order and parent-to-child adjacency until a create, destroy, reparent, or
reorder invalidates the cache. Transform changes walk that adjacency once to dirty the affected subtree; an already
dirty root proves its current descendants are dirty and needs no repeated scan. Callback-bearing lifecycle operations
copy cached hierarchy views before invoking component code so structural mutation cannot invalidate their traversal.

The editor owns authoring selection, undo/redo, atomic source writes, dirty decisions, and recovery files. Runtime scene
activation is refreshed only after source validation/import succeeds. JSON remains private to the scene importer.

Scene schema v6 stores stable entities and component records, prefab instance/override state, entity layers and tags, scene
lighting-bake settings, and an optional baked-lighting identity. The public ECS surface owns stable IDs, weak `Entity`
handles, reference-counted `Component` instances, registration metadata, and Kéire math values. EnTT owns native entity
storage privately and GLM implements matrix/quaternion operations privately. A component registry is application-owned
through `SceneSystemSpecification`; duplicate IDs and incomplete registrations are rejected before a scene uses them.
Schemas v1-v4 migrate in memory, while unknown component records remain round-trippable Missing Components.

`SceneRuntimeSession` clones the in-memory authored scene for Play while retaining entity IDs. `SceneRuntimeWorld` owns
the ordered set of sessions shared by Editor Play and the packaged player, assigns non-reused opaque handles, and
commits additive load/unload/active changes at owner-thread safe boundaries. Each session retains isolated physics,
audio, VFX, UI, and managed lifecycle state while all loaded sessions tick and render. A persistent hierarchy keeps its
original session as an unloaded carrier so identity and lifecycle are not reconstructed across transitions. Pause
suppresses update callbacks, Step advances one fixed tick, and world Close stops every session. Component callback
exceptions fault only their session and preserve the edit scene. Detailed contracts live in
[ECS And Components](ECSAndComponents.md).

Managed entities use an opaque identity derived from the owning `SceneState`, shared by every Behaviour in that
runtime scene. Internal calls resolve an entity through any live Behaviour anchor in the same world and then through
the scene's stable entity table; stale worlds and destroyed entities therefore become inert without exposing native
pointers. Managed physics queries are routed through the application-owned runtime-services boundary and map
`PhysicsBodyId` values back to stable entity IDs. Collider and Rigid Body components remain serializable scene data,
while the Play adapter owns the isolated physics world and tears it down before the runtime scene closes.

The same boundary owns entity names, active-in-hierarchy state, hierarchy traversal, component registration lookup,
enabled state, and deferred component mutations. Managed `Entity` and concrete `Component` wrappers are canonical for
one runtime generation, carry only stable identity, and become non-null invalid objects after destruction. Managed
Behaviour lookup is resolved from a generation-local weak registry, so one Behaviour can reference another without
retaining an obsolete load context after hot reload.

Managed field discovery projects primitives, enums, direct engine-object references, and bounded nested
`[Serializable]` members into ordinary `ComponentProperty` descriptors. Stable field IDs remain the serialized
identity while dotted property paths address nested Inspector leaves. Managed-data fields without an explicit ID
derive one deterministically from the managed asset type and property path; an explicit ID remains the rename-stable
production contract. Validated display labels, headers, groups,
tooltips, slider intent, one-sided bounds, drag steps, multiline height, and read-only state flow through the same
descriptor path used by native components and managed data assets. Canonical state JSON stores entity IDs without runtime-world
identity. Tagged state-v2 references record entity identity, concrete component type, or stable asset identity;
restoration rebinds scene objects to the destination world before any lifecycle callback runs.

Audio playback crosses `IScriptRuntimeServices` as a value request containing the validated clip asset ID, output bus,
gain, pitch, priority, loop/spatial flags, and attenuation distances. The Play adapter creates or updates the scene
Audio Source and presentation runtime at the safe boundary. Editor-only asset preview uses a separately tracked voice
on the `EditorPreview` bus and is stopped when selection changes or the workspace detaches.

Audio asset import probes WAV, Ogg Vorbis, FLAC, and MP3 in-process through miniaudio. Other registered codecs and
media containers are normalized to lossless FLAC by an injected asset-worker backend using private FFmpeg shared
libraries. Custom AVIO callbacks avoid process creation and temporary source/output files; packet decoding and
resampling remain bounded, stream selection is explicit, and only the asset worker loads FFmpeg.

Game-specific simulation such as weapons, ammunition, damage, ballistics, recoil, inventory, and combat HUD policy
lives in each project's managed assembly. The engine exposes generic input, time, scene, physics, audio, VFX, material,
animation, asset, and UI services without owning a combat model. Starter-project examples are copied project content,
not part of `Keire.Managed`, so games can replace their rules without depending on an engine gameplay framework.

`SceneDocument::ActiveScene` is the editor authoring target: it resolves to the clone during Play and the edit scene
otherwise. Play edits have an isolated undo context. A snapshot-derived change set compares entity identity, hierarchy,
component presence/enabled state, and registered property bags before Stop; selected changes produce one validated
replacement definition and one authored-scene undo command. `ScenePlayChangeTracker` records the exact structural or
component path and editor-produced value before simulation advances again, so the review can distinguish Editor,
Runtime, and Mixed final values. Its dependency graph locks required created ancestors/components and rejects
delete/edit or remove/edit contradictions before definition validation.

The primary Play session is assigned before its managed `Awake`/`OnEnable` traversal and adopted into the runtime world
after startup succeeds. Editor managed services therefore resolve the pending primary session when world lookup has not
yet become available; this matches packaged runtime startup and keeps rendering, audio, physics, and UI component access
valid throughout lifecycle callbacks. Managed callback failures retained by `ScriptSystem` are advanced through a
per-service cursor and published once to the Editor Console with their generation, type, callback, entity, and message.

Play-stop decisions are queued from UI callbacks and executed at the next update safe boundary. Render submission
captures camera, lighting, transforms, mesh/material identities, and tint into an immutable frame-local packet, so
device recording never queries a Scene that another lifecycle transition has closed. Docked panel focus is requested
through `UiPanelRegistration` without exposing Dear ImGui: Play selects Game, review/cancel retains Game, and a completed
Stop selects Scene.

## Reference Ownership

Project-owned shared objects derive from `RefCounted` and are constructed with `CreateRef`. `Ref` and `WeakRef` point at an external atomic control block containing a type-erased deleter. The last strong release destroys the object; the implicit weak owner then releases the control block when no explicit weak references remain. `WeakRef::Lock` uses atomic increment-if-nonzero, so it cannot resurrect an object or race its destruction. Cyclic graphs must contain at least one weak edge.

## Logging Lifecycle

KeireCore owns a private spdlog thread pool and two asynchronous loggers inside a reference-counted `LogState`. Public
headers expose only `LogLevel`, `FormatArgument`, `LogMessage`, and logger handles. Kéire parses its supported `{}`
formatting subset before crossing the implementation boundary, so neither spdlog nor fmt types, headers, or compile
levels are part of the SDK contract. The implementation does not register global names, replace the spdlog default, or
shut down unrelated state. Handles keep the state storage valid but take operation locks only while making calls.
Shutdown detaches the global state, takes its exclusive operation lock, flushes pending work, and closes it. It may wait
for an active call but not for a handle's lifetime; detached handles observe the closed state and become safe no-ops.
Accepted Core and Client writes also enter a bounded structured record history under a separate short lock. The editor
polls that history from its owner thread and advances an opaque sequence, so startup and worker messages reach the
Console panel without invoking UI from logging threads or exposing spdlog across the boundary. Coral's host callback is
adapted to the Core channel instead of writing around this path.

File paths are intentionally relative to the process working directory. Scripts and generated IDE targets set that directory to the repository root, producing consistent `Logs/Core.log` and `Logs/Client.log` paths.

## Window Platform Boundary

Public headers contain only Kéire value types. SDL pointers, flags, identifiers, headers, and raw events live in `Window.cpp`. `WindowSystem` owns SDL video state and is unique while active; each abstract `Window` delegates through a reference-counted private implementation. The creating thread owns SDL calls. Native window creation remains under an RAII guard until the handle and both internal indexes commit as one transaction. A final window release on another thread adds its opaque ID to a destruction queue, which event polling and shutdown drain on the owner thread.

The polling translator updates cached state before returning each `WindowEvent`, preserving SDL ordering. A private
tokenized router forwards every native event to application-owned UI and Input sinks before public translation, so
neither subsystem can consume events away from the other. Logical dimensions, pixel dimensions, display scale, and
requested cursor mode remain distinct and observable without exposing SDL.

Configuration is a separate typed boundary. nlohmann/json parses implementation-side input into `WindowSpecification`; callers never depend on JSON types. Strict keys and bounds make configuration mistakes deterministic rather than silently accepting typos.

## Application And Layer Runtime

`Application` is an explicit, single-run orchestrator for logging, event/time services, the SDL window system, the primary window, and an application-bound `LayerStack`. Construction is side-effect free; `Run` initializes services in dependency order and unwinds them in reverse order even when client callbacks throw. The construction thread owns `Run` and layer mutations, while `RequestExit` is the cross-thread application control. KeireCore owns the executable entrypoint, dependency-free help/version handling, the top-level exception boundary, application lifetime, and the `Run` call. A managed client defines a static `GetApplicationCommandLineDescription` and `CreateApplication`, keeping option documentation and validation client-owned. Because the entrypoint is an unreferenced member of the static library, tests and low-level SDK consumers that define their own `main` do not pull it into their executables.

`LayerStack` owns layer records and lifetimes, the overlay partition, pending structural operations, activation, traversal, and reverse-order teardown. Fixed and variable updates traverse bottom-to-top, while events traverse top-to-bottom. A depth-counted traversal guard keeps push/remove operations deferred through nested dispatch and callback re-entry until an application safe boundary. Ownership is unique, attachment is exactly once, automatic subscription creation is disabled once detachment begins, tokens disconnect before `OnDetach`, and shutdown detaches in reverse order before client shutdown and service teardown. `Application` coordinates safe boundaries and delegates its convenience layer methods to the stack.

## UI Runtime

`UiSystem` is an application-owned private implementation. `UiMode::Disabled` preserves the pre-UI runtime, `Headless`
creates a deterministic context without platform or graphics state, and `Rendered` initializes Dear ImGui's SDL3
platform backend plus a private bridge into the application-owned `RenderSystem`. RenderSystem owns the SDL_GPU device,
window claim, swapchain, and presentation lifecycle described above. Partial initialization unwinds in reverse order.
Shutdown removes event forwarding, persists the active layout when configured, closes the UI renderer/platform bridges,
and destroys the context before RenderSystem releases GPU and window resources.

Scene submissions carry a Kéire-owned `RenderEnvironmentSettings` value. JSON persistence stays private in
`ProjectSettings/Rendering.keiresettings`; public headers expose only colors, scalar values, paths, and validation
functions. Fragment-stage lighting consumes that environment together with the deterministic active Directional Light.
`ProjectSettingsDocument` owns the editor draft, validation, dirty lifecycle, atomic save, and coalesced undo command;
the workspace consumes its immutable settings for Scene and Game submissions instead of retaining a mutable copy.
Every primary editor panel owns its registration and persistent UI state. `SceneViewportPanel` owns its render view,
camera, framing, picking, marquee selection, gizmos, toolbar, and viewport drops; `HierarchyPanel` owns traversal and
structural commands; `InspectorPanel` owns component inspection while its `AssetInspectorPanel` composition owns asset
dispatch, diagnostics, naming actions, and material content; and the input-actions, project-settings,
asset-browser, console, and diagnostics panels own their respective tools. Panels receive document data, frame-value
snapshots, and named commands through narrow contracts; none retain, friend, or inspect `EditorWorkspaceLayer`.
Viewport-local overlays are invoked by the owning panel before its RAII scope ends, so backend draw-list ownership cannot
fall through to an implicit debug window. Scene and Game pass their own camera-local visibility diagnostics into those
overlays. FPS, profiler categories, dispatch/indirect totals, recording timings, and CPU preparation remain one completed
renderer-wide frame aggregate shared by both overlays.
Continuous Inspector Transform drags use compact typed undo commands whose final value is updated as samples merge.
Play-mode origin tracking records only the edited Transform component, avoiding whole-scene snapshot, JSON encoding,
and diff work on every pointer sample while preserving Editor versus Mixed change classification.
`UiPanelRegistration` supplies a common session-local view lock. The UI boundary prevents locked panels from moving,
resizing, or collapsing, while selection-driven client panels retain only stable entity or asset IDs and validate them
before each draw.
During Play Mode, Scene and Game retain separate camera and input ownership: Scene always renders through its persistent
editor camera. The editor disables automatic input-user joining and explicitly pairs the fixed keyboard and mouse to its
owned user; pairing failures are fatal during workspace initialization instead of producing a zero-valued gameplay
context. The `Player` map and UI-capture override are validated before the Play session starts. Game input engages from
hover or a runtime capture request, even while dock focus is settling, and remains latched while its panel owns focus.
Losing application focus releases native capture without changing the requested runtime mode. Escape has a global,
editor-owned safety release when managed input fails; clicking the Game image re-engages that suspended capture without
warping the pointer. Hovering Scene therefore routes navigation exclusively to the editor camera without mutating
runtime camera state.
The workspace implementation is kept below 1,500 lines and is limited to service construction, frame order, command
binding, notices, and modal arbitration.
`EditorWorkspaceLayer` remains the owner-thread composition root, while non-copyable internal coordinators own document
transitions and close conflicts, packages, replay/profiling presentation, managed-runtime build scheduling, Play Mode
orchestration, asset-operation admission, and build/cook lifecycle. Each coordinator accepts narrow callbacks or
document references, invalidates generation tokens before idempotent `Shutdown() noexcept`, and rejects off-owner
updates before state or callbacks change. A fixed named-phase trace characterizes the legacy update and teardown order;
delegation does not reorder those phases, late callbacks cannot re-enter a closed workspace, and one shutdown failure
does not prevent later authorities from closing.
The document/workspace coordinator is the sole owner of the primary document objects. The composition root keeps only
owner-thread-affine, non-owning aliases for command and panel binding; replacing the active Scene document (including
Prefab Mode entry and exit) transfers ownership through the coordinator. Mutable coordinator queries and document
access are owner-thread checked, while only a captured generation token's `Current()` observation is cross-thread safe.
Construction moves one complete document bundle into the coordinator before command binding and clears the staging
owner, so the running workspace never co-owns a primary document.
The named workspace teardown invokes `Shutdown() noexcept` while panels and callbacks are alive; destruction closes
callback admission and releases the owned documents without re-entering already-destroyed workspace dependencies.
All authoring mutations, including menu primitives and viewport mesh/material drops, cross `SceneDocument`; workspace
code may inspect an active scene for presentation and picking but does not create, destroy, or edit scene objects itself.
Hierarchy multi-moves validate every source, destination, insertion sibling, cycle, and preserved world transform before
mutation. Selected descendants collapse under their selected root, and the validated roots move in payload order as one
editor transaction.
KeireClient owns the separate Scene gizmo controller and uses only the public UI drawing facade, so neither ImGui draw
lists nor GPU handles cross into client code.

Raw SDL events are forwarded through an implementation-only sink inside `WindowSystem::PollEvent` before Kéire's existing typed translation. Neither the sink, native window, SDL event, GPU device, nor swapchain appears in a public header. `UiCaptureState` is copied out as Kéire values for future input routing.

After fixed and variable updates, `Application` begins one UI frame, creates the root dockspace, and delegates
bottom-to-top UI traversal to `LayerStack`; overlays execute last and structural changes remain deferred. `UiFrame`
validates owner thread and active generation. Its RAII scopes balance backend begin/end calls during normal returns and
exception unwinding. Typed color and scalar/vector style scopes map semantic public roles to private backend slots and
restore them through the same generation-safe stack, so product UI code never owns a Dear ImGui style stack. Scene/Game
declarations and UI draw data are recorded into one coordinated RenderSystem frame.

`SceneTransitionCoordinator` is the editor-only serialization point for Open, New, Close, and Exit. UI, shortcuts,
Project actions, internal drops, and post-import external drops enqueue requests; the next update preflights the target
before replacing the document. Dirty-scene and Play-change decisions approve or cancel that same request, and duplicate
requests remain non-destructive. Panels use `Scene::IsOpen()`-filtered document views so an obsolete reference is inert.
Minimized or unavailable swapchain textures are skipped safely. Docking is active, while multi-viewports are forced off
because detached native windows require a separate ownership milestone.

`UiWorkspace` is an optional application-owned profile service layered above `UiSystem`. Panels register stable IDs and receive move-only registrations; the workspace owns visibility and submitted backend names without exposing Dear ImGui. The Default layout is an immutable factory recipe expressed through `UiLayoutBuilder`, while named layouts capture docking state plus known and unknown panel visibility. Changes autosave to a current-session document and the active custom profile. Explicit reset reapplies the factory recipe; custom profiles support save-as, rename, delete, and portable import/export.

Workspace catalogs, layouts, and custom themes use versioned, bounded JSON documents. Unknown or duplicate keys, invalid types, unsafe names, non-finite theme values, and oversized input are rejected before activation. Writes use a temporary file and recoverable backup replacement. Normal storage lives below `SDL_GetPrefPath(ProjectName, ProjectName)/Editor/Workspace`; an explicit directory supports tests and tools, and ephemeral workspaces perform no disk writes. Native file dialogs are asynchronous: callbacks copy results into a synchronized mailbox and the UI owner thread applies them at the next frame boundary. Shutdown makes late callbacks inert.

Themes cross the public boundary only as stable semantic tokens: canvas, panel surfaces, text, accent states, selection, status colors, spacing, borders, and rounding. Private code maps these tokens to backend style slots. Kéire Dark, Kéire Light, and Classic are immutable; custom themes persist as `.keiretheme` documents. Preview applies at a safe frame boundary, while persistence remains explicit. The client editor enforces Save/Discard/Cancel when a dirty theme would be switched or closed.

Opaque `UiImage` values extend that boundary for editor thumbnails. RGBA uploads happen only on the UI owner thread;
GPU texture identity and release remain private. The client asset browser owns a bounded thumbnail worker and deterministic
project-local cache, then transfers completed pixels through the façade. Its non-owning visible-record view is valid
only for the matching editor-published asset-record revision; a revision, folder, or search change rebuilds the sorted
slice before use. Thumbnail preparation consumes that active slice, preventing whole-catalog work on ordinary frames.
See [Asset Browser](AssetBrowser.md) and [Editor Panels And Commands](EditorPanels.md).

Configuration examples, application-facing workflows, storage details, and troubleshooting live in the
[UI Workspace Guide](UiWorkspace.md).

## Asset Runtime And Pipeline

## Scene Presentation Runtime

`ScenePresentationRuntime` is the per-session boundary for retained UI Toolkit documents and scene audio. A scene owns
only `UIDocumentComponent` references; visual hierarchy (`.keireui`), cascading style (`.keirestyle`), and presentation
policy (`.keireuipanel`) remain versioned assets. Candidate document revisions are completely loaded and instantiated
before replacing the last-good tree. Stable element IDs map generation-safe runtime nodes back to their owning scene
entity without exposing scene, asset-system, or GPU pointers to the public UI model.

The retained tree owns dirty style/layout/geometry propagation, ordered focus, hit testing, nested clipping, bounded
events, and immutable draw-command capture. Screen and camera panels are submitted only to Game/Play/runtime output;
the Scene viewport cannot composite or intercept them. Render-texture panels select explicit offscreen targets.
World-surface panels retain physical dimensions, pixels-per-unit, transform, and depth policy; presentation maps input
rays through panel UV and renderer submission treats the panel as scene content. Additive scenes retain deterministic
document order and dispatch pointer input from the topmost document backward until handled.

Accepted render frames copy UI values, immutable text geometry, and logical texture and surface leases. No frame packet
borrows a visual element, scene pointer, native surface, or mutable draw list. Every lease is qualified by frame slot
and device generation; device loss invalidates the GPU realization and the retry rebuilds it from retained CPU assets.
The first production font slice uses one deterministic, fixed 95-character printable-ASCII fallback atlas rasterized
at high resolution from the embedded scalable font so large and world-surface text remains filtered rather than
pixel-stretched. Authored
font IDs remain in immutable draw commands, but the renderer deliberately resolves every text run to that fallback
atlas until custom font rasterization exists. Unicode shaping, bidirectional layout, ligatures, and script-specific
fallback are not implemented yet. Unsupported code points render the fallback glyph. Nested clip
intersections are resolved into immutable commands before capture, so atlas geometry is clipped deterministically on
screen, render-texture, and world targets. Renderer statistics truthfully report glyph-atlas occupancy and bytes;
ordinary UI images remain independent texture leases and therefore do not count as an image atlas.
Editor Play Mode and packaged players share the same synchronization, input, capture, recovery, and teardown paths.
Managed custom controls are published from an explicit successfully-loaded assembly allowlist, and failed managed
generations retain the previous immutable type catalog.

`UiBuilderDocument` and `UiBuilderStyleSheetDocument` own validated authoring definitions, generation counters,
dirty state, explicit persistence, and independent undo histories. `UiBuilderPanel` translates hierarchy, preview,
Inspector, binding, selector, source, and debugger interactions into those document operations. Its preview settings
are transient and never mutate `UiPanelSettingsAsset`. During Play Mode, `UiBuilderLiveDraftSession` publishes only
the active dirty visual tree as a development asset, remembers the imported baseline, and either commits after an
explicit Save or restores that baseline on document switch, reload, workspace shutdown, or Play teardown. Live
debugging consumes immutable, generation-stamped scene-presentation and renderer-statistics snapshots; unavailable
providers remain explicitly unavailable, and picking never exposes mutable runtime nodes to editor code.

Editor material authoring has three deliberate workflows. `MaterialDocument` and `MaterialInspectorPanel` own compact
Direct Material editing. `MaterialGraphDocument` and `MaterialGraphPanel` own the authoritative OpenPBR/slab surface
program, stable parameters and textures, compatibility diagnostics, dirty lifecycle, and bounded undo/redo.
`ShaderGraphDocument` owns target-based UI, Fullscreen, VFX, Custom Graphics, and Compute programs. Legacy Surface
remains a compatibility target until transactional material migration is complete.
`MaterialInstanceAsset` stores only inherited property and surface overrides and is edited through the Inspector. All
three preserve a tagged raw-Shader or Shader-Graph reference behind the same runtime material boundary. Material file
snapshots enter the project-assets undo context; the workspace and asset-operation service coordinate persistence.
Continuous numeric/color edits update a development-only in-memory asset revision for immediate rendering and share a
property-scoped undo command until the UI edit boundary. The final serialized source is written once and its catalog
refresh runs in the background. Startup mounts a current development catalog directly; stale non-startup sources are
refreshed after the editor becomes usable.

`ShaderGraphDocument` is a separate authoring document layered on the shared stable node-graph canvas rather than a
second graph interaction implementation. It owns graph validation, undo/redo, compile options, generated diagnostics,
and the last-good preview definition and compilation. `ShaderGraphPanel` owns only transient selection, inspector
buffers, searchable node-palette state, graph gestures, output/preview controls, bounded adaptive software-preview
pixels, and presentation. Built-in and function-call node construction lives in `ShaderGraphNodes.cpp`; compilation,
validation, and shader emission remain in the core graph unit and consume those value-only definitions. The preview
evaluates the validated built-in graph per shaded sample using the same coercion,
UV, procedural, shaping, surface, and neutral texture-semantic rules as generated shaders; unsupported custom functions
retain a bounded node-default fallback. The shared stable canvas installs an RAII draw-list clip covering the exact
canvas rectangle, so nodes and connection feedback cannot escape into adjacent preview or inspector regions. The graph
schema-6 descriptor catalog assigns stable node type IDs, canonical pin contracts, cost metadata, legal shader
stages, graph purpose, stable referenced-asset identities, shared editor-only authoring metadata, and a conservative
maximum world-position-displacement radius for occlusion safety. Shader schema 6 adds an explicit target definition
with legal stages, fullscreen injection point, and compute thread-group size. Schema-v1/2/3/4/5 data is upgraded in
memory through deterministic derived pin IDs, target inference, and a zero displacement radius where the old schema
had no bound; only publication writes schema 6, and future schemas fail before the document or last-good preview
changes. This keeps migration from
dirtying the same asset differently across machines. The compiler computes endpoint-aware reverse reachability from the single Master node
before lowering vertex and fragment expressions; structured Material Attributes and BSDF values remain typed through
validation and preview, then lower into private generated-shader structs rather than entering the renderer property ABI.
The resulting immutable statistics and advisory diagnostics expose unused work, texture samples, estimated ALU, and
variant pressure without weakening compile success. Built-in nodes are validated against canonical pin contracts even
when disconnected, while legacy Master nodes accept neutral defaults for later surface and attributes inputs. Graph
compilation is revisioned, debounced, and performed away from the owner thread; stale completions are discarded and only
the newest valid result can replace the last-good preview. The workspace supplies confined include reads, nonblocking
custom-mesh resolution through the asset system, and persistence. Both graph editors reserve the canvas as the dominant
region and place a bounded, collapsible square preview on the right when space permits. Material Graph evaluates its
schema-6 surface graph against the selected reusable shader contract before preview.
Shader Graph expands reusable calls first. Preview evaluation failures remain
visible diagnostics rather than being converted into an indistinguishable checkerboard result.

Shader, Material, and VFX panels share ordered multi-selection, batch movement/deletion, protected anchors, comment
regions, annotation bubbles, bounded canonical clipboard remap, arrange commands, bookmarks, and selection/graph
framing. Documents apply each accepted multi-item operation as one undo transaction. Comments and annotations persist
with source authoring metadata but never enter shader, material, or VFX runtime ABIs. Named reroute declarations and
persisted nested local-graph stacks are not part of the 0.4.0 contract; cable routing remains presentation geometry.
See [Unified Graph Authoring](GraphAuthoring.md) for the interaction and migration contract.

Shader schema 6 retains the renderer-neutral resource declarations and displacement bound introduced by schemas 4 and
5, while Material schema 6 carries the same portable sampler, Texture2D-array/cube/3D, and bounded
read-only structured/byte-address buffer declarations through encoding, reflection, dependency extraction, and typed
material overrides. Generic backend GPU asset and binding realization for array/cube/3D textures and user buffers is
deferred. Runtime import rejects those resources until the backend contract exists instead of manufacturing a fallback.

Reusable Material Function, Shader Function, Material Layer, and Material Layer Blend assets wrap the same typed graph
model behind distinct immutable asset types. Their editor document runs in purpose-aware reusable mode: it validates
and persists the body without invoking standalone shader publication or representing the asset as a material. Parameter
nodes define typed call inputs and output-node inputs define call outputs. The compiler resolves references through a
callback rather than taking asset-system ownership, detects missing/wrong-purpose assets, interface drift, recursion,
and excessive depth, then deterministically clones reachable bodies from stable call-site/source identities before
ordinary stage analysis and lowering. Importers publish sorted dependency edges so changing a function invalidates its
consumers. Material Parameter Collections remain renderer-neutral asset/value definitions plus a revisioned,
thread-safe runtime state object; renderer-wide collection buffers and world ownership are deliberately not hidden in
the asset layer. Dynamic Material Instances similarly expose typed snapshots and revisions without exposing GPU or
backend handles.
The managed runtime world in Editor Play and packaged players owns loaded collection states, carries compatible
overrides across asset revisions by stable parameter ID, and contributes one bounded immutable property snapshot to
each render request. Collection defaults are therefore asset-owned while mutable values die with the world. Both hosts
use the same state owner and precedence contract. The renderer applies shared-material, global-collection,
renderer-block, and material-slot-instance values in that order. Duplicate global names resolve by stable collection
asset order; narrower renderer and slot scopes always win. Mesh Renderer slot instances remain transient component
state and are cleared when their shared slot material changes, preventing incompatible values from crossing an asset
assignment.
Successful graph revisions bake parameter defaults into the stable generated material and publish that material through
the owner-thread development-asset boundary, giving scene renderers immediate immutable revisions without accepting an
invalid graph. Save stages the complete deterministic shader directory under `Library/Transactions`, preserves metadata
for retained variants, moves the previous generated directory aside, and publishes the canonical graph source only
after the staged directory is live. Any source-publication failure restores the previous generated directory while
preserving the original exception. The asset scanner ignores engine atomic-write temporaries and editor backups, so a
concurrent scan cannot assign identities to files that will disappear at commit. A successful save then queues a
targeted import of the parent graph, every generated shader/material subasset, and dependent loaded assets. Runtime
`MaterialGraphAsset` and `MaterialInstanceAsset` remain immutable data. Schema-6 Material Graph import compiles its
authoritative OpenPBR/slab surface program through the shared graph compiler and publishes one runtime material.
Schema-1–5 assets upgrade deterministically and preserve their surface expressions under the canonical
`surfaceGraph`. Material
Instance resolution starts from a Direct Material or Material Graph root
before applying at most 16 ancestors; it rejects cycles, unknown properties, and type changes without introducing
mutable renderer-global state. Instance import publishes its own stable ordinary `MaterialAsset` subasset referencing
the inherited shader variant; editor pickers and viewport drops alias the authoring instance to that renderer-safe
identity. Legacy `ShaderGraphInstanceAsset` remains registered for 0.1.x project compatibility but is not offered for
new creation.
Catalog-producing editor work is isolated in the private `KeireAssetWorker` executable. `AssetOperationService` owns
one child at a time, prioritizes external imports and explicit actions ahead of cook and coalesced material refreshes,
and exchanges versioned request/progress/result documents under `Library/AssetOperations/<operation-id>`. A worker
publishes a validated source-index snapshot beside the development catalog; the editor reloads that immutable snapshot
instead of hashing the project again. Cancellation is cooperative until publication. Shutdown waits 250 ms, terminates
an unresponsive worker, and lets the existing publication journal recover before another operation exposes records.
Native process handles and protocol JSON remain implementation details.
Every database owner also acquires the same crash-released project file lock for publication and source mutation. Cooked
directory swaps carry a versioned sibling journal, so startup either finalizes a fully published catalog or restores the
previous directory. New scene, material, and input-action sources use the external-import staging transaction rather
than becoming visible before validation and catalog publication succeed.
The asset database implementation is divided into source indexing/import preparation, external-import journals,
source mutation/trash transactions, and dependency cooking. Public methods acquire one non-recursive operation mutex;
explicit private unlocked helpers are used only while that boundary is already held, so nested publication no longer
depends on recursive-lock behavior. `AssetPipeline.cpp` remains the small stable API facade.
Scene picking consumes catalog metadata through `AssetSystem` and does not own an importer or source-model cache.
`SceneCameraController` owns navigation persistence and entity locking. `ViewportAssetDropRouter` dispatches scene,
input, mesh, and material drops through narrow commands, leaving the workspace responsible for composition and modal
coordination.

`AssetSystem` is optionally created after `EventBus` and closed before it. Its bounded priority scheduler owns worker
threads, while handles own reference-counted shared state and immutable payload revisions. Workers perform I/O,
decompression, integrity verification, and decode; `Application` commits completions and dispatches typed events at a
safe owner-thread frame boundary. Typed fallbacks cover queued and initial-failure states. Reload failure preserves the
last committed payload and records diagnostics rather than replacing working content.

Catalog mounts are transactional resolved views over versioned packs. Explicit priority and override permission prevent
silent identity replacement; bounded ranges, dependency closure/cycle checks, Zstandard result sizes, and SHA-256
protect decode inputs. Core owns Binary/Text lifecycle types only. No GPU, audio, model, scene, or native backend type
crosses this boundary.

`AssetDatabase` owns source-side identity through adjacent `.keiremeta` files, a content-addressed import cache, and
confined rollback-capable file operations. Every indexed source and metadata path is re-resolved through the canonical
source root before reads or mutations, so replacing an indexed parent with a symbolic link or reparse point cannot
redirect later work outside the project. Importer-side project discovery follows the same rule before enumeration and
again before every dependency read. `AssetCooker` sorts stable IDs, writes deterministic sharded packs and a versioned
build profile into staging, then atomically publishes the directory. The editor and `KeireAssetTool` call the same
public APIs. Detailed contracts live in [Asset Runtime](AssetRuntime.md) and [Asset Pipeline](AssetPipeline.md).

Standalone player builds add a second transaction above cooking. Public value types own player identity and persistent
profile policy; internal Build Support code owns immutable native-template discovery, archive verification, branding,
signing-hook isolation, staged layout validation, and atomic directory publication. The editor only persists saved
profile choices and supervises `KeireAssetTool build-player`; it does not duplicate build logic or pass unsaved scene
state to the child. `KeireRuntime` retains explicit `--content` mounting while packaged startup discovers a validated
relative layout from `PlayerBuild.json`. Platform packs remain separate from projects and install transactionally under
the per-user preference root. Packaged runtime views are designated as native presentation surfaces and blitted directly
to the swapchain. `RuntimeUiTree` remains the shared Game UI layout, interaction, and draw-command model: editor previews
adapt those commands to editor UI, while standalone players submit them to a dedicated SDL_GPU compositor and do not
initialize or frame Dear ImGui. Windows assembly patches the copied PE template to the GUI subsystem without changing
the low-level console runtime used by tests and SDK consumers. Windows-host assembly also replaces the executable icon
resource with the selected or generated multi-resolution ICO; the template's linked resource supplies the fallback on
other assembly hosts. See [Desktop Player Builds](PlayerBuilds.md).

Player build scene policy is a separate schema-1 value document under `ProjectSettings/BuildScenes.keiresettings`.
Rows retain stable Scene asset IDs, enablement, and user-authored order; the first enabled row is startup. AssetTool
validates every enabled ID after import, roots strict cooking at all of them, and writes the same order into schema-4
`runtime-manifest.json`. Runtime validates uniqueness, bounds, and agreement between the startup field and first build
scene before mounting content. Projects without the new file derive a non-mutating default from the descriptor startup
scene for compatibility.

Contextual importers may publish typed generated sub-assets. Their IDs are derived from the parent identity plus a
semantic importer key, reconciled into the parent's metadata, validated with their own dependencies, and flattened into
the same transactional catalog as the parent. Model materials and embedded texture variants use this path. Material
extraction reimports the model in the isolated asset worker, transactionally creates editable source assets, and only
then publishes the replacement development catalog and source index.

Shader Graph import uses the same generated-subasset boundary: each keyword variant becomes a compiled `ShaderAsset`.
Its default `MaterialAsset` is an internal preview and legacy-compatibility artifact, not a user-facing assignable
material. Direct Material, Material Graph, and Material Instance sources sit above the shader interface. They store
tagged shader references plus property, keyword, surface, and baked-lighting state, then import to an ordinary immutable
`MaterialAsset`. Editor assignment resolves only those user-facing material sources to the runtime identity before
writing scene data, so `RenderSystem` continues to consume only its material/shader asset contract. Shader generation,
material binding, reload, dependency closure, and cooking remain asset transactions without teaching Mesh Renderer
components about authoring graph types.

Shader properties carry optional stable IDs. Material Graph resolves a binding by stable ID before its display name,
which preserves values through source-level renames and reports unknown or type-incompatible bindings explicitly. Raw
Shader manifests without stable IDs retain name-based compatibility. Renderer-specific shader generation remains
behind `ShaderAsset`; neither Material Graph nor direct Material public data exposes backend handles or compiler types.

Generated material shaders obey the fixed six-lane mesh vertex ABI and the renderer's complete pixel-interpolator ABI,
including variants whose graph does not consume every lane. Their vertex stage applies world-position offset before
projection; the fragment stage applies bounded pixel-depth offset and evaluates metallic/roughness PBR, Forward+ local
lights, shadows, image-based lighting, clear coat, sheen, subsurface, anisotropy, transmission, and refraction. Hair and
Eye outputs are authored through the same ABI with output-specific neutral defaults and raster state. Graph parameters
are packed into the ordinary material-property block; a zero-property sentinel keeps resource layouts valid on strict
graphics backends.

Shader Graph authoring separates serialized editor metadata from runtime-affecting edits. Node layout uses the
document host's validated metadata transaction and preserves undo/redo without invoking preview publication. Exposed
parameter defaults patch the last-good material compilation and publish immediately; topology, pin defaults, keywords,
and other shader-affecting changes use the generation-checked background compiler. Live shader imports request only the
host runtime format plus SPIR-V reflection, while ordinary asset imports continue to emit every supported platform
format. CPU preview jobs own immutable graph/property snapshots, are canceled by a shared generation token, publish a
quick reduced-resolution image first, and refine only when the graph remains unchanged; UI resources are created only
on the editor thread.

Schema-4 Shader Graphs may carry an independently versioned renderer-neutral resource contract. It preserves portable
sampler values, array/cube/3D texture references, and aligned bounded read-only buffer views; validation shares graph
identity/symbol and sampler limits, generated manifests retain reflection counts, and dependency cooking includes every
referenced resource. Offline HLSL declaration generation is explicit. Runtime Shader Graph import fails closed while
generic GPU texture-dimension and material storage-buffer assets/bindings remain unavailable, so portable schema support
cannot be mistaken for backend realization. Material Parameter Collections expose ordered revision-matched numeric
snapshots, and a renderer-neutral cache converts stable extents into coalesced dirty ranges without owning GPU memory.

Windowing translates SDL drop sessions into an engine-owned event containing only opaque window identity, logical
position, and filesystem paths. Editor hit-test adapters resolve Project folders or the Scene viewport. External import
then moves to a worker, stages confined source/metadata pairs, validates with UI-independent importer option values,
and publishes or rolls back the batch without exposing SDL, ImGui, or JSON through public headers.
On Windows, the Hub prevents an elevated parent token from being inherited by the Editor because the operating system
does not permit Explorer to send drag-and-drop data across that integrity boundary. An elevated Hub verifies that the
interactive shell belongs to the same user and session, duplicates only that non-elevated primary token, and launches
the Editor with a trackable process identity. Missing, mismatched, or unexpectedly elevated shell tokens fail closed.
Direct elevated Editor launches remain diagnosable but never attempt a partial import.

`RenderSystem` holds an owned `AssetSystem` reference and resolves renderable IDs only on the render owner thread.
Revisioned mesh, material, shader, texture, sampler, and attachment-format pipeline caches publish complete GPU
replacements at frame-safe boundaries. Buffers, textures, and pipelines replaced in flight retire behind the submitted
frame fence; failed rebuilds leave the prior revision active.

Asset code is grouped by subsystem: supported headers live in `KeireCore/Include/Keire/Assets`, implementation sources
live in `KeireCore/Source/Assets`, and implementation-only declarations live in
`KeireCore/Include/KeireInternal/Assets`. SDK packaging copies only the `Keire` public include tree, so internal headers
remain available to first-party builds without becoming part of the SDK contract. No first-party header is stored under
a `Source` directory.

## Input Runtime And Authoring

`InputSystem` is constructed after Assets and Windowing and before UI. It owns logical keyboard/mouse devices, SDL
gamepad RAII handles, users, pairing, control-scheme selection, action contexts, frame snapshots, and interactive
rebinding. Action definitions are immutable `InputActionAsset` revisions; runtime state and per-profile overrides stay
outside source assets. Stable IDs preserve context state across rename and hot reload. Input shuts down before Windowing
and Assets. Full contracts live in [Input System](InputSystem.md).

Managed gameplay observes bounded device snapshots and the active player control scheme through callback-local runtime
services. Rebind operations retain native action contexts until completion or cancellation, expose immutable polling
snapshots, and are cancelled before Play Mode or player teardown. Rumble accepts normalized motor strengths for paired
gamepads only. Production Windows, Linux, and macOS dependencies enable native joystick, HIDAPI, virtual-joystick,
and haptic backends; unsupported future ports or custom SDL builds degrade rumble to a rejected operation without
affecting keyboard or mouse input.

Every managed `InputActionContext` owns an independent native context and is released explicitly or with its managed
asset generation. Enabled-map intent is distinct from the effective action set so hot reload can enable new actions in
an enabled map while preserving explicit per-action disables. Enable operations are idempotent; map and action disable
operations synchronously publish cancellation before removing enabled state. Retained action handles, subscriptions,
and capture overrides become inert when their context or the input service closes.
Managed transition callbacks are dispatched at most once per immutable input frame before fixed/update gameplay
callbacks. Direct controls read the same paired-user snapshots, including held, pressed, and released edges; they do
not bypass editor Play/Game-view routing. Fixed-tick replay schema 2 records sparse raw controls as well as resolved
actions so direct polling remains deterministic during playback, while schema 1 recordings remain readable.

`.keireinput` is the first registered typed source importer. It validates bounded versioned JSON and emits deterministic
canonical bytes into the normal content-addressed cache and cooker. The dockable editor exposes every schema-owned
action type, value type, control scheme, composite, interaction, and processor while owning only mutable authoring
documents and uses the public Kéire UI facade. Details live in [Input Actions Editor](InputActionsEditor.md).
Project descriptor schema 4 stores both the default input asset and stable default map ID. The editor and packaged
runtime resolve that map at Play startup, falling back to the asset's first map only for an unset legacy selection.
Managed legacy polling resolves actions through that selected stable map ID in both editor Play Mode and packaged
players. Generated input wrappers use ordinary folder-independent managed-script placement and extend the selected
runtime assembly's source roots when `Assets/Scripts/Generated` is not already covered.

## Event And Time Runtime

`EventBus` uses exact C++ payload types without a base-event hierarchy. Typed and generic listeners share one priority/registration order; inactive tombstones allow safe unsubscribe and nested dispatch without allocating on the immediate path. Owner-thread dispatch and subscription keep callback mutation deterministic. A bounded mutex-protected queue accepts owned events from any thread, rejects overflow without blocking, and drains a fixed snapshot so producers cannot starve a frame. Closing a bus makes retained references and subscription tokens safely inert.

`Time` is application-owned rather than process-global. A monotonic frame sample feeds raw, clamped unscaled, scaled, smoothed, and elapsed clocks. Scaled time feeds a 60 Hz accumulator with a fixed per-frame tick cap; excess whole ticks are recorded as dropped simulation time while the fractional interpolation remainder is retained. Pause and minimized suspension stop scaled simulation without losing real/unscaled time. Rendering, UI, and fixed simulation remain suspended for a minimized main window; applications that own essential background workflows may opt into low-rate variable layer updates. Hub uses that explicit policy to renew account and Marketplace leases while hidden in the tray.

## Dependency Build Boundary

Assimp and stb are immutable private asset-import dependencies. Coral d53b268 with the versioned Kéire patch set,
.NET 10, Jolt 5.6.0, Recast/Detour 1.6.0, and miniaudio
0.11.25 are immutable private gameplay dependencies resolved into compiler-keyed source and build caches from exact
commits in `Config/Dependencies.lock`. The dependency bridge builds Assimp statically with
only OBJ, FBX, glTF, and GLB importers and no tools, tests, samples, or exporters; stb_image has exactly one private
implementation translation unit. SDKs retain the Assimp/stb license notices and the static Assimp link closure, but do
not redistribute either dependency's headers or add a general third-party include directory for consumers.

Premake remains the Kéire build authority. A dependency-only CMake invocation builds and installs pinned SDL3 Debug
and Release variants into ignored, compiler-keyed caches. A generated Lua manifest supplies Premake with the selected
include/archive paths and platform requirements. SDK packages preserve SDL's official CMake target and make
`Keire::Core` transitively depends on the private ImGui, Zstd, Assimp, Jolt, Recast/Detour, miniaudio, Coral.Native,
and nethost libraries
followed by `SDL3::SDL3-static`. Gameplay middleware headers never cross the supported include tree.

Windows may share immutable locked source downloads across clones and linked worktrees. Checkout-bound aliases cannot
be shared: the Assimp submodule alias and short shader-compiler source junction include a deterministic identity derived
from the canonical repository root. This preserves short ASCII build paths while allowing independent worktrees to use
the same `LOCALAPPDATA`, `TEMP`, and `TMP` values concurrently. Unix dependency and shader-tool paths remain directly
checkout-local. Shared cache publication is serialized per dependency, and every reuse verifies both the locked commit
and a clean ordinary checkout. Cache schema changes invalidate stale CMake source paths before reuse, with recursive
replacement rejecting reparse-point ancestors. Windows generation stamps also include the exact Visual Studio, MSVC,
and Windows SDK identity so a compiler change invalidates both final binaries and matching intermediate objects.

On Windows, every generated final executable that links KeireCore stages `nethost.dll` beside itself as part of its
own build rather than relying on a launcher side effect. The editor's generated Visual Studio, Xcode, and Make projects
also stage the Coral assemblies, first-party managed API, and bundled hostfxr/CoreCLR tree through the same scripts used
by repository launcher builds.

The pinned Dear ImGui docking sources, standard-string adapter, SDL3 platform backend, and SDL_GPU renderer compile in
the dedicated `DearImGui` static-library project. It emits `KeireImGui.lib` on Windows and `libKeireImGui.a` on Unix,
uses the workspace runtime/configuration/architecture/sanitizer policies, and alone disables compiler warnings for its
third-party translation units. KeireCore retains private include access for its UI implementation and links the archive;
KeireClient remains free of Dear ImGui includes and symbols. KeireTests may include ImGui privately only for dependency
and lifecycle verification. The internal link closure preserves `KeireCore` → `KeireImGui` → SDL3 for final binaries.
SDL_GPU selects D3D12 or Vulkan on Windows, Vulkan on Linux, and Metal on macOS; SDL_Renderer remains disabled.

Pinned Zstandard sources compile in the dedicated warning-isolated `Zstd` project as `KeireZstd.lib` or
`libKeireZstd.a`. KeireCore privately includes `zstd.h` for pack compression/decompression. Neither its headers nor
implementation types cross the public API. SDK targets preserve the Core → ImGui → Zstd → SDL static link order.

Pinned EnTT 3.16.0 and GLM 1.0.3 are header-only private dependencies. Their IDE utility projects live in the
`Dependencies` solution group, generated metadata stays below `Build/Projects`, and only their utility projects disable
third-party warnings. KeireCore treats their include roots as external. SDKs package their license texts and locked
commits but not source trees because no supported header exposes either dependency.

Pinned SDL_shadercross and its exact recursive DXC, SPIRV-Cross, SPIRV-Headers, and SPIRV-Tools gitlinks build a
host-native `KeireShaderCompiler` during bootstrap. The compiler and runtime libraries are SDK asset tools, never Core
link dependencies. Shader import produces DXIL, SPIR-V, and MSL canonical variants and validates Kéire's fixed resource
ABI through reflection before publication.

## Release Shape

Packages include KeireHub, the KeireClient editor, KeireRuntime, KeireAssetTool, KeireShaderCompiler and its runtime libraries, KeireCore plus private KeireImGui/KeireZstd archives,
public `Keire/<header>` APIs, the SDL static SDK, complete dependency license texts, notices, README, and a
complete `samples/KeireSandbox` project, canonical `Docs/` release guidance, and a separate transitive cooked-content
tree. First-party declarations originate in project `Include/` trees, implementation units remain in `Source/`, and
packaging never relies on case-insensitive path aliases. The packaged asset tool cooks
and validates the sample startup graph, then `KeireRuntime --content <path> --frames 12` renders it as a bounded smoke
from a tracked-file allowlist. Generated workspace and recovery data is rejected in the stage, archive, and extracted
validation copy
before archive publication. Dear ImGui and Zstd headers/sources are not redistributed because Kéire's public facades own
the supported contracts. Direct validation links Core, ImGui, Zstd, then SDL; the generated CMake package carries those
private archives through `Keire::Core`, so low-level and managed consumers still name one Kéire target.
Packaging extracts the archive and compiles, links, and runs both consumers. CMake builds SDL and serves consumers;
Premake builds Kéire. Release debug symbols are uploaded separately where a platform toolchain emits them; Dist is
intentionally stripped. Export annotations describe same-toolchain shared-library preparation only, not a
compiler-independent C++ ABI.

The separate editor distribution is a Dist-only, host-native projection of that validated release stage. It retains
the editor, runtime/asset/shader companions, media libraries, sample, manifests, and notices; replaces the
runtime-only managed payload with the complete .NET 10 SDK; and removes SDK headers, archives, CMake metadata, and
consumer sources. Editor artifacts also remove the Hub executable, its private HubWorker, Hub content catalogs,
templates, fonts, launchers, and desktop integration; those belong to the standalone Hub artifact. Windows, macOS, and
Linux packages are produced on their respective hosts. Editor companions remain sibling native executables under
`bin`; the macOS Editor `.app` and top-level launch scripts delegate into that same layout.

The standalone Hub is a separate Dist artifact boundary. It contains the Hub executable, the private `KeireHubWorker`
task process, and private load-time runtime,
branding, licensed fonts, documentation/learning content, the validated `KeireHubContent/Templates` catalog and
declared payloads, and license material; it rejects the editor-specific AssetTool/AssetWorker/runtime/shader
companions, the
full .NET SDK, and SDK development trees. Editor and Hub schema-2 manifests share project-schema compatibility, module
and canonical metadata fingerprints, optional packaged-template/toolchain/license references, SHA-256 file inventories,
and exact installed sizes while exposing product-owned entrypoints only. Editor manifests preserve their schema-1
top-level fields so existing combined installers and discovery code can migrate without an atomic format cutover.

The distribution-service package also owns a runtime-dependency-free static `Website/` boundary. Caddy serves it for
human routes and forwards only `/v1`, `/v1/*`, `/health`, and `/health/*` to the loopback .NET origin, so site deployment
cannot reinterpret signed catalog bytes, package range requests, or readiness semantics. The `DocumentationSite/`
build validates an exact inventory of every canonical `Docs/**/*.md` source, requires a live code/configuration
authority per guide, resolves local links and heading fragments, and checks key format versions against code. It then
transforms those sources into branded Astro Starlight pages beneath `Website/docs/`, renders GitHub-compatible Mermaid
fences as responsive accessible SVG at build time, and creates Pagefind's same-origin search index plus a documentation
sitemap. Generated pages contain no inline executable code or style values: a deterministic finalizer extracts them
into content-hashed `_astro` assets so Caddy's self-hosted CSP remains enforceable. The Downloads page is an untrusted
catalog consumer: it validates the stable host matrix and exact `hubInstaller` fields before offering a content-addressed
link, while the installed Hub remains the authority for signature verification and managed update installation. An
unsigned development preview is a separate static deployment boundary: it is never inserted into a stable catalog,
must be labeled unsigned, uses a digest-suffixed immutable URL, and is offered only after same-origin metadata and a
matching content-length HEAD probe pass. HTML, CSS, JavaScript, and preview metadata revalidate independently so a new
page cannot execute against stale presentation or release-control assets.
Documentation HTML revalidates on every visit, content-hashed `_astro` assets are immutable, and Pagefind/runtime assets
use bounded revalidation. Pagefind's WebAssembly execution is the only CSP evaluation exception; worker execution is
limited to same-origin and blob workers, and all documentation scripts, styles, fonts, and indexes remain same-origin.
The public Contact form calls a narrowly scoped Supabase Edge Function. That function enforces exact origins, bounded
streamed input, strict UTF-8/object JSON, a honeypot, and a keyed-IP-hash rate limit before writing with server-only
credentials. Only the edge-provided Cloudflare client-address header participates in the rate key; caller-controlled
forwarding headers are ignored. Anonymous and authenticated browser roles have explicit deny policies and no table
privileges. The function's npm graph is committed in `deno.lock` and checked with frozen restore semantics.

The current deployment replaces the legacy static routing surface with one Astro Node application on loopback while
preserving the signed distribution boundary. Caddy remains the only public listener and routes distribution API and
health paths to Kestrel; marketing, documentation, accounts, OAuth consent, marketplace, publisher, moderation, and
their versioned application APIs route to Astro. A website-only release swaps the complete `dist` directory
transactionally, verifies the exact Node executable and entry point that owns port 4321, retains a rollback directory,
and passes public readiness before completion. The router still exposes only ordinary public HTTP/HTTPS through Caddy.

Supabase is the identity and marketplace data authority. Browser sessions and Hub OAuth sessions remain independently
revocable. Authenticated marketplace writes cross JWT-verifying Edge Functions into service-role-only transaction
adapters; they never expose the service credential to Astro or Hub. The adapters bind the already verified actor into
the transaction so existing RLS membership and entitlement helpers remain authoritative. Publisher submission also
requires AAL2, and direct PostgREST policies allow applicants to save or withdraw drafts but not self-submit or
self-approve. Feature flags remain the final operational gate and default to disabled.

Marketplace staff authority is held in the forced-RLS `platform_staff_members` relation and is evaluated on each read
or mutation, so revocation does not wait for a JWT refresh. Browser staff sessions can read their authorized queues but
cannot write moderation states directly. AAL2-protected website adapters invoke `marketplace-moderation`, which verifies
the caller again and delegates to service-role-only transactional functions for publisher, package, report, staff, and
feature-gate decisions. Those functions validate allowed state transitions, preserve a final administrator, and append
audit evidence. New packages are uploaded exactly once to a private immutable object identity. The isolated validator
has no network namespace; it signs a canonical attestation binding upload, version, bucket, path, package digest,
manifest digest, scan results, and a separately hashed bounded evidence document. Staff receive only a short-lived URL
for that sanitized evidence, and the browser verifies its size and SHA-256 before rendering the manifest inventory.

Administrator approval stops at `approved_pending_signature` and atomically creates one durable publication job. A
separate least-privileged online signer authenticates with a dedicated scoped queue secret, receives only approved
metadata and the signed validator attestation, and cannot obtain a package download URL. It verifies the pinned
validator key, signs the existing publication schema, and commits through a leased service-role transition. No package
copy or administrator file handoff occurs. Publication metadata records the original bucket/path, while Hub and Editor
accept a bounded multi-key trust bundle so a new signer key can ship to clients before it becomes active. Private keys
remain operating-system protected on the worker host and are never stored in Supabase, Astro, packages, or the
repository.

macOS release binaries share the deployment target pinned by `MACOS_DEPLOYMENT_TARGET` in the dependency lock. The
package boundary verifies each non-.NET Mach-O load command against that target before publication. Native installers
sign individual Mach-O files and nested code bundles from the inside out, then seal the outer application. The bundled
Microsoft .NET tree remains an independently signed third-party boundary whose signatures and bytes are verified but
never rewritten. Only the managed editor host receives the reviewed JIT/runtime entitlements; the standalone Hub does
not.

On Windows, NSIS owns presentation and optional shell integration while `KeireInstallWorker` is the payload transaction
authority shared by Editor and Hub. Its product-specific receipt binds the package identity, a random installation ID,
manifest fingerprint, and exact regular-file size/SHA-256 inventory to the canonical HKCU registration. The worker
accepts only an absent/empty ordinary root or a receipt/marker/registration-bound existing installation. It stages beside
the root on the same volume, journals each durable phase, revalidates hashes and no-follow path components immediately
before copy, rename, or deletion, and keeps the old payload recoverable until NSIS commits after shell work. Recovery is
idempotent. Uninstall moves only unchanged receipt entries through the same journal, preserves drift and unknown
neighbors, and never recursively deletes the selected root. The real Editor and Hub executables expose a hidden
pre-Application `--verify-installation` handler that revalidates the product's exact schema-2 package manifest and file
inventory without creating preferences, projects, windows, singleton activation, or network clients. NSIS runs that
handler with a bounded timeout while rollback is still possible. Linux and macOS installer authorities are unchanged.

A KeireCore prebuild step refreshes version and source-control identity under `Build/Generated` immediately before compilation, including tracked and untracked dirty state. The generator C-escapes configured strings and only rewrites the header when its content changes. Built-in rendering, skinning, and VFX headers are independently fingerprinted from their generator, compiler, and HLSL inputs. Cache misses use a repository-scoped inter-process lock, so parallel builds publish one complete header while waiters recheck and reuse it. Platform build launchers hold a checkout-wide lock while mutating the shared `Build` tree; compiler parallelism remains internal to that build, while another launcher waits instead of racing links, staging, or dependency publication. The compiler supplies configuration, compiler, platform, and architecture identity. Packaging regenerates identity and verifies the staged binary's commit prefix and dirty marker against its manifest. The resulting `Keire::BuildInfo` describes the binary itself rather than the machine inspecting it.

## Performance Observation And Background Work

The application-frame profiler stores one bounded full frame plus a ring of lightweight summaries. Spans carry stable
thread identifiers and monotonic microsecond timestamps, allowing the editor to present hotspot and chronological
thread-lane views without instrumenting editor code through private APIs. `LatestChromeTrace()` emits a
Perfetto/Chrome-trace-compatible snapshot. Renderer timings remain explicitly CPU-side until SDL_GPU exposes portable
timestamp queries; `GpuTimingSupported` prevents recording or displaying synthetic GPU measurements. The renderer also
records submit-to-observed-fence-completion latency for all frames and for frames containing GPU VFX. That value is a
queue-completion observation, not elapsed GPU execution time, and is deliberately named and gated separately. Managed callback
timing uses fixed per-instance accumulators on the owner thread. The editor performs bounded type/lifecycle aggregation
only at its throttled presentation refresh and limits default-open row submission to reduce profiler observer overhead.
The renderer separately reports oldest-frame GPU fence wait, swapchain acquisition wait, VFX physical particle
capacity, compute dispatches, and compute thread groups. Fence wait is measured at the frames-in-flight boundary, so a
large `Render begin` span can be attributed to GPU/present back-pressure without misclassifying it as scene-recording
CPU time.

Reference-hardware gates live in `Config/PerformanceGates.json`. The validation tool independently recomputes frame
percentiles from exported history, validates capture metadata and driver identity, and applies renderer/VFX counter
budgets. Profiles that require GPU timestamps reject a backend without real timestamp support; fence latency is never
substituted. Production validation always tests the gate machinery and can consume an explicit snapshot, history, and
metadata triplet on a named graphics worker.

Development `AssetDatabase` instances own a stoppable monitor thread. The monitor performs metadata/signature
reconciliation away from the application thread and publishes a complete immutable candidate tagged with the source
revision. The owner thread discards stale candidates, debounces changed identities, and atomically updates its record
snapshot. Explicit asset mutations advance the revision and wake reconciliation, preventing an older background scan
from overwriting a newer editor transaction.

Forward+ retains its deterministic CPU fallback while caching the projected grid and GPU storage uploads by viewport,
camera, and local-light content. An empty visible-light set uses one cached dummy tile and does not depend on camera or
viewport identity. The capability flag keeps a future GPU-compute implementation ABI-compatible without claiming
support on backends that cannot provide it. The editor hierarchy builds prefab membership and parent-child adjacency
once per snapshot, preserving scene order while avoiding recursive full-scene searches.

## Animator Controller Authoring

Animation graph execution remains owned by `KeireCore`: stable local IDs identify parameters, layers, state-machine
subgraphs, states, transitions, conditions, and blend-tree children, while `AnimatorInstance` owns typed runtime values
and immutable debug snapshots. Subgraphs are authoring/navigation groups with validated per-group entry states; the
runtime graph stays flat by stable state ID so transitions can deterministically cross subgraph boundaries. The editor
owns only an `AnimatorControllerDocument` draft and panel selection state. Drafts may be
temporarily incomplete, but Save canonicalizes and validates the complete graph before atomically replacing the source;
failed validation therefore cannot replace the last-good imported asset.

Animator state presentation reuses `StableNodeGraphCanvas`; the panel maps serialized string IDs to deterministic local
canvas IDs, then translates completed node drags, typed pin connections, cable deletion, and context actions back into
one document transaction. Canvas selection and popup state never enter the asset. Runtime state order and transition
evaluation are therefore independent of layout, zoom, pan, or editor interaction state. Edit-mode preview evaluates a
private `AnimatorInstance` against the selected scene object's target skeleton and writes only its transient runtime
pose; teardown clears that pose. Transition visualization, pose/trajectory inspection, and profiling consume the same
immutable debug snapshot as live playback.

Animator schema 7 adds an explicit pose-source boundary. `AnimationGraph` retains the existing instance evaluation;
`ProceduralHumanoid` owns deterministic fixed-step phase/state, prior/current local poses, terrain anchors, and
profile/rig revisions. Gameplay submits value-only intent before physics, and the scene runtime combines it with the
post-physics Character Controller result before solving. Both modes converge at managed IK, authored arm overrides,
palette publication, and immutable diagnostics. `.keiremotionprofile` is a normalized asset boundary rather than
component-owned tuning, so cooking, catalog dependencies, validation, and hot reload use the standard asset pipeline.
The scene-session header retains orchestration and cached state, while `SceneRuntimeProcedural.cpp` owns procedural
advance and publication. Per-Animator pose, matrix, palette, grounding-request, and double-buffered debug storage is
retained after warm-up; published debug snapshots are immutable even when a consumer holds an older snapshot.

## Skeletal Deformation And Rig Authoring

`SkeletonAsset`, `RigDefinitionAsset`, `SkinnedMeshAsset`, and `AnimationClipAsset` are independent immutable assets.
Stable generated-subasset IDs let a model reimport replace payloads without invalidating controller, prefab, or scene
references. Model import either preserves embedded skinning, generates a deterministic profile, or disables rigging.
Embedded skeletons pass through deterministic semantic inference so authored Mixamo, Blender, Unreal, humanoid, biped,
and quadruped names participate in the same retargeting and IK contracts. Unknown bones remain ordered, retained, and
unclassified rather than being discarded.

The scene runtime samples animation into local bone transforms, applies named IK goals, optionally performs scene-owned
world-space ground raycasts and transactional model-space foot grounding, computes the palette, and then submits
deformation. World-authored sole offsets and pelvis limits are converted through the Animator transform before solving,
so imported rigs keep the same physical grounding distances at non-unit model scales. Per-foot runtime state owns both
the sole target and support-surface anchors in entity-local space. The current downward probe arbitrates support
handoffs every frame, while bounded tangential travel re-anchors a moving support before the leg reaches a two-bone
singularity. The standalone
ragdoll transition blends animation and physics-provided local poses but does not own physics bodies or constraints.
Linear-blend skinning uses the SDL_GPU compute cache when available. Dual-quaternion skinning and
unsupported compute paths use the deterministic CPU implementation; both paths produce an engine-owned transient
deformed stream reused by scene, depth, and shadow recording. No native pointer or graphics allocation crosses the
managed boundary.

`RiggingStudioPanel` owns only draft import settings and retarget selection. `AssetDatabase::SetImportSettings` commits
validated metadata atomically, `RequestReimport` advances the source generation, and the isolated asset worker publishes
the complete model/subasset transaction. Import-time animation compression reduces keys against explicit translation,
rotation, and scale tolerances and reports measured errors. Retargeting produces a bone-by-bone exact/semantic/conflict
diagnostic before it writes a standalone `.keireanim` source. Strict cooking decodes every rig, clip, and skinned mesh and validates declared
dependency types, mesh/influence cardinality, and influence bone bounds before publication.

Runtime IK goals are persistent named `AnimatorComponent` state. They are resolved after graph sampling and before
palette generation at the scene-safe animation boundary. Managed calls carry world/entity generations and value data;
the bridge validates animator existence, goal space, solver limits, and stale scene state before mutating the component.
The managed foot-grounding multiplier is runtime-only component state: it scales authored foot, rotation, and pelvis
weights without notifying authoring observers or changing serialized data. A zero multiplier resets transient contact
and support locks before the grounding pass, allowing controllers to disable terrain adaptation while airborne.

Controller node positions are optional schema-v2 authoring metadata and never affect runtime evaluation. Project-panel
clip drops resolve through typed asset records before creating states. Undo commands retain complete graph values but
are scoped to the open asset identity, preventing stale document commands from mutating a newly opened controller.
Managed Animator reads consume the component's immutable runtime debug snapshot, while writes remain queued
generation-safe commands applied by the scene presentation runtime.

## Shared Asset Documents

`AssetDocumentHost<T>` owns typed draft/baseline state, bounded undo/redo, validation, preview, atomic persistence,
discard, and revision-aware reload conflict handling. The host never owns runtime objects: system-specific documents
translate validated definitions into development-asset previews and cancel transient preview state when they close.
Stable local IDs, rather than display names, identify authored graph nodes and connections. `Curve1D`,
`ColorGradient`, stable-node canvases, and selection-aware scene handles are shared value/editor primitives; ImGui and
backend implementation types remain inside KeireClient.

Builtin importer and decoder construction is centralized in `BuiltinAssetRegistry`. The editor, isolated worker,
AssetTool, runtime catalog, and tests request the same registrations, preventing an authored extension from importing
in one process but failing in another.

## Physics Runtime And Authoring

`SceneRuntimeSession` eagerly creates one `PhysicsWorld` for each Play or cooked scene. Its fixed boundary is gameplay
`FixedUpdate`, incremental body/controller synchronization, Jolt stepping, dynamic Transform pullback, then stable
contact dispatch. Body identities are generation-safe and never expose Jolt types. Layer/mask filtering is applied by
the configured 32-slot collision matrix in body broad-phase filters and query paths.

The editor writes collider handle changes through `SceneDocument`, so a drag is one undoable authoring operation.
`PhysicsDebugSnapshot` copies bounded body, contact, and query-ring state only when capture is enabled; the shipping
default records nothing. Physics Material and collision-mesh references remain ordinary asset dependencies.

Managed ray and capsule casts plus sphere overlaps enter through owner-thread runtime services and resolve native body
IDs back to stable scene entity IDs. Capsule self-filtering is applied as a native body filter so a cast can reach the
next surface. Managed overlap copies are deterministic, unique, and capped at 256 entities inside one callback scope.

Character movement queues value displacements from scripts and consumes them at the scene physics boundary. The runtime
uses closest-hit capsule casts with the controller body excluded, skin padding, bounded sweep/slide iterations,
walkable-normal tests, and an up/forward/down stair transaction. Authored capsule height is total tip-to-tip height;
Jolt receives the derived cylinder half-height. Ground state and resolved velocity are copied back to the component
after stepping. Managed code receives only values through the concrete `CharacterController`; no Jolt shape or body handle
crosses the scripting boundary.

Character Controllers and dynamic rigid bodies retain previous/current authoritative world samples after physics.
Render updates interpolate a separate Transform presentation matrix; collision, scripts, and gameplay queries never
read it implicitly. Child presentation composes the interpolated parent with the current local transform, keeping a
camera target and skinned visual synchronized. Teleports explicitly reset interpolation, while body recreation, scene
replacement, and Play initialization snap both samples. This bounded serial state belongs to `SceneRuntimeSession` and
does not introduce a job-system or physics ownership dependency into Transform.

## Managed Scripting

`ScriptSystem` owns managed build orchestration, runtime hosting, reflection, generation-safe reload, Behaviour
instances, and the native call bridge. Private `ManagedSdk` support owns persisted SDK selection and cross-platform
dotnet discovery, keeping filesystem and process-environment policy out of the runtime implementation. Configuration
writes preserve unrelated scripting settings, and custom SDK resolution requires a .NET 10 SDK before it can become an
active build dependency. Managed Behaviours own a phase-aware coroutine scheduler. Coroutine tokens are
generation-local values, nested iterators are disposed in LIFO order, and disable, destruction, or reload stops all
pending routines. Concrete `Transform` and `RigidBody` objects support parent-aware world setters and validated force
modes through internal calls; neither API exposes an ECS component pointer or physics body handle.
The reflected ECS component adapter is compiled separately as `ManagedBehaviourComponent`. It retains only a weak
callback table and value IDs, so components become inert when `ScriptSystem` closes without exposing Coral objects or
the runtime implementation. Creation, callback invocation, state capture/restore, and exception-preserving destruction
remain owned by the active script generation.

Application, time, and screen internal calls are isolated in `ManagedRuntimeFoundation` instead of extending the
already broad script host implementation. A thread-local scope publishes only the active `IScriptRuntimeServices`
interface during a managed callback; it never publishes `Application`, `Window`, or `Time` ownership. The editor
implements that interface on its construction thread. Screen changes capture the previous mode and extent and restore
them on partial failure. Managed preferences use the platform preference root plus validated application identity, then
perform bounded, typed, same-directory atomic file replacement entirely in the managed layer.

Camera, renderer, light, material-slot, and shader-property calls are isolated in `ManagedRuntimeRendering` and cross
the same callback-scoped `IScriptRuntimeServices` boundary as the foundation and scene UI surfaces. The runtime resolves
stable entity identities to native components only on the application thread. Mesh Renderer material replacements are
complete-array transactions. The managed Mesh Renderer `AlwaysVisible` property controls the same authored culling
override as the native component and defaults to false. Per-renderer material property blocks are bounded, validated
transient component state:
they are copied into the immutable frame packet, opt only that draw out of instancing, and are applied to matching
numeric or texture bindings generated by Material/Shader Graph. They never mutate a shared `MaterialAsset` and are
intentionally omitted from scene and prefab serialization.

Scene and render-environment calls are isolated in `ManagedRuntimeWorld` and use the same callback-scoped service
boundary. A `Single` transition loads and validates a replacement while the current sessions continue to run, starts
the replacement physics/audio/VFX/UI/script worlds, verifies the player camera, and only then publishes it. Activation
failure retains the previous world and a bounded operation diagnostic. `Additive` publishes another independent
session without changing the active handle; explicit handle-based unload and activation commit at the next safe
boundary. Managed queries carry an active/loaded/specific/persistent scope, and entity-bound services route to the
session that owns the entity. Editor Play uses this same owner and lifecycle rather than a simulation-only adapter.
Runtime render-environment replacement validates the complete value before publication. Packaged-player state is
process-transient; Editor Play stores a separate Play-session override and never mutates the project settings document.

## Managed Data Assets

`.keiredata` stores the authoritative stable managed type ID, a diagnostic type name, stable-field values, and sorted
dependencies. Script generation preparation discovers concrete runtime `ScriptableObject` types and validates their
property graphs before publishing descriptors. The application-owned `AssetSystem` performs real cancellable loads,
while `ScriptSystem` hydrates objects in generation order and transactionally retains the previous script/asset
generation on failure. Asset-only reload copies supported serialized state into the active object so cached managed
references retain identity.

Native presentation assets cross the managed boundary as canonical `Asset` subclasses plus optional explicit
`AssetLoadOperation<T>` residency leases. `ManagedRuntimeApplicationServices` owns a bounded token table whose entries retain ordinary untyped
`AssetHandle<Asset>` values requested with the direct asset's stable native type ID. The bridge publishes only state,
fallback use, revision, and a copied diagnostic; it never publishes an `Asset`, decoder, or graphics/audio resource.
Managed `AssetLoadOperation<T>.Dispose` removes one token idempotently, application unbind clears the table, and retiring a
script generation releases all tokens tagged with that generation before the collectible context is discarded.

Strict cooking builds and loads runtime managed assemblies before decoding the managed type catalog and validating
managed-data semantics. Scene Behaviour and managed-data references participate in the normal cook graph, so closure
is scene to managed data to referenced managed/native assets. Test assemblies are excluded from runtime discovery.

## Audio Mixer And VFX Assets

`AudioMixerAsset` is an immutable authored definition with stable bus/effect/send/snapshot/ducking IDs and explicit
AudioClip dependencies for convolution. Audio Source schema 2 forwards legacy string buses while carrying a mixer,
stable local bus ID, and `Curve1D`. Scene presentation resolves revisioned mixer assets, while `AudioSystem` owns
immutable compiled routing snapshots behind generation-safe registrations. Each presentation releases its
registrations when a mixer is no longer referenced or the presentation clears, so multiple presentations cannot
invalidate one another or reuse stale project state. A valid stable bus ID wins over the compatibility name and
applies its authored fader, mute, solo, and parent hierarchy to existing and new voices; invalid replacement leaves
the last snapshot active. Convolution dependencies are held through revisioned `AudioClipAsset` handles, converted to
the mix sample rate and channel layout before registration, and retained as immutable PCM by both headless and native
effect graphs. Initial low-level convolution registration supplies an explicit `AudioMixerImpulseResponses` map;
definition-only updates reuse the prior binding. Headless rendering executes ordered effects, sends, parent routing, ducking, and bounded
automatic meters. Reverb Zones select against the primary listener, blend one priority-resolved mixer snapshot and
send scale, and restore the immutable source definition after exit. Legacy string gain and stop controls forward
through the currently resolved authored bus name, while voice diagnostics report the resolved mixer, bus ID, and
registration. Device playback builds a private miniaudio node graph per immutable mixer revision, including hierarchy,
effect racks, pre/post sends, faders, mute, solo, and sidechain ducking, then reattaches live voices transactionally.
Reverb Zones scale return sends when a conventional effect-return bus exists and scale wet parameters only for
direct-insert reverbs, preventing a return from being attenuated twice. The editor's typed `AudioMixerDocument` publishes
transient live routing/fader previews. Its Mix Console exposes per-bus peak meters, dB faders, mute, solo, output
routes, ordered effect racks, sends, snapshots, and sidechain ducking without exposing stable IDs as the normal
authoring path. Effect names commit at the end of a text edit so the surrounding tree remains stable. Mixer, graph,
and device execution share one stateful effect processor so Equalizer bands and convolution tails cannot drift between
the three paths. Equalizer coefficients and dB gains are compiled before processing, with inverted crossovers normalized
to low-to-high order. Convolution uses a 128-frame bounded direct path for short responses and allocation-free uniform
FFT partitions for longer responses; five-second, per-effect, aggregate mixer, and audio-system channel-work limits
reject content that exceeds the callback and memory budgets. Compatible mixer revisions preserve processor state by stable effect ID;
the owner publishes parameters through a bounded atomic mailbox while the native callback remains the sole writer of
DSP history. Stable native fader and send controls update in place. Topology, bypass, ducking, or immutable IR identity
changes rebuild the graph and intentionally reset its state.

Project authoring-settings schema 2 owns the desktop mix sample rate, callback period, mono/stereo/5.1/7.1 output
layout, audible and virtual voice budgets, and an optional editor playback-device identity. The editor falls back to
the system default when that device is unavailable and reports the fallback in the Profiler. Hardware identity is not
copied into cooked content. Cooking instead writes the portable format and capacity fields into the schema-4 runtime
manifest; the player validates and applies those fields before constructing `AudioSystem`. A schema-1 project-settings
document migrates in memory to the schema-2 defaults and is written canonically on the next save.

The Project Settings Default Mixer is the inherited route for Audio Sources and Reverb Zones whose component override
is empty in both Editor Play Mode and cooked players. The Inspector resolves inherited mixers, presents bus and
snapshot names rather than UUID entry, and diagnoses missing routes or reverb returns. Spatial presentation selects an
active primary Audio Listener first and otherwise uses the highest-priority active primary Camera. Listener direction,
up, velocity, and gain enter the same state used by device and offline rendering. Box and sphere Reverb Zones evaluate
the listener in local space, so entity rotation and non-uniform scale affect the authored volume; one zone per mixer is
selected by priority and blend weight. Native spatial voices use the authored distance curve for both audibility
virtualization and output gain while miniaudio retains speaker positioning and Doppler behavior.

The managed API exposes transactional `AudioSource`, `AudioListener`, and `AudioReverbZone` component objects.
Managed and native boundaries validate routing identities, gains, pitch, distance ranges, priorities, shapes,
volume dimensions, blend distances, and wet levels before mutating a component. The Profiler reports device format,
fallback state, voice capacity and virtualization, mixer/effect counts, listener selection, active zones, pending
assets, per-voice state, and bounded peak/RMS/clipping readings.

`VfxWorld` owns fixed-capacity effect and particle storage. Activation is transactional, handles include generations,
and revision-aware replacement preserves bounded lifecycle behavior. Non-looping GPU effects advance through their
particle-drain interval after emission stops so generation-safe handles release at the actual last-death time. The CPU
path publishes immutable sprite/mesh render packets into the transparent pass and records explicit diagnostics when GPU
depth or scene-physics requests select CPU simulation. Each activation compiles the selected execution source, resolves
typed Blackboard defaults and stable-ID overrides, materializes only the scheduled module payloads, applies canonical
property bindings, and resolves portable custom operands. `SetParameterOverrides` swaps the complete resolved value set
transactionally; `SetParameter` and `ResetParameter` are single-value conveniences. Reload preserves compatible
same-ID/type exposed overrides and reports rejected values without compromising unrelated handles. GPU Force,
Size, Color, and Renderer state is baked per particle; a live change advances only that handle's simulation revision so
old baked state is retired without clearing the shared world. Portable Update/Output operand changes remain live.
Immutable render snapshots publish separate document and simulation-step revisions. GPU resources apply mutation-only
snapshots for lifecycle, transform, restart, and spawn sequencing, while consuming each positive world update at most
once. Newer mutation-only snapshots rebuild visible alive indices with a zero simulation delta; equal or stale
snapshots leave the newer persistent buffers and indirect arguments unchanged.

`VfxEffectDocument` owns transactional systems, nodes, pins, connections, blackboard properties, module payload
records, curves, and gradients without exposing ImGui. All graph edits preserve stable IDs; removing a node or pin
removes its incident links in the same validated undo command. The panel projects those values into a context-colored
node canvas and commits graph positions only when a drag finishes. New documents use schema-v5 Graph execution.
Legacy documents display their compatibility execution source until the user invokes the undoable deterministic
conversion. In Graph mode, `ParticleStream` topology supplies the module/custom schedule, Module nodes retain stable
payload references, Parameter nodes retain stable Blackboard references, and Portable Custom HLSL lowers to bounded
CPU/GPU instructions. Built-in module types retain canonical stage semantics while cable topology defines deterministic
Module/Custom order within those stages. The separate Runtime Modules tab is a payload editor, not an alternate
implicit schedule.

The editor owns one shared, transient `VfxWorld` for asset-authoring and scene edit-mode previews. It defaults to CPU
for deterministic authoring and can be rebuilt for GPU runtime inspection. Checked `VfxEmitterComponent` instances are
synchronized by edit-scene identity, entity, effect/revision, compatible serialized parameter overrides, seed, enabled
state, simulation speed, and decomposed world position/rotation. When an open draft matches an eligible scene emitter,
its transient handle replaces one selected or deterministic matching scene handle and is routed to that emitter
transform and compatible override set. This preserves unsaved draft preview while preventing a second copy at world
origin; unrelated emitters retain independent handles. World-space editor previews restart after authored gizmo
relocation so old particle history is not displayed beside the new emitter position, while runtime World-space
semantics remain unchanged. GPU retirement is generation-qualified, so restarting that editor handle preserves the
particles and spawn progress of unrelated preview emitters. Local-space particles follow synchronized
position/rotation changes on both CPU and GPU. Asset preview pause affects only its handle; scene emitters continue
advancing. Handles are stopped on uncheck, disable, deletion, scene replacement, Play transition, panel close where
applicable, and shutdown. Capturing that world for the Scene viewport never creates entities or mutates authored scene
state.
