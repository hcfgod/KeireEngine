-- Replace deprecated role helpers without rewriting applied migration history.
-- These SECURITY DEFINER adapters remain callable only by service_role and retain a JWT role assertion as defense in depth.

create or replace function public.service_create_marketplace_organization(
    p_actor_user_id uuid,
    p_slug text,
    p_display_name text
)
returns uuid
language plpgsql
security definer
set search_path = ''
as $$
declare
    result_id uuid;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       not exists (select 1 from auth.users where id = p_actor_user_id) then
        raise exception using errcode = '42501', message = 'service_actor_invalid';
    end if;
    if not coalesce((select enabled from public.platform_feature_flags
                     where key in ('marketplace_enabled', 'publisher_portal_enabled') and enabled limit 1), false) then
        raise exception using errcode = '55000', message = 'marketplace_disabled';
    end if;
    perform set_config('request.jwt.claim.sub', p_actor_user_id::text, true);
    perform set_config('request.jwt.claim.role', 'authenticated', true);
    perform set_config('request.jwt.claim',
        jsonb_build_object('sub', p_actor_user_id, 'role', 'authenticated')::text, true);
    perform set_config('request.jwt.claims',
        jsonb_build_object('sub', p_actor_user_id, 'role', 'authenticated')::text, true);
    result_id := public.create_marketplace_organization(p_slug, p_display_name);
    return result_id;
end;
$$;

create or replace function public.service_claim_free_marketplace_product(
    p_actor_user_id uuid,
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
    result_id uuid;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       not exists (select 1 from auth.users where id = p_actor_user_id) then
        raise exception using errcode = '42501', message = 'service_actor_invalid';
    end if;
    perform set_config('request.jwt.claim.sub', p_actor_user_id::text, true);
    perform set_config('request.jwt.claim.role', 'authenticated', true);
    perform set_config('request.jwt.claim',
        jsonb_build_object('sub', p_actor_user_id, 'role', 'authenticated')::text, true);
    perform set_config('request.jwt.claims',
        jsonb_build_object('sub', p_actor_user_id, 'role', 'authenticated')::text, true);
    result_id := public.claim_free_marketplace_product(
        p_product_id, p_organization_id, p_idempotency_key, p_license_spdx,
        p_license_revision, p_accepted_license_snapshot);
    return result_id;
end;
$$;

create or replace function public.service_register_marketplace_device_session(
    p_actor_user_id uuid,
    p_actor_session_id text,
    p_device_name text
)
returns uuid
language plpgsql
security definer
set search_path = ''
as $$
declare
    result_id uuid;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       not exists (select 1 from auth.users where id = p_actor_user_id) or
       char_length(p_actor_session_id) not between 16 and 256 then
        raise exception using errcode = '42501', message = 'service_actor_invalid';
    end if;
    if not coalesce((select enabled from public.platform_feature_flags where key = 'hub_oauth_sso_enabled'), false) then
        raise exception using errcode = '55000', message = 'hub_oauth_disabled';
    end if;
    perform set_config('request.jwt.claim.sub', p_actor_user_id::text, true);
    perform set_config('request.jwt.claim.role', 'authenticated', true);
    perform set_config('request.jwt.claim', jsonb_build_object(
        'sub', p_actor_user_id, 'role', 'authenticated', 'session_id', p_actor_session_id)::text, true);
    perform set_config('request.jwt.claims', jsonb_build_object(
        'sub', p_actor_user_id, 'role', 'authenticated', 'session_id', p_actor_session_id)::text, true);
    result_id := public.register_marketplace_device_session(p_actor_session_id, 'hub', p_device_name);
    return result_id;
end;
$$;

create or replace function public.service_issue_marketplace_download_grant(
    p_actor_user_id uuid,
    p_actor_session_id text,
    p_version_id uuid,
    p_device_session_id uuid,
    p_organization_id uuid default null
)
returns table (
    grant_id uuid,
    storage_path text,
    archive_sha256 text,
    archive_size_bytes bigint,
    expires_at timestamptz
)
language plpgsql
security definer
set search_path = ''
as $$
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
    return query select * from public.issue_marketplace_download_grant(
        p_version_id, p_device_session_id, p_organization_id);
end;
$$;

create or replace function public.service_submit_publisher_application(
    p_actor_user_id uuid,
    p_application_id uuid
)
returns uuid
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_organization_id uuid;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       not exists (select 1 from auth.users where id = p_actor_user_id) then
        raise exception using errcode = '42501', message = 'service_actor_invalid';
    end if;
    if not coalesce((select enabled from public.platform_feature_flags
                     where key = 'publisher_portal_enabled'), false) then
        raise exception using errcode = '55000', message = 'publisher_portal_disabled';
    end if;

    select application.organization_id into selected_organization_id
    from public.publisher_applications application
    where application.id = p_application_id and
          application.applicant_user_id = p_actor_user_id and
          application.state in ('draft', 'changes_requested')
    for update;
    if not found then
        raise exception using errcode = 'P0002', message = 'publisher_application_not_editable';
    end if;
    if selected_organization_id is null or not exists (
        select 1 from public.organization_memberships membership
        where membership.organization_id = selected_organization_id and
              membership.user_id = p_actor_user_id and
              membership.role in ('owner', 'admin')
    ) then
        raise exception using errcode = '42501', message = 'organization_authorization_required';
    end if;

    update public.publisher_applications
    set state = 'submitted', submitted_at = now(), reviewed_at = null,
        reviewed_by = null, decision_note = null, updated_at = now()
    where id = p_application_id;

    insert into public.platform_audit_events (actor_user_id, action, target_type, target_id)
    values (p_actor_user_id, 'publisher.application_submitted', 'publisher_application', p_application_id::text);
    return p_application_id;
end;
$$;

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
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' then
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
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' then
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
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' then
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

create or replace function public.service_issue_marketplace_download_grant_v2(
    p_actor_user_id uuid,
    p_actor_session_id text,
    p_version_id uuid,
    p_device_session_id uuid,
    p_organization_id uuid default null
)
returns table (
    grant_id uuid,
    storage_path text,
    archive_sha256 text,
    archive_size_bytes bigint,
    expires_at timestamptz,
    signed_publication text
)
language plpgsql
security definer
set search_path = ''
as $$
declare
    issued record;
    publication_envelope text;
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

    select * into strict issued
    from public.issue_marketplace_download_grant(p_version_id, p_device_session_id, p_organization_id);

    select publication.signed_manifest into publication_envelope
    from public.marketplace_publications publication
    join public.marketplace_product_versions version on version.id = publication.version_id
    where publication.version_id = p_version_id
      and publication.artifact_sha256 = issued.archive_sha256
      and publication.manifest_sha256 = version.manifest_sha256
      and publication.signature_key_id = version.signature_key_id
      and version.archive_storage_path = issued.storage_path
      and version.archive_size_bytes = issued.archive_size_bytes
      and version.security_revoked_at is null;

    if publication_envelope is null then
        raise exception using errcode = '55000', message = 'marketplace_publication_invalid';
    end if;

    grant_id := issued.grant_id;
    storage_path := issued.storage_path;
    archive_sha256 := issued.archive_sha256;
    archive_size_bytes := issued.archive_size_bytes;
    expires_at := issued.expires_at;
    signed_publication := publication_envelope;
    return next;
end;
$$;

revoke all on function public.service_create_marketplace_organization(uuid, text, text)
    from public, anon, authenticated;
revoke all on function public.service_claim_free_marketplace_product(uuid, uuid, uuid, text, text, text, text)
    from public, anon, authenticated;
revoke all on function public.service_register_marketplace_device_session(uuid, text, text)
    from public, anon, authenticated;
revoke all on function public.service_issue_marketplace_download_grant(uuid, text, uuid, uuid, uuid)
    from public, anon, authenticated;
revoke all on function public.service_submit_publisher_application(uuid, uuid)
    from public, anon, authenticated;
revoke all on function public.service_lease_marketplace_upload(text, integer)
    from public, anon, authenticated;
revoke all on function public.service_complete_marketplace_validation(uuid, text, jsonb)
    from public, anon, authenticated;
revoke all on function public.service_renew_marketplace_upload_lease(uuid, text, integer)
    from public, anon, authenticated;
revoke all on function public.service_issue_marketplace_download_grant_v2(uuid, text, uuid, uuid, uuid)
    from public, anon, authenticated;

grant execute on function public.service_create_marketplace_organization(uuid, text, text) to service_role;
grant execute on function public.service_claim_free_marketplace_product(uuid, uuid, uuid, text, text, text, text)
    to service_role;
grant execute on function public.service_register_marketplace_device_session(uuid, text, text) to service_role;
grant execute on function public.service_issue_marketplace_download_grant(uuid, text, uuid, uuid, uuid)
    to service_role;
grant execute on function public.service_submit_publisher_application(uuid, uuid) to service_role;
grant execute on function public.service_lease_marketplace_upload(text, integer) to service_role;
grant execute on function public.service_complete_marketplace_validation(uuid, text, jsonb) to service_role;
grant execute on function public.service_renew_marketplace_upload_lease(uuid, text, integer) to service_role;
grant execute on function public.service_issue_marketplace_download_grant_v2(uuid, text, uuid, uuid, uuid)
    to service_role;
