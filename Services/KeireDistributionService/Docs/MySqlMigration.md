# Local MySQL migration

Current deployment status, reconciled October 24, 2026: the Windows host selects schema-3 MySQL configuration.
Browser sign-in and the Hub's browser handoff/reconnect have been exercised after cutover. The website and current
Hub configuration use the native platform routes, not the Supabase authorization endpoint. The sections below retain
incremental implementation and acceptance history; an earlier pending-cutover statement is not the current provider.

Account and marketplace records have been imported and checked against the snapshot. MySQL account, marketplace,
administration, native authentication and download paths have passed isolated database and HTTPS acceptance.
The actual C# broker, guarded offline worker and publication signer have also passed one complete isolated managed
package flow, including compilation, malware scanning, review transitions and independent publication-signature
verification. That test used connection-local temporary tables and did not publish a customer asset.

Keep service-identity, external authorization, email-delivery, backup/restore, and client workflow evidence separate.
The recorded successful browser/Hub checks do not establish that every provider, email path, or failure mode has been
retested. Use retained deployment and acceptance records when deciding which additional checks a release requires.
Twenty-one imported private Storage object records still lack recovered file bodies. Preserve those records and
their ownership, report unavailability clearly, and do not claim the files were migrated or synthesize replacements.
The isolated worker result alone does not establish production service-account operation or complete recovery of
private file bodies.

## Backend boundary

Keep Supabase available as an explicitly selected backend. The MySQL implementation must provide the same account,
organization, publisher, moderation, catalog, entitlement, upload, validation, publication, and download behavior.
Do not automatically switch providers on an error: independent account and entitlement stores can diverge.
The browser and native clients must use authenticated service endpoints, never connect directly to MySQL.

The local implementation needs authentication/session management and permission enforcement in addition to SQL.
PostgreSQL row-level security policies and security-definer functions are behavioral requirements to port and test;
translating table definitions alone is insufficient. Keep monetary amounts as integer minor units and preserve UUIDs,
timestamps, unique constraints, ownership relationships, and audit history.

## Isolated database provisioning

`scripts/initialize-local-platform-mysql.ps1` creates only a new `keire_platform` schema and two localhost accounts.
The runtime account receives SELECT, INSERT, UPDATE, and DELETE on that schema. The migration account additionally
receives the schema-creation and alteration privileges required for migrations. Neither receives global privileges.
Both require TLS; the bootstrap connection validates the server certificate and hostname.

Supply an existing DPAPI LocalMachine bootstrap credential containing `username` and `password`, its entropy label,
the trusted MySQL CA certificate, and a new absolute output directory. Do not put passwords on the command line.
The bootstrap credential is read, not modified. Generated credentials are saved with DPAPI CurrentUser protection
and restricted Windows ACLs before executing MySQL DDL. Only the provisioning user can decrypt those settings.
Runtime service identity and credential loading must be configured explicitly before deployment.

`scripts/import-local-platform-accounts.ps1` sends the verified encrypted snapshot, connection settings, and a separately
protected account encryption key to the Node importer through explicitly UTF-8 standard input. It never writes plaintext
credentials or account exports to disk. `-CreateSchema` is only for the first import into an empty database; subsequent
read-only checks use `-VerifyOnly` and a new result filename. A duplicate import is rejected rather than overwriting
accounts. MySQL DDL is not transactional, so inspect the recorded failure stage before retrying schema creation.

The importer retains source UUIDs and bcrypt hashes, preserves microsecond timestamps, and verifies linked identities,
profiles, and MFA records against the source. Full sensitive account/MFA source records use AES-256-GCM with independent
nonces and record-specific authenticated context. The 32-byte encryption key is stored separately as
`account-record-key.dpapi` using CurrentUser DPAPI and entropy `Keire.Platform.AccountEncryption.v1`; preserve it with
the protected settings. Loss of this key prevents reading those records. Existing Supabase sessions are archived in
the source backup but are not active MySQL sessions; compatible authentication and a documented cutover sign-in flow
must be completed before switching providers.

The initial `Source/lib/platform-mysql.mjs` connection layer uses pinned mysql2, dedicated local accounts, verified
TLS, bounded pooling, prepared-statement support, and lossless integer/decimal/date settings. Its transaction helper
does not retry callbacks after an uncertain commit and discards connections when rollback fails. Callers own pool
shutdown and must never log connection settings. It is not yet selected by the website middleware.

`DocumentationSite/scripts/test-platform-mysql-live.mjs` accepts protected settings via standard input and checks both
roles, transport, large integers, prepared values, cross-database denial, and transaction reuse without modifying data.
It does not prove transactional writes, account behavior, or a completed migration; those require the later schema
and service acceptance tests.

The script rejects an existing output directory, database, or matching account. MySQL provisioning DDL is not atomic:
on failure, inspect `provisioning.json` and retain the encrypted settings. Do not delete the journal or blindly retry.
Provisioning does not modify other schemas, reset existing passwords, or restart MySQL.

## Migration acceptance gates

`-Section platform-support` covers full profiles, publisher applications, reviews/reports, feature flags, staff,
audit events, contact records, and private marketplace rate limits. The import preserved one profile, one application,
six feature flags, one staff membership, 103 audit events, and three rate-limit records; the other four tables were
empty. Fourteen live rollback-contained checks cover profile JSON/locale/handle rules, staff revocation, review
uniqueness/ratings, report target exclusivity/cascade, audit JSON, contact status, and rate counts. Audit/contact
sequence IDs are retained; rollback tests may leave harmless gaps in MySQL AUTO_INCREMENT counters.

`-VerifyAll` is a separate read-only launcher operation using the runtime account. It requires exact coverage of all
public/private source tables, validates each section's source schema and import ledger, and compares every mapped
field. It verified all 35 domain tables and 221 rows from the pinned source snapshot, plus 61 foreign keys, 50
primary/unique keys, and ten conditional uniqueness rules. Extra or missing source tables fail the check. Auth and
Storage schemas are outside this domain-coverage result and retain their own acceptance gates.

The earlier `platform_profiles` import remains a preserved account-source record. The full `profiles` table is the
domain destination; future service writes must use that destination consistently. Source triggers, RLS, and actor
authorization are not supplied merely by copying domain records. No live routing or worker selection changes occur
during these imports.

`-Section marketplace-publication` imports the two public-key tables, uploads, validation reports, submissions,
signed-publication records, and publication jobs. The initial import preserved one publication key, one validator key,
12 uploads, 12 reports, nine submissions, eight publications, and two jobs. All fields matched the source. Sixteen live
checks cover public-key byte/fingerprint agreement, Unicode upload paths, duplicate paths and active work, malformed
diagnostics, retry limits, lease consistency, and rollback. Existing signed documents are preserved byte-for-byte;
signature verification against recovered package bytes remains a separate publication gate.

Upload paths retain their original Unicode text. A unique SHA-256 generated key avoids MySQL's index-byte limit for
1024-character paths; a digest collision fails closed as a duplicate rather than overwriting an object. Application
lookups must still compare the full path. Active upload/submission uniqueness uses virtual generated columns. JSON
diagnostics are compared recursively with stable object-key ordering while preserving array order, and unsafe integer
values are rejected. Source SMALLINT bounds are checked before inserting attempt counts.

No validator or publication worker is connected to the migrated jobs. Database checks enforce valid record shapes;
authorization, legal state transitions, leasing concurrency, and signing must pass service-level tests before cutover.

`-Section marketplace-content` handles product media metadata, product tags, version dependencies, and wishlists.
All four source tables are currently empty; their existence is still required and verified. Thirteen live checks use
temporary rollback-contained records to verify invalid media paths/hashes/types/dimensions, missing product references,
tag/product deletion cascades, dependency/conflict uniqueness, and wishlist duplicates. No media files are created or
served by this test. Repeat import is refused by the section ledger even when every table is empty.

All sections now compare source column names, types, and nullability before opening the destination database. This
detects schema changes that row-only validation would miss for an empty table. It supplements, rather than replaces,
the constraint/policy review and field-by-field post-import comparison.

The marketplace launcher also accepts `-Section marketplace-access` for orders, order items, entitlements, legacy
device sessions, and download grants. This section requires the core and account tables and has its own import-ledger
entry. The initial import preserved five orders, five order items (including accepted license text), five entitlements,
three device records, and eight grants. Eight live checks passed for exclusive ownership, duplicate entitlement/order
rejection, zero-price enforcement, grant lifetime, device/grant cascade, and valid-write rollback. Both sections were
verified after duplicate-import refusal, and the core constraint tests were rerun with the access tables present.

Imported device-session IDs and grants are historical data, not credentials for the new authentication service.
Do not accept them as local login sessions or serve private files until the new authorization and download routes
have passed their own tests. Database ownership constraints alone do not provide row-level access control.

`scripts/import-local-platform-marketplace.ps1` imports the first nine domain tables from the pinned protected export.
It preserves source identifiers and microsecond timestamps, stores text arrays as JSON arrays, and rejects unsafe
integers, missing/extra columns, unsupported values, and truncation. It compares every field inside the import
transaction before recording the `marketplace-core` batch. Existing rows are never overwritten. The version key
allows 255 characters; the current publisher API limits submitted version strings to 128 characters.

`-CreateSchema` is only for the initial empty destination. MySQL DDL is not transactional: inspect a failed creation
and complete only the missing reviewed tables rather than rerunning or dropping data. `-VerifyOnly` compares current
rows with the source and ledger without writes. `-AcceptanceOnly` additionally runs rollback-contained constraint
tests, including orphan rejection, invalid category slugs, nonzero prices, duplicate active offers, cascade deletion
of an isolated product/offer fixture, and rollback of a valid update. The active-offer key is a virtual generated
column so its unique index remains compatible with the source product-deletion cascade.

The first import contains one organization, one membership, one publisher, five categories, no tags, five license
revisions, seven products, nine versions, and six offers. All fields and six live constraint/rollback checks passed.
These are isolated domain records, not a working marketplace: entitlements, orders, moderation/publication queues,
private storage bodies, transition authorization, and route integration remain separate acceptance gates.

Before domain import, run `DocumentationSite/scripts/check-platform-migration.mjs` with verified decrypted database
and schema JSON strings plus their trusted digests through standard input. It performs no database writes and emits
counts only. The current export passes 61 foreign-key checks, 50 primary/unique-key checks, and ten application-specific
conditional uniqueness checks. These include ownership, personal/organization entitlements, case-insensitive names,
and the single-active-offer/upload/submission rules. Missing references, duplicate keys, unsupported foreign-key
syntax, and unsafe numeric keys fail preflight. This does not validate all check constraints, RLS, or destination DDL.

The initial schema export must be supplemented with indexes, enum labels, and triggers before translating the schema.
The live application schema has 109 indexes, 57 enum labels, and nine triggers. In particular, a plain table copy would
lose partial unique indexes and transition protection. Preserve their behavior using reviewed destination constraints
and service authorization, with concurrency tests. Never execute source SQL definitions blindly against MySQL.

The isolated `platform-local-auth.mjs` service verifies imported bcrypt passwords and requires the existing verified
TOTP factor before issuing an authenticated session. Opaque challenge/session tokens are stored only as hashes;
challenges expire after five minutes and sessions after eight hours. Failed login limits are persisted in MySQL.
The HTTP adapter must supply a trusted client identity, never an arbitrary client-submitted identity. MFA counters
are consumed transactionally, so concurrent reuse of the same code cannot authenticate twice. Logout, password
changes, account bans, and newly required MFA invalidate the relevant session access.

`scripts/test-platform-auth-live.mjs` accepts protected database settings through standard input. It creates and
removes an isolated synthetic account; it never logs in as the imported account. Its 28 checks cover password and
MFA failures, concurrent replay, attempt limits, expiry, logout, password changes, and assurance transitions. Schema
creation is a one-time explicit option and refuses existing authentication tables. The service is not yet connected
to HTTP cookies, CSRF protection, OAuth, registration, recovery, or MFA enrollment. Passing these checks does not
authorize switching the website backend or imply those workflows have passed.

`scripts/PlatformMigrationBackup.psm1` reads the encrypted migration exports in memory. Callers must supply the
trusted SHA-256 and exact part count, and must not print the returned JSON. The reader rejects incomplete, reordered,
corrupted, redirected, or oversized parts and preserves the original timestamp and numeric text. Regression coverage
is provided by `Scripts/Tests/test-platform-migration-backup.ps1`.

The website's `Source/lib/local-blob-store.ts` provides the initial private immutable-byte storage primitive.
Uploads are streamed to a temporary file, checked against the expected size and SHA-256, and published exclusively
without replacing existing bytes. Failed/interrupted uploads are removed; corrupt existing objects cause failure.
This is not yet wired to routes, bucket/object metadata, or authorization. Its directory must remain outside the
public website root and must have service-owner-only write access. Windows ACL configuration remains a deployment
requirement; POSIX file mode arguments alone do not configure Windows ACLs.

1. Export the live source consistently, with schema definitions, constraints, policies, routines, views, and row counts.
   Encrypt account records and credentials at rest. Verify a decrypt/parse round trip and record an export digest.
2. Copy every Storage object, including quarantine files and validation evidence. Preserve bucket privacy, object paths,
   content types, sizes, and checksums. Database Storage metadata is not a backup of file bodies.
3. Inventory Vault and external configuration separately. OAuth application configuration, mail delivery, signing keys,
   service credentials, and encryption keys are not necessarily included in a database export.
4. Implement and test both backend adapters. Preserve existing password verification where supported; never replace
   passwords with defaults. Document session invalidation and test login, logout, recovery, MFA, and Hub authorization.
5. Import into isolation. Compare table counts, relationships, ownership, entitlements, published version identities,
   and actual package hashes. Validate anonymous, member, publisher, moderator, and administrator permissions, including
   cross-account denial and private-object denial.
6. Test free acquisition, existing library access, upload/resume/cancel, validation failure and retry, moderation,
   publication leases, signing, downloads, and installation in Hub/Editor. Retain the existing honest unavailable state
   for paid checkout unless a payment implementation is independently completed and tested.
7. Verify backups and a restore rehearsal. Freeze mutations for a final consistent export/import or implement a tested
   delta reconciliation. Switch the live provider only after acceptance; retain the original source for rollback.
8. Rebuild clients if their backend contracts change. Revalidate release artifacts, publish the Windows 0.4.7 patch,
   and verify public downloads. Only then remove the user-authorized existing product installations.

## Initial source inventory

The October 19, 2026 inventory of the live KeireHub project found one account, one profile, seven products, nine product
versions, five entitlements, six Storage buckets, and 21 Storage objects. Vault contained no secret rows. All existing
file bodies are in private buckets. A consistent data export captured 71 tables and 603 rows; these counts are a
baseline, not a guarantee that the source will remain unchanged until cutover.

The account export also contains one verified TOTP factor, two identity records, nine sessions, and 132 refresh-token
records. Password hashing uses bcrypt (`$2a$`). Preserve the existing MFA requirement; do not silently disable it during
cutover. The encrypted backup contains the factor material, which must never be printed in diagnostics. Session/token
signing compatibility and OAuth provider configuration need their own migration review.

No MySQL cutover or full migration acceptance has occurred. The pending Windows release must not be reported as
published while the website dependency and migration gates remain incomplete.

### Private blob reads

`LocalBlobStore.read` verifies size and SHA-256 before yielding any content, then reads through the same open
handle in bounded chunks. Cancellation closes the handle. Routes must authorize the object before reading.
Private directory ACLs must prohibit external writers: handle identity checks do not prevent in-place writes
by another privileged process. This primitive does not recover missing source objects or enable live routes.

### Storage recovery inventory

`planStorageRecovery` preserves bucket/object metadata and joins archive, upload, and validator-evidence
integrity references. It rejects conflicting hashes/sizes, duplicate identities, and unreviewed versioned
storage. Every object starts missing; metadata alone never counts as recovered bytes. Missing referenced
objects and unreviewed media references remain explicit acceptance work. Plans contain private metadata
and must not be logged; only the aggregate summary is suitable for validation reports.

### Isolated storage metadata import

Use `scripts/import-local-platform-storage.ps1` with the same protected backup/settings directories and a
new result path. `-CreateSchema` is only for first creation; do not repeat it on the migrated database.
`-VerifyOnly` uses the runtime account to compare every preserved source record and normalized field,
including null recovery hash/time. This initial-state verifier is intentionally strict; after real recovery,
acceptance must additionally verify actual blobs rather than weakening it to accept metadata claims.

The import is transactional and refuses nonempty destinations. Original metadata is retained in JSON,
with indexed bucket/object identity and expected hash/size. Name digests enforce uniqueness without
truncating source keys; future lookup must compare the full object name. No local path comes from the
source key. Recovery timestamps require a matching expected hash. Permissions and live routes are still
separate work; this import does not implement storage policies or authorize downloads.

### Browser authentication boundary

`createLocalAuthenticationHttp` is application-owned and accepts the authentication service, canonical HTTPS
origin, and a trusted server-derived client identity. It handles browser session GET/POST, challenge
verification, and sign-out. Session/challenge tokens stay in Secure HttpOnly host-only cookies; mutations
require the configured Origin and bounded JSON bodies. Duplicate session cookies are rejected. Hub bearer
OAuth must use its separate protocol adapter. Service failures return generic retryable errors.

The adapter remains disconnected from live Astro middleware. It needs real-database HTTP acceptance and
frontend MFA challenge-page integration, plus registration/recovery/OAuth and service lifecycle ownership,
before provider cutover. Existing Supabase routes remain active. Unit HTTP tests do not prove those workflows.

The protected `scripts/test-local-platform-auth.ps1` launcher runs synthetic authentication acceptance
against isolated MySQL using credentials over stdin. Forty checks cover password/MFA concurrency and
HTTP adapter Requests/Responses, cookie handling, session lookup, CSRF and logout. This is not yet
a deployed Astro/browser test. Fixture cleanup and a separate real-account comparison passed.

### Backend ownership

`PlatformBackend` is owned by the server application. `withBackend` holds a request lease; `close` rejects
new work, drains existing leases, closes authentication, then ends the MySQL pool. Concurrent requests
share one initialization attempt. Failed initialization remains failed until the application is restarted,
with no automatic Supabase fallback. Supabase clients remain request-owned because they carry cookie state.
The protected loader transfers a fresh encryption-key buffer, which is erased after authentication copies it.
Do not retain backend services beyond the callback or call shutdown from inside a request lease.

Lifecycle acceptance now runs against real isolated MySQL as part of 43 synthetic checks. Host bootstrap,
protected loader wiring and Astro middleware integration remain pending; this does not change the live provider.

### Protected Windows runtime loader

`loadWindowsPlatformSecrets` starts the reviewed `scripts/read-local-platform-runtime.ps1` helper with
absolute executable/script/settings paths, no shell or profile, hidden window, timeout and bounded output.
The helper decrypts CurrentUser DPAPI settings and the account key into its parent pipe only. Never run
that helper interactively or redirect its stdout into logs. Only the runtime database account is returned;
the migration account is excluded. The parent suppresses child errors instead of forwarding possible
secret-bearing stdout or stack traces. The service owner must match the DPAPI owner; provisioning under
another identity requires an explicit protected transfer, not disabling DPAPI. Deployment must provision
trusted helper/config paths and private directory ACLs before wiring this into host startup.

The actual protected runtime load and TLS database connection passed without printing secret values.
Supabase remains live; no host configuration or service identity has changed.

Pending MFA display data is separate from `locals.user`: `pendingChallenge` checks expiry, attempts,
password changes and account availability before returning authenticator labels/IDs. It never returns an
authenticated user or factor secrets. Verification rechecks mutable state under locks. The MFA page accepts
this explicit challenge data; middleware currently initializes it to null until MySQL request wiring is ready.
Fifty real-MySQL synthetic checks now pass, including challenge lookup and status responses.

### Staged request routing (not ready for production selection)

Middleware now recognizes `KEIRE_PLATFORM_BACKEND=mysql`. Its host must inject the application-owned
`platformBackend` and trusted `platformClientIdentity` through request locals. The current standalone host
cannot do this yet; selecting MySQL without that owner returns 503. Supabase remains the default and live
provider. The staged adapter handles browser authentication and sign-in/challenge rendering only. All other
routes fail closed with a clear unavailable response; they never borrow Supabase identities or data. Complete
all remaining route implementations and host acceptance before switching the public provider.

### Isolated rendered-site QA

Build with `KEIRE_NODE_ADAPTER_MODE=middleware` for the loopback QA host (default builds remain standalone).
Run `scripts/start-local-platform-qa.mjs --smoke` with explicit `KEIRE_QA_PORT`, `KEIRE_QA_POWERSHELL`, and
`KEIRE_QA_SETTINGS_DIRECTORY`. The host serves public build assets with pinned send 1.2.1, injects the
application-owned backend into Astro locals, derives client identity from the socket only, and drains
responses before disposing services. It binds only 127.0.0.1 and never replaces the public listener.
This HTTP host is for scripted acceptance; browser Secure-cookie testing still requires an HTTPS QA proxy.
Production proxy trust, deployment packaging and other route migrations remain pending.

The --smoke mode closes the host after checks. Do not deploy its middleware build through the current
standalone production launcher; rebuild with the default adapter mode before standard publication.

`node scripts/test-platform-http-live.mjs` uses the same QA environment settings and current middleware
build. It creates one synthetic account/TOTP factor with the runtime role, launches the isolated host over
private IPC, exercises real HTTP login, rendered MFA, verification, session and logout, then drains the host
and removes its exact fixture IDs/rate keys. Credentials and codes are never logged. An isolated client
namespace avoids modifying another QA session's rate-limit records. Eleven assertions passed, followed by
verification that the imported account records remained unchanged. This test manually sends cookies;
browser HTTPS cookie policy and production reverse-proxy acceptance are still required.

### Account overview migration

The staged MySQL `/account/` page uses `localAccountOverview` with the server-verified session. Queries
scope profiles, identities and devices to that account; library rows require personal ownership or membership
in a non-deleted organization. Staff status does not broaden the personal "My assets" view. Only identity
provider/email display fields are returned, never password hashes or MFA secrets. Rendered local pages use
private/no-store caching. Organization creation remains hidden until its mutation route is ported; security,
OAuth and other linked workflows still need migration before public cutover.

Account visibility acceptance adds 13 live checks inside a rolled-back transaction: personal ownership,
nonmember denial, shared membership, membership removal, soft-deleted organization filtering, entitlement
revocation, and fixture rollback. The authentication harness now passes 63 checks. A post-test full-domain
comparison verified all 35 tables and 221 imported rows unchanged. Existing products/orders are only read
for fixture foreign keys; no source or real-account records are modified.

### Public catalog API

The staged MySQL catalog endpoint preserves the existing DTO and cursor format. It requires the marketplace
feature flag, published products, unsuspended publishers and active categories. Ratings exclude hidden
reviews. Search escapes literal wildcard characters, queries bind values, and limit bounds are checked before
SQL construction. Shared cursor parsing now retains database microseconds instead of truncating to milliseconds.
Eleven rollback-contained live checks cover visibility, literal/case-insensitive search, precision, cursor
exclusion, disabling, and restoration. Product details and mutations still require migration.

## Marketplace listing acceptance

The isolated MySQL host now renders `/marketplace/` from the same visibility-filtered catalog queries as the public
catalog API. Search and active category selection remain available, and disabled or unavailable catalogs use the
existing user-facing states. The live website still uses Supabase. Real HTTP acceptance includes listing rendering
and an empty-search result alongside authentication and account-isolation checks.

## Optional email verification (in progress)

`scripts/configure-local-platform-mail.ps1` collects a Google sending address and app password in a masked local
prompt. It stores a CurrentUser DPAPI-encrypted record under `%LOCALAPPDATA%/Keire/PlatformMail`, with access limited
to the current Windows identity and SYSTEM. It refuses to overwrite existing settings. The service must run under
that identity or use a separately provisioned secret; credentials must never be copied into repository configuration.

The application-owned verification mailer uses authenticated TLS to Gmail, fixed message templates, bounded network
timeouts, no file/URL message content, and sanitized errors. `DocumentationSite/scripts/verify-platform-mail.mjs`
checks SMTP authentication without sending mail. Set `KEIRE_QA_POWERSHELL` to the absolute PowerShell 7 executable.
The private decrypt script's stdout is a parent-process protocol and must never be run directly into terminal logs.

This transport is not yet connected to sign-in. Enrollment, code expiry/attempt/replay protections, account-method
selection, and browser acceptance remain required before enabling email verification. Existing authenticator factors
remain enforced. SMTP authentication success does not prove inbox delivery. Gmail sending limits and service blocks
must be handled as delivery failures, without granting a session or bypassing an enrolled factor.

Email-code proofs use a protected-key HMAC rather than an unkeyed hash of a small numeric code. The proof binds
the account, exact email, password digest, initiating authorization, purpose, opaque challenge token and five-minute
validity window. Only the token hash and keyed proof are persisted; plaintext codes are for delivery only.
The proof helper is deliberately not an authentication endpoint: its caller still must lock/consume challenge rows,
share the five-attempt budget across methods, and issue a session atomically. Those database and HTTP integrations
remain pending; a successful proof comparison alone does not establish a session or prevent replay.

The email challenge store now has explicit schema creation, pending/sent/failed delivery states, persistent
five-attempt exhaustion and row-locked consumption. The authorization callback runs on the same transaction;
rejected callback writes roll back to a savepoint while the failed attempt persists. Exceptions roll back the
transaction. Live synthetic MySQL acceptance covers concurrent consumption, replay, pending/failed delivery,
attempt exhaustion, expiry and callback rejection/rollback. Account/client send throttling and shared password
challenge budgets still must be integrated before exposing an endpoint. `-CreateEmailSchema` on the protected
authentication acceptance launcher is a one-time migration switch; never repeat it after successful creation.

Email-send reservations reuse the persistent authentication limit table: one send per account per minute, five per
account per fifteen minutes, and twenty per trusted client identity per fifteen minutes. Sorted row locks make
concurrent reservations atomic. Denied requests do not consume other budgets, and SMTP failures must retain a
successful reservation. MySQL acceptance covers simultaneous sends, cooldown boundaries, account/client isolation
and expiry. The enrollment/sign-in service still must reserve these limits in its challenge-creation transaction.

Local session lookup now distinguishes verified email factors from authenticator factors. An email session remains
AAL1, is bound to the enrolled email matching the current account email, and is rejected after factor revocation or
an attempted AAL2 upgrade. Existing authenticator sessions retain AAL2. Five live MySQL fixture checks cover these
boundaries. No enrollment or email-session issuance endpoint is exposed yet.

The staged `LocalEmailAuthentication` enrollment service now verifies the current password and an authenticated
session. Accounts with existing verified factors require an authenticator-backed AAL2 session. Issuance reserves
send limits and persists the pending challenge atomically; delivery occurs after commit and only successful delivery
enables consumption. Completion rechecks session/account/factors under locks and creates one encrypted email factor
in the consumption transaction. It does not remove the authenticator. Live acceptance uses synthetic accounts and a
local mail capture; no enrollment HTTP/UI route is enabled yet. Email sign-in issuance remains the next integration.

The staged email service now also issues and consumes sign-in codes after password verification. The parent login
challenge, current account and enrolled email factor are locked and revalidated. Email and TOTP verification share
the parent attempt budget; successful email verification consumes both challenges and creates an AAL1 session in
one transaction. Local mail-capture acceptance covers enrollment through sign-in, replay, password-challenge
invalidation and mixed-method attempt exhaustion. HTTP routes, browser controls, and real inbox delivery acceptance
remain pending; these services are not enabled on the live site.

The browser HTTP adapter accepts an optional email service and handles enrollment/request/verification endpoints
under `/account/email/`. It preserves canonical-origin checks, cookie-only credentials, bounded JSON input and
private/no-store responses. Enrollment and sign-in use separate Secure HttpOnly host cookies; code tokens never
appear in JSON. New login, completed login and logout clear stale email cookies. Delivery failure creates no session,
and rate-limit responses include the actual Retry-After interval. These adapter paths still require host ownership,
Astro route registration, browser controls and rendered-site acceptance before activation.

The application backend now owns an optional mailer, drains requests before closing it, and disposes authentication
and database resources after partial initialization failures. Astro email routes are registered. The isolated QA
host accepts `KEIRE_QA_MAIL_MODE=disabled` (default), `capture` (requires private parent IPC), or explicit `gmail`
(protected local credentials). Capture mode never contacts SMTP. Real rendered-site HTTP acceptance now covers
enrollment, resend cooldown, password login, email verification, AAL1 session and logout using a synthetic account.
Browser controls and HTTPS browser/inbox delivery acceptance remain separate gates before live activation.

The staged account page now exposes email enrollment when the local mailer is available, shows an enabled state,
and explains when authenticator sign-in is needed before enrollment. The challenge page offers enrolled methods,
uses six digits for authenticator codes and eight for email, and provides explicit Send/Resend controls, countdowns,
delivery feedback and network-error handling. Real HTTP acceptance verifies these rendered controls and completes
the backend workflow. Interactive browser behavior and real inbox delivery are not yet accepted.

For isolated HTTPS QA, set `KEIRE_QA_TLS_THUMBPRINT` to an existing current-user, trusted ASP.NET localhost
development certificate. The private PowerShell pipe validates its identity, development extension, expiry and
existing root-store trust before passing an ephemeral encrypted PFX to the process. It does not install a certificate
or write a private key to disk. The listener remains loopback-only, with canonical origin `https://localhost:<port>`.
Use Node's supported `NODE_USE_SYSTEM_CA=1` when running the HTTP acceptance client against Windows development
certificates; never disable certificate verification. The full 43-check workflow passed over this HTTPS path.
