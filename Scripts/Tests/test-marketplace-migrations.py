#!/usr/bin/env python3
"""Static security-contract checks for the additive marketplace migrations."""

from __future__ import annotations

import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[2]
MIGRATIONS = [
    ROOT / "supabase/migrations/20260812032747_marketplace_foundation.sql",
    ROOT / "supabase/migrations/20260812061500_marketplace_storage_and_downloads.sql",
    ROOT / "supabase/migrations/20260812081500_marketplace_staging_seed.sql",
    ROOT / "supabase/migrations/20260812113000_marketplace_security_hardening.sql",
    ROOT / "supabase/migrations/20260812114500_marketplace_policy_consolidation.sql",
    ROOT / "supabase/migrations/20260812123000_marketplace_edge_transition_boundary.sql",
    ROOT / "supabase/migrations/20260812124500_marketplace_publisher_transition_boundary.sql",
    ROOT / "supabase/migrations/20260812165208_marketplace_staff_moderation.sql",
    ROOT / "supabase/migrations/20260812190000_marketplace_validator_leases.sql",
    ROOT / "supabase/migrations/20260812190001_add_marketplace_publisher_upload_boundary.sql",
    ROOT / "supabase/migrations/20260812190002_fix_marketplace_service_rate_limit_subject.sql",
    ROOT / "supabase/migrations/20260812170807_index_marketplace_staff_appointed_by.sql",
]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


sources = {path.name: path.read_text(encoding="utf-8") for path in MIGRATIONS}
foundation = sources[MIGRATIONS[0].name]
storage = sources[MIGRATIONS[1].name]
seed = sources[MIGRATIONS[2].name]
hardening = sources[MIGRATIONS[3].name]
policy_consolidation = sources[MIGRATIONS[4].name]
edge_boundary = sources[MIGRATIONS[5].name]
publisher_boundary = sources[MIGRATIONS[6].name]
staff_moderation = sources[MIGRATIONS[7].name]
validator_leases = sources[MIGRATIONS[8].name]
publisher_uploads = sources[MIGRATIONS[9].name]
rate_limit_subject = sources[MIGRATIONS[10].name]
staff_indexes = sources[MIGRATIONS[11].name]
combined = "\n".join(sources.values())

public_tables = sorted(set(re.findall(r"create table public\.([a-z0-9_]+)\s*\(", combined)))
require(public_tables, "Marketplace migrations contain no public tables.")
for table in public_tables:
    require(
        f"alter table public.{table} enable row level security;" in combined,
        f"public.{table} does not enable RLS.",
    )
    require(
        f"alter table public.{table} force row level security;" in combined,
        f"public.{table} does not force RLS.",
    )

for function_body in re.findall(
    r"create or replace function\s+[^$]+?security definer(?P<body>.+?)\$\$;",
    combined,
    flags=re.IGNORECASE | re.DOTALL,
):
    require("set search_path = ''" in function_body[:300], "A SECURITY DEFINER function has no empty search_path.")

for flag in (
    "marketplace_enabled",
    "publisher_portal_enabled",
    "asset_packages_enabled",
    "hub_oauth_sso_enabled",
    "community_enabled",
    "paid_checkout_enabled",
):
    require(re.search(rf"\('{flag}',\s*false", foundation), f"{flag} must default to disabled.")

for bucket in ("marketplace-quarantine", "marketplace-releases"):
    require(re.search(rf"\('{bucket}',\s*'{bucket}',\s*false", storage), f"{bucket} must remain private.")

for contract in (
    "marketplace_products_protect_transitions",
    "marketplace_versions_protect_transitions",
    "marketplace_reviews_protect_fields",
    "marketplace_license_revisions",
    "issue_marketplace_download_grant",
    "register_marketplace_device_session",
):
    require(contract in combined, f"Marketplace security contract is missing: {contract}.")

require("paid_checkout_enabled', true" not in combined, "Paid checkout must remain disabled in 0.3.1.")
require("state,\n         license_spdx" in seed, "Official staging products must be inserted as explicit drafts.")
require("'draft', 'MIT', '1'" in seed, "Official staging products must be free, licensed drafts.")
require("'published'" not in seed, "The staging seed must never publish products.")
require("private.seed_official_marketplace_drafts" in seed, "The official staging seed must remain private.")
for rpc_signature in (
    "public.create_marketplace_organization(text, text)",
    "public.claim_free_marketplace_product(uuid, uuid, text, text, text, text)",
    "public.register_marketplace_device_session(text, text, text)",
    "public.issue_marketplace_download_grant(uuid, uuid, uuid)",
):
    require(
        f"revoke all on function {rpc_signature}" in hardening,
        f"The direct authenticated RPC boundary was not revoked for {rpc_signature}.",
    )
    require(
        f"grant execute on function {rpc_signature} to service_role;" in hardening,
        f"The service-only RPC boundary is missing for {rpc_signature}.",
    )
require(
    "membership.organization_id = publishers.organization_id" in hardening,
    "The suspended-publisher membership policy must qualify the outer organization.",
)
for service_function in (
    "service_create_marketplace_organization",
    "service_claim_free_marketplace_product",
    "service_register_marketplace_device_session",
    "service_issue_marketplace_download_grant",
):
    require(service_function in edge_boundary, f"Marketplace Edge boundary is missing {service_function}.")
require(
    edge_boundary.count("to service_role;") == 4,
    "Every marketplace Edge adapter must grant execution only to service_role.",
)
require(
    "key in ('marketplace_enabled', 'publisher_portal_enabled')" in edge_boundary,
    "Organization creation must remain gated until a marketplace authoring surface is enabled.",
)
require(
    "key = 'hub_oauth_sso_enabled'" in edge_boundary,
    "Hub device registration must remain gated until Hub OAuth is enabled.",
)
require("service_submit_publisher_application" in publisher_boundary,
        "The publisher application submission transition is missing.")
require("publisher_portal_enabled" in publisher_boundary and "to service_role;" in publisher_boundary,
        "Publisher submissions must be feature-gated and callable only by service_role.")
require("membership.role in ('owner', 'admin')" in publisher_boundary,
        "Publisher applications must be bound to a managed organization.")
require("drop policy if exists publisher_applications_owner_update" in publisher_boundary,
        "The original direct publisher-submission policy must be replaced.")
require("state in ('draft', 'withdrawn')" in publisher_boundary,
        "Applicants must not bypass the MFA-protected submission transition through PostgREST.")
require("create table public.platform_staff_members" in staff_moderation and
        "force row level security" in staff_moderation,
        "Staff roles must be database-authoritative and protected by forced RLS.")
require("from public.platform_staff_members staff" in staff_moderation and
        "auth.jwt() -> 'app_metadata'" not in staff_moderation,
        "Marketplace authorization must not depend on stale browser JWT role metadata.")
require("revoke insert on public.marketplace_submissions from authenticated" in staff_moderation and
        "drop policy if exists submissions_publisher_insert" in staff_moderation,
        "Package submission must not bypass the MFA-protected service transition.")
require("private.is_platform_staff('moderator') or exists" not in
        staff_moderation.split("create or replace function private.can_manage_publisher", 1)[1]
        .split("$$;", 1)[0],
        "Staff status must not silently grant direct publisher write authority.")
require("select coalesce((select auth.jwt() ->> 'role'), '') = 'service_role'" in staff_moderation,
        "Only the service boundary may bypass protected product and version transitions.")
for direct_staff_policy in (
    "publisher_applications_owner_or_staff_update",
    "reviews_author_publisher_or_staff_update",
):
    require(f"drop policy if exists {direct_staff_policy}" in staff_moderation,
            f"Audited staff actions must replace the direct write policy: {direct_staff_policy}.")
for staff_function in (
    "service_get_platform_staff_role",
    "service_set_platform_staff",
    "service_decide_publisher_application",
    "service_submit_marketplace_version",
    "service_decide_marketplace_submission",
    "service_decide_marketplace_report",
    "service_set_platform_feature_flag",
):
    require(staff_function in staff_moderation, f"Staff transition is missing {staff_function}.")
    require(
        re.search(rf"grant execute on function public\.{staff_function}\([^;]+\) to service_role;", staff_moderation),
        f"Staff transition is not restricted to service_role: {staff_function}.",
    )
require("staff_last_administrator_required" in staff_moderation,
        "Staff administration must preserve at least one active administrator.")
require("approved_pending_signature" in staff_moderation and "marketplace_publications" not in staff_moderation,
        "Staff approval must stop before the offline signing and publication boundary.")
require("p_key = 'paid_checkout_enabled' and p_enabled" in staff_moderation,
        "The staff gate boundary must refuse premature paid checkout enablement.")
for policy in (
    "validation_reports_publisher_or_staff_read",
    "publications_publisher_or_staff_read",
    "audit_events_staff_read",
):
    require(policy in hardening, f"Marketplace hardening policy is missing: {policy}.")
for legacy_policy in (
    "product_tags_publisher_write",
    "media_publisher_write",
    "versions_publisher_write",
    "dependencies_publisher_write",
    "reviews_author_update",
    "reviews_publisher_reply_update",
):
    require(
        f"drop policy if exists {legacy_policy}" in policy_consolidation,
        f"The duplicate permissive policy is not removed: {legacy_policy}.",
    )
require(
    "reviews_author_publisher_or_staff_update" in policy_consolidation,
    "The consolidated review update policy is missing.",
)
require(
    "product.state in ('draft', 'changes_requested')" in policy_consolidation,
    "Publisher listing writes must be restricted to editable product states.",
)
for validator_function in (
    "service_lease_marketplace_upload",
    "service_renew_marketplace_upload_lease",
    "service_complete_marketplace_validation",
):
    require(validator_function in validator_leases, f"Validator boundary is missing {validator_function}.")
    require(
        re.search(rf"grant execute on function public\.{validator_function}\([^;]+\) to service_role;", validator_leases),
        f"Validator transition is not restricted to service_role: {validator_function}.",
    )
require("for update of upload skip locked" in validator_leases,
        "Validator leasing must be atomic and skip jobs held by another worker.")
require("lease_expires_at <= now()" in validator_leases,
        "Validator leasing must recover stale jobs.")
require("validation_attempts < 5" in validator_leases and "retry_exhausted" in validator_leases,
        "Validator leasing must terminally bound poison-job retries.")
require("validator_fingerprint_sha256" in validator_leases and "policy_version" in validator_leases,
        "Validator reports must retain binary and policy provenance.")
require("jsonb_array_length(report_diagnostics) > 1024" in validator_leases,
        "Validator diagnostics must have a database-side bound.")
for publisher_upload_function in (
    "service_reserve_marketplace_upload",
    "service_complete_marketplace_upload",
    "service_cancel_marketplace_upload",
):
    require(publisher_upload_function in publisher_uploads,
            f"Publisher upload boundary is missing {publisher_upload_function}.")
    require(
        re.search(rf"grant execute on function public\.{publisher_upload_function}\([^;]+\) to service_role;",
                  publisher_uploads),
        f"Publisher upload transition is not restricted to service_role: {publisher_upload_function}.",
    )
require("coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role'" in publisher_uploads,
        "Publisher upload adapters must validate the service JWT without deprecated auth.role().")
require("publisher_portal_enabled" in publisher_uploads,
        "Publisher upload reservations and completion must remain feature-gated.")
require("membership.role in ('owner', 'admin')" in publisher_uploads,
        "Publisher uploads must require a managed publisher organization.")
require("object.bucket_id = 'marketplace-quarantine'" in publisher_uploads and
        "object_size <> selected_upload.expected_size_bytes" in publisher_uploads,
        "Publisher completion must verify the exact private quarantine object size.")
require("p_expected_sha256 !~ '^[0-9a-f]{64}$'" in publisher_uploads,
        "Publisher reservations must reject malformed expected package hashes.")
require("p_subject uuid" in rate_limit_subject and
        "values (p_bucket, p_subject, now(), 1)" in rate_limit_subject,
        "Marketplace throttles must accept an explicitly trusted actor subject.")
for actor_field in ("user_id", "author_user_id", "reporter_user_id", "created_by", "session_id"):
    require(actor_field in rate_limit_subject,
            f"Marketplace throttle does not derive its actor from {actor_field}.")
require("when 'marketplace_uploads' then nullif(to_jsonb(new) ->> 'created_by', '')::uuid" in rate_limit_subject,
        "Service-role publisher uploads must be throttled by their authenticated creator.")
require("drop function private.consume_marketplace_rate_limit(text, integer, interval)" in rate_limit_subject,
        "The auth.uid()-only throttle implementation must be removed.")
require("idx_platform_staff_members_appointed_by" in staff_indexes,
        "The staff appointer foreign key must keep its covering index.")

print(f"Marketplace migration validation passed for {len(public_tables)} forced-RLS public tables.")
