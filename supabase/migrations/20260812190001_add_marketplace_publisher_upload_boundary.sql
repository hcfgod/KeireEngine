-- Transactional, service-role-only publisher upload boundaries. The website and
-- Edge Function must verify the user JWT and AAL2 before calling these functions.
-- Rollback: disable publisher_portal_enabled, revoke these functions, and drop them.

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
returns table (
    upload_id uuid,
    version_id uuid,
    storage_path text,
    expires_at timestamptz
)
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
       p_expected_size_bytes not between 1 and 68719476736 or
       p_expected_sha256 !~ '^[0-9a-f]{64}$' or
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
        select 1
        from public.marketplace_products product
        join public.publishers publisher on publisher.id = product.publisher_id
        join public.organization_memberships membership
          on membership.organization_id = publisher.organization_id
        where product.id = p_product_id
          and product.state in ('draft', 'changes_requested')
          and publisher.suspended_at is null
          and membership.user_id = p_actor_user_id
          and membership.role in ('owner', 'admin')
    ) then
        raise exception using errcode = '42501', message = 'publisher_product_not_editable';
    end if;

    select version.id, version.state
    into v_version_id, v_version_state
    from public.marketplace_product_versions version
    where version.product_id = p_product_id and version.version = p_version
    for update;

    if found then
        if v_version_state not in ('draft', 'validation_failed', 'changes_requested') then
            raise exception using errcode = '55000', message = 'publisher_version_not_editable';
        end if;
        update public.marketplace_product_versions version
        set state = 'draft',
            install_kind = p_install_kind,
            minimum_engine_version = p_minimum_engine_version,
            maximum_engine_version = p_maximum_engine_version,
            platforms = coalesce(p_platforms, '{}'::text[]),
            architectures = coalesce(p_architectures, '{}'::text[]),
            renderer_capabilities = coalesce(p_renderer_capabilities, '{}'::text[]),
            managed_api_version = nullif(p_managed_api_version, ''),
            release_notes_markdown = coalesce(p_release_notes_markdown, ''),
            archive_storage_path = null,
            archive_sha256 = null,
            archive_size_bytes = null,
            manifest_sha256 = null,
            signature_key_id = null,
            validator_version = null,
            executable_code_fingerprint = null,
            updated_at = now()
        where version.id = v_version_id;
    else
        insert into public.marketplace_product_versions (
            product_id, version, state, install_kind, minimum_engine_version,
            maximum_engine_version, platforms, architectures, renderer_capabilities,
            managed_api_version, release_notes_markdown
        ) values (
            p_product_id, p_version, 'draft', p_install_kind, p_minimum_engine_version,
            p_maximum_engine_version, coalesce(p_platforms, '{}'::text[]),
            coalesce(p_architectures, '{}'::text[]),
            coalesce(p_renderer_capabilities, '{}'::text[]), nullif(p_managed_api_version, ''),
            coalesce(p_release_notes_markdown, '')
        ) returning id into v_version_id;
    end if;

    update public.marketplace_uploads upload
    set state = 'expired', lease_owner = null, lease_expires_at = null
    where upload.version_id = v_version_id
      and upload.state in ('created', 'uploading')
      and upload.expires_at <= now();
    if exists (
        select 1 from public.marketplace_uploads upload
        where upload.version_id = v_version_id
          and upload.state in ('created', 'uploading', 'uploaded', 'leased')
    ) then
        raise exception using errcode = '55000', message = 'publisher_upload_already_active';
    end if;

    v_storage_path := p_product_id::text || '/' || v_version_id::text || '/' ||
                      v_upload_id::text || '.keireassetpackage';
    insert into public.marketplace_uploads (
        id, version_id, created_by, storage_path, expected_size_bytes,
        expected_sha256, state, expires_at
    ) values (
        v_upload_id, v_version_id, p_actor_user_id, v_storage_path,
        p_expected_size_bytes, p_expected_sha256, 'created', v_expires_at
    );

    insert into public.platform_audit_events (
        actor_user_id, action, target_type, target_id, metadata
    ) values (
        p_actor_user_id, 'marketplace.upload_reserved', 'marketplace_upload', v_upload_id::text,
        jsonb_build_object('productId', p_product_id, 'versionId', v_version_id,
                           'expectedSizeBytes', p_expected_size_bytes,
                           'expectedSha256', p_expected_sha256)
    );

    return query select v_upload_id, v_version_id, v_storage_path, v_expires_at;
end;
$$;

create or replace function public.service_complete_marketplace_upload(
    p_actor_user_id uuid,
    p_upload_id uuid
)
returns table (
    upload_id uuid,
    version_id uuid,
    state public.marketplace_upload_state
)
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
    join public.organization_memberships membership
      on membership.organization_id = publisher.organization_id
    where upload.id = p_upload_id
      and upload.created_by = p_actor_user_id
      and upload.state in ('created', 'uploading')
      and upload.expires_at > now()
      and membership.user_id = p_actor_user_id
      and membership.role in ('owner', 'admin')
      and publisher.suspended_at is null
    for update of upload;
    if not found then
        raise exception using errcode = 'P0002', message = 'publisher_upload_not_completable';
    end if;

    select case
        when object.metadata ->> 'size' ~ '^[0-9]+$' then (object.metadata ->> 'size')::bigint
        else null
    end into object_size
    from storage.objects object
    where object.bucket_id = 'marketplace-quarantine'
      and object.name = selected_upload.storage_path;
    if object_size is null or object_size <> selected_upload.expected_size_bytes then
        raise exception using errcode = '55000', message = 'publisher_upload_size_mismatch';
    end if;

    update public.marketplace_uploads upload
    set state = 'uploaded', uploaded_at = now(), lease_owner = null, lease_expires_at = null
    where upload.id = selected_upload.id;
    update public.marketplace_product_versions version
    set state = 'uploaded', updated_at = now()
    where version.id = selected_upload.version_id;
    insert into public.platform_audit_events (
        actor_user_id, action, target_type, target_id, metadata
    ) values (
        p_actor_user_id, 'marketplace.upload_completed', 'marketplace_upload', selected_upload.id::text,
        jsonb_build_object('versionId', selected_upload.version_id,
                           'sizeBytes', selected_upload.expected_size_bytes,
                           'sha256', selected_upload.expected_sha256)
    );

    return query select selected_upload.id, selected_upload.version_id,
                        'uploaded'::public.marketplace_upload_state;
end;
$$;

create or replace function public.service_cancel_marketplace_upload(
    p_actor_user_id uuid,
    p_upload_id uuid
)
returns text
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
    join public.organization_memberships membership
      on membership.organization_id = publisher.organization_id
    where upload.id = p_upload_id
      and upload.created_by = p_actor_user_id
      and upload.state in ('created', 'uploading')
      and membership.user_id = p_actor_user_id
      and membership.role in ('owner', 'admin')
    for update of upload;
    if not found then
        raise exception using errcode = 'P0002', message = 'publisher_upload_not_cancellable';
    end if;

    update public.marketplace_uploads upload
    set state = 'expired', lease_owner = null, lease_expires_at = null
    where upload.id = selected_upload.id;
    update public.marketplace_product_versions version
    set state = 'draft', updated_at = now()
    where version.id = selected_upload.version_id and version.state = 'draft';
    insert into public.platform_audit_events (actor_user_id, action, target_type, target_id)
    values (p_actor_user_id, 'marketplace.upload_cancelled', 'marketplace_upload', selected_upload.id::text);
    return selected_upload.storage_path;
end;
$$;

revoke all on function public.service_reserve_marketplace_upload(
    uuid, uuid, text, public.marketplace_install_kind, text, text, text[], text[], text[], text, text, bigint, text
) from public, anon, authenticated;
revoke all on function public.service_complete_marketplace_upload(uuid, uuid)
    from public, anon, authenticated;
revoke all on function public.service_cancel_marketplace_upload(uuid, uuid)
    from public, anon, authenticated;
grant execute on function public.service_reserve_marketplace_upload(
    uuid, uuid, text, public.marketplace_install_kind, text, text, text[], text[], text[], text, text, bigint, text
) to service_role;
grant execute on function public.service_complete_marketplace_upload(uuid, uuid) to service_role;
grant execute on function public.service_cancel_marketplace_upload(uuid, uuid) to service_role;

comment on function public.service_reserve_marketplace_upload(
    uuid, uuid, text, public.marketplace_install_kind, text, text, text[], text[], text[], text, text, bigint, text
) is 'Service-role-only upload reservation. The publisher Edge Function must verify JWT, AAL2, and return only a path-scoped upload grant.';
comment on function public.service_complete_marketplace_upload(uuid, uuid) is
    'Service-role-only upload completion. It requires an exact-size object in the private quarantine bucket.';
comment on function public.service_cancel_marketplace_upload(uuid, uuid) is
    'Service-role-only cancellation for an unleased publisher upload.';
