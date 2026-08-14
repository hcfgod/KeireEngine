-- Upload-once Marketplace publication with signed validator evidence and an automatic,
-- least-privileged publication signer. Existing quarantine/release objects remain valid;
-- only new reservations use marketplace-packages.

create type public.marketplace_publication_job_state as enum
    ('queued', 'leased', 'retry_wait', 'published', 'failed', 'cancelled');

alter table public.marketplace_uploads
    add column storage_bucket text not null default 'marketplace-quarantine';

alter table public.marketplace_uploads
    add constraint marketplace_uploads_storage_bucket check
        (storage_bucket in ('marketplace-quarantine', 'marketplace-packages'));

alter table public.marketplace_product_versions
    add column archive_storage_bucket text;

-- This is a metadata-only backfill for already-published immutable versions. The
-- normal transition guard deliberately rejects direct archive mutations, so
-- suspend it only for this bounded migration statement. A failed migration
-- rolls the trigger state back with the surrounding transaction.
alter table public.marketplace_product_versions
    disable trigger marketplace_versions_protect_transitions;

update public.marketplace_product_versions
set archive_storage_bucket = case
    when state in ('published', 'withdrawn', 'security_revoked') then 'marketplace-releases'
    else 'marketplace-quarantine'
end
where archive_storage_path is not null and archive_storage_bucket is null;

alter table public.marketplace_product_versions
    enable trigger marketplace_versions_protect_transitions;

alter table public.marketplace_product_versions
    add constraint marketplace_versions_archive_bucket check
        ((archive_storage_path is null and archive_storage_bucket is null) or
         (archive_storage_path is not null and
          archive_storage_bucket in ('marketplace-quarantine', 'marketplace-releases', 'marketplace-packages')));

create table public.marketplace_validator_attestation_keys (
    key_id text primary key,
    algorithm text not null default 'ed25519',
    public_key_base64 text not null,
    fingerprint text not null unique,
    active boolean not null default true,
    valid_from timestamptz not null default now(),
    valid_until timestamptz,
    created_at timestamptz not null default now(),
    constraint marketplace_validator_keys_id check (key_id ~ '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$'),
    constraint marketplace_validator_keys_algorithm check (algorithm = 'ed25519'),
    constraint marketplace_validator_keys_public_key check (public_key_base64 ~ '^[A-Za-z0-9+/]{43}=$'),
    constraint marketplace_validator_keys_fingerprint check (fingerprint ~ '^sha256:[0-9a-f]{64}$'),
    constraint marketplace_validator_keys_validity check (valid_until is null or valid_until > valid_from)
);

alter table public.marketplace_validation_reports
    add column evidence_storage_path text,
    add column evidence_sha256 text,
    add column evidence_size_bytes bigint,
    add column attestation_key_id text references public.marketplace_validator_attestation_keys(key_id) on delete restrict,
    add column signed_attestation text;

alter table public.marketplace_validation_reports
    add constraint marketplace_validation_evidence check
        ((evidence_storage_path is null and evidence_sha256 is null and evidence_size_bytes is null) or
         (evidence_storage_path ~ '^[0-9a-f-]{36}/review-evidence-[0-9a-f]{64}\.json$' and
          evidence_sha256 ~ '^[0-9a-f]{64}$' and evidence_size_bytes between 2 and 8388608)),
    add constraint marketplace_validation_attestation check
        ((attestation_key_id is null and signed_attestation is null) or
         (attestation_key_id is not null and char_length(signed_attestation) between 2 and 1048576));

create sequence public.marketplace_publication_sequence as bigint start with 1 increment by 1 no cycle;

create table public.marketplace_publication_jobs (
    id uuid primary key default gen_random_uuid(),
    submission_id uuid not null unique references public.marketplace_submissions(id) on delete restrict,
    version_id uuid not null unique references public.marketplace_product_versions(id) on delete restrict,
    approved_by uuid not null references auth.users(id) on delete restrict,
    signing_key_id text references public.marketplace_signature_keys(key_id) on delete restrict,
    publication_sequence bigint not null default nextval('public.marketplace_publication_sequence'),
    publication_expires_at timestamptz not null default (now() + interval '10 years'),
    state public.marketplace_publication_job_state not null default 'queued',
    attempt_count smallint not null default 0,
    lease_owner text,
    lease_expires_at timestamptz,
    retry_after timestamptz,
    last_error_code text,
    created_at timestamptz not null default now(),
    updated_at timestamptz not null default now(),
    published_at timestamptz,
    cancelled_at timestamptz,
    cancelled_by uuid references auth.users(id) on delete set null,
    constraint marketplace_publication_jobs_sequence check (publication_sequence > 0),
    constraint marketplace_publication_jobs_expiry check (publication_expires_at > created_at),
    constraint marketplace_publication_jobs_attempts check (attempt_count between 0 and 5),
    constraint marketplace_publication_jobs_worker check
        (lease_owner is null or lease_owner ~ '^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$'),
    constraint marketplace_publication_jobs_error check
        (last_error_code is null or last_error_code ~ '^[a-z][a-z0-9._-]{2,127}$'),
    constraint marketplace_publication_jobs_lease check
        ((state = 'leased' and lease_owner is not null and lease_expires_at is not null) or
         (state <> 'leased' and lease_owner is null and lease_expires_at is null))
);

create index idx_marketplace_publication_jobs_queue
    on public.marketplace_publication_jobs (retry_after, created_at, id)
    where state in ('queued', 'retry_wait');
create index idx_marketplace_publication_jobs_lease
    on public.marketplace_publication_jobs (lease_expires_at, id)
    where state = 'leased';
create index idx_marketplace_publication_jobs_approved_by
    on public.marketplace_publication_jobs (approved_by);
create index idx_marketplace_publication_jobs_cancelled_by
    on public.marketplace_publication_jobs (cancelled_by)
    where cancelled_by is not null;
create index idx_marketplace_publication_jobs_signing_key
    on public.marketplace_publication_jobs (signing_key_id)
    where signing_key_id is not null;
create index idx_marketplace_validation_reports_attestation_key
    on public.marketplace_validation_reports (attestation_key_id)
    where attestation_key_id is not null;

alter table public.marketplace_validator_attestation_keys enable row level security;
alter table public.marketplace_validator_attestation_keys force row level security;
alter table public.marketplace_publication_jobs enable row level security;
alter table public.marketplace_publication_jobs force row level security;

revoke all on public.marketplace_validator_attestation_keys from public, anon, authenticated;
revoke all on public.marketplace_publication_jobs from public, anon, authenticated;
revoke all on sequence public.marketplace_publication_sequence from public, anon, authenticated;
grant select on public.marketplace_validator_attestation_keys to service_role;
grant select on public.marketplace_publication_jobs to authenticated, service_role;
grant usage, select on sequence public.marketplace_publication_sequence to service_role;
grant usage on type public.marketplace_publication_job_state to authenticated, service_role;

create policy marketplace_validator_attestation_keys_service_read
on public.marketplace_validator_attestation_keys for select to service_role
using (true);

create policy marketplace_publication_jobs_staff_read
on public.marketplace_publication_jobs for select to authenticated
using (private.is_platform_staff('moderator'));

insert into storage.buckets (id, name, public, file_size_limit, allowed_mime_types)
values
    ('marketplace-packages', 'marketplace-packages', false, 68719476736,
     array['application/octet-stream', 'application/vnd.keire.asset-package']),
    ('marketplace-validation-evidence', 'marketplace-validation-evidence', false, 8388608,
     array['application/json'])
on conflict (id) do update
set public = false,
    file_size_limit = excluded.file_size_limit,
    allowed_mime_types = excluded.allowed_mime_types;

-- Human staff review signed, sanitized evidence rather than downloading untrusted package bytes.
drop policy if exists marketplace_quarantine_owner_read on storage.objects;

create or replace function public.service_reserve_marketplace_upload(
    p_actor_user_id uuid,
    p_product_id uuid,
    p_version text,
    p_install_kind public.marketplace_install_kind,
    p_minimum_engine_version text,
    p_maximum_engine_version text,
    p_platforms text[],
    p_architectures text[],
    p_renderer_capabilities text[],
    p_managed_api_version text,
    p_release_notes_markdown text,
    p_expected_size_bytes bigint,
    p_expected_sha256 text
)
returns table (upload_id uuid, version_id uuid, storage_path text, expires_at timestamptz)
language plpgsql
security definer
set search_path = ''
as $$
declare
    v_version_id uuid;
    v_version_state public.marketplace_version_state;
    v_upload_id uuid := gen_random_uuid();
    v_storage_path text;
    v_expires_at timestamptz := now() + interval '2 hours';
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       not exists (select 1 from auth.users where id = p_actor_user_id) then
        raise exception using errcode = '42501', message = 'service_actor_invalid';
    end if;
    if not coalesce((select enabled from public.platform_feature_flags
                     where key = 'publisher_portal_enabled'), false) then
        raise exception using errcode = '55000', message = 'publisher_portal_disabled';
    end if;
    if p_version !~ '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)([-+][0-9A-Za-z.-]+)?$' or
       p_minimum_engine_version !~
           '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)([-+][0-9A-Za-z.-]+)?$' or
       (p_maximum_engine_version is not null and p_maximum_engine_version !~
           '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)([-+][0-9A-Za-z.-]+)?$') or
       p_expected_size_bytes not between 1 and 68719476736 or p_expected_sha256 !~ '^[0-9a-f]{64}$' or
       char_length(coalesce(p_managed_api_version, '')) > 128 or
       char_length(coalesce(p_release_notes_markdown, '')) > 50000 then
        raise exception using errcode = '22023', message = 'publisher_upload_invalid';
    end if;
    if cardinality(coalesce(p_platforms, '{}'::text[])) > 16 or
       cardinality(coalesce(p_architectures, '{}'::text[])) > 16 or
       cardinality(coalesce(p_renderer_capabilities, '{}'::text[])) > 32 or
       exists (select 1 from unnest(coalesce(p_platforms, '{}'::text[]) ||
                                    coalesce(p_architectures, '{}'::text[]) ||
                                    coalesce(p_renderer_capabilities, '{}'::text[])) value
               where value !~ '^[A-Za-z0-9][A-Za-z0-9._+-]{0,63}$') then
        raise exception using errcode = '22023', message = 'publisher_upload_invalid';
    end if;
    if not exists (
        select 1 from public.marketplace_products product
        join public.publishers publisher on publisher.id = product.publisher_id
        join public.organization_memberships membership on membership.organization_id = publisher.organization_id
        where product.id = p_product_id and product.state in ('draft', 'changes_requested')
          and publisher.suspended_at is null and membership.user_id = p_actor_user_id
          and membership.role in ('owner', 'admin')
    ) then
        raise exception using errcode = '42501', message = 'publisher_product_not_editable';
    end if;

    select version.id, version.state into v_version_id, v_version_state
    from public.marketplace_product_versions version
    where version.product_id = p_product_id and version.version = p_version for update;
    if found then
        if v_version_state not in ('draft', 'validation_failed', 'changes_requested') then
            raise exception using errcode = '55000', message = 'publisher_version_not_editable';
        end if;
        update public.marketplace_product_versions version
        set state = 'draft', install_kind = p_install_kind, minimum_engine_version = p_minimum_engine_version,
            maximum_engine_version = p_maximum_engine_version, platforms = coalesce(p_platforms, '{}'::text[]),
            architectures = coalesce(p_architectures, '{}'::text[]),
            renderer_capabilities = coalesce(p_renderer_capabilities, '{}'::text[]),
            managed_api_version = nullif(p_managed_api_version, ''),
            release_notes_markdown = coalesce(p_release_notes_markdown, ''), archive_storage_path = null,
            archive_storage_bucket = null, archive_sha256 = null, archive_size_bytes = null,
            manifest_sha256 = null, signature_key_id = null, validator_version = null,
            executable_code_fingerprint = null, updated_at = now()
        where version.id = v_version_id;
    else
        insert into public.marketplace_product_versions (
            product_id, version, state, install_kind, minimum_engine_version, maximum_engine_version,
            platforms, architectures, renderer_capabilities, managed_api_version, release_notes_markdown
        ) values (
            p_product_id, p_version, 'draft', p_install_kind, p_minimum_engine_version, p_maximum_engine_version,
            coalesce(p_platforms, '{}'::text[]), coalesce(p_architectures, '{}'::text[]),
            coalesce(p_renderer_capabilities, '{}'::text[]), nullif(p_managed_api_version, ''),
            coalesce(p_release_notes_markdown, '')
        ) returning id into v_version_id;
    end if;

    update public.marketplace_uploads upload
    set state = 'expired', lease_owner = null, lease_expires_at = null
    where upload.version_id = v_version_id and upload.state in ('created', 'uploading')
      and upload.expires_at <= now();
    if exists (select 1 from public.marketplace_uploads upload where upload.version_id = v_version_id
               and upload.state in ('created', 'uploading', 'uploaded', 'leased')) then
        raise exception using errcode = '55000', message = 'publisher_upload_already_active';
    end if;

    v_storage_path := p_product_id::text || '/' || v_version_id::text || '/' ||
                      p_expected_sha256 || '.keireassetpackage';
    insert into public.marketplace_uploads (
        id, version_id, created_by, storage_bucket, storage_path, expected_size_bytes,
        expected_sha256, state, expires_at
    ) values (
        v_upload_id, v_version_id, p_actor_user_id, 'marketplace-packages', v_storage_path,
        p_expected_size_bytes, p_expected_sha256, 'created', v_expires_at
    );
    insert into public.platform_audit_events (actor_user_id, action, target_type, target_id, metadata)
    values (p_actor_user_id, 'marketplace.upload_reserved', 'marketplace_upload', v_upload_id::text,
            jsonb_build_object('productId', p_product_id, 'versionId', v_version_id,
                'storageBucket', 'marketplace-packages', 'expectedSizeBytes', p_expected_size_bytes,
                'expectedSha256', p_expected_sha256));
    return query select v_upload_id, v_version_id, v_storage_path, v_expires_at;
end;
$$;

create or replace function public.service_complete_marketplace_upload(
    p_actor_user_id uuid,
    p_upload_id uuid
)
returns table (upload_id uuid, version_id uuid, state public.marketplace_upload_state)
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_upload public.marketplace_uploads%rowtype;
    object_size bigint;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       not exists (select 1 from auth.users where id = p_actor_user_id) then
        raise exception using errcode = '42501', message = 'service_actor_invalid';
    end if;
    if not coalesce((select enabled from public.platform_feature_flags
                     where key = 'publisher_portal_enabled'), false) then
        raise exception using errcode = '55000', message = 'publisher_portal_disabled';
    end if;
    select upload.* into selected_upload
    from public.marketplace_uploads upload
    join public.marketplace_product_versions version on version.id = upload.version_id
    join public.marketplace_products product on product.id = version.product_id
    join public.publishers publisher on publisher.id = product.publisher_id
    join public.organization_memberships membership on membership.organization_id = publisher.organization_id
    where upload.id = p_upload_id and upload.created_by = p_actor_user_id
      and upload.state in ('created', 'uploading') and upload.expires_at > now()
      and membership.user_id = p_actor_user_id and membership.role in ('owner', 'admin')
      and publisher.suspended_at is null
    for update of upload;
    if not found then
        raise exception using errcode = 'P0002', message = 'publisher_upload_not_completable';
    end if;
    select case when object.metadata ->> 'size' ~ '^[0-9]+$'
        then (object.metadata ->> 'size')::bigint else null end into object_size
    from storage.objects object
    where object.bucket_id = selected_upload.storage_bucket and object.name = selected_upload.storage_path;
    if object_size is null or object_size <> selected_upload.expected_size_bytes then
        raise exception using errcode = '55000', message = 'publisher_upload_size_mismatch';
    end if;
    update public.marketplace_uploads upload
    set state = 'uploaded', uploaded_at = now(), lease_owner = null, lease_expires_at = null
    where upload.id = selected_upload.id;
    update public.marketplace_product_versions version set state = 'uploaded', updated_at = now()
    where version.id = selected_upload.version_id;
    insert into public.platform_audit_events (actor_user_id, action, target_type, target_id, metadata)
    values (p_actor_user_id, 'marketplace.upload_completed', 'marketplace_upload', selected_upload.id::text,
            jsonb_build_object('versionId', selected_upload.version_id,
                'storageBucket', selected_upload.storage_bucket, 'sizeBytes', selected_upload.expected_size_bytes,
                'sha256', selected_upload.expected_sha256));
    return query select selected_upload.id, selected_upload.version_id, 'uploaded'::public.marketplace_upload_state;
end;
$$;

create or replace function public.service_cancel_marketplace_upload_v2(
    p_actor_user_id uuid,
    p_upload_id uuid
)
returns table (storage_bucket text, storage_path text)
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_upload public.marketplace_uploads%rowtype;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       not exists (select 1 from auth.users where id = p_actor_user_id) then
        raise exception using errcode = '42501', message = 'service_actor_invalid';
    end if;
    select upload.* into selected_upload
    from public.marketplace_uploads upload
    join public.marketplace_product_versions version on version.id = upload.version_id
    join public.marketplace_products product on product.id = version.product_id
    join public.publishers publisher on publisher.id = product.publisher_id
    join public.organization_memberships membership on membership.organization_id = publisher.organization_id
    where upload.id = p_upload_id and upload.created_by = p_actor_user_id
      and upload.state in ('created', 'uploading') and membership.user_id = p_actor_user_id
      and membership.role in ('owner', 'admin') for update of upload;
    if not found then
        raise exception using errcode = 'P0002', message = 'publisher_upload_not_cancellable';
    end if;
    update public.marketplace_uploads upload
    set state = 'expired', lease_owner = null, lease_expires_at = null where upload.id = selected_upload.id;
    update public.marketplace_product_versions version set state = 'draft', updated_at = now()
    where version.id = selected_upload.version_id and version.state = 'draft';
    insert into public.platform_audit_events (actor_user_id, action, target_type, target_id)
    values (p_actor_user_id, 'marketplace.upload_cancelled', 'marketplace_upload', selected_upload.id::text);
    return query select selected_upload.storage_bucket, selected_upload.storage_path;
end;
$$;

create or replace function public.service_lease_marketplace_upload_v2(
    p_worker_id text,
    p_lease_seconds integer default 900
)
returns table (
    upload_id uuid,
    version_id uuid,
    storage_bucket text,
    storage_path text,
    evidence_storage_path text,
    expected_size_bytes bigint,
    expected_sha256 text,
    lease_expires_at timestamptz
)
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_upload public.marketplace_uploads%rowtype;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' then
        raise exception using errcode = '42501', message = 'service_role_required';
    end if;
    if p_worker_id !~ '^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$' or p_lease_seconds not between 60 and 1800 then
        raise exception using errcode = '22023', message = 'validator_lease_invalid';
    end if;
    with recovered as (
        update public.marketplace_uploads stale
        set state = case when stale.validation_attempts >= 5 then 'failed'::public.marketplace_upload_state
                         else 'uploaded'::public.marketplace_upload_state end,
            lease_owner = null, lease_expires_at = null
        where stale.state = 'leased' and stale.lease_expires_at <= now()
        returning stale.id, stale.version_id, stale.state
    ), recovered_versions as (
        update public.marketplace_product_versions version
        set state = case when recovered.state = 'failed' then 'validation_failed'::public.marketplace_version_state
                         else 'uploaded'::public.marketplace_version_state end, updated_at = now()
        from recovered where version.id = recovered.version_id and version.state = 'validating'
        returning recovered.id, recovered.state
    )
    insert into public.platform_audit_events (action, target_type, target_id, metadata)
    select 'marketplace.validation.retry_exhausted', 'marketplace_upload', recovered_versions.id::text,
           jsonb_build_object('attempts', 5) from recovered_versions where recovered_versions.state = 'failed';

    select upload.* into selected_upload
    from public.marketplace_uploads upload
    join public.marketplace_product_versions version on version.id = upload.version_id
    where upload.state = 'uploaded' and upload.uploaded_at is not null and upload.expires_at > now()
      and upload.validation_attempts < 5 and version.state in ('draft', 'uploaded', 'validation_failed')
      and not exists (select 1 from public.marketplace_validation_reports report where report.upload_id = upload.id)
    order by upload.uploaded_at, upload.id for update of upload skip locked limit 1;
    if not found then return; end if;

    update public.marketplace_uploads upload
    set state = 'leased', lease_owner = p_worker_id,
        lease_expires_at = now() + make_interval(secs => p_lease_seconds),
        validation_attempts = upload.validation_attempts + 1
    where upload.id = selected_upload.id returning upload.lease_expires_at into selected_upload.lease_expires_at;
    update public.marketplace_product_versions version set state = 'validating', updated_at = now()
    where version.id = selected_upload.version_id;
    insert into public.platform_audit_events (action, target_type, target_id, metadata)
    values ('marketplace.validation.leased', 'marketplace_upload', selected_upload.id::text,
            jsonb_build_object('workerId', p_worker_id, 'leaseSeconds', p_lease_seconds,
                'storageBucket', selected_upload.storage_bucket));
    return query select selected_upload.id, selected_upload.version_id, selected_upload.storage_bucket,
        selected_upload.storage_path, selected_upload.id::text || '/review-evidence-' ||
        selected_upload.expected_sha256 || '.json', selected_upload.expected_size_bytes,
        selected_upload.expected_sha256, selected_upload.lease_expires_at;
end;
$$;

create or replace function public.service_complete_marketplace_validation_v2(
    p_upload_id uuid,
    p_worker_id text,
    p_report jsonb
)
returns uuid
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_upload public.marketplace_uploads%rowtype;
    report_id uuid;
    report_passed boolean;
    report_diagnostics jsonb;
    attestation_document jsonb;
    attestation_signature jsonb;
    evidence_size bigint;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' then
        raise exception using errcode = '42501', message = 'service_role_required';
    end if;
    if p_worker_id !~ '^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$' or
       jsonb_typeof(p_report) <> 'object' or octet_length(p_report::text) > 1048576 then
        raise exception using errcode = '22023', message = 'validator_report_invalid';
    end if;
    select upload.* into selected_upload from public.marketplace_uploads upload
    where upload.id = p_upload_id for update;
    if not found or selected_upload.state <> 'leased' or selected_upload.lease_owner <> p_worker_id or
       selected_upload.lease_expires_at <= now() then
        raise exception using errcode = '55000', message = 'validator_lease_lost';
    end if;

    report_diagnostics := p_report -> 'diagnostics';
    begin
        attestation_document := ((p_report -> 'attestation') ->> 'document')::jsonb;
        attestation_signature := (p_report -> 'attestation') -> 'signature';
    exception when others then
        raise exception using errcode = '22023', message = 'validator_attestation_invalid';
    end;
    if coalesce(jsonb_typeof(p_report -> 'passed'), 'missing') <> 'boolean' or
       coalesce(p_report ->> 'schemaVersion', '') <> '2' or
       coalesce(p_report ->> 'packageSha256', '') <> selected_upload.expected_sha256 or
       coalesce(p_report ->> 'validatorVersion', '') !~ '^[A-Za-z0-9][A-Za-z0-9._+-]{0,127}$' or
       coalesce(p_report ->> 'validatorFingerprintSha256', '') !~ '^[0-9a-f]{64}$' or
       coalesce(p_report ->> 'policyVersion', '') !~ '^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$' or
       coalesce(p_report ->> 'malwareScanResult', '') not in ('clean', 'infected', 'error', 'not_applicable') or
       coalesce(p_report ->> 'secretScanResult', '') not in ('clean', 'error', 'not_applicable') or
       coalesce(p_report ->> 'managedValidationResult', '') not in ('passed', 'error', 'not_applicable') or
       coalesce(jsonb_typeof(report_diagnostics), 'missing') <> 'array' or
       coalesce(p_report ->> 'evidenceSha256', '') !~ '^[0-9a-f]{64}$' or
       coalesce(p_report ->> 'evidenceSizeBytes', '') !~ '^[0-9]+$' or
       (p_report ->> 'evidenceSizeBytes')::bigint not between 2 and 8388608 or
       coalesce(p_report ->> 'evidenceStoragePath', '') !~
           '^[0-9a-f-]{36}/review-evidence-[0-9a-f]{64}\.json$' then
        raise exception using errcode = '22023', message = 'validator_report_invalid';
    end if;
    report_passed := (p_report ->> 'passed')::boolean;
    if jsonb_array_length(report_diagnostics) > 1024 or
       (p_report ->> 'manifestSha256' is not null and p_report ->> 'manifestSha256' !~ '^[0-9a-f]{64}$') or
       (p_report ->> 'codeFingerprintSha256' is not null and p_report ->> 'codeFingerprintSha256' !~ '^[0-9a-f]{64}$') or
       (report_passed and (p_report ->> 'malwareScanResult' <> 'clean' or
                           p_report ->> 'secretScanResult' <> 'clean' or
                           p_report ->> 'managedValidationResult' not in ('passed', 'not_applicable') or
                           jsonb_array_length(report_diagnostics) <> 0 or p_report ->> 'manifestSha256' is null)) then
        raise exception using errcode = '22023', message = 'validator_report_invalid';
    end if;

    if coalesce((p_report -> 'attestation' ->> 'schemaVersion')::integer, 0) <> 1 or
       attestation_document ->> 'uploadId' is distinct from selected_upload.id::text or
       attestation_document ->> 'versionId' is distinct from selected_upload.version_id::text or
       attestation_document ->> 'storageBucket' is distinct from selected_upload.storage_bucket or
       attestation_document ->> 'storagePath' is distinct from selected_upload.storage_path or
       attestation_document ->> 'packageSha256' is distinct from selected_upload.expected_sha256 or
       attestation_document ->> 'packageSizeBytes' is distinct from selected_upload.expected_size_bytes::text or
       attestation_document ->> 'manifestSha256' is distinct from p_report ->> 'manifestSha256' or
       attestation_document ->> 'evidenceSha256' is distinct from p_report ->> 'evidenceSha256' or
       attestation_document ->> 'evidenceSizeBytes' is distinct from p_report ->> 'evidenceSizeBytes' or
       attestation_document ->> 'evidenceStoragePath' is distinct from p_report ->> 'evidenceStoragePath' or
       attestation_document ->> 'validatorVersion' is distinct from p_report ->> 'validatorVersion' or
       attestation_document ->> 'validatorFingerprintSha256' is distinct from p_report ->> 'validatorFingerprintSha256' or
       attestation_document ->> 'policyVersion' is distinct from p_report ->> 'policyVersion' or
       (attestation_document ->> 'passed')::boolean is distinct from report_passed or
       attestation_signature ->> 'algorithm' is distinct from 'ed25519' or
       attestation_signature ->> 'keyId' is distinct from attestation_document ->> 'keyId' or
       attestation_signature ->> 'value' !~ '^[A-Za-z0-9+/]{86}==$' or
       not exists (
           select 1 from public.marketplace_validator_attestation_keys key
           where key.key_id = attestation_document ->> 'keyId' and key.active and key.valid_from <= now()
             and (key.valid_until is null or key.valid_until > now())
       ) then
        raise exception using errcode = '22023', message = 'validator_attestation_invalid';
    end if;

    select case when object.metadata ->> 'size' ~ '^[0-9]+$'
        then (object.metadata ->> 'size')::bigint else null end into evidence_size
    from storage.objects object where object.bucket_id = 'marketplace-validation-evidence'
      and object.name = p_report ->> 'evidenceStoragePath';
    if evidence_size is null or evidence_size <> (p_report ->> 'evidenceSizeBytes')::bigint then
        raise exception using errcode = '55000', message = 'validator_evidence_missing';
    end if;

    insert into public.marketplace_validation_reports (
        upload_id, validator_version, validator_fingerprint_sha256, policy_version, package_sha256,
        manifest_sha256, passed, diagnostics, malware_scan_result, secret_scan_result,
        managed_validation_result, code_fingerprint, evidence_storage_path, evidence_sha256,
        evidence_size_bytes, attestation_key_id, signed_attestation, completed_at
    ) values (
        selected_upload.id, p_report ->> 'validatorVersion', p_report ->> 'validatorFingerprintSha256',
        p_report ->> 'policyVersion', p_report ->> 'packageSha256', p_report ->> 'manifestSha256',
        report_passed, report_diagnostics, p_report ->> 'malwareScanResult', p_report ->> 'secretScanResult',
        p_report ->> 'managedValidationResult', p_report ->> 'codeFingerprintSha256',
        p_report ->> 'evidenceStoragePath', p_report ->> 'evidenceSha256',
        (p_report ->> 'evidenceSizeBytes')::bigint, attestation_document ->> 'keyId',
        (p_report -> 'attestation')::text, (p_report ->> 'completedAt')::timestamptz
    ) returning id into report_id;
    update public.marketplace_uploads upload
    set state = case when report_passed then 'validated'::public.marketplace_upload_state
                     else 'failed'::public.marketplace_upload_state end,
        lease_owner = null, lease_expires_at = null where upload.id = selected_upload.id;
    update public.marketplace_product_versions version
    set state = case when report_passed then 'validated'::public.marketplace_version_state
                     else 'validation_failed'::public.marketplace_version_state end,
        archive_storage_bucket = case when report_passed then selected_upload.storage_bucket
                                      else version.archive_storage_bucket end,
        archive_storage_path = case when report_passed then selected_upload.storage_path else version.archive_storage_path end,
        archive_sha256 = case when report_passed then selected_upload.expected_sha256 else version.archive_sha256 end,
        archive_size_bytes = case when report_passed then selected_upload.expected_size_bytes else version.archive_size_bytes end,
        manifest_sha256 = case when report_passed then p_report ->> 'manifestSha256' else version.manifest_sha256 end,
        validator_version = case when report_passed then p_report ->> 'validatorVersion' else version.validator_version end,
        executable_code_fingerprint = case when report_passed then p_report ->> 'codeFingerprintSha256'
                                           else version.executable_code_fingerprint end,
        updated_at = now() where version.id = selected_upload.version_id;
    insert into public.platform_audit_events (action, target_type, target_id, metadata)
    values ('marketplace.validation.completed', 'marketplace_upload', selected_upload.id::text,
            jsonb_build_object('workerId', p_worker_id, 'passed', report_passed, 'reportId', report_id,
                'validatorVersion', p_report ->> 'validatorVersion', 'policyVersion', p_report ->> 'policyVersion',
                'attestationKeyId', attestation_document ->> 'keyId',
                'evidenceSha256', p_report ->> 'evidenceSha256'));
    return report_id;
end;
$$;

create or replace function public.service_decide_marketplace_submission(
    p_actor_user_id uuid,
    p_submission_id uuid,
    p_decision text,
    p_decision_note text
)
returns table (submission_id uuid, submission_state public.marketplace_submission_state)
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_submission public.marketplace_submissions%rowtype;
    selected_version public.marketplace_product_versions%rowtype;
    selected_validation public.marketplace_validation_reports%rowtype;
    next_version_state public.marketplace_version_state;
    next_product_state public.marketplace_product_state;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       not private.service_actor_is_staff(p_actor_user_id, 'moderator') then
        raise exception using errcode = '42501', message = 'staff_moderator_required';
    end if;
    if p_decision not in ('in_review', 'changes_requested', 'approved_pending_signature', 'rejected') or
       char_length(coalesce(p_decision_note, '')) > 10000 or
       (p_decision in ('changes_requested', 'rejected') and
        char_length(btrim(coalesce(p_decision_note, ''))) < 3) then
        raise exception using errcode = '22023', message = 'marketplace_submission_decision_invalid';
    end if;
    if p_decision = 'approved_pending_signature' and
       not private.service_actor_is_staff(p_actor_user_id, 'administrator') then
        raise exception using errcode = '42501', message = 'staff_administrator_required';
    end if;
    select submission.* into selected_submission from public.marketplace_submissions submission
    where submission.id = p_submission_id and submission.state in ('submitted', 'in_review', 'approved_pending_signature')
    for update;
    if not found then raise exception using errcode = 'P0002', message = 'marketplace_submission_not_reviewable'; end if;
    if selected_submission.state = 'approved_pending_signature' and
       (p_decision <> 'rejected' or not private.service_actor_is_staff(p_actor_user_id, 'administrator')) then
        raise exception using errcode = '42501',
            message = 'marketplace_signing_approval_withdrawal_requires_administrator';
    end if;
    select version.* into selected_version from public.marketplace_product_versions version
    where version.id = selected_submission.version_id for update;
    select report.* into selected_validation from public.marketplace_validation_reports report
    where report.id = selected_submission.validation_report_id and report.passed for share;
    if not found then raise exception using errcode = '55000', message = 'marketplace_submission_validation_failed'; end if;
    if p_decision = 'approved_pending_signature' and
       (selected_validation.signed_attestation is null or selected_validation.evidence_sha256 is null or
        selected_validation.attestation_key_id is null) then
        raise exception using errcode = '55000', message = 'marketplace_submission_attestation_required';
    end if;

    next_version_state := case p_decision
        when 'changes_requested' then 'changes_requested'::public.marketplace_version_state
        when 'approved_pending_signature' then 'approved_pending_signature'::public.marketplace_version_state
        when 'rejected' then 'withdrawn'::public.marketplace_version_state
        else 'submitted'::public.marketplace_version_state end;
    next_product_state := case p_decision
        when 'changes_requested' then 'changes_requested'::public.marketplace_product_state
        when 'approved_pending_signature' then 'approved_pending_signature'::public.marketplace_product_state
        when 'rejected' then 'changes_requested'::public.marketplace_product_state
        else 'submitted'::public.marketplace_product_state end;
    update public.marketplace_submissions submission
    set state = p_decision::public.marketplace_submission_state, assigned_to = p_actor_user_id,
        decision_note = nullif(btrim(coalesce(p_decision_note, '')), ''),
        decided_at = case when p_decision = 'in_review' then null else now() end
    where submission.id = selected_submission.id;
    update public.marketplace_product_versions version set state = next_version_state, updated_at = now()
    where version.id = selected_version.id;
    update public.marketplace_products product set state = next_product_state, updated_at = now()
    where product.id = selected_version.product_id;

    if p_decision = 'approved_pending_signature' then
        insert into public.marketplace_publication_jobs (submission_id, version_id, approved_by)
        values (selected_submission.id, selected_version.id, p_actor_user_id)
        on conflict (version_id) do nothing;
    elsif selected_submission.state = 'approved_pending_signature' then
        update public.marketplace_publication_jobs job
        set state = 'cancelled', cancelled_at = now(), cancelled_by = p_actor_user_id,
            lease_owner = null, lease_expires_at = null, updated_at = now()
        where job.version_id = selected_version.id and job.state in ('queued', 'leased', 'retry_wait', 'failed');
    end if;
    insert into public.platform_audit_events (actor_user_id, action, target_type, target_id, metadata)
    values (p_actor_user_id,
            case when selected_submission.state = 'approved_pending_signature'
                then 'marketplace.signing_approval_withdrawn' else 'marketplace.submission_decided' end,
            'marketplace_submission', selected_submission.id::text,
            jsonb_build_object('decision', p_decision, 'previousState', selected_submission.state,
                'versionId', selected_version.id,
                'automaticPublicationQueued', p_decision = 'approved_pending_signature'));
    return query select selected_submission.id, p_decision::public.marketplace_submission_state;
end;
$$;

create or replace function public.service_lease_marketplace_publication(
    p_worker_id text,
    p_signing_key_id text,
    p_lease_seconds integer default 300
)
returns table (
    job_id uuid, version_id uuid, product_id uuid, storage_bucket text, storage_path text,
    artifact_sha256 text, artifact_size_bytes bigint, manifest_sha256 text,
    publication_sequence bigint, publication_expires_at timestamptz,
    validation_attestation text, attestation_key_id text,
    evidence_storage_path text, evidence_sha256 text, evidence_size_bytes bigint
)
language plpgsql
security definer
set search_path = ''
as $$
declare selected_job public.marketplace_publication_jobs%rowtype;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' then
        raise exception using errcode = '42501', message = 'service_role_required';
    end if;
    if p_worker_id !~ '^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$' or
       p_signing_key_id !~ '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$' or p_lease_seconds not between 60 and 900 or
       not exists (select 1 from public.marketplace_signature_keys key where key.key_id = p_signing_key_id
                   and key.active and key.valid_from <= now() and (key.valid_until is null or key.valid_until > now())) then
        raise exception using errcode = '22023', message = 'publication_signer_invalid';
    end if;
    update public.marketplace_publication_jobs stale
    set state = case when stale.attempt_count >= 5 then 'failed'::public.marketplace_publication_job_state
                     else 'retry_wait'::public.marketplace_publication_job_state end,
        retry_after = case when stale.attempt_count >= 5 then null else now() + interval '5 minutes' end,
        lease_owner = null, lease_expires_at = null, last_error_code = 'publication.lease_expired', updated_at = now()
    where stale.state = 'leased' and stale.lease_expires_at <= now();
    select job.* into selected_job from public.marketplace_publication_jobs job
    join public.marketplace_submissions submission on submission.id = job.submission_id
    join public.marketplace_product_versions version on version.id = job.version_id
    where job.state in ('queued', 'retry_wait') and coalesce(job.retry_after, now()) <= now()
      and job.attempt_count < 5 and submission.state = 'approved_pending_signature'
      and version.state = 'approved_pending_signature'
    order by job.created_at, job.id for update of job skip locked limit 1;
    if not found then return; end if;
    update public.marketplace_publication_jobs job
    set state = 'leased', signing_key_id = p_signing_key_id, lease_owner = p_worker_id,
        lease_expires_at = now() + make_interval(secs => p_lease_seconds), retry_after = null,
        attempt_count = job.attempt_count + 1, last_error_code = null, updated_at = now()
    where job.id = selected_job.id returning job.* into selected_job;
    insert into public.platform_audit_events (action, target_type, target_id, metadata)
    values ('marketplace.publication.leased', 'marketplace_publication_job', selected_job.id::text,
            jsonb_build_object('workerId', p_worker_id, 'signingKeyId', p_signing_key_id,
                'attempt', selected_job.attempt_count));
    return query
    select selected_job.id, version.id, version.product_id, upload.storage_bucket, upload.storage_path,
        version.archive_sha256, version.archive_size_bytes, version.manifest_sha256,
        selected_job.publication_sequence, selected_job.publication_expires_at,
        validation.signed_attestation, validation.attestation_key_id,
        validation.evidence_storage_path, validation.evidence_sha256, validation.evidence_size_bytes
    from public.marketplace_submissions submission
    join public.marketplace_product_versions version on version.id = submission.version_id
    join public.marketplace_validation_reports validation on validation.id = submission.validation_report_id
    join public.marketplace_uploads upload on upload.id = validation.upload_id
    where submission.id = selected_job.submission_id;
end;
$$;

create or replace function public.service_renew_marketplace_publication_lease(
    p_job_id uuid,
    p_worker_id text,
    p_lease_seconds integer default 300
)
returns timestamptz
language plpgsql
security definer
set search_path = ''
as $$
declare renewed_until timestamptz;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       p_worker_id !~ '^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$' or p_lease_seconds not between 60 and 900 then
        raise exception using errcode = '42501', message = 'publication_signer_invalid';
    end if;
    update public.marketplace_publication_jobs job
    set lease_expires_at = now() + make_interval(secs => p_lease_seconds), updated_at = now()
    where job.id = p_job_id and job.state = 'leased' and job.lease_owner = p_worker_id
      and job.lease_expires_at > now() returning job.lease_expires_at into renewed_until;
    if not found then raise exception using errcode = '55000', message = 'publication_lease_lost'; end if;
    return renewed_until;
end;
$$;

create or replace function public.service_fail_marketplace_publication(
    p_job_id uuid,
    p_worker_id text,
    p_error_code text,
    p_retryable boolean
)
returns public.marketplace_publication_job_state
language plpgsql
security definer
set search_path = ''
as $$
declare selected_job public.marketplace_publication_jobs%rowtype;
declare next_state public.marketplace_publication_job_state;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       p_worker_id !~ '^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$' or
       p_error_code !~ '^[a-z][a-z0-9._-]{2,127}$' then
        raise exception using errcode = '42501', message = 'publication_signer_invalid';
    end if;
    select job.* into selected_job from public.marketplace_publication_jobs job
    where job.id = p_job_id and job.state = 'leased' and job.lease_owner = p_worker_id
      and job.lease_expires_at > now() for update;
    if not found then raise exception using errcode = '55000', message = 'publication_lease_lost'; end if;
    next_state := case when p_retryable and selected_job.attempt_count < 5
        then 'retry_wait'::public.marketplace_publication_job_state
        else 'failed'::public.marketplace_publication_job_state end;
    update public.marketplace_publication_jobs job
    set state = next_state, lease_owner = null, lease_expires_at = null,
        retry_after = case when next_state = 'retry_wait' then now() +
            case selected_job.attempt_count when 1 then interval '1 minute' when 2 then interval '5 minutes'
                when 3 then interval '15 minutes' else interval '1 hour' end else null end,
        last_error_code = p_error_code, updated_at = now() where job.id = selected_job.id;
    insert into public.platform_audit_events (action, target_type, target_id, metadata)
    values ('marketplace.publication.failed', 'marketplace_publication_job', selected_job.id::text,
            jsonb_build_object('workerId', p_worker_id, 'errorCode', p_error_code,
                'retryable', p_retryable, 'state', next_state, 'attempt', selected_job.attempt_count));
    return next_state;
end;
$$;

create or replace function public.service_publish_marketplace_version_v2(
    p_job_id uuid,
    p_worker_id text,
    p_signed_manifest text
)
returns table (publication_id uuid, version_id uuid, product_id uuid, published_at timestamptz)
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_job public.marketplace_publication_jobs%rowtype;
    selected_version public.marketplace_product_versions%rowtype;
    selected_product public.marketplace_products%rowtype;
    selected_submission public.marketplace_submissions%rowtype;
    selected_validation public.marketplace_validation_reports%rowtype;
    selected_upload public.marketplace_uploads%rowtype;
    publication_document jsonb;
    publication_signature jsonb;
    object_size bigint;
    publication_time timestamptz;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       p_worker_id !~ '^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$' or
       char_length(p_signed_manifest) not between 2 and 65536 then
        raise exception using errcode = '42501', message = 'publication_signer_invalid';
    end if;
    select job.* into selected_job from public.marketplace_publication_jobs job
    where job.id = p_job_id and job.state = 'leased' and job.lease_owner = p_worker_id
      and job.lease_expires_at > now() for update;
    if not found then raise exception using errcode = '55000', message = 'publication_lease_lost'; end if;
    begin
        publication_document := (p_signed_manifest::jsonb ->> 'document')::jsonb;
        publication_signature := p_signed_manifest::jsonb -> 'signature';
    exception when others then
        raise exception using errcode = '22023', message = 'marketplace_signed_manifest_invalid';
    end;
    if (p_signed_manifest::jsonb ->> 'schemaVersion')::integer <> 1 or publication_document is null or
       publication_signature is null or publication_document ->> 'versionId' is distinct from selected_job.version_id::text or
       publication_document ->> 'keyId' is distinct from selected_job.signing_key_id or
       publication_document ->> 'sequence' is distinct from selected_job.publication_sequence::text or
       (publication_document ->> 'expiresAt')::timestamptz is distinct from selected_job.publication_expires_at or
       publication_signature ->> 'algorithm' is distinct from 'ed25519' or
       publication_signature ->> 'keyId' is distinct from selected_job.signing_key_id or
       publication_signature ->> 'sequence' is distinct from selected_job.publication_sequence::text or
       publication_signature ->> 'expiresAt' is distinct from publication_document ->> 'expiresAt' or
       publication_signature ->> 'value' !~ '^[A-Za-z0-9+/]{86}==$' then
        raise exception using errcode = '22023', message = 'marketplace_signed_manifest_invalid';
    end if;
    if not exists (select 1 from public.marketplace_signature_keys key
                   where key.key_id = selected_job.signing_key_id and key.active and key.valid_from <= now()
                     and (key.valid_until is null or key.valid_until > now())) then
        raise exception using errcode = '22023', message = 'marketplace_signature_key_untrusted';
    end if;
    select version.* into selected_version from public.marketplace_product_versions version
    where version.id = selected_job.version_id and version.state = 'approved_pending_signature' for update;
    if not found then raise exception using errcode = '55000', message = 'marketplace_version_not_publishable'; end if;
    select product.* into selected_product from public.marketplace_products product
    where product.id = selected_version.product_id for update;
    select submission.* into selected_submission from public.marketplace_submissions submission
    where submission.id = selected_job.submission_id and submission.state = 'approved_pending_signature' for update;
    if not found then raise exception using errcode = '55000', message = 'marketplace_submission_not_publishable'; end if;
    select validation.* into selected_validation from public.marketplace_validation_reports validation
    where validation.id = selected_submission.validation_report_id and validation.passed for share;
    select upload.* into selected_upload from public.marketplace_uploads upload
    where upload.id = selected_validation.upload_id and upload.state = 'validated' for share;
    if not found or selected_validation.package_sha256 is distinct from selected_version.archive_sha256 or
       selected_validation.manifest_sha256 is distinct from selected_version.manifest_sha256 or
       selected_validation.signed_attestation is null or selected_validation.evidence_sha256 is null then
        raise exception using errcode = '55000', message = 'marketplace_publication_validation_mismatch';
    end if;
    select case when object.metadata ->> 'size' ~ '^[0-9]+$'
        then (object.metadata ->> 'size')::bigint else null end into object_size
    from storage.objects object where object.bucket_id = selected_upload.storage_bucket
      and object.name = selected_upload.storage_path;
    if object_size is null or object_size is distinct from selected_version.archive_size_bytes or
       publication_document ->> 'productId' is distinct from selected_product.id::text or
       publication_document ->> 'artifactSha256' is distinct from selected_version.archive_sha256 or
       publication_document ->> 'artifactSizeBytes' is distinct from selected_version.archive_size_bytes::text or
       publication_document ->> 'manifestSha256' is distinct from selected_version.manifest_sha256 or
       publication_document ->> 'releaseStoragePath' is distinct from selected_upload.storage_path then
        raise exception using errcode = '55000', message = 'marketplace_publication_artifact_mismatch';
    end if;
    insert into public.marketplace_publications
        (version_id, artifact_sha256, manifest_sha256, signature_key_id, signed_manifest, approved_by)
    values (selected_version.id, selected_version.archive_sha256, selected_version.manifest_sha256,
            selected_job.signing_key_id, p_signed_manifest, selected_job.approved_by)
    returning id, marketplace_publications.published_at into publication_id, publication_time;
    update public.marketplace_product_versions version
    set state = 'published', archive_storage_bucket = selected_upload.storage_bucket,
        archive_storage_path = selected_upload.storage_path, signature_key_id = selected_job.signing_key_id,
        published_at = publication_time, updated_at = now() where version.id = selected_version.id;
    update public.marketplace_products product
    set state = 'published', published_at = coalesce(product.published_at, publication_time), updated_at = now()
    where product.id = selected_product.id;
    update public.marketplace_submissions submission set state = 'approved', decided_at = coalesce(decided_at, now())
    where submission.id = selected_submission.id;
    update public.marketplace_publication_jobs job
    set state = 'published', published_at = publication_time, lease_owner = null, lease_expires_at = null,
        updated_at = now() where job.id = selected_job.id;
    insert into public.platform_audit_events (actor_user_id, action, target_type, target_id, metadata)
    values (selected_job.approved_by, 'marketplace.version_published', 'marketplace_product_version', selected_version.id::text,
            jsonb_build_object('publicationId', publication_id, 'publicationJobId', selected_job.id,
                'productId', selected_product.id, 'artifactSha256', selected_version.archive_sha256,
                'manifestSha256', selected_version.manifest_sha256, 'signatureKeyId', selected_job.signing_key_id,
                'storageBucket', selected_upload.storage_bucket, 'storagePath', selected_upload.storage_path,
                'workerId', p_worker_id));
    version_id := selected_version.id; product_id := selected_product.id; published_at := publication_time;
    return next;
end;
$$;

create or replace function public.service_issue_marketplace_download_grant_v3(
    p_actor_user_id uuid,
    p_actor_session_id text,
    p_version_id uuid,
    p_device_session_id uuid,
    p_organization_id uuid default null
)
returns table (
    grant_id uuid, storage_bucket text, storage_path text, archive_sha256 text,
    archive_size_bytes bigint, expires_at timestamptz, signed_publication text
)
language plpgsql
security definer
set search_path = ''
as $$
declare issued record;
declare selected_bucket text;
declare publication_envelope text;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       not exists (select 1 from auth.users where id = p_actor_user_id) or
       char_length(p_actor_session_id) not between 16 and 256 then
        raise exception using errcode = '42501', message = 'service_actor_invalid';
    end if;
    perform set_config('request.jwt.claim.sub', p_actor_user_id::text, true);
    perform set_config('request.jwt.claim.role', 'authenticated', true);
    perform set_config('request.jwt.claim', jsonb_build_object(
        'sub', p_actor_user_id, 'role', 'authenticated', 'session_id', p_actor_session_id)::text, true);
    perform set_config('request.jwt.claims', jsonb_build_object(
        'sub', p_actor_user_id, 'role', 'authenticated', 'session_id', p_actor_session_id)::text, true);
    select * into strict issued from public.issue_marketplace_download_grant(
        p_version_id, p_device_session_id, p_organization_id);
    select version.archive_storage_bucket, publication.signed_manifest
    into selected_bucket, publication_envelope
    from public.marketplace_product_versions version
    join public.marketplace_publications publication on publication.version_id = version.id
    where version.id = p_version_id and publication.artifact_sha256 = issued.archive_sha256
      and publication.manifest_sha256 = version.manifest_sha256
      and publication.signature_key_id = version.signature_key_id
      and version.archive_storage_path = issued.storage_path
      and version.archive_size_bytes = issued.archive_size_bytes and version.security_revoked_at is null;
    if selected_bucket is null or publication_envelope is null then
        raise exception using errcode = '55000', message = 'marketplace_publication_invalid';
    end if;
    grant_id := issued.grant_id; storage_bucket := selected_bucket; storage_path := issued.storage_path;
    archive_sha256 := issued.archive_sha256; archive_size_bytes := issued.archive_size_bytes;
    expires_at := issued.expires_at; signed_publication := publication_envelope; return next;
end;
$$;

revoke all on function public.service_cancel_marketplace_upload_v2(uuid, uuid) from public, anon, authenticated;
revoke all on function public.service_lease_marketplace_upload_v2(text, integer) from public, anon, authenticated;
revoke all on function public.service_complete_marketplace_validation_v2(uuid, text, jsonb) from public, anon, authenticated;
revoke all on function public.service_lease_marketplace_publication(text, text, integer) from public, anon, authenticated;
revoke all on function public.service_renew_marketplace_publication_lease(uuid, text, integer) from public, anon, authenticated;
revoke all on function public.service_fail_marketplace_publication(uuid, text, text, boolean) from public, anon, authenticated;
revoke all on function public.service_publish_marketplace_version_v2(uuid, text, text) from public, anon, authenticated;
revoke all on function public.service_issue_marketplace_download_grant_v3(uuid, text, uuid, uuid, uuid)
    from public, anon, authenticated;
grant execute on function public.service_cancel_marketplace_upload_v2(uuid, uuid) to service_role;
grant execute on function public.service_lease_marketplace_upload_v2(text, integer) to service_role;
grant execute on function public.service_complete_marketplace_validation_v2(uuid, text, jsonb) to service_role;
grant execute on function public.service_lease_marketplace_publication(text, text, integer) to service_role;
grant execute on function public.service_renew_marketplace_publication_lease(uuid, text, integer) to service_role;
grant execute on function public.service_fail_marketplace_publication(uuid, text, text, boolean) to service_role;
grant execute on function public.service_publish_marketplace_version_v2(uuid, text, text) to service_role;
grant execute on function public.service_issue_marketplace_download_grant_v3(uuid, text, uuid, uuid, uuid) to service_role;

comment on table public.marketplace_validator_attestation_keys is
    'Rotatable public Ed25519 keys used to verify reports produced by the isolated Marketplace validator.';
comment on table public.marketplace_publication_jobs is
    'Audited, leased automatic-signing queue. Workers receive approved metadata and never receive package bytes.';
comment on function public.service_publish_marketplace_version_v2(uuid, text, text) is
    'Atomically publishes an already-validated immutable object after the automatic signer signature is verified.';
