# Kéire Production Readiness Review

Review date: 2026-08-12

Revision: current-source and Windows-candidate refresh

Review scope: Kéire 0.3.1 source at commit `6f27aaa`, the newly rebuilt clean Windows Editor candidate, the Windows
packages published from commit `7619442` in signed distribution snapshot sequence 9, the 58-guide documentation
library, and the current feature-gated online platform. Candidate evidence and public-release evidence are kept
separate throughout this review.

Review target: sustained commercial and AAA-team production use. A feature existing in source is not equivalent to a
validated workflow, and a validated local workflow is not equivalent to a supported public release.

## What Changed In This Refresh

- The clean Windows Editor package was rebuilt from `6f27aaa`. Its Dist runtime reports that exact commit and its
  package gate includes the current Hub/process launch hardening, rendered-test backend normalization, marketplace
  service-boundary fixes, and documentation corrections.
- Current Dist suites pass 591 Core tests with 47,736 assertions, 133 Editor tests with 2,230 assertions, and 347 Hub
  tests with 3,604 assertions. The D3D12 and Vulkan rendered-output suites remain green at 21/21 cases each.
- Windows Hub and Editor now diagnose elevated launch sessions that cannot accept Explorer file drops, while the Hub
  avoids starting another elevated Editor. The browser-to-Hub URL-protocol handoff and package ownership contracts are
  represented in the current source baseline.
- The active public catalog remains sequence 9 until a matching replacement Hub installer completes the release gates.
  Its Ed25519 catalog and SHA-256 identities are verified, but the native Windows installer is not Authenticode-signed.
  The Downloads UI now describes that distinction instead of calling the executable itself signed.
- Linux source and package contracts remain maintained, but no Linux artifact is advertised while exact-release native
  installation and Vulkan acceptance are pending. This is intentionally unaffected by the unavailable local WSL run.

## Executive Assessment

Kéire is a serious production-oriented pre-1.0 engine with unusually explicit ownership, deterministic serialization,
transactional assets and projects, strong failure isolation, and broad native test coverage. Version 0.3.1 is a usable
Windows technology preview and development platform. It is not yet justified as a finished AAA production engine or a
generally available cross-platform marketplace.

The current overall grade is **B (84/100)**. The engineering foundation is stronger than the release-evidence score:
the Windows Editor candidate and graphics paths are current and reproducible, while the public Hub still trails that
candidate and native validation of this exact commit on Linux and macOS, named-hardware performance gates,
Authenticode/notarization, marketplace acceptance, and shipped-project soak evidence remain incomplete.

| Assessment | Grade | Score | Current judgment |
| --- | ---: | ---: | --- |
| Architecture, ownership, and lifecycle | A | 95/100 | Coherent public/private boundaries and deterministic service lifetimes. |
| Tests and local quality policy | A- | 92/100 | Large executable surface with sanitizers, package gates, and rendered tests. |
| Assets, projects, packaging, and recovery | A- | 92/100 | Transactional publication, migration, immutable content, and recovery are major strengths. |
| Editor and authoring workflows | B+ | 88/100 | Broad and usable, with interaction, accessibility, and content-scale evidence still growing. |
| Rendering, shaders, materials, and VFX | B | 85/100 | Modern production slices work; parity matrices and hardware evidence remain incomplete. |
| Managed gameplay and runtime services | A- | 91/100 | Reload-safe handles, last-good retention, and broad gameplay APIs are implemented. |
| Hub, distribution, and website | B+ | 88/100 | Signed immutable distribution and the unified site are live; native signing and full desktop SSO acceptance remain. |
| Marketplace and package ecosystem | C | 73/100 | Strong feature-gated foundations; public catalog, signed products, moderation, and end-to-end acceptance are not open. |
| Performance evidence | C+ | 78/100 | Useful profiling and machine-readable gates exist, but current reference captures cannot supply true portable GPU time. |
| Cross-platform release evidence | C | 75/100 | Windows is current; exact-commit Linux and native macOS/Metal evidence are outstanding. |
| Overall production readiness | **B** | **84/100** | Production-oriented preview, not finished AAA production readiness. |

Scores are review shorthand, not release gates. The release gates below are authoritative.

## Current Reproducible Evidence

### Windows 0.3.1 candidate and public release paths

The clean `package-editor` workflow completed from commit `6f27aaa` and produced a schema-2 Dist editor candidate with
a clean-worktree identity. Its package gate passed:

- 591/591 Core tests with 47,736 assertions;
- 133/133 Editor tests with 2,230 assertions;
- 347/347 Hub tests with 3,604 assertions;
- the complete Client compile gate, Dist Client smoke, packaged Sandbox smoke, and extracted package validation;
- direct, managed, and source-module SDK consumer checks owned by the editor packaging workflow.

The currently public Editor and Hub artifacts were built from commit `7619442`. The release publisher created their
package records, signed both v1 and v2 catalogs with the rotated Ed25519 release key, verified every signed document,
and atomically activated immutable snapshot `release-0.3.1-sequence-9-08e10a7`. The live Windows catalog exposes only
`keire.editor@0.3.1` and `keire.hub@0.3.1`; both content-addressed artifacts respond over HTTPS with exact lengths and
byte-range support. The Hub installer is 55,834,101 bytes with SHA-256
`29096f0d837294cd79bfe2b099d67f75e7441b0ef791567a964af50d15795384`. Catalog signing does not replace Authenticode
signing of the native installer, and the `6f27aaa` Editor candidate is not relabeled as public sequence-9 evidence.

### Rendered output

The current Release rendered-output executable contains 21 cases after including the backend-alias regression test.
Direct3D 12 and Vulkan each pass the 20 production rendered-output cases; these cover native runtime UI, lighting,
VFX, asset revision safety, Shader Graph, Material Graph, skinning, shadows, resize/minimize, bounded queues, and injected
device loss. The Direct3D 12 run passes when selected by either the canonical `direct3d12` name or the normalized
`d3d12` shorthand.

These are local functional image/readback tests. They are not a multi-vendor GPU matrix, long-duration soak, or formal
reference-hardware performance capture.

### Capability ledgers

- The frozen Unity 6.3 VFX ledger contains 278 rows: 245 have a Kéire implementation and 33 remain disabled. The first
  50-item milestone and the subsequent 120-item portable expansion are complete, but the remaining 33 GPU-resource and
  renderer-dependent rows prevent a complete parity claim.
- The Unreal-inspired Material Ecosystem matrix contains 145 rows: 90 Complete, 7 Partial, and 48 Planned. This is a
  strong authoring foundation, not Unreal Engine material-system parity. The largest gaps are renderer-side dynamic
  instances and collections, broader resources and blend paths, analyzers, performance budgets, and cross-platform QA.
- Current content authorities are project schema 3, scene schema 5, mesh schema 5, VFX schema 4, material source schema
  3, and cooked runtime-manifest schema 4. Older supported schemas are upgraded through explicit parsers and save paths.

### Documentation and web platform

All 58 canonical Markdown guides are mapped to implementation authorities. Source validation checks exact inventory,
headings, local links and fragments, schema statements, required website security contracts, and prohibited unresolved
markers. The Astro build synchronizes those sources into Starlight, renders Mermaid diagrams, builds local Pagefind
search, and validates generated routes, assets, CSP-compatible output, sitemap content, and the branded 404 page.

The unified Astro account, documentation, marketplace, publisher, policy, download, news, and roadmap surfaces are live
behind Caddy on the DuckDNS staging origin. Supabase SSR sessions, GitHub sign-in, MFA surfaces, Hub OAuth contracts,
forced RLS, private Storage, Edge transition boundaries, and validator deployment foundations exist. Public marketplace
and asset-package flags remain disabled until the launch runbook passes.

## System Assessment

| System | Grade | Evidence and remaining risk |
| --- | ---: | --- |
| Application, layers, events, and time | A | Owner-thread rules, deferred structural mutation, bounded event queues, scaled/fixed time, exception containment, and shutdown have focused lifecycle coverage. Continued long-session and platform differential testing remains appropriate. |
| ECS, scenes, prefabs, and undo | A- | Stable identities, missing-component retention, schema migration, Play isolation, nested prefabs, recovery, and transactional undo are mature. Larger collaborative scenes and merge-heavy production projects need sustained evidence. |
| Editor workspace and authoring | B+ | Docking, documents, hierarchy/Inspector, asset browsing, previews, graph editors, settings, diagnostics, and package management are implemented. Accessibility, keyboard-only operation, localization, high-DPI/text scaling, and large-project interaction automation are incomplete. |
| Rendering | B | SDL GPU isolation, D3D12/Vulkan shader formats, HDR/ACES, Forward+, shadows, lighting bake data, probes, LOD/submeshes, instancing, skinning, graph-generated pipelines, and last-good revision safety are substantial. Metal, multiple GPU vendors, true GPU timestamps, sustained device-loss/resize stress, and broader render-feature coverage remain. |
| Shader and material ecosystem | B- | Shader Graph and Material Graph are distinct assets, materials consume reflected shader parameters, functions/layers/instances exist, and live revisions reach the renderer. The 90/145 Complete matrix and 48 Planned rows make a full Unreal-parity claim inaccurate. |
| VFX | B+ | Typed schema-4 graphs, ordered contexts/blocks, CPU/GPU execution, events, strips, resource-backed operations, mesh/ribbon/volume output, diagnostics, and bounded pools cover 245/278 ledger rows. The 33 disabled rows and incomplete scalability/per-effect GPU attribution remain visible work. |
| Managed scripting | A- | Collectible generations, generation-safe handles, serialized fields, ScriptableObjects, gameplay services, coroutines, last-good assembly retention, and explicit Play readiness are production-minded. Cross-platform reload soaks and larger gameplay-project evidence remain necessary. |
| Assets and project packages | A- | Stable metadata, isolated workers, deterministic cooking, content-addressed packs, archive hardening, dependency resolution, lockfiles, embedding, selective import, executable-code consent, three-way conflict handling, journals, and recovery are implemented. Catalog-backed Hub/Editor acceptance and production package corpus fuzzing remain gated. |
| Hub and software distribution | B+ | Independent Hub/editor products, version selection, compatibility checks, resumable task worker, signed catalogs, immutable snapshots, package verification, and safe activation are implemented. The 0.3.1 Windows catalog is current; automatic desktop SSO still requires an installed protocol handler and complete native acceptance. |
| Online platform and marketplace | C | Accounts, organizations, publisher schemas, RLS, private uploads, resumable publishing UI, bounded APIs, validator worker/broker, malware/secret checks, and free-entitlement contracts exist behind flags. No public marketplace should be claimed before signed products, moderation, legal review, backup restore, abuse controls, and Windows/Linux end-to-end scenarios pass. |
| Audio, animation, physics, navigation, input, and UI | B+ | Each area has implemented runtime/authoring contracts and focused tests. Platform/device matrices, content diversity, dense UI/controller/IME workflows, physics/crowd stress, and long-running content scenarios are not yet equivalent to shipped AAA evidence. |
| Build, package, and SDK | A- | Premake authority, supported launchers, clean-worktree manifests, deterministic inventories, SDK consumers, native installers, distribution signing, and regression harnesses are strong. Exact-current-commit Linux packages and native macOS signing/notarization remain release evidence, not design work. |

## Performance Assessment

Kéire exposes native and managed profiler markers, rolling history, trace export, renderer/VFX counters, stutter
statistics, pipeline-warmup observations, and machine-readable reference profiles. The performance validator requires
one uninterrupted capture with matching snapshot, history, and hardware metadata; it rejects incomparable data.

The current SDL GPU boundary publishes completion latency and fence wait but cannot provide portable timestamp-query
execution time. Both shipped reference profiles require true GPU timestamps and therefore fail closed on this backend.
No new 0.3.1 named-hardware capture satisfying the complete profile is recorded in this review. Historical frame data
remains useful diagnostic context but is not carried forward as current release evidence.

Production performance acceptance still requires:

1. reviewed per-backend timestamp support or an explicitly supported backend-specific boundary;
2. cold and warm captures on named CPU/GPU/driver/resolution/workload profiles;
3. D3D12, Vulkan, and eventually Metal runs across representative vendors and tiers;
4. long-duration scene, asset-streaming, managed-reload, VFX-capacity, audio, and project-operation soaks;
5. retained raw artifacts and intentional baseline review rather than copied summary values.

## Release Gates

### Required for the current Windows preview

- Keep the catalog snapshot immutable, signed, unexpired, and available with exact package hashes.
- Authenticode-sign and timestamp the native Hub installer before calling it a production Windows installer.
- Install the current Hub and validate `keirehub://` registration, browser PKCE handoff, refresh rotation, revocation,
  update handoff, uninstall ownership, and a clean Editor installation from the live catalog.
- Repeat Debug, Release, AddressSanitizer, package, Sandbox, Direct3D 12, and Vulkan acceptance on the release commit.

### Required before advertising Linux 0.3.1 downloads

- Build from the exact release commit on the declared oldest glibc baseline.
- Pass native Debug, Release, sanitizer, Vulkan, Hub, Editor, package, DEB, and RPM acceptance without dirty markers.
- Verify desktop files, `keirehub://`, declared runtime dependencies, install/update/remove ownership, and packaged
  Sandbox/player behavior on supported Debian/Ubuntu and Rocky/Fedora families.
- Publish only the artifacts whose platform, architecture, dependency baseline, hash, and support statement were
  validated. The active sequence-9 catalog is currently Windows-only.

### Required before advertising macOS

- Complete native x86-64 and ARM64 builds, tests, Metal rendered output, package consumers, and player smoke.
- Sign hardened-runtime bundles inside-out, notarize, staple, validate Keychain session storage and URL activation, and
  exercise the pinned deployment target on supported macOS versions.

### Required before public marketplace launch

- Complete every ordered gate in [Marketplace Launch](MarketplaceLaunch.md), including SMTP and leaked-password
  protection, policy review, signed official packages, moderation, validator acceptance, backup/restore rehearsal,
  abuse controls, accessibility/performance audits, and Windows/Linux end-to-end recovery scenarios.
- Keep `paid_checkout_enabled=false` for 0.3.1. Native plugins remain unsupported.

### Required for an AAA production-readiness claim

- Close or explicitly scope every advertised capability matrix; planned rows cannot be marketed as supported.
- Ship and support multiple representative projects through upgrades, builds, crashes, asset corruption, and long
  production sessions, with defect and recovery evidence retained.
- Establish named hardware tiers, enforce CPU/GPU/memory/load-time budgets, and maintain multi-vendor platform labs.
- Complete accessibility, localization, input-device, source-control, large-depot, and content-team usability programs.
- Maintain a clean hosted and self-hosted release matrix with signed/notarized artifacts and rehearsed rollback.

## Recommended Closure Order

1. Validate and install the clean Windows 0.3.1 Hub, including URL-protocol SSO and Authenticode release signing.
2. Reproduce the exact commit on supported Linux baselines, publish clean DEB/RPM/editor artifacts, and run the full
   Hub-to-Editor flow.
3. Complete marketplace signing, official products, moderation, validator, backup/restore, and legal launch gates while
   flags remain closed.
4. Implement renderer timestamps and collect current named-hardware cold/warm and soak evidence.
5. Prioritize the 48 Planned and 7 Partial material rows plus the 33 disabled VFX rows by production scenario, not raw
   feature count.
6. Establish native macOS/Metal validation before presenting macOS as supported.

The correct public description is **production-oriented Kéire 0.3.1 technology preview with a validated Windows
foundation**. “AAA production ready,” “complete Unreal/Unity parity,” and “cross-platform release complete” remain
future acceptance outcomes, not current product facts.
