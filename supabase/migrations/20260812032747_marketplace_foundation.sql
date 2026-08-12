-- Kéire 0.3.1 marketplace foundation. All state transitions remain disabled until the
-- corresponding feature flag is enabled and the staging launch gates have passed.

create schema if not exists private;
revoke all on schema private from public, anon, authenticated;

create type public.organization_role as enum ('owner', 'admin', 'member');
create type public.publisher_application_state as enum
    ('draft', 'submitted', 'changes_requested', 'approved', 'rejected', 'withdrawn');
create type public.marketplace_product_state as enum
    ('draft', 'validation', 'submitted', 'changes_requested', 'approved_pending_signature', 'published', 'delisted',
     'suspended');
create type public.marketplace_version_state as enum
    ('draft', 'uploaded', 'validating', 'validation_failed', 'validated', 'submitted', 'changes_requested',
     'approved_pending_signature', 'published', 'withdrawn', 'security_revoked');
create type public.marketplace_install_kind as enum ('registry', 'asset_import', 'complete_project');
create type public.marketplace_upload_state as enum
    ('created', 'uploading', 'uploaded', 'leased', 'validated', 'failed', 'expired');
create type public.marketplace_submission_state as enum
    ('submitted', 'in_review', 'changes_requested', 'approved_pending_signature', 'approved', 'rejected', 'withdrawn');
create type public.marketplace_report_state as enum ('open', 'triaged', 'resolved', 'dismissed');

alter table public.profiles
    add column if not exists handle text,
    add column if not exists biography text,
    add column if not exists notification_preferences jsonb not null default '{}'::jsonb,
    add column if not exists locale text not null default 'en-US',
    add column if not exists deletion_requested_at timestamptz,
    add column if not exists export_requested_at timestamptz;

alter table public.profiles
    add constraint profiles_handle_format
        check (handle is null or handle ~ '^[a-z0-9][a-z0-9_-]{2,31}$'),
    add constraint profiles_biography_length
        check (biography is null or char_length(biography) <= 500),
    add constraint profiles_notification_preferences_object
        check (jsonb_typeof(notification_preferences) = 'object'),
    add constraint profiles_locale_format
        check (locale ~ '^[A-Za-z]{2,3}(-[A-Za-z0-9]{2,8})?$');

create unique index profiles_handle_unique on public.profiles (lower(handle)) where handle is not null;

create table public.platform_feature_flags (
    key text primary key,
    enabled boolean not null default false,
    description text not null,
    updated_at timestamptz not null default now(),
    updated_by uuid references auth.users(id) on delete set null,
    constraint platform_feature_flags_key check (key ~ '^[a-z][a-z0-9_]{2,63}$'),
    constraint platform_feature_flags_description check (char_length(description) between 1 and 500)
);

insert into public.platform_feature_flags (key, enabled, description) values
    ('marketplace_enabled', false, 'Public marketplace catalog and library workflows.'),
    ('publisher_portal_enabled', false, 'Publisher application and submission workflows.'),
    ('asset_packages_enabled', false, 'Hub and Editor asset-package workflows.'),
    ('hub_oauth_sso_enabled', false, 'OAuth 2.1 browser authorization for Kéire Hub.'),
    ('community_enabled', false, 'Wishlists, reviews, publisher replies, and reports.'),
    ('paid_checkout_enabled', false, 'Provider-neutral paid checkout. Must remain disabled for 0.3.1.')
on conflict (key) do update
set description = excluded.description;

create table public.organizations (
    id uuid primary key default gen_random_uuid(),
    slug text not null,
    display_name text not null,
    avatar_url text,
    created_by uuid not null references auth.users(id) on delete restrict,
    created_at timestamptz not null default now(),
    updated_at timestamptz not null default now(),
    deleted_at timestamptz,
    constraint organizations_slug_format check (slug ~ '^[a-z0-9][a-z0-9-]{2,62}$'),
    constraint organizations_display_name_length check (char_length(display_name) between 1 and 96),
    constraint organizations_avatar_url check
        (avatar_url is null or (char_length(avatar_url) <= 2048 and avatar_url like 'https://%'))
);
create unique index organizations_slug_unique on public.organizations (lower(slug)) where deleted_at is null;

create table public.organization_memberships (
    organization_id uuid not null references public.organizations(id) on delete cascade,
    user_id uuid not null references auth.users(id) on delete cascade,
    role public.organization_role not null default 'member',
    invited_by uuid references auth.users(id) on delete set null,
    created_at timestamptz not null default now(),
    primary key (organization_id, user_id)
);

create table public.publisher_applications (
    id uuid primary key default gen_random_uuid(),
    applicant_user_id uuid not null references auth.users(id) on delete restrict,
    organization_id uuid references public.organizations(id) on delete restrict,
    public_name text not null,
    website_url text,
    portfolio_url text,
    statement text not null,
    state public.publisher_application_state not null default 'draft',
    submitted_at timestamptz,
    reviewed_at timestamptz,
    reviewed_by uuid references auth.users(id) on delete set null,
    decision_note text,
    created_at timestamptz not null default now(),
    updated_at timestamptz not null default now(),
    constraint publisher_applications_name check (char_length(public_name) between 2 and 96),
    constraint publisher_applications_statement check (char_length(statement) between 20 and 5000),
    constraint publisher_applications_urls check
        ((website_url is null or (char_length(website_url) <= 2048 and website_url like 'https://%')) and
         (portfolio_url is null or (char_length(portfolio_url) <= 2048 and portfolio_url like 'https://%'))),
    constraint publisher_applications_decision_note check
        (decision_note is null or char_length(decision_note) <= 5000)
);

create table public.publishers (
    id uuid primary key default gen_random_uuid(),
    organization_id uuid not null unique references public.organizations(id) on delete restrict,
    slug text not null,
    display_name text not null,
    summary text not null default '',
    website_url text,
    verified boolean not null default false,
    suspended_at timestamptz,
    created_at timestamptz not null default now(),
    updated_at timestamptz not null default now(),
    constraint publishers_slug_format check (slug ~ '^[a-z0-9][a-z0-9-]{2,62}$'),
    constraint publishers_display_name check (char_length(display_name) between 2 and 96),
    constraint publishers_summary check (char_length(summary) <= 1000),
    constraint publishers_website_url check
        (website_url is null or (char_length(website_url) <= 2048 and website_url like 'https://%'))
);
create unique index publishers_slug_unique on public.publishers (lower(slug));

create table public.marketplace_categories (
    id uuid primary key default gen_random_uuid(),
    slug text not null unique,
    display_name text not null,
    description text not null default '',
    sort_order integer not null default 0,
    active boolean not null default true,
    constraint marketplace_categories_slug check (slug ~ '^[a-z0-9][a-z0-9-]{1,62}$'),
    constraint marketplace_categories_name check (char_length(display_name) between 2 and 64),
    constraint marketplace_categories_description check (char_length(description) <= 500)
);

create table public.marketplace_license_revisions (
    license_id text not null,
    revision text not null,
    display_name text not null,
    license_kind text not null,
    canonical_url text not null,
    acceptance_snapshot text not null,
    approved_by uuid references auth.users(id) on delete restrict,
    approved_at timestamptz not null default now(),
    primary key (license_id, revision),
    constraint marketplace_license_identity check
        (char_length(license_id) between 2 and 64 and char_length(revision) between 1 and 64),
    constraint marketplace_license_display_name check (char_length(display_name) between 2 and 128),
    constraint marketplace_license_kind check (license_kind in ('spdx', 'creative_commons', 'custom')),
    constraint marketplace_license_url check
        (char_length(canonical_url) between 8 and 2048 and canonical_url like 'https://%'),
    constraint marketplace_license_snapshot check (char_length(acceptance_snapshot) between 1 and 100000),
    constraint marketplace_license_allowlist check
        ((license_kind = 'spdx' and license_id in ('MIT', 'Apache-2.0', 'BSD-3-Clause')) or
         (license_kind = 'creative_commons' and license_id in ('CC0-1.0', 'CC-BY-4.0')) or
         (license_kind = 'custom' and license_id ~ '^LicenseRef-Keire-[A-Za-z0-9.-]{1,46}$' and
          approved_by is not null))
);

insert into public.marketplace_license_revisions
    (license_id, revision, display_name, license_kind, canonical_url, acceptance_snapshot)
values
    ('MIT', '1', 'MIT License', 'spdx', 'https://spdx.org/licenses/MIT.html',
     E'SPDX-License-Identifier: MIT\nLicense-Revision: 1'),
    ('Apache-2.0', '1', 'Apache License 2.0', 'spdx', 'https://spdx.org/licenses/Apache-2.0.html',
     E'SPDX-License-Identifier: Apache-2.0\nLicense-Revision: 1'),
    ('BSD-3-Clause', '1', 'BSD 3-Clause License', 'spdx', 'https://spdx.org/licenses/BSD-3-Clause.html',
     E'SPDX-License-Identifier: BSD-3-Clause\nLicense-Revision: 1'),
    ('CC0-1.0', '1', 'Creative Commons Zero 1.0', 'creative_commons',
     'https://creativecommons.org/publicdomain/zero/1.0/legalcode',
     E'SPDX-License-Identifier: CC0-1.0\nLicense-Revision: 1'),
    ('CC-BY-4.0', '1', 'Creative Commons Attribution 4.0', 'creative_commons',
     'https://creativecommons.org/licenses/by/4.0/legalcode',
     E'SPDX-License-Identifier: CC-BY-4.0\nLicense-Revision: 1');

create table public.marketplace_products (
    id uuid primary key default gen_random_uuid(),
    publisher_id uuid not null references public.publishers(id) on delete restrict,
    category_id uuid not null references public.marketplace_categories(id) on delete restrict,
    slug text not null,
    display_name text not null,
    short_description text not null,
    description_markdown text not null default '',
    state public.marketplace_product_state not null default 'draft',
    license_spdx text not null,
    license_revision text not null,
    support_url text,
    repository_url text,
    featured boolean not null default false,
    published_at timestamptz,
    delisted_at timestamptz,
    created_at timestamptz not null default now(),
    updated_at timestamptz not null default now(),
    constraint marketplace_products_slug check (slug ~ '^[a-z0-9][a-z0-9-]{1,95}$'),
    constraint marketplace_products_name check (char_length(display_name) between 2 and 128),
    constraint marketplace_products_short_description check (char_length(short_description) between 20 and 240),
    constraint marketplace_products_description check (char_length(description_markdown) <= 100000),
    constraint marketplace_products_license check
        (char_length(license_spdx) between 2 and 64 and char_length(license_revision) between 1 and 64),
    constraint marketplace_products_license_revision foreign key (license_spdx, license_revision)
        references public.marketplace_license_revisions(license_id, revision) on update restrict on delete restrict,
    constraint marketplace_products_urls check
        ((support_url is null or (char_length(support_url) <= 2048 and support_url like 'https://%')) and
         (repository_url is null or (char_length(repository_url) <= 2048 and repository_url like 'https://%'))),
    unique (publisher_id, slug)
);

create table public.marketplace_tags (
    id uuid primary key default gen_random_uuid(),
    slug text not null unique,
    display_name text not null,
    constraint marketplace_tags_slug check (slug ~ '^[a-z0-9][a-z0-9-]{1,62}$'),
    constraint marketplace_tags_name check (char_length(display_name) between 2 and 64)
);

create table public.marketplace_product_tags (
    product_id uuid not null references public.marketplace_products(id) on delete cascade,
    tag_id uuid not null references public.marketplace_tags(id) on delete cascade,
    primary key (product_id, tag_id)
);

create table public.marketplace_product_media (
    id uuid primary key default gen_random_uuid(),
    product_id uuid not null references public.marketplace_products(id) on delete cascade,
    storage_path text not null,
    media_type text not null,
    alt_text text not null,
    width integer,
    height integer,
    sort_order integer not null default 0,
    approved_at timestamptz,
    sha256 text not null,
    constraint marketplace_media_path check
        (char_length(storage_path) between 1 and 1024 and storage_path !~ '(^|/)\.\.(/|$)'),
    constraint marketplace_media_type check (media_type in ('image/avif', 'image/webp', 'image/png', 'video/mp4')),
    constraint marketplace_media_alt check (char_length(alt_text) between 1 and 300),
    constraint marketplace_media_dimensions check
        ((width is null and height is null) or (width between 1 and 16384 and height between 1 and 16384)),
    constraint marketplace_media_sha256 check (sha256 ~ '^[0-9a-f]{64}$')
);

create table public.marketplace_product_versions (
    id uuid primary key default gen_random_uuid(),
    product_id uuid not null references public.marketplace_products(id) on delete restrict,
    version text not null,
    state public.marketplace_version_state not null default 'draft',
    install_kind public.marketplace_install_kind not null,
    minimum_engine_version text not null,
    maximum_engine_version text,
    platforms text[] not null default '{}',
    architectures text[] not null default '{}',
    renderer_capabilities text[] not null default '{}',
    managed_api_version text,
    release_notes_markdown text not null default '',
    archive_storage_path text,
    archive_sha256 text,
    archive_size_bytes bigint,
    manifest_sha256 text,
    signature_key_id text,
    validator_version text,
    executable_code_fingerprint text,
    published_at timestamptz,
    withdrawn_at timestamptz,
    security_revoked_at timestamptz,
    created_at timestamptz not null default now(),
    updated_at timestamptz not null default now(),
    constraint marketplace_versions_semver check
        (version ~ '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)([-+][0-9A-Za-z.-]+)?$'),
    constraint marketplace_versions_engine_min check
        (minimum_engine_version ~ '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)([-+][0-9A-Za-z.-]+)?$'),
    constraint marketplace_versions_engine_max check
        (maximum_engine_version is null or maximum_engine_version ~
         '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)([-+][0-9A-Za-z.-]+)?$'),
    constraint marketplace_versions_release_notes check (char_length(release_notes_markdown) <= 50000),
    constraint marketplace_versions_archive_integrity check
        ((archive_storage_path is null and archive_sha256 is null and archive_size_bytes is null) or
         (char_length(archive_storage_path) between 1 and 1024 and archive_storage_path !~ '(^|/)\.\.(/|$)' and
          archive_sha256 ~ '^[0-9a-f]{64}$' and archive_size_bytes between 1 and 68719476736)),
    constraint marketplace_versions_manifest_hash check
        (manifest_sha256 is null or manifest_sha256 ~ '^[0-9a-f]{64}$'),
    unique (product_id, version)
);

create table public.marketplace_version_dependencies (
    version_id uuid not null references public.marketplace_product_versions(id) on delete cascade,
    package_id text not null,
    version_range text not null,
    conflict boolean not null default false,
    primary key (version_id, package_id, conflict),
    constraint marketplace_dependencies_package_id
        check (package_id ~ '^[a-z0-9][a-z0-9.-]{2,127}$' and package_id like '%.%'),
    constraint marketplace_dependencies_version check (char_length(version_range) between 1 and 128)
);

create table public.marketplace_offers (
    id uuid primary key default gen_random_uuid(),
    product_id uuid not null references public.marketplace_products(id) on delete cascade,
    currency char(3) not null default 'USD',
    amount_minor bigint not null default 0,
    active boolean not null default true,
    created_at timestamptz not null default now(),
    constraint marketplace_offers_currency check (currency ~ '^[A-Z]{3}$'),
    constraint marketplace_offers_amount check (amount_minor >= 0),
    constraint marketplace_offers_031_free_only check (amount_minor = 0)
);
create unique index marketplace_offers_one_active
    on public.marketplace_offers (product_id) where active;

create table public.marketplace_uploads (
    id uuid primary key default gen_random_uuid(),
    version_id uuid not null references public.marketplace_product_versions(id) on delete cascade,
    created_by uuid not null references auth.users(id) on delete restrict,
    storage_path text not null unique,
    expected_size_bytes bigint not null,
    expected_sha256 text not null,
    state public.marketplace_upload_state not null default 'created',
    uploaded_at timestamptz,
    lease_owner text,
    lease_expires_at timestamptz,
    created_at timestamptz not null default now(),
    expires_at timestamptz not null default (now() + interval '24 hours'),
    constraint marketplace_uploads_path check
        (char_length(storage_path) between 1 and 1024 and storage_path !~ '(^|/)\.\.(/|$)'),
    constraint marketplace_uploads_size check (expected_size_bytes between 1 and 68719476736),
    constraint marketplace_uploads_sha check (expected_sha256 ~ '^[0-9a-f]{64}$')
);

create table public.marketplace_validation_reports (
    id uuid primary key default gen_random_uuid(),
    upload_id uuid not null unique references public.marketplace_uploads(id) on delete restrict,
    validator_version text not null,
    package_sha256 text not null,
    passed boolean not null,
    diagnostics jsonb not null default '[]'::jsonb,
    malware_scan_result text not null,
    code_fingerprint text,
    completed_at timestamptz not null default now(),
    constraint marketplace_validation_version check (char_length(validator_version) between 1 and 128),
    constraint marketplace_validation_hash check (package_sha256 ~ '^[0-9a-f]{64}$'),
    constraint marketplace_validation_diagnostics check (jsonb_typeof(diagnostics) = 'array'),
    constraint marketplace_validation_scan check
        (malware_scan_result in ('clean', 'infected', 'error', 'not_applicable'))
);

create table public.marketplace_submissions (
    id uuid primary key default gen_random_uuid(),
    version_id uuid not null references public.marketplace_product_versions(id) on delete restrict,
    validation_report_id uuid not null references public.marketplace_validation_reports(id) on delete restrict,
    submitted_by uuid not null references auth.users(id) on delete restrict,
    state public.marketplace_submission_state not null default 'submitted',
    assigned_to uuid references auth.users(id) on delete set null,
    decision_note text,
    submitted_at timestamptz not null default now(),
    decided_at timestamptz,
    constraint marketplace_submissions_decision_note check
        (decision_note is null or char_length(decision_note) <= 10000)
);
create unique index marketplace_submissions_active
    on public.marketplace_submissions (version_id)
    where state in ('submitted', 'in_review', 'changes_requested', 'approved_pending_signature');

create table public.marketplace_publications (
    id uuid primary key default gen_random_uuid(),
    version_id uuid not null unique references public.marketplace_product_versions(id) on delete restrict,
    artifact_sha256 text not null,
    manifest_sha256 text not null,
    signature_key_id text not null,
    signed_manifest text not null,
    approved_by uuid not null references auth.users(id) on delete restrict,
    published_at timestamptz not null default now(),
    constraint marketplace_publications_artifact_hash check (artifact_sha256 ~ '^[0-9a-f]{64}$'),
    constraint marketplace_publications_manifest_hash check (manifest_sha256 ~ '^[0-9a-f]{64}$'),
    constraint marketplace_publications_signature_key check (char_length(signature_key_id) between 1 and 128),
    constraint marketplace_publications_manifest check (char_length(signed_manifest) between 2 and 8388608)
);

create table public.marketplace_orders (
    id uuid primary key default gen_random_uuid(),
    user_id uuid not null references auth.users(id) on delete restrict,
    organization_id uuid references public.organizations(id) on delete restrict,
    currency char(3) not null default 'USD',
    total_minor bigint not null default 0,
    status text not null default 'completed',
    idempotency_key text not null,
    created_at timestamptz not null default now(),
    constraint marketplace_orders_currency check (currency ~ '^[A-Z]{3}$'),
    constraint marketplace_orders_free check (total_minor = 0),
    constraint marketplace_orders_status check (status in ('completed', 'cancelled')),
    constraint marketplace_orders_idempotency check (char_length(idempotency_key) between 16 and 128),
    unique (user_id, idempotency_key)
);

create table public.marketplace_order_items (
    id uuid primary key default gen_random_uuid(),
    order_id uuid not null references public.marketplace_orders(id) on delete restrict,
    product_id uuid not null references public.marketplace_products(id) on delete restrict,
    offer_id uuid not null references public.marketplace_offers(id) on delete restrict,
    amount_minor bigint not null default 0,
    license_spdx text not null,
    license_revision text not null,
    accepted_license_snapshot text not null,
    constraint marketplace_order_items_free check (amount_minor = 0),
    constraint marketplace_order_items_license check
        (char_length(license_spdx) between 2 and 64 and char_length(license_revision) between 1 and 64 and
         char_length(accepted_license_snapshot) between 1 and 100000),
    unique (order_id, product_id)
);

create table public.marketplace_entitlements (
    id uuid primary key default gen_random_uuid(),
    product_id uuid not null references public.marketplace_products(id) on delete restrict,
    user_id uuid references auth.users(id) on delete restrict,
    organization_id uuid references public.organizations(id) on delete restrict,
    order_item_id uuid not null references public.marketplace_order_items(id) on delete restrict,
    granted_at timestamptz not null default now(),
    revoked_at timestamptz,
    revocation_reason text,
    constraint marketplace_entitlements_one_owner check
        ((user_id is not null)::integer + (organization_id is not null)::integer = 1),
    constraint marketplace_entitlements_revocation_reason check
        (revocation_reason is null or char_length(revocation_reason) <= 1000)
);
create unique index marketplace_entitlements_personal_unique
    on public.marketplace_entitlements (product_id, user_id) where user_id is not null;
create unique index marketplace_entitlements_organization_unique
    on public.marketplace_entitlements (product_id, organization_id) where organization_id is not null;

create table public.marketplace_wishlist_items (
    user_id uuid not null references auth.users(id) on delete cascade,
    product_id uuid not null references public.marketplace_products(id) on delete cascade,
    created_at timestamptz not null default now(),
    primary key (user_id, product_id)
);

create table public.marketplace_reviews (
    id uuid primary key default gen_random_uuid(),
    product_id uuid not null references public.marketplace_products(id) on delete cascade,
    author_user_id uuid references auth.users(id) on delete set null,
    owner_organization_id uuid references public.organizations(id) on delete set null,
    rating smallint not null,
    title text not null,
    body text not null,
    publisher_reply text,
    hidden_at timestamptz,
    created_at timestamptz not null default now(),
    updated_at timestamptz not null default now(),
    constraint marketplace_reviews_rating check (rating between 1 and 5),
    constraint marketplace_reviews_title check (char_length(title) between 3 and 120),
    constraint marketplace_reviews_body check (char_length(body) between 20 and 5000),
    constraint marketplace_reviews_reply check
        (publisher_reply is null or char_length(publisher_reply) between 1 and 5000)
);
create unique index marketplace_reviews_personal_unique
    on public.marketplace_reviews (product_id, author_user_id)
    where author_user_id is not null and owner_organization_id is null;
create unique index marketplace_reviews_organization_unique
    on public.marketplace_reviews (product_id, owner_organization_id)
    where owner_organization_id is not null;

create table public.marketplace_reports (
    id uuid primary key default gen_random_uuid(),
    reporter_user_id uuid not null references auth.users(id) on delete restrict,
    product_id uuid references public.marketplace_products(id) on delete cascade,
    review_id uuid references public.marketplace_reviews(id) on delete cascade,
    reason text not null,
    details text not null default '',
    state public.marketplace_report_state not null default 'open',
    assigned_to uuid references auth.users(id) on delete set null,
    resolution_note text,
    created_at timestamptz not null default now(),
    resolved_at timestamptz,
    constraint marketplace_reports_one_target check
        ((product_id is not null)::integer + (review_id is not null)::integer = 1),
    constraint marketplace_reports_reason check
        (reason in ('copyright', 'malware', 'fraud', 'abuse', 'spam', 'license', 'other')),
    constraint marketplace_reports_details check (char_length(details) <= 5000),
    constraint marketplace_reports_resolution check
        (resolution_note is null or char_length(resolution_note) <= 5000)
);

create table public.oauth_device_sessions (
    id uuid primary key default gen_random_uuid(),
    user_id uuid not null references auth.users(id) on delete cascade,
    oauth_session_id text not null unique,
    client_type text not null,
    device_name text not null,
    created_at timestamptz not null default now(),
    last_used_at timestamptz not null default now(),
    revoked_at timestamptz,
    constraint oauth_device_sessions_id check (char_length(oauth_session_id) between 16 and 256),
    constraint oauth_device_sessions_client check (client_type in ('website', 'hub', 'editor_broker')),
    constraint oauth_device_sessions_device check (char_length(device_name) between 1 and 128)
);

create table public.marketplace_download_grants (
    id uuid primary key default gen_random_uuid(),
    entitlement_id uuid not null references public.marketplace_entitlements(id) on delete cascade,
    version_id uuid not null references public.marketplace_product_versions(id) on delete restrict,
    session_id uuid not null references public.oauth_device_sessions(id) on delete cascade,
    created_at timestamptz not null default now(),
    expires_at timestamptz not null,
    used_at timestamptz,
    constraint marketplace_download_grants_expiry check (expires_at > created_at and expires_at <= created_at + interval '15 minutes')
);

create table public.platform_audit_events (
    id bigint generated always as identity primary key,
    occurred_at timestamptz not null default now(),
    actor_user_id uuid references auth.users(id) on delete set null,
    actor_session_id uuid references public.oauth_device_sessions(id) on delete set null,
    action text not null,
    target_type text not null,
    target_id text not null,
    correlation_id uuid not null default gen_random_uuid(),
    metadata jsonb not null default '{}'::jsonb,
    constraint platform_audit_action check (char_length(action) between 3 and 128),
    constraint platform_audit_target_type check (char_length(target_type) between 2 and 64),
    constraint platform_audit_target_id check (char_length(target_id) between 1 and 256),
    constraint platform_audit_metadata check (jsonb_typeof(metadata) = 'object')
);

create or replace function private.is_platform_staff(required_role text default null)
returns boolean
language sql
stable
security invoker
set search_path = ''
as $$
    select case
        when (select auth.uid()) is null then false
        when required_role is null then coalesce((select auth.jwt() -> 'app_metadata' ->> 'platform_role') in
            ('moderator', 'administrator'), false)
        else coalesce((select auth.jwt() -> 'app_metadata' ->> 'platform_role') = required_role or
            ((select auth.jwt() -> 'app_metadata' ->> 'platform_role') = 'administrator' and
             required_role = 'moderator'), false)
    end;
$$;

create or replace function private.is_organization_member(target_organization uuid,
                                                           allowed_roles public.organization_role[] default null)
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
    select exists (
        select 1
        from public.organization_memberships membership
        where membership.organization_id = target_organization
          and membership.user_id = (select auth.uid())
          and (select auth.uid()) is not null
          and (allowed_roles is null or membership.role = any(allowed_roles))
    );
$$;

create or replace function private.can_manage_publisher(target_publisher uuid)
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
    select (select auth.uid()) is not null and (private.is_platform_staff('moderator') or exists (
        select 1
        from public.publishers publisher
        join public.organization_memberships membership
          on membership.organization_id = publisher.organization_id
        where publisher.id = target_publisher
          and membership.user_id = (select auth.uid())
          and membership.role in ('owner', 'admin')
          and publisher.suspended_at is null
    ));
$$;

create or replace function private.has_product_entitlement(target_product uuid, target_organization uuid default null)
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
    select (select auth.uid()) is not null and exists (
        select 1
        from public.marketplace_entitlements entitlement
        where entitlement.product_id = target_product
          and entitlement.revoked_at is null
          and ((target_organization is null and entitlement.user_id = (select auth.uid())) or
               (target_organization is not null and entitlement.organization_id = target_organization and
                private.is_organization_member(target_organization, null)))
    );
$$;

revoke all on all functions in schema private from public, anon, authenticated;
grant usage on schema private to authenticated;
grant usage on schema private to anon;
grant execute on function private.is_platform_staff(text) to anon;
grant execute on function private.can_manage_publisher(uuid) to anon;
grant execute on function private.is_platform_staff(text) to authenticated;
grant execute on function private.is_organization_member(uuid, public.organization_role[]) to authenticated;
grant execute on function private.can_manage_publisher(uuid) to authenticated;
grant execute on function private.has_product_entitlement(uuid, uuid) to authenticated;

create or replace function public.create_marketplace_organization(p_slug text, p_display_name text)
returns uuid
language plpgsql
security definer
set search_path = ''
as $$
declare
    current_user_id uuid := (select auth.uid());
    created_id uuid;
begin
    if current_user_id is null then
        raise exception using errcode = '42501', message = 'authentication_required';
    end if;

    insert into public.organizations (slug, display_name, created_by)
    values (lower(p_slug), p_display_name, current_user_id)
    returning id into created_id;

    insert into public.organization_memberships (organization_id, user_id, role, invited_by)
    values (created_id, current_user_id, 'owner', current_user_id);

    insert into public.platform_audit_events (actor_user_id, action, target_type, target_id)
    values (current_user_id, 'organization.created', 'organization', created_id::text);
    return created_id;
end;
$$;

create or replace function public.claim_free_marketplace_product(
    p_product_id uuid,
    p_organization_id uuid,
    p_idempotency_key text,
    p_license_spdx text,
    p_license_revision text,
    p_accepted_license_snapshot text
)
returns uuid
language plpgsql
security definer
set search_path = ''
as $$
declare
    current_user_id uuid := (select auth.uid());
    selected_product public.marketplace_products%rowtype;
    selected_offer public.marketplace_offers%rowtype;
    expected_license_snapshot text;
    order_id uuid;
    order_item_id uuid;
    entitlement_id uuid;
begin
    if current_user_id is null then
        raise exception using errcode = '42501', message = 'authentication_required';
    end if;
    if not coalesce((select enabled from public.platform_feature_flags where key = 'marketplace_enabled'), false) then
        raise exception using errcode = '55000', message = 'marketplace_disabled';
    end if;
    if char_length(p_idempotency_key) not between 16 and 128 then
        raise exception using errcode = '22023', message = 'invalid_idempotency_key';
    end if;
    if p_organization_id is not null and not private.is_organization_member(
        p_organization_id, array['owner', 'admin']::public.organization_role[]) then
        raise exception using errcode = '42501', message = 'organization_authorization_required';
    end if;

    select * into selected_product
    from public.marketplace_products product
    where product.id = p_product_id and product.state = 'published'
    for share;
    if not found then
        raise exception using errcode = 'P0002', message = 'product_not_found';
    end if;
    if selected_product.license_spdx <> p_license_spdx or
       selected_product.license_revision <> p_license_revision then
        raise exception using errcode = '40001', message = 'license_revision_changed';
    end if;
    select license.acceptance_snapshot into expected_license_snapshot
    from public.marketplace_license_revisions license
    where license.license_id = selected_product.license_spdx and
          license.revision = selected_product.license_revision;
    if expected_license_snapshot is null or expected_license_snapshot <> p_accepted_license_snapshot then
        raise exception using errcode = '40001', message = 'license_revision_changed';
    end if;

    select * into selected_offer
    from public.marketplace_offers offer
    where offer.product_id = p_product_id and offer.active and offer.amount_minor = 0
    for share;
    if not found then
        raise exception using errcode = '55000', message = 'free_offer_unavailable';
    end if;

    perform pg_advisory_xact_lock(hashtextextended(
        current_user_id::text || ':' || coalesce(p_organization_id::text, 'personal') || ':' || p_product_id::text, 0));

    select entitlement.id into entitlement_id
    from public.marketplace_entitlements entitlement
    where entitlement.product_id = p_product_id
      and ((p_organization_id is null and entitlement.user_id = current_user_id) or
           (p_organization_id is not null and entitlement.organization_id = p_organization_id));
    if entitlement_id is not null then
        return entitlement_id;
    end if;

    insert into public.marketplace_orders
        (user_id, organization_id, currency, total_minor, status, idempotency_key)
    values
        (current_user_id, p_organization_id, selected_offer.currency, 0, 'completed', p_idempotency_key)
    on conflict (user_id, idempotency_key) do nothing
    returning id into order_id;
    if order_id is null then
        select marketplace_order.id into order_id
        from public.marketplace_orders marketplace_order
        where marketplace_order.user_id = current_user_id and
              marketplace_order.idempotency_key = p_idempotency_key and
              marketplace_order.organization_id is not distinct from p_organization_id;
        if order_id is null then
            raise exception using errcode = '23505', message = 'idempotency_key_reused';
        end if;
    end if;

    insert into public.marketplace_order_items
        (order_id, product_id, offer_id, amount_minor, license_spdx, license_revision, accepted_license_snapshot)
    values
        (order_id, p_product_id, selected_offer.id, 0, p_license_spdx, p_license_revision,
         p_accepted_license_snapshot)
    on conflict (order_id, product_id) do update
    set product_id = excluded.product_id
    returning id into order_item_id;

    insert into public.marketplace_entitlements (product_id, user_id, organization_id, order_item_id)
    values (p_product_id,
            case when p_organization_id is null then current_user_id else null end,
            p_organization_id,
            order_item_id)
    returning id into entitlement_id;

    insert into public.platform_audit_events
        (actor_user_id, action, target_type, target_id, metadata)
    values
        (current_user_id, 'marketplace.product_claimed', 'entitlement', entitlement_id::text,
         jsonb_build_object('productId', p_product_id, 'organizationId', p_organization_id));
    return entitlement_id;
end;
$$;

revoke all on function public.create_marketplace_organization(text, text) from public, anon, authenticated;
revoke all on function public.claim_free_marketplace_product(uuid, uuid, text, text, text, text)
    from public, anon, authenticated;
grant execute on function public.create_marketplace_organization(text, text) to authenticated;
grant execute on function public.claim_free_marketplace_product(uuid, uuid, text, text, text, text) to authenticated;

create view public.marketplace_catalog
with (security_invoker = true)
as
select product.id,
       product.slug,
       product.display_name,
       product.short_description,
       product.description_markdown,
       product.license_spdx,
       product.license_revision,
       license.acceptance_snapshot as license_acceptance_snapshot,
       product.featured,
       product.published_at,
       publisher.id as publisher_id,
       publisher.slug as publisher_slug,
       publisher.display_name as publisher_name,
       publisher.verified as publisher_verified,
       category.slug as category_slug,
       category.display_name as category_name,
       coalesce(review_summary.rating_average, 0)::numeric(3, 2) as rating_average,
       coalesce(review_summary.rating_count, 0)::bigint as rating_count
from public.marketplace_products product
join public.publishers publisher on publisher.id = product.publisher_id
join public.marketplace_categories category on category.id = product.category_id
join public.marketplace_license_revisions license
  on license.license_id = product.license_spdx and license.revision = product.license_revision
left join lateral (
    select avg(review.rating)::numeric(3, 2) as rating_average, count(*) as rating_count
    from public.marketplace_reviews review
    where review.product_id = product.id and review.hidden_at is null
) review_summary on true
where product.state = 'published' and publisher.suspended_at is null and category.active;

-- RLS is mandatory for every table reachable through the Data API.
alter table public.platform_feature_flags enable row level security;
alter table public.platform_feature_flags force row level security;
alter table public.organizations enable row level security;
alter table public.organizations force row level security;
alter table public.organization_memberships enable row level security;
alter table public.organization_memberships force row level security;
alter table public.publisher_applications enable row level security;
alter table public.publisher_applications force row level security;
alter table public.publishers enable row level security;
alter table public.publishers force row level security;
alter table public.marketplace_categories enable row level security;
alter table public.marketplace_categories force row level security;
alter table public.marketplace_license_revisions enable row level security;
alter table public.marketplace_license_revisions force row level security;
alter table public.marketplace_products enable row level security;
alter table public.marketplace_products force row level security;
alter table public.marketplace_tags enable row level security;
alter table public.marketplace_tags force row level security;
alter table public.marketplace_product_tags enable row level security;
alter table public.marketplace_product_tags force row level security;
alter table public.marketplace_product_media enable row level security;
alter table public.marketplace_product_media force row level security;
alter table public.marketplace_product_versions enable row level security;
alter table public.marketplace_product_versions force row level security;
alter table public.marketplace_version_dependencies enable row level security;
alter table public.marketplace_version_dependencies force row level security;
alter table public.marketplace_offers enable row level security;
alter table public.marketplace_offers force row level security;
alter table public.marketplace_uploads enable row level security;
alter table public.marketplace_uploads force row level security;
alter table public.marketplace_validation_reports enable row level security;
alter table public.marketplace_validation_reports force row level security;
alter table public.marketplace_submissions enable row level security;
alter table public.marketplace_submissions force row level security;
alter table public.marketplace_publications enable row level security;
alter table public.marketplace_publications force row level security;
alter table public.marketplace_orders enable row level security;
alter table public.marketplace_orders force row level security;
alter table public.marketplace_order_items enable row level security;
alter table public.marketplace_order_items force row level security;
alter table public.marketplace_entitlements enable row level security;
alter table public.marketplace_entitlements force row level security;
alter table public.marketplace_wishlist_items enable row level security;
alter table public.marketplace_wishlist_items force row level security;
alter table public.marketplace_reviews enable row level security;
alter table public.marketplace_reviews force row level security;
alter table public.marketplace_reports enable row level security;
alter table public.marketplace_reports force row level security;
alter table public.oauth_device_sessions enable row level security;
alter table public.oauth_device_sessions force row level security;
alter table public.marketplace_download_grants enable row level security;
alter table public.marketplace_download_grants force row level security;
alter table public.platform_audit_events enable row level security;
alter table public.platform_audit_events force row level security;

revoke all on all tables in schema public from anon, authenticated;
grant usage on schema public to anon, authenticated;
grant select on public.platform_feature_flags, public.marketplace_categories, public.marketplace_license_revisions,
    public.marketplace_tags,
    public.marketplace_catalog to anon, authenticated;
grant select, insert, update on public.profiles to authenticated;
grant select on public.publishers, public.marketplace_products, public.marketplace_product_media,
    public.marketplace_product_versions, public.marketplace_version_dependencies, public.marketplace_offers,
    public.marketplace_reviews to anon, authenticated;
grant select, update on public.organizations, public.organization_memberships to authenticated;
grant select, insert, update on public.publisher_applications, public.oauth_device_sessions to authenticated;
grant select, insert, update, delete on public.marketplace_wishlist_items, public.marketplace_reviews to authenticated;
grant select, insert on public.marketplace_reports to authenticated;
grant select on public.marketplace_orders, public.marketplace_order_items, public.marketplace_entitlements,
    public.marketplace_download_grants to authenticated;
grant select, insert, update, delete on public.marketplace_products, public.marketplace_product_tags,
    public.marketplace_product_media, public.marketplace_product_versions, public.marketplace_version_dependencies,
    public.marketplace_uploads to authenticated;
grant select, insert on public.marketplace_submissions to authenticated;

create policy feature_flags_read on public.platform_feature_flags for select to anon, authenticated using (true);
create policy organizations_member_read on public.organizations for select to authenticated
    using (private.is_organization_member(id, null) or private.is_platform_staff('moderator'));
create policy organizations_create on public.organizations for insert to authenticated
    with check (created_by = (select auth.uid()));
create policy organizations_admin_update on public.organizations for update to authenticated
    using (private.is_organization_member(id, array['owner', 'admin']::public.organization_role[]) or
           private.is_platform_staff('moderator'))
    with check (private.is_organization_member(id, array['owner', 'admin']::public.organization_role[]) or
                private.is_platform_staff('moderator'));
create policy organization_memberships_member_read on public.organization_memberships for select to authenticated
    using (private.is_organization_member(organization_id, null) or private.is_platform_staff('moderator'));
create policy organization_memberships_admin_insert on public.organization_memberships for insert to authenticated
    with check (private.is_organization_member(organization_id, array['owner', 'admin']::public.organization_role[]) or
                private.is_platform_staff('moderator'));
create policy organization_memberships_admin_update on public.organization_memberships for update to authenticated
    using (private.is_organization_member(organization_id, array['owner']::public.organization_role[]) or
           private.is_platform_staff('moderator'))
    with check (private.is_organization_member(organization_id, array['owner']::public.organization_role[]) or
                private.is_platform_staff('moderator'));

create policy publisher_applications_owner_read on public.publisher_applications for select to authenticated
    using (applicant_user_id = (select auth.uid()) or private.is_platform_staff('moderator'));
create policy publisher_applications_owner_insert on public.publisher_applications for insert to authenticated
    with check (applicant_user_id = (select auth.uid()) and state = 'draft');
create policy publisher_applications_owner_update on public.publisher_applications for update to authenticated
    using ((applicant_user_id = (select auth.uid()) and state in ('draft', 'changes_requested')) or
           private.is_platform_staff('moderator'))
    with check ((applicant_user_id = (select auth.uid()) and state in ('draft', 'submitted', 'withdrawn')) or
                private.is_platform_staff('moderator'));

create policy publishers_public_read on public.publishers for select to anon, authenticated
    using (suspended_at is null or private.is_platform_staff('moderator') or exists (
        select 1 from public.organization_memberships membership
        where membership.organization_id = organization_id and membership.user_id = (select auth.uid()) and
              membership.role in ('owner', 'admin')));
create policy categories_public_read on public.marketplace_categories for select to anon, authenticated
    using (active or private.is_platform_staff('moderator'));
create policy marketplace_license_revisions_public_read on public.marketplace_license_revisions
    for select to anon, authenticated using (true);
create policy tags_public_read on public.marketplace_tags for select to anon, authenticated using (true);
create policy products_public_or_owner_read on public.marketplace_products for select to anon, authenticated
    using (state = 'published' or private.can_manage_publisher(publisher_id) or private.is_platform_staff('moderator'));
create policy products_publisher_insert on public.marketplace_products for insert to authenticated
    with check (private.can_manage_publisher(publisher_id) and state = 'draft');
create policy products_publisher_update on public.marketplace_products for update to authenticated
    using (private.can_manage_publisher(publisher_id) or private.is_platform_staff('moderator'))
    with check (private.can_manage_publisher(publisher_id) or private.is_platform_staff('moderator'));
create policy products_publisher_delete_draft on public.marketplace_products for delete to authenticated
    using (state = 'draft' and private.can_manage_publisher(publisher_id));

create policy product_tags_public_read on public.marketplace_product_tags for select to anon, authenticated
    using (exists (select 1 from public.marketplace_products product where product.id = product_id));
create policy product_tags_publisher_write on public.marketplace_product_tags for all to authenticated
    using (exists (select 1 from public.marketplace_products product
                   where product.id = product_id and private.can_manage_publisher(product.publisher_id)))
    with check (exists (select 1 from public.marketplace_products product
                        where product.id = product_id and private.can_manage_publisher(product.publisher_id)));
create policy media_public_or_owner_read on public.marketplace_product_media for select to anon, authenticated
    using ((approved_at is not null and exists (select 1 from public.marketplace_products product
                                                where product.id = product_id and product.state = 'published')) or
           exists (select 1 from public.marketplace_products product
                   where product.id = product_id and private.can_manage_publisher(product.publisher_id)) or
           private.is_platform_staff('moderator'));
create policy media_publisher_write on public.marketplace_product_media for all to authenticated
    using (exists (select 1 from public.marketplace_products product
                   where product.id = product_id and private.can_manage_publisher(product.publisher_id)))
    with check (exists (select 1 from public.marketplace_products product
                        where product.id = product_id and private.can_manage_publisher(product.publisher_id)));

create policy versions_public_or_owner_read on public.marketplace_product_versions for select to anon, authenticated
    using (state in ('published', 'withdrawn', 'security_revoked') or
           exists (select 1 from public.marketplace_products product
                   where product.id = product_id and private.can_manage_publisher(product.publisher_id)) or
           private.is_platform_staff('moderator'));
create policy versions_publisher_write on public.marketplace_product_versions for all to authenticated
    using (exists (select 1 from public.marketplace_products product
                   where product.id = product_id and private.can_manage_publisher(product.publisher_id)) and
           state not in ('published', 'withdrawn', 'security_revoked'))
    with check (exists (select 1 from public.marketplace_products product
                        where product.id = product_id and private.can_manage_publisher(product.publisher_id)) and
                state not in ('published', 'withdrawn', 'security_revoked'));
create policy dependencies_public_read on public.marketplace_version_dependencies for select to anon, authenticated
    using (exists (select 1 from public.marketplace_product_versions version where version.id = version_id));
create policy dependencies_publisher_write on public.marketplace_version_dependencies for all to authenticated
    using (exists (select 1 from public.marketplace_product_versions version
                   join public.marketplace_products product on product.id = version.product_id
                   where version.id = version_id and private.can_manage_publisher(product.publisher_id) and
                         version.state not in ('published', 'withdrawn', 'security_revoked')))
    with check (exists (select 1 from public.marketplace_product_versions version
                        join public.marketplace_products product on product.id = version.product_id
                        where version.id = version_id and private.can_manage_publisher(product.publisher_id) and
                              version.state not in ('published', 'withdrawn', 'security_revoked')));
create policy offers_public_read on public.marketplace_offers for select to anon, authenticated
    using (active and exists (select 1 from public.marketplace_products product
                              where product.id = product_id and product.state = 'published'));

create policy uploads_publisher_read on public.marketplace_uploads for select to authenticated
    using (created_by = (select auth.uid()) or exists (
        select 1 from public.marketplace_product_versions version
        join public.marketplace_products product on product.id = version.product_id
        where version.id = version_id and private.can_manage_publisher(product.publisher_id)) or
        private.is_platform_staff('moderator'));
create policy uploads_publisher_insert on public.marketplace_uploads for insert to authenticated
    with check (created_by = (select auth.uid()) and state = 'created' and exists (
        select 1 from public.marketplace_product_versions version
        join public.marketplace_products product on product.id = version.product_id
        where version.id = version_id and private.can_manage_publisher(product.publisher_id)));
create policy uploads_publisher_update on public.marketplace_uploads for update to authenticated
    using (created_by = (select auth.uid()) and state in ('created', 'uploading', 'uploaded'))
    with check (created_by = (select auth.uid()) and state in ('created', 'uploading', 'uploaded'));
create policy submissions_publisher_read on public.marketplace_submissions for select to authenticated
    using (submitted_by = (select auth.uid()) or private.is_platform_staff('moderator') or exists (
        select 1 from public.marketplace_product_versions version
        join public.marketplace_products product on product.id = version.product_id
        where version.id = version_id and private.can_manage_publisher(product.publisher_id)));
create policy submissions_publisher_insert on public.marketplace_submissions for insert to authenticated
    with check (submitted_by = (select auth.uid()) and state = 'submitted' and exists (
        select 1 from public.marketplace_product_versions version
        join public.marketplace_products product on product.id = version.product_id
        where version.id = version_id and private.can_manage_publisher(product.publisher_id)));

create policy orders_owner_read on public.marketplace_orders for select to authenticated
    using (user_id = (select auth.uid()) or
           (organization_id is not null and private.is_organization_member(organization_id, null)) or
           private.is_platform_staff('moderator'));
create policy order_items_owner_read on public.marketplace_order_items for select to authenticated
    using (exists (select 1 from public.marketplace_orders marketplace_order
                   where marketplace_order.id = order_id and
                         (marketplace_order.user_id = (select auth.uid()) or
                          (marketplace_order.organization_id is not null and
                           private.is_organization_member(marketplace_order.organization_id, null)) or
                          private.is_platform_staff('moderator'))));
create policy entitlements_owner_read on public.marketplace_entitlements for select to authenticated
    using (user_id = (select auth.uid()) or
           (organization_id is not null and private.is_organization_member(organization_id, null)) or
           private.is_platform_staff('moderator'));
create policy wishlist_owner_all on public.marketplace_wishlist_items for all to authenticated
    using (user_id = (select auth.uid())) with check (user_id = (select auth.uid()));
create policy reviews_public_read on public.marketplace_reviews for select to anon, authenticated
    using (hidden_at is null or author_user_id = (select auth.uid()) or private.is_platform_staff('moderator'));
create policy reviews_entitled_insert on public.marketplace_reviews for insert to authenticated
    with check (author_user_id = (select auth.uid()) and private.has_product_entitlement(product_id, owner_organization_id));
create policy reviews_author_update on public.marketplace_reviews for update to authenticated
    using (author_user_id = (select auth.uid()) or private.is_platform_staff('moderator'))
    with check (author_user_id = (select auth.uid()) or private.is_platform_staff('moderator'));
create policy reviews_author_delete on public.marketplace_reviews for delete to authenticated
    using (author_user_id = (select auth.uid()));
create policy reports_owner_read on public.marketplace_reports for select to authenticated
    using (reporter_user_id = (select auth.uid()) or private.is_platform_staff('moderator'));
create policy reports_owner_insert on public.marketplace_reports for insert to authenticated
    with check (reporter_user_id = (select auth.uid()) and state = 'open');
create policy sessions_owner_read on public.oauth_device_sessions for select to authenticated
    using (user_id = (select auth.uid()) or private.is_platform_staff('administrator'));
create policy sessions_owner_insert on public.oauth_device_sessions for insert to authenticated
    with check (user_id = (select auth.uid()) and revoked_at is null);
create policy sessions_owner_revoke on public.oauth_device_sessions for update to authenticated
    using (user_id = (select auth.uid()) or private.is_platform_staff('administrator'))
    with check (user_id = (select auth.uid()) or private.is_platform_staff('administrator'));
create policy download_grants_owner_read on public.marketplace_download_grants for select to authenticated
    using (exists (select 1 from public.marketplace_entitlements entitlement
                   where entitlement.id = entitlement_id and
                         (entitlement.user_id = (select auth.uid()) or
                          (entitlement.organization_id is not null and
                           private.is_organization_member(entitlement.organization_id, null)))));

comment on table public.platform_feature_flags is
    'Server-authoritative staged rollout flags. paid_checkout_enabled remains false for Kéire 0.3.1.';
comment on table public.marketplace_publications is
    'Immutable offline-signing record. Signing private keys never reside in Supabase or the web process.';
comment on table public.platform_audit_events is
    'Append-only privileged transition audit trail. Direct Data API writes are intentionally not granted.';
