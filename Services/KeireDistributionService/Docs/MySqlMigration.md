# Local MySQL migration

Status: preparation in progress. The website and Hub still use Supabase. Creating the local database does not
enable a MySQL application backend, migrate accounts, or copy Storage files.

The isolated account-data import is now implemented and has been verified against the initial snapshot. This is not
login acceptance or live cutover. Marketplace data and file-body migration remain pending.

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
