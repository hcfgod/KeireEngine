# Diagnostic Bundles

Kéire diagnostic bundles are local ZIP archives intended for a user to inspect before manually sharing them with
support. The Editor exposes the workflow through **Help > Collect Diagnostics…**. The Hub exposes the same collection
workflow from its Help surface. Kéire never uploads a bundle automatically.

## Collection Contract

Collection is allowlist based. Providers submit individual text records and explicit log filenames to the shared
builder; the builder never recursively scans a project, home directory, package cache, or operating-system crash
directory. Supported records include:

- Kéire version, Git/build identity, platform, architecture, compiler, and configuration;
- operating-system, CPU, memory, cached GPU/backend/capability information when a provider is available;
- bounded renderer statistics and recent handled failures;
- installed package identifiers and versions, without package source URLs or package contents;
- allowlisted project settings that do not disclose the project name, root, asset names, or document contents;
- recognized, structured Kéire last-failure data without exception message text or native minidumps; and
- explicitly named log tails, capped at 2 MiB per file and 8 MiB for all logs.

System, renderer, and failure summaries are always included when available. The preview lets the user exclude logs,
project metadata, package versions, and crash information. Missing optional files are recorded as omissions, while an
unavailable hardware or renderer provider is represented explicitly without causing unrelated diagnostics to fail.
Hardware records label availability for the operating-system identity, CPU identity, logical processor count, and
physical memory. Renderer records label both provider and adapter-identity availability, so an empty value is never
presented as a successfully collected identity.

## Privacy Boundary

Every text entry passes through the sanitizer before the preview is created. It removes credential-bearing key/value
pairs (including quoted JSON fields), authorization tokens, cookies, URLs, private keys, email addresses, absolute
paths, private project/workspace/environment labels, control characters, and credential-like high-entropy text. Pure
hexadecimal build commits and content hashes remain available for identifying the build and verifying the preview.

The archive does not contain project names or paths, assets, documents, source URLs, private package contents,
credentials, entitlements, process environment data, unrelated personal files, or native crash dumps. The collector
rejects symbolic links and Windows reparse points in log roots, log paths, and the output directory.

## Frozen Preview and Publication

Sanitized entry bytes are frozen in memory first. The preview lists every frozen archive filename, exact byte size,
SHA-256 digest, section, and redaction count, plus every omission. The ZIP is then generated from those same bytes with
deterministic entry ordering and timestamps. Saving writes the already-frozen ZIP through an anchored same-directory
temporary file and atomically publishes it; cancellation or failure leaves no partial destination.
Cancelling the native save dialog does not invoke publication and leaves the frozen preview available for another save
attempt or an orderly, repeatable shutdown.

`manifest.json` records the bundle schema, local-only/no-upload policy, no-native-dump policy, included entry metadata,
and omissions. It intentionally does not contain a self-referential digest; the UI preview supplies the manifest's own
size and digest.

## Limits and Failure Behavior

- Maximum text provider entry: 16 MiB.
- Maximum log tail: 2 MiB per file.
- Maximum retained log data: 8 MiB total.
- Maximum complete ZIP: 32 MiB.
- ZIP entry names must be confined portable relative paths; Windows device names, trailing dots/spaces, duplicates,
  and `manifest.json` overrides are rejected.

All validation and sanitization completes before publication. An unsafe path, duplicate inventory name, size overflow,
or cancellation aborts the candidate bundle without modifying an existing destination.
