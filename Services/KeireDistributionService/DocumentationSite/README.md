# Kéire website

The product website, documentation, marketplace, and account pages share an Astro
Node deployment. Run `npm test` and `npm run build` from this directory. The build
syncs canonical documentation, validates local media, generates search, externalizes
inline content for the production CSP, and checks links and document structure.

## Staged MySQL GitHub authentication

GitHub sign-in uses provider identity IDs, single-use browser-bound state, S256
PKCE, verified provider email, and the account's existing MFA requirements.
Matching an email does not merge accounts. New GitHub identities must first
create an account and explicitly link GitHub while signed in.

Provision the authorization table once with the protected authentication acceptance
launcher's `-CreateGitHubSchema` option. Store OAuth application credentials with
`../scripts/configure-local-platform-github.ps1`; its interactive secret prompt
writes CurrentUser DPAPI settings in a new dedicated directory. Register
`https://YOUR-HOST/account/github/callback/` in the GitHub OAuth application.
Configure the application-owned `windowsGitHubServiceFactory` with that directory
and the exact site origin. The isolated QA host accepts
`KEIRE_QA_GITHUB_SETTINGS_DIRECTORY`; the secret itself is never an environment
variable. Supabase remains separately selectable.

Unit, protected-settings, and MySQL acceptance checks do not prove external GitHub
consent works. Production credentials, callback registration, and a real provider
sign-in must pass before live activation.

## Staged production MySQL host

Windows host settings schema 3 selects `platformBackend: "mysql"` and an absolute
`platformConfigurationPath`; schemas 1/2 and explicit `supabase` retain their
existing behavior. The MySQL configuration must match the website root, canonical
HTTPS origin (without trailing slash), and loopback port 4321. Its schema is
validated by `scripts/platform-production-backend.mjs`.

Build the website in middleware mode and deploy its `Source/lib` and `scripts`
dependencies along with `dist`, plus the protected-settings PowerShell readers.
The production entry is `scripts/platform-production-start.mjs`. Run it under
the same Windows identity that provisioned the CurrentUser DPAPI settings.
Provision worker credentials using `../scripts/configure-local-platform-workers.ps1`;
credentials remain separate from public JSON configuration and environment variables.

The host requires separate private spool, package and evidence directories, all
provider settings, and a single-writer marker. Inspect a marker left after a crash
before recovery; it is not automatically removed. The Caddy template replaces
forwarded client identity and omits URLs/headers from access logs. Existing live
proxy configuration is not modified by editing the template.
Set `KEIRE_QA_CADDY` to the installed Caddy executable and run
`node scripts/test-production-proxy-live.mjs` to test the adapted template on
isolated loopback listeners. It checks upload/ordinary body limits, forwarding
header replacement, and exclusion of synthetic secrets from actual access logs.

Staff operations require current MFA and staff membership. Evidence reads verify
the trusted signature and stored content hash and recheck access on each request.
`KEIRE_QA_EVIDENCE_ROOT` enables isolated signed-evidence HTTP acceptance alongside
the QA package root. Passing these fixtures does not establish external GitHub
consent, production email delivery, real worker process supervision, or live
cutover readiness; those remain separate acceptance gates.

## Showcase media

`Source/media/outpost` contains rendered output only. The private project generator
and controls are documented in [`Scripts/Examples/Outpost/README.md`](../../../Scripts/Examples/Outpost/README.md).
Do not copy licensed FBX files, textures, or the cooked interactive demo into this site.

The required set is six 1920 × 1080 WebP images (`arrival`, `materials`, `vehicle`,
`effects`, `editor`, `graph`), the main `outpost-tour.mp4`, and `tour-description.vtt`.
The homepage uses still images. The showcase places one optional tour below its image
gallery; the tour preloads metadata so its duration is available before playback.
Earlier short clips remain in source but are excluded from the published media set.
`scripts/showcase-media.mjs` validates the complete set before copying to the generated
`public/assets/outpost` directory. Each still is limited to 600 KB and the film to 18 MB.
The tour is user-initiated and has native controls.
The main film includes a visible text description.

## Product navigation

The homepage is a short product introduction. `/features/` links to dedicated
`/features/editor/`, `/features/graphics/`, and `/features/scripting/` pages, all
generated from `Source/lib/product-features.ts` and sharing `ProductNavigation`.
Keep tool descriptions and guide links in that model so the overview stays aligned
with the detail pages. Showcase images open a native modal viewer with previous/next
buttons, arrow keys, Escape dismissal, and focus restoration. Without JavaScript,
the same links open the full-size local images.

The roadmap presents Now, Next, and Later side by side on desktop and stacks them
on phones. Native disclosures retain all capability and evidence content without
requiring JavaScript. Release notes also use native disclosures; their section
links open the matching group, including on a direct URL with a fragment.
The mobile header includes a navigation fallback for JavaScript-disabled browsers.

## Responsive verification

Preview the built server on an unused loopback port with `HOST=127.0.0.1` and `PORT`
set in the process environment, then run `node dist/server/entry.mjs`. Check desktop
and mobile layouts, keyboard navigation, forms, video playback, documentation search,
and reduced-motion behavior. Protected pages require a configured test account to
verify authenticated content. Do not create fake account or marketplace data for screenshots.

Public marketplace browsing also needs `PUBLIC_SUPABASE_URL` and
`PUBLIC_SUPABASE_PUBLISHABLE_KEY` in the server process environment. Use the host's
configured public values; never use a service-role key for a website preview.
Signed-out visitors can browse published free assets and their details. Sign-in is
required to claim assets, and paid checkout remains separately gated. Missing
configuration or catalog failures return an uncached 503 with a retry message,
not an announcement that the marketplace has not launched.

Source identity comes from `Config/Project.conf`; downloadable release identity comes
from the publication metadata. `publication-state.test.ts` verifies these remain
separate when the next source release is ahead of the signed catalog.

Use the existing `../Scripts/deploy-windows-web.ps1` transactional deployment after
validation. Its settings must identify the actual host. Keep its process-ownership
checks and rollback behavior intact. A local preview is not a live deployment.

### Isolated MySQL authentication browser QA

With the existing protected QA settings, `node scripts/test-platform-http-live.mjs
--interactive --publisher-preview` adds a disposable publisher and an 8 MiB
synthetic package for browser transport testing. These bytes are not a valid
engine package and do not prove validator acceptance. Enter `stop` in the test
terminal to close the host and clean up fixtures; verify the final
`fixturesRemoved` result before considering cleanup complete.

Staged local uploads retain the same reservation when completion cannot be
confirmed. Use **Retry confirmation** on the upload form to check that upload
without resending its bytes. This recovery currently applies while the page
remains open; reload recovery is still pending.

The internal MySQL validator lease service uses transactions and `SKIP LOCKED`
selection, expires abandoned leases in bounded batches, and stops after five
attempts. Renewal requires the current worker and a live lease. Its acceptance
tests copy synthetic uploads into connection-local temporary tables, preserving
the real queue; the migration account therefore needs `CREATE TEMPORARY TABLES`
on `keire_platform`, while the runtime account retains its existing permissions.
The service is not exposed over HTTP until broker authentication, signed storage
access, and report verification are integrated. Multi-connection contention
acceptance remains required before production use.

The staged validator HTTP boundary requires a separate 32–256 character broker
secret, HTTPS, and POST requests without browser Origin, cookies, bearer tokens,
or Supabase API keys. It bounds JSON bodies to 1 MiB and hides unexpected database
errors. It requires lease, renewal, and verified-completion service operations
before construction; it is not currently connected to the website router.

Native desktop authentication is being implemented separately from browser
sessions. Its internal authorization modules validate registered callbacks and
S256 PKCE, hash single-use request/code credentials, and bind consent to the
current account and completed browser authentication. `platform-native-identity`
uses pinned `jose` signing with a dedicated host-loaded Ed25519 key. ID tokens
carry the registered issuer/client, account, desktop session, nonce, five-minute
expiry, and only approved email/profile claims; JWKS contains public material
only. Native sessions persist hashed opaque access/refresh credentials separately
from browser sessions, with 15-minute access and a fixed 30-day session limit.
Refresh rotates credentials; reuse commits session revocation before returning
an error. Every authenticated native operation checks revocation and current
account/password/MFA binding in a transaction. Database acceptance uses
connection-local tables and synthetic accounts, including rollback and reuse
cases. Authorization-code database acceptance also runs the normal browser
session reader, consent, PKCE exchange, native issuance, and code reuse rejection.
Browser logout invalidates pending codes while an already-issued desktop
session stays independent. These modules are not yet exposed as a complete
OAuth service: production schema/key provisioning, host configuration,
concurrent-session acceptance, and native client acceptance remain
activation gates.

Native refresh acceptance now also holds two distinct physical MySQL
connections and races the same refresh credential. Exactly one rotation may
succeed; the replay must revoke that session, including the returned replacement
access and refresh credentials. This test uses only the harness-owned synthetic
identity and removes its native tokens/sessions afterward. A separate race
starts with approved synthetic consent and redeems the same code on two physical
connections, requiring exactly one session and one rejected replay. Browser
consent itself is covered by the earlier HTTP/database flow, not this seeded
race. Full native-client acceptance remains pending.

The internal native token HTTP adapter accepts bounded form-encoded
authorization-code and refresh grants at `/auth/v1/oauth/token`. It rejects
browser cookies, Origin, bearer/API-key headers, duplicate fields and query
parameters. OAuth error responses and all token responses are non-cacheable.
The host supplies trusted client identity for persisted request limits
(20 authorization requests per ten minutes, 60 token requests per minute).
The optional `PlatformBackend.createNative` factory owns the native service and
drains accepted requests before closing its identity signer. Exclusive routing
covers authorization, token exchange, consent, decisions, discovery and public
JWKS; missing configuration fails closed. MySQL consent uses the existing
browser session and an origin-checked decision form, while Supabase retains its
original consent implementation. Isolated MySQL acceptance covers the browser
redirect, sign-in requirement, consent render state and cross-origin rejection.
The protected `test-local-platform-auth.ps1 -CreateNativeSchema` runner provisions
the four native tables with the migration account and verifies runtime read
access before running synthetic-account acceptance. This is an initial setup
operation: it refuses any existing native table, including a partial setup.
MySQL DDL is not transactional; inspect a failed setup rather than deleting
tables or rerunning blindly. Provisioning does not enable public routes.
Production key/configuration and actual Hub/browser HTTPS acceptance are still
required before enabling this service publicly.

`platform-native-settings.mjs` defines the protected issuer-settings payload.
Setup generates a dedicated Ed25519 key; runtime loading validates the exact
schema, expected HTTPS origin, registered client, canonical PKCS8 encoding and
public-key fingerprint. Invalid settings fail without exposing parser details
or generating a replacement key. The payload contains a private key: transport
it only through a private pipe into OS-protected storage. This module does not
itself persist or encrypt settings. Windows setup is provided by
`scripts/configure-local-platform-native.ps1` in the parent service directory:
pass a dedicated new absolute `-SettingsDirectory` and canonical `-SiteOrigin`.
It stores `native-settings.dpapi` with CurrentUser DPAPI and a directory ACL for
the current user and SYSTEM. Existing directories are refused. The service must
run under the same Windows identity and use `loadWindowsNativeSecrets` with the
private `read-local-platform-native.ps1` subprocess; never run that reader in a
visible terminal. No private key is placed in command arguments or plaintext
files. `windowsNativeServiceFactory` supplies the optional backend `createNative`
factory and captures the expected origin/client at host construction. The
isolated QA host opts in with `KEIRE_QA_NATIVE_SETTINGS_DIRECTORY`; its settings
must match `PUBLIC_SITE_URL`. Production listener deployment remains a separate
activation gate. Do not use a production-bound key with a different QA origin.

The native service also implements bearer-only `GET /auth/v1/user` and
`POST /auth/v1/logout?scope=local`. Every request rechecks the native session,
account security binding and registered client. Account responses expose only
the Hub's user ID, email and confirmation timestamp and require email scope.
Local logout revokes the authenticated native session; browser sessions remain
independent. Cookies, browser Origin and legacy API-key headers are rejected.
The exact Hub profile contract at `/rest/v1/profiles` supports reading the
authenticated user's display name/avatar and updating their display name.
Profile scope is required; arbitrary queries, other users and extra update
fields are rejected. Names are bounded to the native client's 64 UTF-8 byte
limit. Both local profile representations update in one transaction; missing
records fail and roll back rather than reporting a partial success. This is
not a general-purpose database REST endpoint. Device registration, download
authorization and native client provider configuration remain migration gates.

`registerNativeDevice` is the internal MySQL device-registration transaction.
It requires a live native session, the registered client and the Hub OAuth
feature flag. Stored bindings use a `mysql:` prefix so imported Supabase device
identifiers are not reused. Repeated registration updates the name/last-used
time but cannot reactivate a revoked row. The native service routes bearer-only
`POST /functions/v1/marketplace-hub` with the exact `session.register` request
and response contract. Bounded JSON rejects extra actor fields, browser
credentials and unsupported operations. Isolated MySQL acceptance covers
registration, stable repeated registration, renaming, the provider binding and
rejection after revocation. The endpoint also supports `download.issue` when
the native service receives both a private package store and a dedicated download key.

The internal native download helpers now check feature flags, the authenticated
device binding, published/withdrawn version publication metadata, and personal
or current organization-member entitlements under transaction locks. Grant
issuance verifies immutable package bytes first, then rechecks session expiry
before inserting a ten-minute grant. Missing/damaged files produce an explicit
unavailable response. Isolated MySQL acceptance uses copies of the actual table
schemas and a real temporary `LocalBlobStore`: missing/damaged bytes, revoked
ownership, security revocation, owned withdrawn versions, and expiry during
hashing are covered. Failed verification leaves no grant; the injected expiry
mutation also rolls back. The fixture's publication text is synthetic; signature
interoperability is tested separately by the publication pipeline. An authorized
delivery handler rechecks session, account, device, entitlement and version
access before and after verifying the file. Signed links expire within ten minutes
and never outlive the database grant. Their query strings are credentials and must
be redacted from proxy and application logs. The stream retains the verified file
handle; cancellation and service shutdown close it. The native service owns its
copied capability key and drains downloads before clearing it, while borrowing the
immutable package store. Private directory permissions must prevent external writers.
Isolated acceptance exercises issuance and delivery through the native service,
including exact returned bytes, altered links, revocation and damaged files.
`configure-local-platform-downloads.ps1` creates a separate host-bound download
key in a new restricted directory, protected with Windows CurrentUser DPAPI.
It refuses to overwrite an existing directory. The private reader must only run
through `loadWindowsDownloadSecrets`, which captures its output without logging.
`windowsNativeServiceFactory` accepts optional `downloads` configuration containing
`settingsDirectory`, `decryptScriptPath`, and `packageStore`; it loads the key for
the identity's origin and clears the temporary buffer after service construction.
`scripts/test-download-protected-settings.mjs` tests actual Windows protection,
reload, overwrite refusal and host mismatch. The native protected-settings fixture
also tests both protected keys together through service startup and shutdown.
The isolated HTTPS QA host accepts `KEIRE_QA_DOWNLOAD_SETTINGS_DIRECTORY` together
with `KEIRE_QA_PACKAGE_ROOT` and `KEIRE_QA_NATIVE_SETTINGS_DIRECTORY`. Both keys
must match that QA host's origin. The package directory must already exist and
have private permissions; the host does not create, populate or alter its ACLs.
Incomplete configuration or a symlink root prevents startup. Omit both download
variables to leave delivery disabled.
Native account and private-download endpoints have explicit dynamic Astro routes.
Routing accepts slashless API URLs without credential redirects; authored page links
retain their trailing slashes. The QA host sends native routes directly to the
application handler, while hidden static files remain denied. With native settings,
`start-local-platform-qa.mjs --smoke` checks trusted HTTPS discovery, public-only
keys, anonymous account/profile/marketplace denial, and hidden-file protection.
The full `test-platform-http-live.mjs` run also exercises native authorization when
`KEIRE_QA_NATIVE_SETTINGS_DIRECTORY` is set: rendered consent, PKCE exchange,
signed identity and access-token binding, profile reads, idempotent device
registration, refresh, and logout revocation. It uses the harness's disposable
account and removes only that fixture's native sessions, devices, and requests.
When the QA download settings and package root are configured too, the same run
creates a private draft product with a withdrawn version and synthetic transport
bytes. It verifies HTTPS byte equality, tampered-link rejection, and entitlement
revocation, then removes its own rows and file. This does not test installation
of a signed engine package or publish the fixture to the marketplace.
Production host key provisioning and storage configuration remain pending; this
does not enable live downloads or establish complete Hub migration readiness.

`scripts/test-native-protected-settings.mjs` exercises actual Windows protection,
stable reload, overwrite refusal and mismatched-host rejection. Set
`KEIRE_QA_POWERSHELL` to the absolute PowerShell 7 executable. It retains only
encrypted isolated fixture settings in the reported temporary directory.

`platform-validator-report.mjs` verifies the worker's exact Ed25519 signature,
trusted-key validity, package/version identity, evidence metadata, scan outcomes,
and completion timestamp. It rejects duplicate signed fields and contradictory
success reports. Transactional completion uses this verifier before recording
reports and checks the evidence bytes separately. For C# worker compatibility,
set `KEIRE_TEST_NODE_ATTESTATION=1` when running the Release distribution test
harness; Node must be on PATH. That optional check sends only a synthetic signed
report and its public key to Node, never a signing key.

Publication signature interoperability has a separate
`KEIRE_TEST_NODE_PUBLICATION=1` switch in the same Release test harness. It
passes documents produced by the actual C# publication signer to the Node
verifier, including the maximum signed 64-bit sequence. It checks exact signed
bytes and rejects changed sequence numbers, storage paths, and inactive keys.
Fixtures contain only synthetic release metadata and public keys. This checks
the signature contract; database publication completion and queue acceptance
must also pass before enabling the MySQL signer.

`completeLocalPublication` locks the current job and approval records, verifies
the signed metadata and immutable package/evidence stores, and rechecks lease
and key validity after file verification. Publication, product/version states,
submission state, and an exact retry receipt commit together. The database
acceptance fixture tests rollback, missing evidence, expiry during verification,
key deactivation, and duplicate/foreign retries using temporary tables and
stubbed blob checks. Full queue acceptance with actual private files remains
required before deployment. SQL boolean results are compared numerically:
the driver can return `"0"`, which must not be interpreted as truthy.

`createLocalPublicationService` assembles lease, renewal, completion and failure
operations behind `/internal/publication/v1/queue/`. The host supplies a
dedicated `x-keire-publication-secret`, verified stores, and database pool through
`PlatformBackend.createPublication`. Browser credentials are rejected before
body parsing; publication sequences are serialized as exact JSON integers.
Shutdown drains accepted operations before the host closes their dependencies.
An unconfigured MySQL publication service returns an unavailable response and
does not fall through to Supabase or browser handlers. Production configuration
and C# HTTP acceptance remain deployment prerequisites.

Internal validator completion locks the current lease and trusted key, verifies
the signature and immutable evidence bytes, rechecks expiry, and commits the
report, upload/version states, and audit receipt in one transaction. An exact
retry from the same worker returns its recorded report ID; it cannot change an
accepted report or consume another validation attempt. This is not yet routed
to the live broker: private transfer endpoints and full pipeline acceptance
remain required.

Internal private-transfer handlers issue HMAC-signed package GET and evidence PUT
links bound to an upload, worker, and validation attempt. Every transfer checks
the live database lease; renewal preserves links, while expiry, reassignment,
and completion revoke them. Package reads verify stored bytes before streaming.
Evidence uploads are bounded to 8 MiB and stored immutably by SHA-256. Hosts must
redact URL query strings from access logs because these links carry credentials.
The handlers have isolated MySQL/filesystem acceptance coverage but are not yet
connected to the application router or tested through the complete C# broker
workflow. Production configuration requires a separate 32-byte link-signing key.

`createLocalValidatorService` assembles queue operations and private transfers,
minting both transfer URLs while keeping the internal attempt counter out of the
strict C# lease response. It borrows the host's pool and verified stores, rejects
new requests during shutdown, and drains accepted handlers before clearing its
keys. Hosts must also finish or cancel outstanding response streams before
disposing storage. `PlatformBackend` accepts an optional `createValidator`
factory and disposes that service before uploads and the database, including
partial startup failures. MySQL requests under `/internal/validator/` route
exclusively to this service, returning 503 when it is not configured; they never
fall through to browser authentication or rendering. Production and QA host
configuration and complete broker acceptance remain pending.

Set `KEIRE_TEST_VALIDATOR_HTTPS=1`, `NODE_USE_SYSTEM_CA=1`,
`KEIRE_QA_TLS_THUMBPRINT`, and `KEIRE_QA_POWERSHELL` when running the local
authentication acceptance harness to exercise validator lease, download,
renewal, evidence upload, and signed completion through a real loopback HTTPS
listener. This uses connection-local copies of the synthetic upload tables and
temporary blob storage; it cannot claim imported uploads. It requires an already
trusted localhost certificate and never disables certificate verification.
The default client in this check is Node. To additionally exercise the real C#
`ValidatorQueueApi`, first build `KeireMarketplaceValidator.Tests` in Release,
set `KEIRE_TEST_VALIDATOR_CSHARP=1`, and set `KEIRE_QA_DOTNET` to the absolute
compatible .NET executable. The child receives only fixture credentials through
stdin, generates an ephemeral signing key, and registers its public key in the
connection-local fixture. It verifies package bytes, renews its lease, uploads
evidence, signs and submits a synthetic report, and retries completion; the
database must contain exactly one report. This tests the real broker transport
and signature contracts, not the separate malware/compilation worker process.

Add `KEIRE_TEST_PUBLICATION_CSHARP=1` to that C# validator HTTPS run to exercise
the actual `PublicationQueueApi` and signer against the resulting validated
report. The isolated publication fixture verifies the report's signature,
renews the lease, signs sequence `9007199254740993`, completes publication,
and retries the same completion. The service verifies the real temporary
package and evidence files; the database must contain exactly one publication
and audit receipt. Both C# validator and HTTPS flags are required, so this
additional acceptance cannot silently skip its prerequisites. The temporary
queue records are reset only within the fixture's connection-local tables.

The staged `submitLocalVersion` operation preserves the existing publisher review
transition: verified session, enabled publisher portal, active organization
owner/admin membership, an unsuspended publisher, a validated version, and a
matching successful report are required. Requested changes reuse their submission
ID and clear the previous assignment/decision. State updates and the audit entry
use the authenticated transaction. The MySQL adapter routes
`POST /publisher/v1/versions/{id}/submit/` through this operation with exact
HTTPS Origin and browser-session checks. Bearer credentials are rejected and
unexpected errors give an unconfirmed-submission message without database details.
The local authentication acceptance harness exercises submission against temporary
MySQL product, version, report, submission, and audit tables, including failed
authorization, audit-write rollback, duplicate rejection, and resubmission.
Browser-driven review and staff decisions remain separate production gates.

Staged staff-review policy preserves administrator-only signing approval and
administrator rejection to withdraw approval. A local publication sequence
allocator locks a dedicated counter within the caller's transaction and stays
above imported jobs and signed publication documents. It preserves exact
64-bit values and fails on malformed sequence metadata or exhaustion. Its
temporary-table acceptance covers allocation and rollback. The staged
`decideLocalSubmission` transaction updates the submission, release, product,
publication job, and audit together. Approval queues signing; withdrawal cancels
queued or leased jobs and clears their leases. Real MySQL acceptance verifies
administrator enforcement and audit-failure rollback, including counter allocation.
`POST /admin/marketplace/v1/actions/` accepts the existing `submission.decide`
contract through browser-session and exact-Origin guards. Other staff operations
still return an explicit unavailable response. Approval requires the report's
validated upload to belong to the reviewed version, in addition to matching its
archive hash. Two-connection allocation acceptance verifies ten distinct sequence
numbers and removal of its uniquely named fixture tables. Enable it with
`KEIRE_TEST_PUBLICATION_CONCURRENCY=1` and configure `KEIRE_QA_POWERSHELL`,
`KEIRE_QA_BOOTSTRAP_CREDENTIAL`, `KEIRE_QA_BOOTSTRAP_ENTROPY`, and
`KEIRE_QA_BOOTSTRAP_CA`. The protected bootstrap credential is used only by a
scoped cleanup helper; the migration account receives no additional DROP rights.
Provision the local counter by passing `-CreatePublicationSequenceSchema` to
`test-local-platform-auth.ps1` with the existing protected settings/result
arguments. This additive step creates only the counter table, then verifies
allocation using the runtime account inside a transaction that is rolled back;
it consumes no sequence and grants no additional runtime permissions.
Staff UI
and evidence access, and the signer queue remain pending.

Internal MySQL publication lease operations now require an active signing key,
current approval, and matching validated upload/report metadata. They select work
with `SKIP LOCKED`, recover expired leases in bounded batches, delay retries by
five minutes, and stop after five attempts. Renewal checks the owning worker and
continued approval. Real temporary-table tests cover those transitions; the
Explicit worker failure handling also checks a live owned lease, records a
bounded error code, and commits its audit with the state change. Retry delays are
1, 5, 15, and 60 minutes; permanent errors and attempt five stop retries.
The publication HTTP boundary and signed completion are not enabled yet.
Sequence values remain exact decimal strings internally and
must be serialized as exact JSON integers at the eventual C# HTTP boundary.

Build with `KEIRE_NODE_ADAPTER_MODE=middleware` before running
`node scripts/test-platform-http-live.mjs --interactive`. Configure
`KEIRE_QA_POWERSHELL`, `KEIRE_QA_SETTINGS_DIRECTORY`, and `KEIRE_QA_PORT` for the
protected local test backend. For browser cookies, also provide
`KEIRE_QA_TLS_THUMBPRINT` for an existing trusted localhost development certificate.
The harness validates the certificate rather than disabling TLS checks or changing trust.

This explicit test workflow creates a disposable account and captures verification
emails locally; it never sends them through Gmail. Its terminal displays only the
synthetic credentials and captured test codes. Enter `totp` for the test authenticator
code and `stop` to close the host and remove its fixtures. EOF or the 45-minute
interactive limit also ends the session and runs cleanup. Treat screenshots as QA
evidence, never as real marketplace/account content. Without `--interactive`, the
harness runs its automated HTTP assertions and cleans up immediately.

The middleware build is for this custom host. Rebuild with the default adapter mode
before using the existing standalone production launcher.

The staged MySQL public API also serves `GET /marketplace/v1/products/{id}/`.
It reads the visible catalog product and its public version history, approved media,
visible reviews, and active offer in one database transaction. Private archive locations
are excluded. Missing or non-public products return 404; disabled marketplace access
returns 503. This API does not grant ownership or download access.

Product HTML pages resolve publisher/product slugs through the same public-detail
service. They use private, non-cacheable responses when rendering personal ownership.
Acquisition remains explicitly unavailable in this staged backend until its mutation
and download routes have been migrated and validated. The Supabase path is retained.

Publisher pages also use the MySQL catalog visibility rules, exclude suspended
publishers, and provide a cursor-based More assets link after each 30-product page.
Private local-backend account and product pages render their verified signed-in
navigation immediately; public catalog pages keep the existing session-status lookup.

The staged browser library API uses the verified local session and explicitly scopes
personal ownership or membership in the requested active organization. Revoked
entitlements and deleted organizations are excluded. Pages retain the existing
24-item response and microsecond cursor ordering. Bearer credentials remain rejected
until the separate native-client OAuth migration is complete.
