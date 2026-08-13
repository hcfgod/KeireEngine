-- Return the immutable offline-signed publication envelope with each authorized download.
-- The Storage URL remains short-lived, while Hub and Editor independently verify this
-- token-free proof against the packaged Kéire public key and the exact archive digest.

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

revoke all on function public.service_issue_marketplace_download_grant_v2(uuid, text, uuid, uuid, uuid)
    from public, anon, authenticated;
grant execute on function public.service_issue_marketplace_download_grant_v2(uuid, text, uuid, uuid, uuid)
    to service_role;

comment on function public.service_issue_marketplace_download_grant_v2(uuid, text, uuid, uuid, uuid) is
    'Service-role-only download adapter returning the immutable offline-signed publication proof with the grant.';
