# Kéire website

The product website, documentation, marketplace, and account pages share an Astro
Node deployment. Run `npm test` and `npm run build` from this directory. The build
syncs canonical documentation, validates local media, generates search, externalizes
inline content for the production CSP, and checks links and document structure.

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

`platform-validator-report.mjs` verifies the worker's exact Ed25519 signature,
trusted-key validity, package/version identity, evidence metadata, scan outcomes,
and completion timestamp. It rejects duplicate signed fields and contradictory
success reports. Transactional completion uses this verifier before recording
reports and checks the evidence bytes separately. For C# worker compatibility,
set `KEIRE_TEST_NODE_ATTESTATION=1` when running the Release distribution test
harness; Node must be on PATH. That optional check sends only a synthetic signed
report and its public key to Node, never a signing key.

Internal validator completion locks the current lease and trusted key, verifies
the signature and immutable evidence bytes, rechecks expiry, and commits the
report, upload/version states, and audit receipt in one transaction. An exact
retry from the same worker returns its recorded report ID; it cannot change an
accepted report or consume another validation attempt. This is not yet routed
to the live broker: private transfer endpoints and full pipeline acceptance
remain required.

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
