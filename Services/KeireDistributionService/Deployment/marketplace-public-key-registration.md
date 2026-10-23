# Marketplace public-key registration backends

`provision-windows-marketplace-signing-keys.ps1` accepts `-Backend mysql` for the local MySQL platform. Omitting this option preserves the existing Supabase PostgreSQL output. Select the backend explicitly when preparing a MySQL deployment; SQL for one backend must not be applied to the other.

The ceremony creates two independent signing identities and protected private-key files as before. It writes `register-marketplace-public-keys.sql` for manual review and application; it does not connect to a database or execute the registration. MySQL output targets `keire_platform` and supplies the required `valid_from` and `created_at` fields in UTC with microsecond precision. Both backends use ordinary inserts so an existing trusted identity is never silently replaced.

Public documents are checked for canonical 32-byte Ed25519 encoding, matching SHA-256 fingerprints, safe identifiers, and independent identities. Only public keys enter the generated SQL. Release the candidate Hub trust bundle before activating the publication key. Keep private PEM backups offline and protected copies on their intended worker hosts.

The pure SQL formatter is tested independently of the key ceremony:

```powershell
pwsh -NoProfile -File Services/KeireDistributionService/tests/KeireMarketplaceValidator.Tests/TestPublicKeyRegistration.ps1
```

These tests use fixed public byte fixtures, do not generate or protect private keys, and make no database connections. They verify both SQL dialects, required MySQL fields, rejection of invalid documents, and preservation of the Supabase default. Actual registration and subsequent worker acceptance remain separate deployment gates.
