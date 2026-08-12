-- Service-role-only adapters for the JWT-verified marketplace Edge Functions.
-- Rollback: revoke and drop the four service_* functions below. The marketplace
-- feature flags must remain disabled if the Edge Function boundary is absent.

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
    if (select auth.role()) <> 'service_role' or
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
    if (select auth.role()) <> 'service_role' or
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
    if (select auth.role()) <> 'service_role' or
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
    if (select auth.role()) <> 'service_role' or
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

revoke all on function public.service_create_marketplace_organization(uuid, text, text)
    from public, anon, authenticated;
revoke all on function public.service_claim_free_marketplace_product(uuid, uuid, uuid, text, text, text, text)
    from public, anon, authenticated;
revoke all on function public.service_register_marketplace_device_session(uuid, text, text)
    from public, anon, authenticated;
revoke all on function public.service_issue_marketplace_download_grant(uuid, text, uuid, uuid, uuid)
    from public, anon, authenticated;
grant execute on function public.service_create_marketplace_organization(uuid, text, text) to service_role;
grant execute on function public.service_claim_free_marketplace_product(uuid, uuid, uuid, text, text, text, text)
    to service_role;
grant execute on function public.service_register_marketplace_device_session(uuid, text, text) to service_role;
grant execute on function public.service_issue_marketplace_download_grant(uuid, text, uuid, uuid, uuid)
    to service_role;

comment on function public.service_claim_free_marketplace_product(uuid, uuid, uuid, text, text, text, text) is
    'Service-role-only adapter. The Edge Function must verify the caller JWT and supply its immutable user identity.';
comment on function public.service_issue_marketplace_download_grant(uuid, text, uuid, uuid, uuid) is
    'Service-role-only adapter. Entitlement grants access; clients must still verify package hash and signature.';
