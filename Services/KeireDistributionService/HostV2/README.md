# Kéire Distribution Service

`KeireDistributionService` is the stateless, read-only origin for Kéire Hub catalogs, learning/resource catalogs, and
content-addressed Editor packages. It targets the repository's pinned .NET 10 toolchain, binds to `127.0.0.1:5088` by
default, and runs behind Caddy on public HTTPS port 443. The unified Astro application serves marketing, documentation,
accounts, marketplace, publisher, administration, and `/marketplace/v1` routes from `127.0.0.1:4321`. Caddy reserves
`/v1`, `/v2`, `/health/live`, and `/health/ready` for the unchanged distribution origin and routes every other product
surface to Astro.

The service has no upload, publishing, account, entitlement, administration, or directory-listing API. Publishing is
an offline filesystem operation performed by `KeireDistributionPublisher`; the online process never receives a
private signing key.

## HTTP contract

| Route | Behavior |
|---|---|
| `GET/HEAD /v1/catalog/{channel}/{platform}/{architecture}` | Streams the complete schema-1 signed catalog for previously released Hubs |
| `GET/HEAD /v2/catalog/{channel}/{platform}/{architecture}` | Streams the compact schema-2 signed catalog used by current Hubs and the website |
| `GET/HEAD /v1/content/{locale}` | Streams the exact signed Learn/Resources catalog bytes |
| `GET/HEAD /v1/manifests/{sha256}` | Streams an immutable package manifest whose digest is bound by the signed catalog |
| `GET/HEAD /v1/packages/{sha256}` | Streams an immutable SHA-256-addressed package; supports one RFC 7233 byte range |
| `GET /health/live` | Process liveness; does not imply that content is ready |
| `GET /health/ready` | `200` when a validated current or last-known-good snapshot is available; otherwise `503` |

Catalog and content responses carry `ETag`, `X-Keire-Signature-Algorithm`, `X-Keire-Signature-Key-Id`,
`X-Keire-Signature`, `X-Keire-Sequence`, and `X-Keire-Expires`. `If-None-Match` returns `304`. Package responses add
`Accept-Ranges: bytes`; valid single ranges return `206`, unsatisfiable or multiple ranges return `416`, and a changed
`If-Range` validator restarts a complete `200` response. Package manifests and packages use a one-year immutable cache
policy; moving catalog routes revalidate after 60 seconds.

The detached Ed25519 signature covers the exact raw catalog/content file bytes. Each signed JSON document must include
top-level `schemaVersion`, `keyId`, `sequence`, and UTC `expiresAt` fields that exactly match its detached metadata. The
offline publisher signs and cryptographically verifies this binding before an immutable snapshot can be published. The
online service performs structural validation and serves the already verified bytes; it never receives a private key.
Package catalogs may additionally declare `minimumSupportedHubVersion` as a semantic version. Because that policy is
inside the signed bytes, changing it requires a new catalog sequence and signature.

Schema-2 package catalogs on `/v2/catalog` contain the package identity, dependency, compatibility, size, and artifact fields needed
for discovery plus a `manifest` byte length and SHA-256. The complete file inventory is stored separately at
`/v1/manifests/{sha256}`. The signed catalog therefore cryptographically binds the exact immutable manifest without
transferring thousands of file records during ordinary discovery. The Hub fetches and hashes that manifest only when
an install or repair is queued, then requires every catalog summary field to match before downloading the package.
The existing `/v1/catalog` route continues serving complete schema-1 inline catalogs so updating the release service
does not disable discovery in an already-downloaded Hub. Both representations are separately signed with the same
release identity and sequence.

The public Downloads page fetches the stable Windows, macOS, and Linux x86_64/ARM64 catalog matrix. It validates catalog
identity and exact `hubInstaller` records before exposing `/v1/packages/{sha256}`. This browser validation improves
failure handling but is not a signature-verification trust boundary; the Hub and offline release workflow remain the
authorities. Missing or malformed native artifacts stay visibly unavailable.

Trusted public keys use schema 1 JSON. `keyId` is derived, not chosen: `ed25519-` followed by the first 16 bytes of the
SHA-256 digest of the raw 32-byte public key in lowercase hexadecimal. The document also carries the complete
`sha256:<hex>` fingerprint and canonical base64 raw public key. The Hub can pin this small public document or generate
equivalent distribution configuration from it.

## Distribution storage contract

```text
distribution-root/
|-- current
`-- snapshots/
    |-- 2026.08.0/
    |   |-- snapshot.json
    |   |-- catalogs/stable/windows/x86_64.json
    |   |-- catalogs-v2/stable/windows/x86_64.json
    |   |-- content/en-US.json
    |   |-- manifests/<64-character-lowercase-sha256>.json
    |   `-- packages/<64-character-lowercase-sha256>
    `-- 2026.08.1/...
```

`current` contains one safe snapshot ID and a newline. `snapshot.json` is generated by the publisher and declares every
served file, type, byte length, SHA-256, and detached signature metadata. Snapshot directories are immutable. The
service rejects path traversal, absolute paths, symbolic links/reparse points, undeclared files, case-colliding paths,
digest or size mismatches, unsupported path layouts, malformed JSON, and invalid signed-document metadata. It validates
a candidate fully before swapping its in-memory index. If a newly selected snapshot is invalid, requests continue using
the last validated snapshot and readiness reports `ready-degraded`. Every request also verifies the indexed size,
timestamp, and link status before emitting a digest-derived validator. The active snapshot is cryptographically
revalidated every `SnapshotIntegrityPollSeconds` (300 seconds by default); a failure withdraws that snapshot and makes
readiness fail until a different immutable snapshot ID is activated.

Publisher input uses the same `catalogs/`, `catalogs-v2/`, `content/`, `manifests/`, and `packages/` layout plus an offline-generated
`signatures.json`:

```json
{
  "schemaVersion": 1,
  "documents": [
    {
      "path": "catalogs/stable/windows/x86_64.json",
      "algorithm": "Ed25519",
      "keyId": "ed25519-0123456789abcdef0123456789abcdef",
      "signature": "<base64-encoded-64-byte-offline-signature>",
      "sequence": 42,
      "expiresAt": "<future-UTC-expiry>"
    }
  ]
}
```

Every catalog/content file needs exactly one signature entry. Package manifests and packages must be named by their
computed SHA-256 and do not appear in `signatures.json`; their exact identities are already bound by the signed catalog.
The publisher preserves every document, manifest, and package byte without normalization.

## Offline signing workflow

For an editor release, first create a schema-2 host package and convert it into the generic package format. The
signature key ID is public metadata from the trusted release-key document; the private key is not used by either of
these steps.

```powershell
$futureUtcExpiry = (Get-Date).ToUniversalTime().AddDays(30).ToString("o")
./Scripts/project.ps1 package-editor -Generator ninja -Toolset msc
./Scripts/project.ps1 build -Generator ninja -Configuration Release -Toolset msc `
    -Target KeireHubPackagePublisher
./Build/Bin/Release-windows-x86_64/KeireHubPackagePublisher/KeireHubPackagePublisher.exe create-editor `
    --payload-root Build/Distributions/keire-editor-windows-x86_64-Dist `
    --output C:/release/editor.keirepackage --manifest-output C:/release/editor.manifest.json `
    --signature-key-id ed25519-0123456789abcdef0123456789abcdef
python Scripts/Packaging/prepare-distribution-snapshot.py `
    --package-manifest C:/release/editor.manifest.json --package C:/release/editor.keirepackage `
    --output C:/release/prepared --key-id ed25519-0123456789abcdef0123456789abcdef `
    --sequence 42 --expires-at $futureUtcExpiry
```

After producing and signing a native Hub installer, create its canonical catalog manifest and prepare it beside one or
more editor packages by repeating the manifest/artifact options:

```powershell
./Build/Bin/Release-windows-x86_64/KeireHubPackagePublisher/KeireHubPackagePublisher.exe create-hub-installer `
    --hub-manifest Build/Distributions/keire-hub-windows-x86_64-Dist/hub-package.json `
    --installer C:/release/KeireHubSetup.exe --manifest-output C:/release/hub.manifest.json `
    --signature-key-id ed25519-0123456789abcdef0123456789abcdef
python Scripts/Packaging/prepare-distribution-snapshot.py `
    --package-manifest C:/release/editor.manifest.json --package C:/release/editor.keirepackage `
    --package-manifest C:/release/hub.manifest.json --package C:/release/KeireHubSetup.exe `
    --output C:/release/prepared --key-id ed25519-0123456789abcdef0123456789abcdef `
    --sequence 42 --expires-at $futureUtcExpiry
```

Both release-package commands reject dirty or development manifests, symlinks/reparse points, existing outputs,
mismatched host extensions, unsafe identities, invalid sizes/digests, and a manifest that does not round-trip through
the catalog parser. Linux Hub manifests accept native DEB and RPM installers. Do not publish an unsigned Windows
executable, an unnotarized macOS DMG, or a Linux package not built and validated on its claimed distribution baseline.

Use a future expiry appropriate to the release rather than copying the example date. The preparer refuses an existing
output, draft manifest fields, unsafe input files, key mismatches, expired metadata, and archive size or SHA-256
mismatches. Adding a newly prepared catalog to an existing release snapshot is an offline composition step; never edit
already signed or published snapshot bytes.

Generate a release key once on an offline machine. Keep the private PEM outside the repository, prepared snapshot, and
distribution root. The command refuses existing outputs and writes a PKCS#8 PEM protected as owner-only on Unix or as
a protected Windows ACL limited to the current user, Local System, and Administrators. Later file-based operations
reject symlinks, inherited Windows ACLs, access granted to other identities, and Unix group/other access.

```powershell
& $dotnet run --project Services\KeireDistributionService\Source\KeireDistributionPublisher -c Release -- `
    generate-key --private-key C:\offline-keys\release.pem `
    --public-key C:\release\trusted-release-key.json
```

Each exact catalog/content document must name the generated key ID and contain a positive, monotonically managed
`sequence` plus a UTC `expiresAt`. Sign only after all document bytes are final, then verify before transfer:

```powershell
& $dotnet run --project Services\KeireDistributionService\Source\KeireDistributionPublisher -c Release -- `
    sign --source C:\release\prepared --output C:\release\prepared\signatures.json `
    --private-key C:\offline-keys\release.pem --minimum-sequence 42
& $dotnet run --project Services\KeireDistributionService\Source\KeireDistributionPublisher -c Release -- `
    verify --source C:\release\prepared --signatures C:\release\prepared\signatures.json `
    --public-key C:\release\trusted-release-key.json --minimum-sequence 42
```

`--minimum-sequence` defaults to 1 and prevents signing or publishing an older sequence. An expired document is always
rejected; `--minimum-validity-hours` defaults to 24 and rejects documents too close to expiry. The same policy options
are available on `sign`, `verify`, and `publish`.

Secret runners may supply canonical base64 PKCS#8 DER through a deliberately named process environment variable:
`--private-key-env KEIRE_OFFLINE_RELEASE_KEY`. The secret is never accepted as a command-line value, never logged, and
the publisher clears its own process copy immediately after reading it. A parent shell or secret runner remains
responsible for clearing its own environment. `derive-public` can recover the trusted public-key JSON from either input
form without signing content.

## Build, test, and run

Use a .NET 10 SDK. The repository's packaged editor contains the pinned SDK, or set `KEIRE_DOTNET`/`-Dotnet` to another
approved .NET 10 executable.

```powershell
$dotnet = 'C:\path\to\dotnet.exe'
& $dotnet build Services\KeireDistributionService\tests\KeireDistributionService.Tests\KeireDistributionService.Tests.csproj -c Release
& $dotnet run --project Services\KeireDistributionService\tests\KeireDistributionService.Tests -c Release
& $dotnet run --project Services\KeireDistributionService\Source\KeireDistributionService -c Release -- `
    --Distribution:StorageRoot=C:\srv\keire-distribution
```

```bash
dotnet build Services/KeireDistributionService/tests/KeireDistributionService.Tests/KeireDistributionService.Tests.csproj -c Release
dotnet run --project Services/KeireDistributionService/tests/KeireDistributionService.Tests -c Release
dotnet run --project Services/KeireDistributionService/Source/KeireDistributionService -c Release -- \
  --Distribution:StorageRoot=/srv/keire-distribution
```

Publish and atomically activate a prepared snapshot:

Run these commands from the service source directory or the root of an extracted self-contained package. Packaged
wrappers invoke the bundled publisher directly; source-tree wrappers use the pinned .NET project.

```powershell
./scripts/publish-snapshot.ps1 `
    -Source C:\release\prepared -DistributionRoot C:\srv\keire-distribution `
    -SnapshotId 2026.08.0 -PublicKey C:\release\trusted-release-key.json -Activate
```

```bash
KEIRE_DOTNET=dotnet ./scripts/publish-snapshot.sh \
  /release/prepared /srv/keire-distribution 2026.08.0 /release/trusted-release-key.json --activate
```

`package-service.ps1` produces self-contained `win-x64` and `linux-x64` service/publisher packages by default. The shell
variant defaults to the current Linux architecture. Both refuse to overwrite an existing package directory and include
the publisher dependency licenses and notices, package-local publish and health-check wrappers, the Caddy and production
settings examples, the locked Astro server/client bundle under `Web/`, target-host runtime installers, and separate
distribution and web systemd units. Packaging requires Node.js 22.12 or newer and npm 10.8.2 or newer; the PowerShell
packager also requires Python 3 when producing a Linux target. Linux archives created on Windows use deterministic tar
metadata: directories, service/publisher entrypoints, and shell wrappers use mode `0755`, while every other regular
file uses mode `0644`.

### Documentation site

`DocumentationSite/` is the source for the complete website, including the public `/docs/` experience. Astro uses
static-first rendering for marketing, policy, release, and documentation content and on-demand server rendering for
account, marketplace, OAuth consent, publisher, moderation, and versioned API routes. Its source audit requires the navigation
inventory to cover every `Docs/**/*.md` file exactly once, maps every guide to a current implementation/configuration
authority, resolves local files and heading fragments, and checks key content-schema statements against code. The sync
step adds website metadata without modifying the canonical files, rewrites repository-document links to stable native
routes, and converts GitHub-compatible Mermaid fences to responsive accessible SVG at build time. Starlight then
produces static pages, a Pagefind search index, a sitemap, and a branded docs 404. The finalizer externalizes
Starlight's generated inline code and style values so the deployed site preserves the self-hosted CSP. Generated
content, dependencies, and `dist/` output are ignored.

```powershell
cd DocumentationSite
npm ci
npm test
npm run build
```

The service packagers must run the locked restore and production build once and deploy the standalone Node entrypoint
from `dist/server/entry.mjs` with `dist/client/` beside it. Do not hand-edit generated pages or commit `node_modules`,
`.astro`, `dist`, or synchronized content.
The deployed Pagefind WebAssembly requires the narrowly scoped CSP token `'wasm-unsafe-eval'` plus the self/blob worker
policy; external scripts, external fonts, and inline script/style execution remain disallowed.

## Deployment

1. Create separate unprivileged `keire-distribution` and `keire-web` service accounts. Distribution needs read-only
   access to its immutable root; the web process receives only publishable Supabase configuration and narrowly scoped
   runtime secrets through an external environment file.
2. Place the self-contained package under `/opt/keire-distribution`, install Node.js 22.12 or newer, and run
   `scripts/install-web-runtime.sh`. The installer restores the exact production dependency lock for the target host,
   disables dependency lifecycle scripts, and validates the packaged Node entry point.
3. Copy `appsettings.Production.example.json` to `appsettings.Production.json`, set an absolute storage root, and install
   the example distribution and web systemd units. Keep SMTP, GitHub OAuth, service-role, validator, and signing secrets
   outside the application tree.
4. Keep Kestrel and Node on loopback. Configure Caddy with `Deployment/Caddyfile.example`, set the real DNS name, and expose only
   Caddy on port 443. Do not expose Kestrel directly or add certificate-bypass behavior.
   Routers may translate public ports 80/443 to different internal ports by setting `KEIRE_CADDY_HTTP_PORT` and
   `KEIRE_CADDY_HTTPS_PORT` for Caddy; clients still use ordinary public HTTPS on port 443. Development-preview
   installers are intentionally not
   repository or service-package payloads. Stage them beneath a separate read-only `PreviewDownloads/` directory, or
   set `KEIRE_PREVIEW_DOWNLOAD_ROOT`, and keep their exact size and SHA-256 synchronized with
   `Website/assets/preview-downloads.json`. Use a digest-suffixed filename for every rebuild so immutable browser and
   proxy caches cannot alias different bytes. Publish a schema-2 record containing a unique release ID, Hub version,
   editor version, UTC publication time, platform, architecture, native `packageFormat`, exact size, and digest. Retain
   one verified preview per Hub version, platform, architecture, and package format. A complete preview withdrawal uses
   an empty package collection plus a validated `releaseStatus` containing the pending semantic version and public
   message. Deploy that metadata and the matching status UI before moving the retired bytes from the served preview
   root into private recovery storage. For an ordinary replacement, remove the superseded preview record and artifact
   together so the manifest never advertises a missing or ambiguous file. `/downloads/previous/` renders
   this bounded retained set and hides any record whose file is missing or has the wrong size. Signed stable releases
   remain immutable and are never pruned through the preview-retention workflow. Preview builds never belong in a
   signed stable catalog.
5. Confirm `/`, `/marketplace/`, `/docs/`, a deep guide route, `/docs/pagefind/pagefind.js`, `/health/`, and a missing
   route before running
   `scripts/health-check.sh https://distribution.example` or the PowerShell equivalent after deployment.

The public Contact form submits directly to the project-owned Supabase `website-contact` Edge Function. Its database
migrations and function source live under `supabase/`. Deploy those before enabling the CSP origin in Caddy. The
function is public by design for a website form, but accepts only exact production/local origins, bounds JSON input,
uses a honeypot and keyed-IP-hash throttle, and writes through server-only credentials into tables unavailable to
browser roles. It streams at most 16 KiB before strict UTF-8/object JSON parsing and ignores caller-controlled
`Forwarded`/`X-Forwarded-For` values; the platform-provided `CF-Connecting-IP` value is the only client address used for
the rate key. A dedicated `CONTACT_RATE_LIMIT_SECRET` is required; the function fails closed when it is absent so
rate-limit pseudonyms can never reuse the database credential. Deploy and validate with the committed dependency graph:

```sh
supabase secrets set CONTACT_RATE_LIMIT_SECRET='<random deployment secret>'
cd supabase/functions/website-contact
deno check --frozen --lock=deno.lock index.ts
```

On Windows, an extracted self-contained package can be supervised at user sign-in without an administrator-owned
service. Copy Caddy beside the service, copy `Deployment/Caddyfile.example` to `Caddyfile`, and create
`host-settings.json` beside `scripts/start-windows-host.ps1`:

```json
{
  "schemaVersion": 2,
  "host": "distribution.example.org",
  "storageRoot": "C:\\srv\\keire-distribution",
  "httpPort": 80,
  "httpsPort": 443,
  "serviceExecutable": "..\\KeireDistributionService.exe",
  "caddyExecutable": "..\\caddy.exe",
  "caddyConfig": "..\\Caddyfile",
  "logDirectory": "..\\Logs",
  "webRoot": "..\\Web",
  "nodeExecutable": "C:\\Program Files\\nodejs\\node.exe"
}
```

Run `./scripts/install-web-runtime.ps1`, then validate the host with
`./scripts/start-windows-host.ps1 -ValidateOnly`. The supervisor starts Kestrel, the loopback Astro Node service, and
Caddy in dependency order and checks each service independently. For an unattended host, open an elevated PowerShell
session and install the repository-owned startup task:

```powershell
./scripts/install-windows-startup-task.ps1 -SettingsPath ./scripts/host-settings.json
```

To move an already-running interactive-user deployment into a protected machine location, use the transactional
migration helper from an elevated PowerShell session. It accepts legacy static schema 1 hosts and unified web schema 2
hosts, copies only the active host payload and immutable distribution
root, validates the staged copy, replaces the existing task with the Local System startup task, verifies public
readiness, and restores the previous task definition if activation fails:

```powershell
./scripts/migrate-windows-host.ps1 `
    -SourceHostRoot 'C:\Users\operator\AppData\Local\Programs\Keire Distribution Host' `
    -SourceDistributionRoot 'C:\Users\operator\AppData\Local\Keire Hub\Distribution\ServiceRoot' `
    -DestinationRoot 'D:\ProgramData\Keire Distribution Host'
```

If activation fails after the protected destination has been created, correct the reported cause and rerun the same
command with `-Resume`. Resume mode repairs and reapplies the protected ACL, revalidates the existing staged payload,
and retries only activation; it never merges new source content into that destination.

The task runs the supervisor as Local System 30 seconds after boot, before interactive sign-in, restarts it after
failures, and can be removed with `-Uninstall`. Keep the package, settings, Caddy data, logs, website, preview downloads,
and distribution root in administrator-protected machine paths that Local System can access; do not install this task
against a disposable user-profile extraction. The supervisor is single-instance, launches both processes without
visible windows, waits for their readiness endpoints, and restarts a process whose listening port disappears.

### WSL2 access to a Windows-loopback development host

Windows 10 WSL2 uses a separate network namespace. If the distribution hostname is mapped to `127.0.0.1` in the
Windows hosts file while Caddy uses a router-facing high port, Ubuntu inherits the loopback DNS answer but cannot
reach the Windows loopback port proxy. Keep the Hub's production HTTPS URL and certificate validation unchanged. In
WSL2, install `socat`, then install the repository-owned systemd bridge using the Caddy `httpsPort` from
`host-settings.json`:

```sh
sudo apt-get install socat
sudo ./scripts/install-wsl2-host-bridge.sh \
  --host keireengine.duckdns.org --upstream-port 50255
curl --fail https://keireengine.duckdns.org/health/ready
```

The service resolves the current WSL2 Windows-host gateway on every start, verifies the upstream HTTPS readiness
endpoint with the real hostname, binds only WSL's `127.0.0.1:443`, and then forwards TLS bytes without terminating or
bypassing certificate validation. Remove it with `sudo ./scripts/install-wsl2-host-bridge.sh --uninstall`. This bridge
is only for a Hub running inside WSL2 on the same Windows machine as the self-hosted origin; normal Linux users connect
to public HTTPS directly and must not install it.

Run the availability monitor on a separate network and machine so it covers power, router, DNS, TLS, and origin
failures rather than only process health. Both variants notify a generic JSON webhook only when availability changes:

```powershell
./scripts/monitor-distribution.ps1 -BaseUrl https://distribution.example `
    -NotificationWebhook $env:KEIRE_UPTIME_WEBHOOK -Once
```

```sh
./scripts/monitor-distribution.sh https://distribution.example /var/lib/keire-monitor/state \
  "$KEIRE_UPTIME_WEBHOOK" --once
```

Schedule the one-shot command every minute on the external monitor. A continuous mode is also available by omitting
`-Once`/`--once`. Alert delivery itself must be tested quarterly by stopping the origin or monitoring a deliberate
failure URL and confirming both the down and recovery notifications.

A hosted uptime service also satisfies the separate-network requirement; a second computer is not required. Configure
an HTTPS monitor for the exact `/health/ready` URL, enable email plus mobile push notifications, and retain the same
quarterly down/recovery drill. A monitor running only on the origin does not cover power, router, DNS, or TLS failures.

The metadata rate limiter is fixed-window and deliberately conservative; package streams use a bounded global
concurrency limiter and queue. Kestrel request headers and timeouts are bounded. Application logs use the JSON console
formatter, and the Caddy example writes structured access logs with rotation.

## Backup, restore, and migration

Snapshots are immutable, so a backup copies `snapshots/` first and `current` last. The backup destination must be an
off-machine UNC path, network mount, or independently replicated volume. The scripts validate both the source and the
completed immutable backup and refuse nested source/destination paths:

```powershell
./scripts/backup-distribution.ps1 -DistributionRoot C:\srv\keire-distribution `
    -DestinationRoot \\backup-host\keire-distribution
./scripts/restore-distribution.ps1 -BackupRoot \\backup-host\keire-distribution\<backup-id> -ValidateOnly
```

```sh
./scripts/backup-distribution.sh /srv/keire-distribution /mnt/off-machine/keire-distribution
./scripts/restore-distribution.sh /mnt/off-machine/keire-distribution/<backup-id> --validate-only
```

Google Drive is a suitable off-machine destination when it is accessed by a dedicated backup tool rather than a
login-dependent desktop sync client. Create a dedicated Google OAuth **Desktop app**, leave its publishing status **In
production** so personal-use refresh tokens do not expire after seven days, and configure the `rclone` remote with the
least-privilege `drive.file` scope. Use an application-owned client ID and secret; rclone's shared Google Drive client
is being retired during 2026. Keep the OAuth configuration below the protected host root so only Local System and
administrators can read its refresh token.

The remote workflow does not upload another complete backup tree each day. Immutable snapshots are copied once to
`snapshots/<snapshot-id>`, each successful run adds a small immutable record under `records/<backup-id>`, and `latest`
is updated only after checksum verification. Runs never use `sync`, delete a remote object, or overwrite a changed
snapshot. This layout preserves recovery history without consuming another complete snapshot's storage on every
schedule:

```powershell
./scripts/install-windows-backup-task.ps1 `
    -HostRoot 'D:\ProgramData\Keire Distribution Host' `
    -RemoteRoot 'keire-drive:KeireEngine/DistributionBackups' `
    -StartNow
```

The installer validates the protected config and named remote, installs the runtime scripts below the host root, and
creates `Keire Distribution Backup` as Local System. It runs daily at 03:15 local time, starts when a missed run becomes
possible, requires a network connection, rejects concurrent runs, and can start the first backup immediately. Human
and machine-readable results are written to `Logs/distribution-backup.log` and
`Logs/distribution-backup-status.json`. Run the backup script directly after an important snapshot activation when a
24-hour recovery-point window is too large.

Linux uses the same remote format and checksum/immutability rules. Protect the config for the service identity and run
the following from a systemd timer or equivalent distro-native scheduler:

```sh
./scripts/backup-distribution-rclone.sh /srv/keire-distribution \
  keire-drive:KeireEngine/DistributionBackups /etc/keire-distribution/rclone.conf
```

Treat the Drive backup as operational only after restoring it into a new empty directory and validating the downloaded
snapshot. On Windows, run this from an elevated PowerShell session after the initial scheduled task succeeds:

```powershell
./scripts/restore-distribution-rclone.ps1 `
    -DestinationRoot 'D:\ProgramData\Keire Distribution Host\RestoreDrills\initial' `
    -RclonePath 'D:\ProgramData\Keire Distribution Host\tools\rclone\rclone.exe' `
    -RcloneConfigPath 'D:\ProgramData\Keire Distribution Host\Secrets\rclone.conf' `
    -RemoteRoot 'keire-drive:KeireEngine/DistributionBackups' `
    -PublisherPath 'D:\ProgramData\Keire Distribution Host\tools\publisher\KeireDistributionPublisher.exe'
```

The Linux equivalent is:

```sh
./scripts/restore-distribution-rclone.sh /var/tmp/keire-restore-drill \
  keire-drive:KeireEngine/DistributionBackups /etc/keire-distribution/rclone.conf
```

Recorded restore drills:

| UTC date | Backup | Recovery time | Verification |
| --- | --- | ---: | --- |
| 2026-08-09 | `backup-20260809T130616Z-d4c9ee59eb484dcd-ae175c6a` | 33.253 seconds | rclone reported 9 matching files and 0 differences; the packaged publisher validated `local-20260809-sequence-3`; an independent SHA-256 comparison matched all 9 files (564,953,618 bytes); an isolated service using the restored root became ready in 0.990 seconds; the public origin remained ready. |

For the quarterly restore drill, restore into a new empty root, validate it, launch a second service instance against
that root, and run `health-check` before recording the printed recovery time. The campaign-readiness objective is a
verified restore in under 60 minutes (RTO) with no loss of any activated snapshot (RPO bounded by the backup schedule).
Run backups after every snapshot activation and at least daily while the service is active. Keep at least the current
and previous known-good snapshots until all supported Hubs have advanced; do not edit `snapshot.json` or signed
documents in place. Monitor Drive capacity before publishing large releases. Remote cleanup is deliberately manual so
a compromised origin or faulty schedule cannot propagate deletions into the recovery copy.

The layout is intentionally object-storage friendly: each package key is immutable and content-addressed, each snapshot
manifest is immutable, and `current` is the only moving pointer. A future CDN migration can upload snapshot objects
unchanged, validate them, and update a strongly consistent pointer/alias last. Preserve exact bytes, ETags, signature
headers, range semantics, and rollback to the previous pointer on validation failure.
