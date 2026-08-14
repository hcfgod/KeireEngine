-- Let approved publishers create or reuse their own product draft directly from the upload flow.
-- The service-only boundary preserves the existing MFA-authenticated Edge Function as the sole caller.
create or replace function public.service_reserve_marketplace_named_upload(
    p_actor_user_id uuid,
    p_product_id uuid,
    p_publisher_id uuid,
    p_category_id uuid,
    p_product_name text,
    p_product_summary text,
    p_license_spdx text,
    p_license_revision text,
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
    product_id uuid,
    storage_path text,
    expires_at timestamptz
)
language plpgsql
security definer
set search_path = ''
as $$
declare
    v_product_id uuid;
    v_product_slug text;
    v_product_state public.marketplace_product_state;
    v_existing_name text;
    v_existing_category_id uuid;
    v_existing_summary text;
    v_existing_license_spdx text;
    v_existing_license_revision text;
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
    if p_publisher_id is null or p_category_id is null or
       char_length(trim(coalesce(p_product_name, ''))) not between 2 and 128 or
       char_length(trim(coalesce(p_product_summary, ''))) not between 20 and 240 or
       char_length(coalesce(p_license_spdx, '')) not between 2 and 64 or
       char_length(coalesce(p_license_revision, '')) not between 1 and 64 or
       p_version !~ '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)([-+][0-9A-Za-z.-]+)?$' or
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
        select 1 from public.publishers publisher
        join public.organization_memberships membership
          on membership.organization_id = publisher.organization_id
        where publisher.id = p_publisher_id and publisher.suspended_at is null
          and membership.user_id = p_actor_user_id and membership.role in ('owner', 'admin')
    ) then
        raise exception using errcode = '42501', message = 'publisher_not_manageable';
    end if;
    if not exists (select 1 from public.marketplace_categories category
                   where category.id = p_category_id and category.active) or
       not exists (select 1 from public.marketplace_license_revisions license
                   where license.license_id = p_license_spdx and license.revision = p_license_revision) then
        raise exception using errcode = '22023', message = 'publisher_product_metadata_invalid';
    end if;

    if p_product_id is not null then
        select product.id, product.state, product.display_name, product.category_id,
               product.short_description, product.license_spdx, product.license_revision
          into v_product_id, v_product_state, v_existing_name, v_existing_category_id,
               v_existing_summary, v_existing_license_spdx, v_existing_license_revision
        from public.marketplace_products product
        where product.id = p_product_id and product.publisher_id = p_publisher_id
        for update;
        if not found or v_product_state not in ('draft', 'changes_requested', 'published') then
            raise exception using errcode = '42501', message = 'publisher_product_not_editable';
        end if;
    else
        v_product_slug := trim(both '-' from regexp_replace(
            lower(trim(p_product_name)), '[^a-z0-9]+', '-', 'g'));
        if char_length(v_product_slug) < 2 then
            v_product_slug := 'asset-' || substring(md5(trim(p_product_name)) from 1 for 12);
        end if;
        v_product_slug := trim(both '-' from left(v_product_slug, 96));
        perform pg_advisory_xact_lock(hashtextextended(
            'marketplace-product:' || p_publisher_id::text || ':' || v_product_slug, 0));

        select product.id, product.state, product.display_name, product.category_id,
               product.short_description, product.license_spdx, product.license_revision
          into v_product_id, v_product_state, v_existing_name, v_existing_category_id,
               v_existing_summary, v_existing_license_spdx, v_existing_license_revision
        from public.marketplace_products product
        where product.publisher_id = p_publisher_id and lower(product.slug) = lower(v_product_slug)
        for update;
        if found and lower(v_existing_name) <> lower(trim(p_product_name)) then
            v_product_slug := trim(both '-' from left(v_product_slug, 86)) || '-' ||
                              substring(md5(trim(p_product_name)) from 1 for 8);
            select product.id, product.state, product.display_name, product.category_id,
                   product.short_description, product.license_spdx, product.license_revision
              into v_product_id, v_product_state, v_existing_name, v_existing_category_id,
                   v_existing_summary, v_existing_license_spdx, v_existing_license_revision
            from public.marketplace_products product
            where product.publisher_id = p_publisher_id and lower(product.slug) = lower(v_product_slug)
            for update;
        end if;
        if found and lower(v_existing_name) <> lower(trim(p_product_name)) then
            raise exception using errcode = '23505', message = 'publisher_product_name_conflict';
        end if;

        if not found then
            insert into public.marketplace_products (
                publisher_id, category_id, slug, display_name, short_description, state,
                license_spdx, license_revision
            ) values (
                p_publisher_id, p_category_id, v_product_slug, trim(p_product_name),
                trim(p_product_summary), 'draft', p_license_spdx, p_license_revision
            ) returning id, state, display_name, category_id, short_description, license_spdx, license_revision
              into v_product_id, v_product_state, v_existing_name, v_existing_category_id,
                   v_existing_summary, v_existing_license_spdx, v_existing_license_revision;
            insert into public.marketplace_offers (product_id, currency, amount_minor, active)
            values (v_product_id, 'USD', 0, true);
            insert into public.platform_audit_events (actor_user_id, action, target_type, target_id, metadata)
            values (p_actor_user_id, 'marketplace.product_created', 'marketplace_product', v_product_id::text,
                    jsonb_build_object('publisherId', p_publisher_id, 'categoryId', p_category_id,
                                       'displayName', trim(p_product_name), 'slug', v_product_slug));
        elsif v_product_state not in ('draft', 'changes_requested', 'published') then
            raise exception using errcode = '42501', message = 'publisher_product_not_editable';
        end if;
    end if;

    if v_product_state in ('draft', 'changes_requested') then
        update public.marketplace_products product
        set category_id = p_category_id,
            display_name = trim(p_product_name),
            short_description = trim(p_product_summary),
            license_spdx = p_license_spdx,
            license_revision = p_license_revision,
            updated_at = now()
        where product.id = v_product_id;
    elsif lower(v_existing_name) <> lower(trim(p_product_name)) then
        raise exception using errcode = '22023', message = 'publisher_published_product_name_immutable';
    elsif v_existing_category_id is distinct from p_category_id or
          v_existing_summary is distinct from trim(p_product_summary) or
          v_existing_license_spdx is distinct from p_license_spdx or
          v_existing_license_revision is distinct from p_license_revision then
        raise exception using errcode = '22023', message = 'publisher_published_product_metadata_immutable';
    end if;

    select version.id, version.state into v_version_id, v_version_state
    from public.marketplace_product_versions version
    where version.product_id = v_product_id and version.version = p_version for update;
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
            v_product_id, p_version, 'draft', p_install_kind, p_minimum_engine_version, p_maximum_engine_version,
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

    v_storage_path := v_product_id::text || '/' || v_version_id::text || '/' ||
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
            jsonb_build_object('productId', v_product_id, 'versionId', v_version_id,
                'storageBucket', 'marketplace-packages', 'expectedSizeBytes', p_expected_size_bytes,
                'expectedSha256', p_expected_sha256));
    return query select v_upload_id, v_version_id, v_product_id, v_storage_path, v_expires_at;
end;
$$;

revoke all on function public.service_reserve_marketplace_named_upload(
    uuid, uuid, uuid, uuid, text, text, text, text, text, public.marketplace_install_kind,
    text, text, text[], text[], text[], text, text, bigint, text
) from public, anon, authenticated;
grant execute on function public.service_reserve_marketplace_named_upload(
    uuid, uuid, uuid, uuid, text, text, text, text, text, public.marketplace_install_kind,
    text, text, text[], text[], text[], text, text, bigint, text
) to service_role;

comment on function public.service_reserve_marketplace_named_upload(
    uuid, uuid, uuid, uuid, text, text, text, text, text, public.marketplace_install_kind,
    text, text, text[], text[], text[], text, text, bigint, text
) is 'Service-only MFA publisher boundary that creates or reuses a named product and reserves a private package upload.';
