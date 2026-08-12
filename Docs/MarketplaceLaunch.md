# Marketplace Launch Runbook

This runbook is the release authority for the Kéire 0.3.1 website, identity, marketplace, and asset-package staging
initiative. A deployed page or database object is not proof that its feature is ready. Public behavior is controlled by
the feature flags in `public.platform_feature_flags`, which default to disabled.

## Current staging state

As of 2026-08-12, `https://keireengine.duckdns.org` serves the unified Astro website through Caddy. The router exposes
only public TCP 80/443; Astro (`127.0.0.1:4321`) and the distribution service (`127.0.0.1:5088`) remain private. The
marketplace schema, private Storage buckets, RLS policies, official draft products, MFA account surfaces, and
JWT-protected marketplace Edge Functions are deployed. GitHub sign-in is configured through Supabase, the Kéire Hub
public OAuth client is registered, and `hub_oauth_sso_enabled=true` is intentionally active for private staging tests.
The publisher portal is enabled for the internal staging publisher, and the signed-only marketplace catalog preview is
now enabled after the first real upload, isolated validation, publisher submission, and staff moderation path passed.
The catalog shows an explicit signing queue until an immutable signed product exists; claim and download controls are
never synthesized for unsigned drafts. Asset-package, community, and paid-checkout gates remain disabled. Leaked-
password protection and custom SMTP are deferred until the paid
Supabase plan and branded production domain are available.

Staff authorization is database-authoritative in `public.platform_staff_members`; browser JWT metadata is not an
authorization source. Moderators can review publisher applications, validated packages, and marketplace reports.
Administrators can additionally appoint or revoke staff and change launch gates, except that paid checkout is blocked
for 0.3.1. Every staff action requires AAL2 and crosses `marketplace-moderation` into a service-role-only, transactional
RPC that writes an audit event. Direct authenticated moderation writes are revoked, and the final active administrator
cannot be removed. The initial staging account is the bootstrapped administrator.

The live signed software-distribution snapshot is sequence 9. Its Windows x86-64 catalog contains the clean
`keire.editor@0.3.1` and `keire.hub@0.3.1` artifacts only. This proves software distribution, not marketplace product
publication: `.keireassetpackage` products use a separate trust root and remain behind their launch gates.

The dedicated asset-marketplace Ed25519 trust root is now established separately from software distribution. Its
private key is stored in an ACL-protected offline-key directory outside the repository and host; only the public trust
document is versioned. `prepare-marketplace-publication.ps1` signs and independently verifies an exact release envelope.
The MFA-protected `marketplace-publication` Edge boundary verifies that signature against the active public key, binds
it to the passing validator hashes and approved submission, copies the same quarantine object into content-addressed
private release Storage, and calls a service-only transaction that publishes the version and audit record. A failed
database commit removes the promoted object. The first official products have not crossed this boundary yet. Their
deterministic unsigned quarantine artifacts are prepared with
`python Scripts/Marketplace/create-official-marketplace-packages.py`; the command emits all five packages and a
hash-bound release index beneath `Build/Marketplace/Official/0.3.1`. Staff opens an official draft's standard Publisher
release workflow, uploads the matching artifact, and never fabricates a signed envelope for draft metadata.

The repository now contains the 0.3.1 isolated package validator, network broker, atomic lease/report migration,
ClamAV adapter, secret scanner, generated no-network C# compilation, Windows firewall launcher, and hardened Linux
units. The validator lease migration is applied to the staging project and `marketplace-validator-queue` version 2 is
deployed in a fail-closed state. The scoped broker secret is now stored as ACL-protected machine-DPAPI ciphertext on
the Windows staging host. The reviewed 0.3.1 worker, broker, Asset Tool, managed API, pinned .NET SDK, and current
ClamAV definitions are provisioned under separate `LOCAL SERVICE` and `NETWORK SERVICE` startup tasks. The offline
worker blocks all four untrusted-content processes with verified firewall rules and an administrator-generated,
ACL-protected path/hash attestation. Harmless and EICAR fixtures passed their expected offline outcomes, a controlled
task restart preserved both service identities, and the broker authenticates to the staging queue with HTTP 200
responses. On 2026-08-12, an internal package completed the live lease, isolated validation, report-commit, publisher
submission, and staff approval path successfully; its unpublished test records and quarantine object were then purged
after the evidence was recorded in the append-only audit trail. The publisher activity surface now polls its
authenticated, RLS-scoped status endpoint until that asynchronous result is terminal instead of leaving an early
server-rendered `pending` label stale. `marketplace_enabled=true` opens catalog browsing only; no product is claimable
until offline signing and immutable publication complete. Live stale-lease recovery and broader adversarial acceptance
remain required before `asset_packages_enabled` or `community_enabled` can be enabled.

For repeatable end-to-end upload testing, run
`python Scripts/Marketplace/create-neon-forge-sample-package.py`. The generated unsigned
`Build/Marketplace/UploadSamples/NeonForgeCreatorPack/1.0.0/neon-forge-creator-pack-1.0.0.keireassetpackage` contains
two VFX graphs, two Shader Graphs, two Material Graphs, and one managed runtime C# behaviour. It is a quarantine input,
not a pre-signed release, and must pass the normal upload, isolated validation, moderation, and offline-signing flow.

Do not add a router rule for Astro, Supabase, the validator, or Kestrel. Caddy is the only public ingress.

## Ordered launch gates

Complete the gates in this order. If a gate fails, keep its feature flag disabled and preserve the last-good deployment.

1. **Identity hardening**
   - Enable Supabase leaked-password protection.
   - Configure custom SMTP and validate verification, recovery, rate limits, and deliverability.
   - Create the GitHub OAuth application, configure its secret outside the repository, enable explicit identity linking,
     and test duplicate-email recovery.
   - Register Kéire Hub as a public OAuth client with exact website and `keirehub://` callbacks. Validate PKCE, state,
     nonce, single-use codes, rotation, cancellation, and remote revocation.
2. **Publisher and legal readiness**
   - Obtain owner or qualified legal review of privacy, terms, marketplace, publisher, acceptable-use, copyright, and
     reporting policies.
   - Validate organization ownership and MFA-protected publisher application/moderation transitions with staging users.
   - Review submitted applications at `/admin/marketplace/`; approval transactionally activates the publisher, while
     changes and rejection preserve the recorded decision reason.
   - Keep `paid_checkout_enabled=false`; 0.3.1 products are free only.
3. **Package validation and signing**
   - Deploy the isolated validator worker under a dedicated unprivileged identity and private temporary root.
   - Add malware and secret scanning, no-network managed compilation, lease recovery, structured diagnostics, bounded
     archive handling, and validator-version fingerprints.
   - The dedicated offline marketplace signing key and public trust root exist. Back up its private material securely
     and rehearse key rotation and security revocation. Never place the private key in Astro, Supabase, the repository,
     or Hub.
   - After a package has a passing validation report, submit its exact digest from the publisher portal, review the
     validator evidence in `/admin/marketplace/`, and approve it only to `approved_pending_signature`. Staff approval
     never creates a publication or accesses the offline private key.
4. **Official staging products**
   - Run `python Scripts/Marketplace/create-official-marketplace-packages.py` from a clean, validated checkout to build
     deterministic `.keireassetpackage` artifacts for the five seeded official drafts. The builder fails on missing
     dependency identities, undeclared C#, links, nonportable paths, duplicate assets, or an existing output set.
   - Open **Staff -> Official releases -> Upload first release** for each product. This selects the exact first-party
     draft in the Publisher portal; official content uses the same quarantine, validation, moderation, and signature
     boundary as external publisher content.
   - Validate the exact quarantined bytes, moderate them, sign their canonical manifests offline, publish immutable
     artifact hashes, and confirm licenses and compatibility.

Rejected or change-requested submissions leave the active staff queue immediately. They are not deleted: the terminal
decision, reason, actor, version, and hashes remain in **Package review history** and the append-only audit trail.
Administrators may withdraw an `approved_pending_signature` release before publication; that withdrawal is an audited
rejection and cannot mutate an already published version.
5. **Hub and Editor acceptance**
   - Package a Hub containing the browser OAuth adapter, marketplace catalog/library client, verified package cache,
     resumable tasks, notifications, and current-user broker.
   - Validate Editor registry install, update, downgrade, embed/revert, selective import, executable-code consent,
     three-way conflicts, rollback, recovery, safe removal, cook, and player build.
   - Complete the full flow natively on Windows and Linux. Keep macOS unadvertised until native validation passes.
6. **Operations and public launch**
   - Move from DuckDNS staging to the branded domain, update exact origins/callbacks/CSP, and validate TLS and protocol
     registration.
   - Back up database data, Storage objects, immutable packages, audit events, and signing metadata; perform and record a
     restore rehearsal.
   - Run accessibility, keyboard/screen-reader, responsive, performance-budget, cache-isolation, RLS, abuse, package
     fuzzing, and end-to-end acceptance suites.
   - Enable flags one at a time, observing logs and rollback criteria between each change.

## Feature-flag sequence

The recommended activation order is:

1. `publisher_portal_enabled` for approved internal staging accounts only.
2. `marketplace_enabled` may expose the current signed-only staging catalog preview after upload, validation, and
   moderation acceptance. Claims remain unavailable until a signed publication exists.
3. `hub_oauth_sso_enabled` after the registered OAuth client and secure stores pass Windows/Linux validation. It may
   remain enabled only on the current private staging deployment while that acceptance is in progress.
4. `asset_packages_enabled` after downloads, signature verification, Hub cache, and Editor recovery pass together.
5. `community_enabled` after reviews, replies, reports, moderation, and abuse throttling pass.

`paid_checkout_enabled` remains false for the complete 0.3.1 release.

## Go/no-go evidence

Record the exact commit, migration list, Edge Function versions, package hashes, public-key IDs, backup identifier,
restore result, Windows/Linux build artifacts, test reports, Lighthouse results, accessibility review, and policy
approval. A launch is no-go if any privileged database transition is executable by `anon` or `authenticated`, any
private bucket is public, any OAuth or signing secret exists in a client/package/repository, any required flag was
enabled before its gate, or recovery cannot restore the last-good state.
