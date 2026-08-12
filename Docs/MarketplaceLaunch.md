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
The other five feature flags remain disabled. Leaked-password protection and custom SMTP are deferred until the paid
Supabase plan and branded production domain are available.

The repository now contains the 0.3.1 isolated package validator, network broker, atomic lease/report migration,
ClamAV adapter, secret scanner, generated no-network C# compilation, Windows firewall launcher, and hardened Linux
units. The validator lease migration is applied to the staging project and `marketplace-validator-queue` version 2 is
deployed in a fail-closed state. The scoped broker secret is now stored as ACL-protected machine-DPAPI ciphertext on
the Windows staging host. The reviewed 0.3.1 worker, broker, Asset Tool, managed API, pinned .NET SDK, and current
ClamAV definitions are provisioned under separate `LOCAL SERVICE` and `NETWORK SERVICE` startup tasks. The offline
worker blocks all four untrusted-content processes with verified firewall rules and an administrator-generated,
ACL-protected path/hash attestation. Harmless and EICAR fixtures passed their expected offline outcomes, a controlled
task restart preserved both service identities, and the broker now authenticates to the empty staging queue with HTTP
200 responses. A real quarantined harmless upload, live stale-lease recovery, and report-commit acceptance are still
required before any marketplace or asset-package flag can be enabled.

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
   - Keep `paid_checkout_enabled=false`; 0.3.1 products are free only.
3. **Package validation and signing**
   - Deploy the isolated validator worker under a dedicated unprivileged identity and private temporary root.
   - Add malware and secret scanning, no-network managed compilation, lease recovery, structured diagnostics, bounded
     archive handling, and validator-version fingerprints.
   - Create a dedicated offline marketplace signing key, backup its private material securely, publish only the public
     trust root, and rehearse key rotation and security revocation. Never place the private key in Astro, Supabase, the
     repository, or Hub.
4. **Official staging products**
   - Build deterministic `.keireassetpackage` artifacts for the five seeded official drafts.
   - Validate the exact quarantined bytes, moderate them, sign their canonical manifests offline, publish immutable
     artifact hashes, and confirm licenses and compatibility.
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
2. `marketplace_enabled` after signed official products exist and public catalog/claim acceptance passes.
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
