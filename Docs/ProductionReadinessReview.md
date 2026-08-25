# Kéire Production Readiness Review

Review date: 2026-08-24

Revision: Kéire 0.4.1 exact-release evidence with retained Kéire 0.3.2 historical evidence

Review scope: the clean Windows and Rocky Linux 9 compatibility-baseline 0.4.1 Dist packages, signed distribution
snapshot `release-0.4.1-sequence-16-0c21d51`, the canonical documentation library, and the feature-gated online
platform. Source behavior, package validation,
native installation, and public-catalog evidence remain distinct; success in one lane is not silently promoted into
another.

Review target: sustained commercial and AAA-team production use. A feature existing in source is not equivalent to a
validated workflow, and a validated preview is not equivalent to a production-proven 1.0 release.

## 0.4.1 Release Update

Version 0.4.1 is active through signed catalog sequence 16. The immutable package set contains Windows and Linux
x86-64 Editors, the Windows Hub installer, and separate DEB and RPM Hub installers. The exact DEB passed installation
and version checks on Ubuntu 22.04 and Debian 13; the exact RPM passed on Rocky Linux 9, Fedora 44, and openSUSE
Tumbleweed. The website and Hub catalog expose only those verified records.

The candidate adds the source-breaking Unity-shaped managed API and managed-state v2, shared Shader/Material/VFX graph
selection and comments, bounded canonical clipboard remap, arrange/navigation tools, Shader/Material schema 4, VFX
schema 5, and executable Operator/Block/System VFX Subgraphs. Focused graph, Shader/Material, VFX, scripting, Debug,
Release, and AddressSanitizer tests passed in their owning integration tasks. The final scripting run reported 696/696
Debug tests, 697/697 Release tests, and 696/696 DebugASan tests, with managed consumer and Sandbox Release builds clean.

The release is not feature-complete against the original 0.4.0 ambition. Named reroute declarations, persisted
nested local-graph stacks, generic backend realization for array/cube/3D texture and user-buffer graph resources,
23 disabled P0/P1 VFX parity rows, and scene-Inspector collection add/remove/reorder controls remain open. Native
Authenticode/RPM GPG signatures, new D3D12/Vulkan reference-hardware captures, and macOS/Metal release evidence remain
outstanding. These gaps prevent a 1.0 or AAA-production-readiness claim, not the catalog-verified preview release.

## What Changed In This Refresh

- Kéire 0.4.1 is the active Windows and Linux x86-64 public preview. Immutable sequence 16 publishes Windows
  Editor/Hub packages and Linux Editor, DEB Hub, and RPM Hub packages.
- ARM64 Linux acceptance is currently limited to emulated Ubuntu 24.04 toolchain coverage: a QEMU-driven ARM64 shader and
  dependency pipeline is running again from the prior attempt and has now passed the vkd3d install phase, but no native
  end-to-end ARM64 Hub runtime or distribution acceptance exists yet in this refresh.
- The clean Windows package gate passed 726 Core tests, 199 Editor tests, and 372 Hub tests, together with SDK/package
  consumers, smoke, cooking, and inventory validation.
- The clean Rocky Linux package gate passed 726 Core tests, 199 Editor tests, and 370 Hub tests, together with the full
  Client compile gate and a packaged Vulkan/WSLg Play Mode smoke.
- The DEB installed and reported the exact release version on Ubuntu 22.04 and Debian. The RPM did the same on Rocky
  Linux 9, Fedora 44, and openSUSE Tumbleweed. These are native package-acceptance observations; they are not a claim
  that every complete test suite ran independently on every distribution.
- The `ProceduralHumanoid` Animator pose source, schema-1 `.keiremotionprofile`, managed locomotion intent/state/events,
  terrain-aware contacts, and fixed-step presentation interpolation are now public release contracts. Existing
  Animators migrate to graph mode unchanged.
- Post-release source adds shared Editor/player multi-scene worlds, expanded managed input/physics/render/UI services,
  native-asset residency handles, world-scoped material collections, and automatic Marketplace publication. These do
  not retroactively change the immutable 0.3.2 packages.
- Hub catalogs distinguish Linux DEB and RPM package formats and preserve the native installer extension through the
  download worker and update handoff. The download site presents the formats independently.
- Catalog and artifact integrity are verified, but the current Windows Hub EXE is not Authenticode-signed and the RPM
  is not GPG-signed. Those facts are disclosed on the download surface and remain production-signing gaps.
- The canonical documentation inventory contains 81 guides. Source validation checks inventory, authorities, local
  links/fragments, schema statements, release/platform claims, and the generated site.
- A fresh Windows Debug source audit passed 669 Core tests/49,254 assertions, 157 Editor tests/2,395 assertions, 370
  Hub tests/3,789 assertions, the full Client compile gate, managed production API checks, 10 distribution-service
  tests, and 11 Marketplace validator tests.
- That same source audit found a real release-gate regression: `KeireRenderTests` does not compile because four
  rendered-output request initializers were not updated when global material properties joined `SceneRenderRequest`.
  No all-green current-source, sanitizer, or rendered-output claim is made until that gate is repaired and rerun.
- The website audit covers the unified Astro platform plus its static migration fallback. Navigation labels, heading
  hierarchy, social metadata, Windows/Linux roadmap status, automatic Marketplace publication language, readiness
  dates, and all 63 fallback guide/category counts now agree with their authorities.

## Executive Assessment

Kéire is a serious production-oriented pre-1.0 engine with explicit ownership, deterministic serialization,
transactional assets/projects, strong failure isolation, broad native tests, and real Windows/Linux distribution.
Version 0.4.1 is a usable cross-platform technology preview and development platform. It is not yet justified as a
finished AAA production engine or a generally available cross-platform marketplace.

The historical immutable 0.3.2 release grade remains **B+ (89/100)**. The 0.4.1 current-source grade is also
**B+ (89/100)**. New multi-scene, managed-residency, material, authoring, and Marketplace capabilities improve several
domains, but the fresh rendered-output compile failure prevents a higher current-source grade. Native release signing,
macOS/Metal, reference-hardware GPU evidence, Marketplace launch acceptance, accessibility/localization, and
shipped-project soak evidence remain incomplete.

| Assessment | Weight | Grade | Score | Current judgment |
| --- | ---: | ---: | ---: | --- |
| Architecture, ownership, and lifecycle | 12% | A | 95/100 | Coherent public/private boundaries and deterministic service lifetimes. |
| Tests and local quality policy | 12% | A- | 91/100 | Broad suites pass, but the rendered-output compile regression blocks an all-green source claim. |
| Assets, projects, packaging, and recovery | 10% | A | 94/100 | Transactional publication, targeted dependency-closure import, migration, immutable content, and recovery are major strengths. |
| Editor and authoring workflows | 10% | A- | 90/100 | Broad and increasingly polished; accessibility, localization, and content-scale evidence still lag. |
| Rendering, shaders, materials, and VFX | 12% | B | 84/100 | Capability grew, but the rendered-output gate is broken and hardware evidence remains incomplete. |
| Managed gameplay and runtime services | 10% | A | 94/100 | Reload safety, multi-scene worlds, runtime UI, asset residency, and gameplay APIs are substantial. |
| Hub, distribution, and website | 10% | A | 94/100 | Signed Windows/Linux distribution and a unified validated web platform are live; native signatures remain. |
| Marketplace and package ecosystem | 8% | B+ | 87/100 | Upload-once validation and automatic metadata signing exist; public launch and legal gates remain closed. |
| Performance evidence | 8% | C+ | 78/100 | Profiling and fail-closed gates exist, but current reference captures lack true portable GPU time. |
| Cross-platform release evidence | 8% | B- | 82/100 | Windows and Linux x86-64 are published; native macOS/Metal and ARM64 remain unobserved. |
| Weighted current-source readiness | **100%** | **B+** | **89/100** | Production-oriented cross-platform preview, not finished AAA production readiness. |

Scores use a fixed weighted rubric and round to the nearest integer. `A` is 94–100, `A-` 90–93, `B+` 87–89, `B`
83–86, `B-` 80–82, and `C+` 77–79. A broken mandatory compile or test gate caps its affected domain and the overall
current-source grade below `A-`, regardless of feature breadth. Confidence is high for immutable 0.3.2 artifacts and
medium for current source because this refresh ran on Windows Debug and did not complete the blocked render,
sanitizer, Release, or native non-Windows matrices. Scores remain review shorthand; the gates below are authoritative.

## Current Reproducible Evidence

### Fresh current-source Windows audit

`./Scripts/project.ps1 test -Generator ninja -Configuration Debug -Toolset msc` was run from the current working tree
on 2026-08-21. Documentation and website edits do not participate in the native results. The run passed:

- 669/669 Core tests with 49,254 assertions;
- 157/157 Editor tests with 2,395 assertions;
- 370/370 Hub tests with 3,789 assertions;
- the complete Editor/Client compile gate and all managed production API checks reached before the render gate;
- 10/10 distribution-service tests and 11/11 Marketplace validator tests.

The run then stopped while compiling `KeireRenderTests`. `VfxKillShapeRenderedOutputTests.cpp:182` and
`RenderedOutputTests.cpp:937`, `:1037`, and `:1057` still aggregate-initialize the pre-global-property
`SceneRenderRequest` shape, so a `VfxRenderSnapshot` is now matched against the global material-property map field.
This is a test-source contract regression after world-scoped material collection binding, not evidence that the
rendered behavior passed. The DebugASan, Release, rendered-output execution, and later aggregate workflow stages were
not rerun past this blocker.

### Windows 0.3.2 release

The clean Windows Dist Editor archive has SHA-256
`a305020d42fd132f349715ccd2cdd672828cd4b3c20ecb4c73cfbf3f82215cfd`; the Hub setup executable has SHA-256
`50a6fd5880ce3aca8a4a5140e91034b1d28a1f080a48765724bddd3604c5f1ff`. Both report
`Kéire 0.3.2 (4b966260f9f6, Dist, MSVC 1944, Windows x86_64)`.

The package workflow passed:

- 647/647 Core tests with 48,377 assertions;
- 147/147 Editor tests with 2,300 assertions;
- 370/370 Hub tests with 3,789 assertions;
- the complete Client compile gate, SDK consumers, Sandbox cook/runtime smoke, and staged/extracted inventory checks.

The Hub EXE is distributed through the signed catalog and exact SHA-256 identity but is not Authenticode-signed.
Windows may therefore show an unknown-publisher warning, and automatic Windows update installation remains unavailable
until native signature verification can succeed.

### Linux 0.3.2 release

The clean Linux Editor package has SHA-256
`ce9d6da5582e463fe7ded7ada764e13669525425e8c75b529b05c8c08b8e3bcb`. The Hub DEB has SHA-256
`30eb63828577143a64fee2844cb504ca7816c3c735c901de8d7e5d384bbf1023`; the Hub RPM has SHA-256
`87e99e88bfc42f958ccf9db75bb570402655e3817fc68c24ed13491ad18c4dff`.

The Rocky Linux release gate passed:

- 641/641 Core tests with 48,343 assertions;
- 147/147 Editor tests with 2,290 assertions;
- 368/368 Hub tests with 3,792 assertions;
- the full Client compile gate, SDK/package consumers, package inventory, and packaged Vulkan/WSLg Play Mode smoke.

The DEB installed successfully on Ubuntu 22.04 and Debian. The RPM installed successfully on Rocky Linux 9, Fedora 44,
and openSUSE Tumbleweed. Every installed Hub reported version 0.3.2 and commit `4b966260f9f6`. The RPM has no distro GPG
signature; its current preview trust boundary is the Ed25519 catalog plus the bound SHA-256.

### Distribution and website

The publisher signed and verified four catalog documents with key
`ed25519-019da85781015fa51aefbaeb3acdca5e`, then atomically activated
`release-0.3.2-sequence-14-4b96626`. The snapshot expires on 2026-11-14 and contains five packages:

- Windows Editor and Hub;
- Linux Editor, DEB Hub, and RPM Hub.

The v1 and v2 Windows/Linux x86-64 catalog endpoints and all five content-addressed package endpoints returned HTTP
200 with their exact advertised lengths and ETags during release validation. The Downloads page exposes the verified
Windows, DEB, and RPM records and states the native-signing limitations explicitly.

### Capability ledgers

- The frozen Unity 6.3 VFX ledger contains 278 rows: 248 implemented and 30 disabled. This is substantial coverage, not
  a complete Unity parity claim.
- The Unreal-inspired Material Ecosystem matrix contains 145 rows: 98 Complete, 8 Partial, and 39 Planned. The ledger
  distinguishes current source capability from immutable 0.3.2 package evidence; Planned rows remain unsupported.
- Current content authorities are project schema 4, scene schema 6, mesh schema 5, VFX schema 4, material source
  schema 3, Animator component schema 7, procedural motion profile schema 1, and cooked runtime-manifest schema 4.

### Documentation and web platform

All 62 canonical Markdown guides are mapped to implementation authorities. Source validation checks the exact
inventory, headings, local links and fragments, schema statements, current product version, current release-platform
claims, and prohibited unresolved markers. The Astro build synchronizes those sources into Starlight, renders Mermaid
diagrams, builds local Pagefind search, and validates generated routes, assets, CSP-compatible output, sitemap content,
and the branded 404 page.

The unified account, documentation, marketplace, publisher, policy, download, news, and roadmap surfaces are live
behind Caddy. Supabase SSR sessions, GitHub sign-in, MFA surfaces, Hub OAuth contracts, forced RLS, private Storage,
Edge transition boundaries, and isolated validator/publication-worker foundations exist. Marketplace feature flags and
the separate launch runbook remain authoritative; software-distribution publication is not evidence that marketplace
products are publicly launch-ready.

## Website Assessment

The current website grade is **A- (92/100)**. This is a source-and-build assessment, not proof of a complete public
launch. The unified Astro site has coherent navigation, responsive shared layouts, static-first public pages,
server-rendered identity and Marketplace routes, canonical metadata, local search, a strict content-security policy,
and explicit preview/launch labeling. The static migration fallback remains independently valid and now mirrors the
62-guide inventory.

| Website area | Grade | Score | Evidence and remaining risk |
| --- | ---: | ---: | --- |
| Content accuracy and release labeling | A | 95/100 | Version, platform, capability, Marketplace, roadmap, and readiness labels now distinguish 0.3.2 packages from newer source. Automated drift checks cover duplicated facts. |
| Information architecture and navigation | A- | 93/100 | Engine/Editor, Marketplace, roadmap, downloads, documentation, changelog, account, and trust routes use consistent desktop/mobile labels. |
| Accessibility structure | B+ | 89/100 | Skip links, landmarks, labels, responsive navigation, one primary heading, accessible diagrams, and reduced-motion-aware styling exist. A formal screen-reader/browser matrix remains open. |
| Metadata, discovery, and sharing | A- | 93/100 | Canonicals, sitemap, RSS, structured data, Open Graph, X cards, Pagefind, and per-page descriptions are present. Production-domain search-console evidence is not retained here. |
| Security and deployment boundaries | A | 95/100 | Self-hosted CSP-compatible assets, canonical-origin mutations, SSR sessions, forced RLS, private Storage, health contracts, and Caddy isolation are substantial. |
| Dynamic account and Marketplace workflows | B+ | 88/100 | Identity, MFA, OAuth, publishing, moderation, entitlement, and automatic publication paths exist behind gates; public end-to-end launch acceptance remains incomplete. |
| Browser and performance evidence | B- | 82/100 | Build-time route/CSP/link/search validation is strong, but the formal browser, assistive-technology, and branded-domain performance audit is still a launch gate. |

## System Assessment

| System | Grade | Evidence and remaining risk |
| --- | ---: | --- |
| Application, layers, events, and time | A | Owner-thread rules, deferred mutation, bounded queues, fixed time, exception containment, and shutdown have focused coverage. Continued long-session and platform differential testing remains appropriate. |
| ECS, scenes, prefabs, and undo | A | Stable identities, multi-scene runtime worlds, scoped queries, persistence, schema migration, Play isolation, nested prefabs, recovery, and transactional undo are mature. Larger collaborative scenes need sustained evidence. |
| Editor workspace and authoring | A- | Docking, documents, hierarchy/Inspector, asset browsing, targeted imports, previews, graph editors, settings, diagnostics, procedural profiles, and package management are implemented. Full accessibility, localization, and collaborative-depot automation remain incomplete. |
| Rendering | B- | SDL GPU isolation, D3D12/Vulkan formats, HDR/ACES, Forward+, shadows, lighting data, LODs, skinning, graph pipelines, and last-good safety are substantial. The current rendered-output target compile regression must be fixed before the grade returns to B. |
| Animation and rigging | B+ | Graph animation, retargeting, semantic rigs, arm/leg IK, procedural humanoid motion, terrain contacts, airborne states, and presentation interpolation are implemented. Procedural motion still needs broader rig/content tuning and long-session visual acceptance. |
| Shader and material ecosystem | B | Shader/Material Graphs, functions, layers, persistent/dynamic instances, world-scoped collections, and live revisions exist. The 98/145 Complete matrix and 39 Planned rows make a full Unreal-parity claim inaccurate. |
| VFX | B+ | Typed schema-4 graphs, ordered contexts/blocks, CPU/GPU execution, events, strips, resource operations, mesh/ribbon/volume output, diagnostics, and bounded pools cover 248/278 rows. The 30 disabled rows and incomplete GPU attribution remain. |
| Managed scripting | A | Collectible generations, generation-safe handles, multi-scene worlds, native-asset residency, runtime UI, gameplay services, coroutines, last-good retention, and procedural callbacks are production-minded. Cross-platform reload soaks remain necessary. |
| Assets and project packages | A | Stable metadata, isolated targeted imports, deterministic cooking, hardened archives, dependency resolution, selective import, executable-code consent, conflicts, journals, and recovery are implemented. Sustained production-corpus fuzzing remains gated. |
| Hub and software distribution | A- | Independent Hub/editor products, version selection, process-identity tracking, resumable tasks, signed catalogs, immutable snapshots, DEB/RPM selection, package verification, and safe activation are implemented. Native signatures and real-host update/remove drills remain. |
| Online platform and marketplace | B | Accounts, organizations, publisher schemas, RLS, private uploads, isolated validation, moderation, automatic metadata signing, immutable publication, and free-entitlement contracts exist. Legal, backup, abuse, accessibility, and public end-to-end launch gates remain. |
| Build, package, and SDK | B+ | Premake authority, launchers, clean manifests, deterministic inventories, SDK consumers, native installers, Windows/Linux packages, and regression harnesses are strong, but the current rendered-output test target is not build-clean. |

## Performance Assessment

Kéire exposes native and managed profiler markers, rolling history, trace export, renderer/VFX counters, stutter
statistics, pipeline-warmup observations, and machine-readable reference profiles. The performance validator requires
one uninterrupted capture with matching snapshot, history, and hardware metadata; it rejects incomparable data.

The current SDL GPU boundary publishes completion latency and fence wait but cannot provide portable timestamp-query
execution time. Both shipped reference profiles require true GPU timestamps and therefore fail closed on this backend.
No new 0.3.2 named-hardware capture satisfying the complete profile is recorded in this review. Functional Vulkan/WSLg
startup and Play Mode evidence must not be relabeled as performance evidence.

Production performance acceptance still requires:

1. reviewed per-backend timestamp support or an explicitly supported backend-specific boundary;
2. cold and warm captures on named CPU/GPU/driver/resolution/workload profiles;
3. D3D12, Vulkan, and eventually Metal runs across representative vendors and tiers;
4. long-duration scene, asset-streaming, managed-reload, VFX, audio, and project-operation soaks;
5. retained raw artifacts and intentional baseline review rather than copied summary values.

## Remaining Release Gates

### Before promoting the current source

- Update every rendered-output `SceneRenderRequest` initializer for global material properties, compile and execute
  `KeireRenderTests`, and retain D3D12/Vulkan output evidence.
- Rerun the complete Windows Debug, DebugASan, and Release gates after the render target is green; do not infer the
  unexecuted tail of the interrupted Debug workflow from earlier passing stages.

### Before a production-grade Windows claim

- Authenticode-sign and timestamp the Hub installer, then validate the automatic update handoff.
- Validate installed `keirehub://` registration, browser PKCE, refresh/revocation, update, repair, and uninstall on clean
  supported Windows 10 and Windows 11 hosts.
- Retain exact-release D3D12/Vulkan, package, Sandbox, and sanitizer evidence for each promoted artifact.

### Before a production-grade Linux claim

- Repeat graphical Hub-to-Editor, URL-protocol, native update, repair, and uninstall acceptance on real non-WSL
  Ubuntu/Debian and Rocky/Fedora/openSUSE hosts.
- GPG-sign the RPM and publish repository metadata if distro-managed repository installation is advertised.
- Publish and validate exact-version Build Support for every advertised player target.
- Keep Linux ARM64 and Alpine/musl unadvertised until their own native build, package, graphics, and player gates pass.

### Before advertising macOS

- Complete native x86-64 and ARM64 builds, tests, Metal output, package consumers, and player smoke.
- Sign hardened-runtime bundles inside-out, notarize, staple, validate Keychain session storage and URL activation, and
  exercise the pinned deployment target on supported macOS versions.

### Before public marketplace launch

- Complete every ordered gate in [Marketplace Launch](MarketplaceLaunch.md), including SMTP and leaked-password
  protection, policy review, official signed packages, moderation, validator recovery, backup/restore rehearsal, abuse
  controls, accessibility/performance audits, and Windows/Linux end-to-end recovery scenarios.
- Keep `paid_checkout_enabled=false` for 0.4.1. Native plugins remain unsupported.

### Before an AAA production-readiness claim

- Close or explicitly scope every advertised capability matrix; Planned rows cannot be marketed as supported.
- Ship and support representative projects through upgrades, builds, crashes, corruption, and long production sessions.
- Establish named hardware tiers and enforce CPU/GPU/memory/load-time budgets across multiple vendors.
- Complete accessibility, localization, input-device, source-control, large-depot, and content-team usability programs.
- Maintain clean hosted and self-hosted release matrices with native signatures and rehearsed rollback.

## Recommended Closure Order

1. Acquire Windows Authenticode and RPM GPG signing identities and exercise native update/rollback from the live 0.4.1
   catalog.
2. Repeat Linux graphical and package-manager acceptance on real distro hosts, then publish exact-version Build Support.
3. Complete Marketplace official products, automatic signing/publication, claim/download, validator recovery,
   backup/restore, and legal gates
   while launch flags remain closed.
4. Implement renderer timestamps and collect current named-hardware cold/warm and soak evidence.
5. Prioritize the 39 Planned and 8 Partial material rows plus the 30 disabled VFX rows by production scenario.
6. Establish native macOS/Metal validation before presenting macOS as supported.

The correct public description is **production-oriented Kéire 0.4.1 technology preview with catalog-verified Windows
and Linux x86-64 downloads**. “AAA production ready,” “complete Unreal/Unity parity,” and “cross-platform release complete”
remain future acceptance outcomes, not current product facts.
