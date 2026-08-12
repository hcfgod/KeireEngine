#!/usr/bin/env python3
"""Static trust-boundary checks for marketplace Edge Functions and website adapters."""

from __future__ import annotations

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
SHARED = ROOT / "supabase/functions/_shared/marketplace.ts"
LIBRARY = ROOT / "supabase/functions/marketplace-library/index.ts"
HUB = ROOT / "supabase/functions/marketplace-hub/index.ts"
PUBLISHER = ROOT / "supabase/functions/marketplace-publisher/index.ts"
VALIDATOR_QUEUE = ROOT / "supabase/functions/marketplace-validator-queue/index.ts"
SUPABASE_CONFIG = ROOT / "supabase/config.toml"
MIDDLEWARE = ROOT / "Services/KeireDistributionService/DocumentationSite/Source/middleware.ts"
API_ROOT = ROOT / "Services/KeireDistributionService/DocumentationSite/Source/pages/marketplace/v1"
PUBLISHER_API_ROOT = ROOT / "Services/KeireDistributionService/DocumentationSite/Source/pages/publisher/v1"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


shared = SHARED.read_text(encoding="utf-8")
library = LIBRARY.read_text(encoding="utf-8")
hub = HUB.read_text(encoding="utf-8")
publisher = PUBLISHER.read_text(encoding="utf-8")
validator_queue = VALIDATOR_QUEUE.read_text(encoding="utf-8")
supabase_config = SUPABASE_CONFIG.read_text(encoding="utf-8")
middleware = MIDDLEWARE.read_text(encoding="utf-8")
routes = "\n".join(
    path.read_text(encoding="utf-8")
    for api_root in (API_ROOT, PUBLISHER_API_ROOT)
    for path in api_root.rglob("*.ts")
)

require('new Set(["https://keireengine.duckdns.org"])' in shared,
        "Marketplace browser requests must use an explicit origin allowlist.")
require("maximumRequestBytes = 64 * 1024" in shared,
        "Marketplace Edge request bodies must remain bounded.")
require("auth.getUser(token)" in shared,
        "Edge Functions must verify the bearer token with Supabase Auth before decoding claims.")
require("SUPABASE_SECRET_KEYS" in shared and "SUPABASE_SERVICE_ROLE_KEY" in shared,
        "Edge Functions must use the hosted secret-key contract with a legacy service-role fallback.")
require('requiredUuid(input, "productId")' in library,
        "Product claims must reject missing product identifiers before reaching Postgres.")
require('caller.assuranceLevel !== "aal2"' in library,
        "Organization creation must independently require a verified second factor.")
require('requiredUuid(input, "versionId")' in hub and
        'requiredUuid(input, "deviceSessionId")' in hub,
        "Download grants must reject missing package and device identifiers.")
require("caller.sessionId" in hub,
        "Hub transitions must be bound to the verified OAuth session claim.")
require('caller.assuranceLevel !== "aal2"' in publisher,
        "Publisher transitions must independently require a verified second factor.")
require("service_submit_publisher_application" in publisher,
        "Publisher submission must use its service-only database transition.")
for upload_rpc in (
    "service_reserve_marketplace_upload",
    "service_complete_marketplace_upload",
    "service_cancel_marketplace_upload",
):
    require(upload_rpc in publisher, f"Publisher upload boundary does not call {upload_rpc}.")
require('.createSignedUploadUrl(reservation.storage_path, { upsert: false })' in publisher,
        "Publisher uploads must receive only a non-overwriting path-scoped storage grant.")
require('bucket: "marketplace-quarantine"' in publisher and
        'storage.supabase.co/storage/v1/upload/resumable' in publisher,
        "Publisher upload reservations must target the private direct-storage TUS endpoint.")
require('expectedSha256' in publisher and 'expectedSizeBytes' in publisher,
        "Publisher reservations must bind the expected package digest and byte count.")
require('functions.invoke("marketplace-publisher"' in routes,
        "The website publisher API must invoke the MFA-protected Edge transition.")
for rpc in (
    "service_create_marketplace_organization",
    "service_claim_free_marketplace_product",
    "service_register_marketplace_device_session",
    "service_issue_marketplace_download_grant",
):
    require(rpc in library + hub, f"Edge Function does not call the service-only adapter {rpc}.")
for revoked_rpc in (
    '.rpc("create_marketplace_organization"',
    '.rpc("claim_free_marketplace_product"',
    '.rpc("register_marketplace_device_session"',
    '.rpc("issue_marketplace_download_grant"',
):
    require(revoked_rpc not in routes,
            f"Website routes must not bypass the Edge boundary through {revoked_rpc}.")
require('normalizedPath === "/marketplace/v1/sessions"' in middleware,
        "The first verified Hub OAuth session must be allowed to reach its registration boundary.")
require("throwEdgeFunctionError" in routes,
        "Website adapters must preserve bounded Edge error codes and HTTP status values.")
require('Deno.env.get("VALIDATOR_BROKER_SECRET")' in validator_queue,
        "The validator queue must use its dedicated broker credential.")
require('request.headers.has("origin")' in validator_queue,
        "The server-only validator queue must reject browser-origin requests.")
require('crypto.subtle.digest("SHA-256"' in validator_queue,
        "The validator queue must compare fixed-length credential digests.")
require('createSignedUrl(lease.storage_path, leaseSeconds' in validator_queue,
        "The validator broker must receive only a short-lived object-specific URL.")
for rpc in (
    "service_lease_marketplace_upload",
    "service_renew_marketplace_upload_lease",
    "service_complete_marketplace_validation",
):
    require(rpc in validator_queue, f"Validator Edge boundary does not call {rpc}.")
require("[functions.marketplace-validator-queue]" in supabase_config and
        "verify_jwt = false" in supabase_config.split("[functions.marketplace-validator-queue]", 1)[1],
        "The scoped-secret validator queue must bypass platform JWT parsing.")

print("Marketplace Edge trust-boundary validation passed.")
