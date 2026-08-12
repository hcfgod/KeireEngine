alter table public.marketplace_validation_reports
    add column if not exists manifest_sha256 text,
    add column if not exists validator_fingerprint_sha256 text,
    add column if not exists policy_version text not null default 'legacy',
    add column if not exists secret_scan_result text not null default 'not_applicable',
    add column if not exists managed_validation_result text not null default 'not_applicable';

alter table public.marketplace_uploads
    add column if not exists validation_attempts smallint not null default 0;

alter table public.marketplace_validation_reports
    add constraint marketplace_validation_manifest_sha
        check (manifest_sha256 is null or manifest_sha256 ~ '^[0-9a-f]{64}$'),
    add constraint marketplace_validation_validator_sha
        check (validator_fingerprint_sha256 is null or validator_fingerprint_sha256 ~ '^[0-9a-f]{64}$'),
    add constraint marketplace_validation_policy_version
        check (char_length(policy_version) between 1 and 128),
    add constraint marketplace_validation_secret_scan
        check (secret_scan_result in ('clean', 'error', 'not_applicable')),
    add constraint marketplace_validation_managed
        check (managed_validation_result in ('passed', 'error', 'not_applicable')),
    add constraint marketplace_validation_code_sha
        check (code_fingerprint is null or code_fingerprint ~ '^[0-9a-f]{64}$');
alter table public.marketplace_uploads
    add constraint marketplace_uploads_validation_attempts check (validation_attempts between 0 and 5);

create index if not exists idx_marketplace_uploads_validator_queue
    on public.marketplace_uploads (uploaded_at, id)
    where state = 'uploaded';
create index if not exists idx_marketplace_uploads_expired_lease
    on public.marketplace_uploads (lease_expires_at, id)
    where state = 'leased';
create unique index if not exists marketplace_uploads_one_active_per_version
    on public.marketplace_uploads (version_id)
    where state in ('created', 'uploading', 'uploaded', 'leased');

create or replace function public.service_lease_marketplace_upload(
    p_worker_id text,
    p_lease_seconds integer default 900)
returns table (
    upload_id uuid,
    version_id uuid,
    storage_path text,
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
    if (select auth.role()) <> 'service_role' then
        raise exception using errcode = '42501', message = 'service_role_required';
    end if;
    if p_worker_id !~ '^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$' or p_lease_seconds not between 60 and 1800 then
        raise exception using errcode = '22023', message = 'validator_lease_invalid';
    end if;

    with recovered as (
        update public.marketplace_uploads as stale
        set state = case when stale.validation_attempts >= 5 then 'failed'::public.marketplace_upload_state
                         else 'uploaded'::public.marketplace_upload_state end,
            lease_owner = null,
            lease_expires_at = null
        where stale.state = 'leased' and stale.lease_expires_at <= now()
        returning stale.id, stale.version_id, stale.state
    ), recovered_versions as (
        update public.marketplace_product_versions as version
        set state = case when recovered.state = 'failed' then 'validation_failed'::public.marketplace_version_state
                         else 'uploaded'::public.marketplace_version_state end,
            updated_at = now()
        from recovered
        where version.id = recovered.version_id and version.state = 'validating'
        returning recovered.id, recovered.state
    )
    insert into public.platform_audit_events (action, target_type, target_id, metadata)
    select 'marketplace.validation.retry_exhausted', 'marketplace_upload', recovered_versions.id::text,
           jsonb_build_object('attempts', 5)
    from recovered_versions
    where recovered_versions.state = 'failed';

    select upload.*
    into selected_upload
    from public.marketplace_uploads as upload
    join public.marketplace_product_versions as version on version.id = upload.version_id
    where upload.state = 'uploaded'
      and upload.uploaded_at is not null
      and upload.expires_at > now()
      and upload.validation_attempts < 5
      and version.state in ('draft', 'uploaded', 'validation_failed')
      and not exists (
          select 1 from public.marketplace_validation_reports as report where report.upload_id = upload.id)
    order by upload.uploaded_at, upload.id
    for update of upload skip locked
    limit 1;

    if not found then
        return;
    end if;

    update public.marketplace_uploads as upload
    set state = 'leased',
        lease_owner = p_worker_id,
        lease_expires_at = now() + make_interval(secs => p_lease_seconds),
        validation_attempts = upload.validation_attempts + 1
    where upload.id = selected_upload.id
    returning upload.lease_expires_at into selected_upload.lease_expires_at;

    update public.marketplace_product_versions as version
    set state = 'validating', updated_at = now()
    where version.id = selected_upload.version_id;

    insert into public.platform_audit_events (action, target_type, target_id, metadata)
    values ('marketplace.validation.leased', 'marketplace_upload', selected_upload.id::text,
            jsonb_build_object('workerId', p_worker_id, 'leaseSeconds', p_lease_seconds));

    return query
    select selected_upload.id,
           selected_upload.version_id,
           selected_upload.storage_path,
           selected_upload.expected_size_bytes,
           selected_upload.expected_sha256,
           selected_upload.lease_expires_at;
end;
$$;

create or replace function public.service_complete_marketplace_validation(
    p_upload_id uuid,
    p_worker_id text,
    p_report jsonb)
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
begin
    if (select auth.role()) <> 'service_role' then
        raise exception using errcode = '42501', message = 'service_role_required';
    end if;
    if p_worker_id !~ '^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$' or
       jsonb_typeof(p_report) <> 'object' or octet_length(p_report::text) > 1048576 then
        raise exception using errcode = '22023', message = 'validator_report_invalid';
    end if;

    select upload.*
    into selected_upload
    from public.marketplace_uploads as upload
    where upload.id = p_upload_id
    for update;
    if not found or selected_upload.state <> 'leased' or selected_upload.lease_owner <> p_worker_id or
       selected_upload.lease_expires_at <= now() then
        raise exception using errcode = '55000', message = 'validator_lease_lost';
    end if;

    report_diagnostics := p_report -> 'diagnostics';
    if coalesce(jsonb_typeof(p_report -> 'passed'), 'missing') <> 'boolean' or
       coalesce(p_report ->> 'schemaVersion', '') <> '1' or
       coalesce(p_report ->> 'packageSha256', '') <> selected_upload.expected_sha256 or
       coalesce(p_report ->> 'validatorVersion', '') !~ '^[A-Za-z0-9][A-Za-z0-9._+-]{0,127}$' or
       coalesce(p_report ->> 'validatorFingerprintSha256', '') !~ '^[0-9a-f]{64}$' or
       coalesce(p_report ->> 'policyVersion', '') !~ '^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$' or
       coalesce(p_report ->> 'malwareScanResult', '') not in ('clean', 'infected', 'error', 'not_applicable') or
       coalesce(p_report ->> 'secretScanResult', '') not in ('clean', 'error', 'not_applicable') or
       coalesce(p_report ->> 'managedValidationResult', '') not in ('passed', 'error', 'not_applicable') or
       coalesce(jsonb_typeof(report_diagnostics), 'missing') <> 'array' then
        raise exception using errcode = '22023', message = 'validator_report_invalid';
    end if;
    report_passed := (p_report ->> 'passed')::boolean;
    if jsonb_array_length(report_diagnostics) > 1024 or
       (p_report ->> 'manifestSha256' is not null and p_report ->> 'manifestSha256' !~ '^[0-9a-f]{64}$') or
       (p_report ->> 'codeFingerprintSha256' is not null and
        p_report ->> 'codeFingerprintSha256' !~ '^[0-9a-f]{64}$') or
       (report_passed and (p_report ->> 'malwareScanResult' <> 'clean' or
                           p_report ->> 'secretScanResult' <> 'clean' or
                           p_report ->> 'managedValidationResult' not in ('passed', 'not_applicable') or
                           jsonb_array_length(report_diagnostics) <> 0)) then
        raise exception using errcode = '22023', message = 'validator_report_invalid';
    end if;
    if report_passed and p_report ->> 'manifestSha256' is null then
        raise exception using errcode = '22023', message = 'validator_report_invalid';
    end if;

    insert into public.marketplace_validation_reports (
        upload_id,
        validator_version,
        validator_fingerprint_sha256,
        policy_version,
        package_sha256,
        manifest_sha256,
        passed,
        diagnostics,
        malware_scan_result,
        secret_scan_result,
        managed_validation_result,
        code_fingerprint,
        completed_at)
    values (
        selected_upload.id,
        p_report ->> 'validatorVersion',
        p_report ->> 'validatorFingerprintSha256',
        p_report ->> 'policyVersion',
        p_report ->> 'packageSha256',
        p_report ->> 'manifestSha256',
        report_passed,
        report_diagnostics,
        p_report ->> 'malwareScanResult',
        p_report ->> 'secretScanResult',
        p_report ->> 'managedValidationResult',
        p_report ->> 'codeFingerprintSha256',
        now())
    returning id into report_id;

    update public.marketplace_uploads as upload
    set state = case when report_passed then 'validated'::public.marketplace_upload_state
                     else 'failed'::public.marketplace_upload_state end,
        lease_owner = null,
        lease_expires_at = null
    where upload.id = selected_upload.id;

    update public.marketplace_product_versions as version
    set state = case when report_passed then 'validated'::public.marketplace_version_state
                     else 'validation_failed'::public.marketplace_version_state end,
        archive_storage_path = case when report_passed then selected_upload.storage_path
                                    else version.archive_storage_path end,
        archive_sha256 = case when report_passed then selected_upload.expected_sha256 else version.archive_sha256 end,
        archive_size_bytes = case when report_passed then selected_upload.expected_size_bytes
                                  else version.archive_size_bytes end,
        manifest_sha256 = case when report_passed then p_report ->> 'manifestSha256' else version.manifest_sha256 end,
        validator_version = case when report_passed then p_report ->> 'validatorVersion'
                                 else version.validator_version end,
        executable_code_fingerprint = case when report_passed then p_report ->> 'codeFingerprintSha256'
                                           else version.executable_code_fingerprint end,
        updated_at = now()
    where version.id = selected_upload.version_id;

    insert into public.platform_audit_events (action, target_type, target_id, metadata)
    values ('marketplace.validation.completed', 'marketplace_upload', selected_upload.id::text,
            jsonb_build_object('workerId', p_worker_id,
                               'passed', report_passed,
                               'reportId', report_id,
                               'validatorVersion', p_report ->> 'validatorVersion',
                               'policyVersion', p_report ->> 'policyVersion'));
    return report_id;
end;
$$;

create or replace function public.service_renew_marketplace_upload_lease(
    p_upload_id uuid,
    p_worker_id text,
    p_lease_seconds integer default 900)
returns timestamptz
language plpgsql
security definer
set search_path = ''
as $$
declare
    renewed_until timestamptz;
begin
    if (select auth.role()) <> 'service_role' then
        raise exception using errcode = '42501', message = 'service_role_required';
    end if;
    if p_worker_id !~ '^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$' or p_lease_seconds not between 60 and 1800 then
        raise exception using errcode = '22023', message = 'validator_lease_invalid';
    end if;

    update public.marketplace_uploads as upload
    set lease_expires_at = now() + make_interval(secs => p_lease_seconds)
    where upload.id = p_upload_id
      and upload.state = 'leased'
      and upload.lease_owner = p_worker_id
      and upload.lease_expires_at > now()
    returning upload.lease_expires_at into renewed_until;
    if not found then
        raise exception using errcode = '55000', message = 'validator_lease_lost';
    end if;
    return renewed_until;
end;
$$;

revoke all on function public.service_lease_marketplace_upload(text, integer)
    from public, anon, authenticated;
revoke all on function public.service_renew_marketplace_upload_lease(uuid, text, integer)
    from public, anon, authenticated;
revoke all on function public.service_complete_marketplace_validation(uuid, text, jsonb)
    from public, anon, authenticated;
grant execute on function public.service_lease_marketplace_upload(text, integer) to service_role;
grant execute on function public.service_renew_marketplace_upload_lease(uuid, text, integer) to service_role;
grant execute on function public.service_complete_marketplace_validation(uuid, text, jsonb) to service_role;

comment on function public.service_lease_marketplace_upload(text, integer) is
    'Service-role-only atomic queue lease with stale-lease recovery for the networked validator broker.';
comment on function public.service_complete_marketplace_validation(uuid, text, jsonb) is
    'Service-role-only atomic validation report commit. The offline validator never receives this credential.';
comment on function public.service_renew_marketplace_upload_lease(uuid, text, integer) is
    'Service-role-only validator lease renewal for bounded downloads and offline validation.';
