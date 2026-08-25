#!/usr/bin/env python3
"""Static trust-boundary checks for marketplace Edge Functions and website adapters."""

from __future__ import annotations

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
SHARED = ROOT / "supabase/functions/_shared/marketplace.ts"
LIBRARY = ROOT / "supabase/functions/marketplace-library/index.ts"
HUB = ROOT / "supabase/functions/marketplace-hub/index.ts"
PUBLISHER = ROOT / "supabase/functions/marketplace-publisher/index.ts"
MODERATION = ROOT / "supabase/functions/marketplace-moderation/index.ts"
VALIDATOR_QUEUE = ROOT / "supabase/functions/marketplace-validator-queue/index.ts"
PUBLICATION = ROOT / "supabase/functions/marketplace-publication/index.ts"
PUBLICATION_QUEUE = ROOT / "supabase/functions/marketplace-publication-queue/index.ts"
WEBSITE_CONTACT = ROOT / "supabase/functions/website-contact/index.ts"
SUPABASE_CONFIG = ROOT / "supabase/config.toml"
MIDDLEWARE = (
    ROOT / "Services/KeireDistributionService/DocumentationSite/Source/middleware.ts"
)
API_ROOT = (
    ROOT
    / "Services/KeireDistributionService/DocumentationSite/Source/pages/marketplace/v1"
)
PUBLISHER_API_ROOT = (
    ROOT
    / "Services/KeireDistributionService/DocumentationSite/Source/pages/publisher/v1"
)
STAFF_API_ROOT = (
    ROOT
    / "Services/KeireDistributionService/DocumentationSite/Source/pages/admin/marketplace/v1"
)
STAFF_PAGE = (
    ROOT
    / "Services/KeireDistributionService/DocumentationSite/Source/pages/admin/marketplace/index.astro"
)
EDGE_FUNCTIONS = (
    "website-contact",
    "marketplace-hub",
    "marketplace-library",
    "marketplace-moderation",
    "marketplace-publication",
    "marketplace-publication-queue",
    "marketplace-publisher",
    "marketplace-validator-queue",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


for edge_function in EDGE_FUNCTIONS:
    function_root = ROOT / "supabase/functions" / edge_function
    require(
        (function_root / "deno.json").is_file(),
        f"{edge_function} has no isolated Deno configuration.",
    )
    lock_path = function_root / "deno.lock"
    require(
        lock_path.is_file(), f"{edge_function} has no committed Deno dependency lock."
    )
    require(
        '"version": "4"' in lock_path.read_text(encoding="utf-8"),
        f"{edge_function} does not use the expected Deno lockfile schema.",
    )


shared = SHARED.read_text(encoding="utf-8")
library = LIBRARY.read_text(encoding="utf-8")
hub = HUB.read_text(encoding="utf-8")
publisher = PUBLISHER.read_text(encoding="utf-8")
moderation = MODERATION.read_text(encoding="utf-8")
validator_queue = VALIDATOR_QUEUE.read_text(encoding="utf-8")
publication = PUBLICATION.read_text(encoding="utf-8")
publication_queue = PUBLICATION_QUEUE.read_text(encoding="utf-8")
website_contact = WEBSITE_CONTACT.read_text(encoding="utf-8")
staff_page = STAFF_PAGE.read_text(encoding="utf-8")
supabase_config = SUPABASE_CONFIG.read_text(encoding="utf-8")
middleware = MIDDLEWARE.read_text(encoding="utf-8")
routes = "\n".join(
    path.read_text(encoding="utf-8")
    for api_root in (API_ROOT, PUBLISHER_API_ROOT, STAFF_API_ROOT)
    for path in api_root.rglob("*.ts")
)

require(
    "[auth.email]" in supabase_config
    and "secure_password_change = true"
    in supabase_config.split("[auth.email]", 1)[1].split("[", 1)[0],
    "Local Supabase Auth must require secure password changes.",
)
require(
    '"X-Client-Info": "keire-marketplace-edge/0.4.2"' in shared,
    "Marketplace Edge requests must identify the current 0.4.2 source client.",
)
require(
    '"X-Client-Info": "keire-validator-queue/0.4.2"' in validator_queue,
    "Marketplace validator queue requests must identify the current 0.4.2 source client.",
)
require(
    '"X-Client-Info": "keire-publication-queue/0.4.2"' in publication_queue,
    "Marketplace publication queue requests must identify the current 0.4.2 source client.",
)
require(
    '"X-Client-Info": "keire-website-contact/1.0"' in website_contact,
    "The website contact function contract version must remain independent from the Kéire product release.",
)
require(
    'new Set(["https://keireengine.duckdns.org"])' in shared,
    "Marketplace browser requests must use an explicit origin allowlist.",
)
require(
    "maximumRequestBytes = 64 * 1024" in shared,
    "Marketplace Edge request bodies must remain bounded.",
)
require(
    "auth.getUser(token)" in shared,
    "Edge Functions must verify the bearer token with Supabase Auth before decoding claims.",
)
require(
    "SUPABASE_SECRET_KEYS" in shared and "SUPABASE_SERVICE_ROLE_KEY" in shared,
    "Edge Functions must use the hosted secret-key contract with a legacy service-role fallback.",
)
require(
    '"marketplace_rate_limited", [429, "marketplace.rate_limited", "Too many marketplace changes were requested.'
    in shared,
    "Rate-limit failures must provide a bounded, actionable publisher message.",
)
require(
    '"marketplace_signing_approval_withdrawal_requires_administrator"' in shared,
    "Signing-approval withdrawal must provide an explicit administrator-only diagnostic.",
)
require(
    'requiredUuid(input, "productId")' in library,
    "Product claims must reject missing product identifiers before reaching Postgres.",
)
require(
    'caller.assuranceLevel !== "aal2"' in library,
    "Organization creation must independently require a verified second factor.",
)
require(
    'requiredUuid(input, "versionId")' in hub
    and 'requiredUuid(input, "deviceSessionId")' in hub,
    "Download grants must reject missing package and device identifiers.",
)
require(
    "caller.sessionId" in hub,
    "Hub transitions must be bound to the verified OAuth session claim.",
)
require(
    'caller.assuranceLevel !== "aal2"' in publisher,
    "Publisher transitions must independently require a verified second factor.",
)
require(
    "service_submit_publisher_application" in publisher,
    "Publisher submission must use its service-only database transition.",
)
for upload_rpc in (
    "service_reserve_marketplace_named_upload",
    "service_complete_marketplace_upload",
    "service_cancel_marketplace_upload",
):
    require(
        upload_rpc in publisher,
        f"Publisher upload boundary does not call {upload_rpc}.",
    )
require(
    ".createSignedUploadUrl(reservation.storage_path, { upsert: false })" in publisher,
    "Publisher uploads must receive only a non-overwriting path-scoped storage grant.",
)
require(
    'bucket: "marketplace-packages"' in publisher
    and "storage.supabase.co/storage/v1/upload/resumable/sign" in publisher,
    "Signed publisher grants must target Supabase Storage's signed TUS endpoint.",
)
require(
    "expectedSha256" in publisher and "expectedSizeBytes" in publisher,
    "Publisher reservations must bind the expected package digest and byte count.",
)
require(
    'optionalUuid(input, "productId")' in publisher
    and 'requiredUuid(input, "publisherId")' in publisher
    and 'requiredUuid(input, "categoryId")' in publisher
    and 'stringField(input, "productName", 2, 128)' in publisher
    and 'stringField(input, "productSummary", 20, 240)' in publisher,
    "Publisher reservations must accept a named, categorized product while preserving existing-product releases.",
)
require(
    "typeof reservation.product_id" in publisher
    and "productId: reservation.product_id" in publisher,
    "Publisher reservations must return the created or selected product identity.",
)
require(
    'operation === "version.submit"' in publisher
    and "service_submit_marketplace_version" in publisher,
    "Validated package submission must use its service-only state transition.",
)
require(
    'functions.invoke("marketplace-publisher"' in routes,
    "The website publisher API must invoke the MFA-protected Edge transition.",
)
require(
    'caller.assuranceLevel !== "aal2"' in moderation
    and "service_get_platform_staff_role" in moderation,
    "Every moderation operation must require MFA and a current database staff role.",
)
for moderation_rpc in (
    "service_decide_publisher_application",
    "service_decide_marketplace_submission",
    "service_decide_marketplace_report",
    "service_set_platform_staff",
    "service_set_platform_feature_flag",
):
    require(
        moderation_rpc in moderation,
        f"Moderation boundary does not call {moderation_rpc}.",
    )
require(
    'functions.invoke("marketplace-moderation"' in routes,
    "The staff website adapter must use the audited moderation Edge boundary.",
)
require(
    'operation === "evidence.issue"' in moderation
    and 'from("marketplace-validation-evidence").createSignedUrl' in moderation
    and "crypto.subtle.verify(" in moderation,
    "Staff review evidence must receive a short-lived URL only after validator-attestation verification.",
)
require(
    "data-evidence-button" in staff_page
    and 'crypto.subtle.digest("SHA-256"' in staff_page,
    "The staff console must verify evidence bytes before rendering the package inventory.",
)
require(
    "data-publication-form" not in staff_page
    and "/admin/marketplace/v1/publications/" not in staff_page,
    "The staff console must not require an administrator to upload a signed publication envelope.",
)
require(
    'functions.invoke("marketplace-publication"' in routes,
    "The staff website adapter must use the signature-verifying publication Edge boundary.",
)
require(
    'caller.assuranceLevel !== "aal2"' in publication
    and 'data !== "administrator"' in publication,
    "Marketplace publication must require current administrator MFA.",
)
require(
    'crypto.subtle.verify("Ed25519"' in publication
    and 'from("marketplace_signature_keys")' in publication,
    "Marketplace publication must verify the offline signature against a database trust root.",
)
require(
    '.copy(upload.storage_path, document.releaseStoragePath, { destinationBucket: "marketplace-releases" })'
    in publication
    and "service_publish_marketplace_version" in publication,
    "Marketplace publication must promote the exact quarantine object before the atomic database commit.",
)
require(
    'from("marketplace-releases").remove([document.releaseStoragePath])' in publication,
    "A failed publication commit must compensate for the promoted release object.",
)
for rpc in (
    "service_create_marketplace_organization",
    "service_claim_free_marketplace_product",
    "service_register_marketplace_device_session",
    "service_issue_marketplace_download_grant_v3",
):
    require(
        rpc in library + hub,
        f"Edge Function does not call the service-only adapter {rpc}.",
    )
for revoked_rpc in (
    '.rpc("create_marketplace_organization"',
    '.rpc("claim_free_marketplace_product"',
    '.rpc("register_marketplace_device_session"',
    '.rpc("issue_marketplace_download_grant"',
):
    require(
        revoked_rpc not in routes,
        f"Website routes must not bypass the Edge boundary through {revoked_rpc}.",
    )
require(
    'normalizedPath === "/marketplace/v1/sessions"' in middleware,
    "The first verified Hub OAuth session must be allowed to reach its registration boundary.",
)
require(
    "throwEdgeFunctionError" in routes,
    "Website adapters must preserve bounded Edge error codes and HTTP status values.",
)
require(
    'Deno.env.get("VALIDATOR_BROKER_SECRET")' in validator_queue,
    "The validator queue must use its dedicated broker credential.",
)
require(
    'request.headers.has("origin")' in validator_queue,
    "The server-only validator queue must reject browser-origin requests.",
)
require(
    'crypto.subtle.digest("SHA-256"' in validator_queue,
    "The validator queue must compare fixed-length credential digests.",
)
require(
    "createSignedUrl(lease.storage_path, leaseSeconds" in validator_queue,
    "The validator broker must receive only a short-lived object-specific URL.",
)
for rpc in (
    "service_lease_marketplace_upload_v2",
    "service_renew_marketplace_upload_lease",
    "service_complete_marketplace_validation_v2",
):
    require(rpc in validator_queue, f"Validator Edge boundary does not call {rpc}.")
require(
    "[functions.marketplace-validator-queue]" in supabase_config
    and "verify_jwt = false"
    in supabase_config.split("[functions.marketplace-validator-queue]", 1)[1],
    "The scoped-secret validator queue must bypass platform JWT parsing.",
)
require(
    'from("marketplace-validation-evidence")' in validator_queue
    and "createSignedUploadUrl(lease.evidence_storage_path" in validator_queue
    and "crypto.subtle.verify(" in validator_queue,
    "The validator queue must bind signed attestations to separately hashed review evidence.",
)
require(
    'Deno.env.get("MARKETPLACE_PUBLICATION_SIGNER_SECRET")' in publication_queue
    and 'request.headers.has("origin")' in publication_queue
    and 'crypto.subtle.digest("SHA-256"' in publication_queue,
    "The automatic signer queue must use a dedicated constant-time server credential boundary.",
)
for rpc in (
    "service_lease_marketplace_publication",
    "service_renew_marketplace_publication_lease",
    "service_publish_marketplace_version_v2",
    "service_fail_marketplace_publication",
):
    require(
        rpc in publication_queue,
        f"Automatic publication Edge boundary does not call {rpc}.",
    )
require(
    "[functions.marketplace-publication-queue]" in supabase_config
    and "verify_jwt = false"
    in supabase_config.split("[functions.marketplace-publication-queue]", 1)[1],
    "The scoped-secret publication queue must bypass platform JWT parsing.",
)
require(
    "[functions.marketplace-publication]" in supabase_config
    and "verify_jwt = true"
    in supabase_config.split("[functions.marketplace-publication]", 1)[1],
    "The administrator publication boundary must require a platform-verified JWT.",
)

print("Marketplace Edge trust-boundary validation passed.")
