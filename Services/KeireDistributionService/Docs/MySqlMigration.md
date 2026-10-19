# Local MySQL migration

Status: preparation in progress. The website and Hub still use Supabase. Creating the local database does not
enable a MySQL application backend, migrate accounts, or copy Storage files.

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

The script rejects an existing output directory, database, or matching account. MySQL provisioning DDL is not atomic:
on failure, inspect `provisioning.json` and retain the encrypted settings. Do not delete the journal or blindly retry.
Provisioning does not modify other schemas, reset existing passwords, or restart MySQL.

## Migration acceptance gates

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

No MySQL cutover or full migration acceptance has occurred. The pending Windows release must not be reported as
published while the website dependency and migration gates remain incomplete.
