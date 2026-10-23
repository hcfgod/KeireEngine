# Re-protect local platform settings for the SYSTEM service identity

These helpers prepare credentials for the existing SYSTEM-hosted service without changing the live host, source settings, passwords, or signing keys. They require PowerShell 7 and an elevated console belonging to the same Windows user who originally protected the settings. They do not elevate themselves.

**Do not run these commands until migration execution is approved.** Current validation covers parser and planning tests; the actual SYSTEM task and DPAPI handoff still require an isolated staging acceptance run.

## 1. Prepare a public path-only plan

The plan contains no passwords, keys, or token values. Select existing settings groups. Missing GitHub/worker settings must be provisioned separately; omitted groups are not silently invented. Selecting `mysql` includes both its database settings and account encryption key.

```json
{
  "schemaVersion": 1,
  "sources": [
    { "kind": "mysql", "sourceDirectory": "C:/Users/keith/AppData/Local/Keire/PlatformMySql" },
    { "kind": "mail", "sourceDirectory": "C:/Users/keith/AppData/Local/Keire/PlatformMail" },
    { "kind": "native", "sourceDirectory": "C:/Users/keith/AppData/Local/Keire/PlatformNativeIdentity" },
    { "kind": "downloads", "sourceDirectory": "C:/Users/keith/AppData/Local/Keire/PlatformDownloads" }
  ]
}
```

Also supported: `github`, `workers`. Use their actual source directories. The original ciphertext files are read-only inputs.

## 2. Prepare the encrypted bridge

Run the coordinator explicitly from an elevated PowerShell 7 console as the original provisioning user. Both bridge and destination paths must be new, disjoint local directories. Never point either at an existing install or project.

```powershell
./migrate-platform-service-identity.ps1 -Mode Prepare `
    -PlanPath 'D:/ProgramData/KeireIdentityPlan.json' `
    -BridgeDirectory 'D:/ProgramData/KeireIdentityBridge-NEW' `
    -DestinationRoot 'D:/ProgramData/KeirePlatformSecrets-NEW'
```

Preparation decrypts each original CurrentUser DPAPI file only in memory, then re-encrypts it with LocalMachine DPAPI and migration-specific entropy. The bridge has inheritance disabled and grants access only to SYSTEM and Administrators. It contains encrypted payloads, a credential-free manifest and hash-checked copies of the receiver and its module. No plaintext credential files are written. The destination is not created yet.

Partial output is retained if preparation fails. Inspect it and use a new directory for a retry; the helper never guesses whether old output can be removed.

## 3. Execute the explicit SYSTEM handoff

```powershell
./migrate-platform-service-identity.ps1 -Mode Execute `
    -BridgeDirectory 'D:/ProgramData/KeireIdentityBridge-NEW' `
    -PowerShellExecutable 'C:/Program Files/PowerShell/7/pwsh.exe'
```

The coordinator verifies the same source SID, source ciphertext hashes, bridge ACLs and helper integrity. It creates one uniquely named temporary SYSTEM task, waits up to two minutes, and unregisters only that task. The protected receiver decrypts the bridge in memory, writes **new** SYSTEM CurrentUser DPAPI files using the existing per-service entropy, verifies each roundtrip, and records only output ciphertext hashes. It does not change service startup configuration or start the platform.

The receiver refuses non-SYSTEM execution and existing destinations. Failed/partial output is preserved, never overwritten. No unrelated scheduled task or process is touched.

## 4. Verify and stage

```powershell
./migrate-platform-service-identity.ps1 -Mode Verify `
    -BridgeDirectory 'D:/ProgramData/KeireIdentityBridge-NEW'
```

Verification checks the SYSTEM receipt, private ACLs, original input hashes and output ciphertext hashes. A successful receipt proves preservation and SYSTEM decryptability; it does **not** replace application acceptance.

Point an isolated staging configuration at the new settings directories and run it as SYSTEM. Confirm MySQL TLS CA readability, signing-key IDs, mail delivery, GitHub settings, private storage ACLs and all required services before changing the live host. Keep the original settings and bridge for rollback until acceptance passes. Bridge cleanup is a separate, explicit administrator action after verifying the exact paths; no automatic cleanup is provided.

Regression command (no elevation, tasks, DPAPI migration or live changes):

```powershell
./test-platform-service-identity.ps1
```


## Explicit synthetic SYSTEM acceptance (not run by the ordinary regression command)

After review, launch this from an elevated PowerShell 7 console as the original provisioning user:

```powershell
$fixture = Join-Path $env:TEMP ('keire-identity-system-qa-' + [Guid]::NewGuid().ToString('N'))
./test-platform-service-identity-system.ps1 -FixtureRoot $fixture
```

This runner creates seven **synthetic** CurrentUser DPAPI files in a new private fixture, then exercises Prepare/Execute/Verify through the actual temporary SYSTEM task. It verifies unchanged source ciphertext, the SYSTEM receipt, failure of original-user decryption of SYSTEM output, task removal and refusal to overwrite. It never reads production settings or changes live configuration. It retains the encrypted fixture and a sanitized `result.json` on success or failure. Its explicit execution creates a temporary scheduled task; parser/planning tests do not.

## Read-only application acceptance after transfer

Run `DocumentationSite/scripts/platform-production-system-acceptance.mjs` as SYSTEM with two absolute arguments:
a configuration JSON path and a new result JSON path. Configuration fields are `schemaVersion: 1`, `siteOrigin`,
`expectedNativeKeyId`, `powerShellExecutable`, `scriptsDirectory`, `settingsRoot`, and `storageRoot`.
The paths point to the installed PowerShell, protected-settings reader scripts, transferred credential root,
and private storage root containing `Spool`, `Packages`, and `Evidence`.

The runner verifies the actual service identity, storage permissions, MySQL TLS and read-only queries, native public
key identity, local download signing, worker credential loading, and SMTP authentication. It sends no email, writes
no database records, and starts no website. Results contain only check counts, the public key ID, or a failing stage.
A passed result does not replace real email delivery, GitHub consent, worker execution, or end-to-end Hub acceptance.
