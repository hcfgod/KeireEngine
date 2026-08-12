-- Private marketplace storage, device-session registration, download grants, and database-side abuse controls.
-- Rollback: disable all marketplace feature flags, drop the public functions and storage policies below, then remove
-- the empty buckets. Published object bytes must be archived before bucket removal.

create table private.marketplace_rate_limits (
    bucket text not null,
    subject uuid not null,
    window_started_at timestamptz not null,
    request_count integer not null,
    primary key (bucket, subject),
    constraint marketplace_rate_limits_bucket check (bucket ~ '^[a-z][a-z0-9_.-]{2,63}$'),
    constraint marketplace_rate_limits_count check (request_count between 1 and 1000000)
);

revoke all on private.marketplace_rate_limits from public, anon, authenticated;

create or replace function private.consume_marketplace_rate_limit(
    p_bucket text,
    p_limit integer,
    p_window interval
)
returns boolean
language plpgsql
volatile
security definer
set search_path = ''
as $$
declare
    current_user_id uuid := (select auth.uid());
    current_count integer;
begin
    if current_user_id is null or p_limit not between 1 and 100000 or
       p_window < interval '1 minute' or p_window > interval '1 day' then
        return false;
    end if;

    insert into private.marketplace_rate_limits (bucket, subject, window_started_at, request_count)
    values (p_bucket, current_user_id, now(), 1)
    on conflict (bucket, subject) do update
    set window_started_at = case
            when private.marketplace_rate_limits.window_started_at + p_window <= now() then now()
            else private.marketplace_rate_limits.window_started_at
        end,
        request_count = case
            when private.marketplace_rate_limits.window_started_at + p_window <= now() then 1
            else private.marketplace_rate_limits.request_count + 1
        end
    returning request_count into current_count;
    return current_count <= p_limit;
end;
$$;

revoke all on function private.consume_marketplace_rate_limit(text, integer, interval)
    from public, anon, authenticated;

create or replace function private.enforce_marketplace_write_rate()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_bucket text;
    selected_limit integer;
begin
    selected_bucket := case tg_table_name
        when 'marketplace_orders' then 'claims'
        when 'marketplace_reviews' then 'reviews'
        when 'marketplace_reports' then 'reports'
        when 'marketplace_uploads' then 'uploads'
        when 'marketplace_download_grants' then 'download_grants'
        else 'unknown'
    end;
    selected_limit := case tg_table_name
        when 'marketplace_orders' then 30
        when 'marketplace_reviews' then 20
        when 'marketplace_reports' then 15
        when 'marketplace_uploads' then 20
        when 'marketplace_download_grants' then 120
        else 0
    end;
    if selected_limit = 0 or not private.consume_marketplace_rate_limit(
        selected_bucket, selected_limit, interval '1 hour') then
        raise exception using errcode = 'P0001', message = 'marketplace_rate_limited';
    end if;
    return new;
end;
$$;

revoke all on function private.enforce_marketplace_write_rate() from public, anon, authenticated;

create trigger marketplace_orders_rate_limit
before insert on public.marketplace_orders
for each row execute function private.enforce_marketplace_write_rate();
create trigger marketplace_reviews_rate_limit
before insert on public.marketplace_reviews
for each row execute function private.enforce_marketplace_write_rate();
create trigger marketplace_reports_rate_limit
before insert on public.marketplace_reports
for each row execute function private.enforce_marketplace_write_rate();
create trigger marketplace_uploads_rate_limit
before insert on public.marketplace_uploads
for each row execute function private.enforce_marketplace_write_rate();
create trigger marketplace_download_grants_rate_limit
before insert on public.marketplace_download_grants
for each row execute function private.enforce_marketplace_write_rate();

insert into storage.buckets (id, name, public, file_size_limit, allowed_mime_types)
values
    ('marketplace-quarantine', 'marketplace-quarantine', false, 68719476736,
     array['application/octet-stream', 'application/vnd.keire.asset-package']),
    ('marketplace-releases', 'marketplace-releases', false, 68719476736,
     array['application/octet-stream', 'application/vnd.keire.asset-package']),
    ('marketplace-media-quarantine', 'marketplace-media-quarantine', false, 1073741824,
     array['image/png', 'image/webp', 'image/avif', 'video/mp4']),
    ('marketplace-media', 'marketplace-media', true, 1073741824,
     array['image/png', 'image/webp', 'image/avif', 'video/mp4'])
on conflict (id) do update
set public = excluded.public,
    file_size_limit = excluded.file_size_limit,
    allowed_mime_types = excluded.allowed_mime_types;

create policy marketplace_quarantine_owner_insert
on storage.objects for insert to authenticated
with check (
    bucket_id = 'marketplace-quarantine' and exists (
        select 1
        from public.marketplace_uploads upload
        join public.marketplace_product_versions version on version.id = upload.version_id
        join public.marketplace_products product on product.id = version.product_id
        where upload.storage_path = name
          and upload.created_by = (select auth.uid())
          and upload.state in ('created', 'uploading')
          and upload.expires_at > now()
          and private.can_manage_publisher(product.publisher_id)
    )
);

create policy marketplace_quarantine_owner_read
on storage.objects for select to authenticated
using (
    bucket_id = 'marketplace-quarantine' and exists (
        select 1
        from public.marketplace_uploads upload
        join public.marketplace_product_versions version on version.id = upload.version_id
        join public.marketplace_products product on product.id = version.product_id
        where upload.storage_path = name
          and (upload.created_by = (select auth.uid()) or private.is_platform_staff('moderator') or
               private.can_manage_publisher(product.publisher_id))
    )
);

create policy marketplace_media_quarantine_publisher_all
on storage.objects for all to authenticated
using (
    bucket_id = 'marketplace-media-quarantine' and exists (
        select 1 from public.marketplace_product_media media
        join public.marketplace_products product on product.id = media.product_id
        where media.storage_path = name and private.can_manage_publisher(product.publisher_id)
    )
)
with check (
    bucket_id = 'marketplace-media-quarantine' and exists (
        select 1 from public.marketplace_product_media media
        join public.marketplace_products product on product.id = media.product_id
        where media.storage_path = name and private.can_manage_publisher(product.publisher_id)
    )
);

create policy marketplace_release_entitled_read
on storage.objects for select to authenticated
using (
    bucket_id = 'marketplace-releases' and exists (
        select 1
        from public.marketplace_product_versions version
        where version.archive_storage_path = name
          and version.state in ('published', 'withdrawn')
          and version.security_revoked_at is null
          and exists (
              select 1 from public.marketplace_entitlements entitlement
              where entitlement.product_id = version.product_id and entitlement.revoked_at is null and
                    (entitlement.user_id = (select auth.uid()) or
                     (entitlement.organization_id is not null and
                      private.is_organization_member(entitlement.organization_id, null)))
          )
    )
);

create or replace function public.register_marketplace_device_session(
    p_oauth_session_id text,
    p_client_type text,
    p_device_name text
)
returns uuid
language plpgsql
security definer
set search_path = ''
as $$
declare
    current_user_id uuid := (select auth.uid());
    jwt_session_id text := (select auth.jwt() ->> 'session_id');
    registered_id uuid;
begin
    if current_user_id is null then
        raise exception using errcode = '42501', message = 'authentication_required';
    end if;
    if p_oauth_session_id is distinct from jwt_session_id or char_length(p_oauth_session_id) not between 16 and 256 or
       p_client_type <> 'hub' or char_length(p_device_name) not between 1 and 128 then
        raise exception using errcode = '22023', message = 'invalid_device_session';
    end if;
    insert into public.oauth_device_sessions
        (user_id, oauth_session_id, client_type, device_name, last_used_at, revoked_at)
    values (current_user_id, p_oauth_session_id, p_client_type, p_device_name, now(), null)
    on conflict (oauth_session_id) do update
    set last_used_at = now(),
        device_name = excluded.device_name
    where public.oauth_device_sessions.user_id = current_user_id and
          public.oauth_device_sessions.revoked_at is null
    returning id into registered_id;
    if registered_id is null then
        raise exception using errcode = '42501', message = 'device_session_revoked';
    end if;
    return registered_id;
end;
$$;

create or replace function public.issue_marketplace_download_grant(
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
declare
    current_user_id uuid := (select auth.uid());
    selected_version public.marketplace_product_versions%rowtype;
    selected_entitlement public.marketplace_entitlements%rowtype;
    selected_expiry timestamptz := now() + interval '10 minutes';
begin
    if current_user_id is null then
        raise exception using errcode = '42501', message = 'authentication_required';
    end if;
    if not coalesce((select enabled from public.platform_feature_flags where key = 'marketplace_enabled'), false) or
       not coalesce((select enabled from public.platform_feature_flags where key = 'asset_packages_enabled'), false) then
        raise exception using errcode = '55000', message = 'asset_packages_disabled';
    end if;
    if not exists (
        select 1 from public.oauth_device_sessions session
        where session.id = p_device_session_id
          and session.user_id = current_user_id
          and session.oauth_session_id = (select auth.jwt() ->> 'session_id')
          and session.client_type = 'hub'
          and session.revoked_at is null
    ) then
        raise exception using errcode = '42501', message = 'device_session_invalid';
    end if;

    select * into selected_version from public.marketplace_product_versions version
    where version.id = p_version_id and version.state in ('published', 'withdrawn') and
          version.security_revoked_at is null and version.archive_storage_path is not null
    for share;
    if not found then
        raise exception using errcode = 'P0002', message = 'version_unavailable';
    end if;

    select * into selected_entitlement from public.marketplace_entitlements entitlement
    where entitlement.product_id = selected_version.product_id and entitlement.revoked_at is null and
          ((p_organization_id is null and entitlement.user_id = current_user_id) or
           (p_organization_id is not null and entitlement.organization_id = p_organization_id and
            private.is_organization_member(p_organization_id, null)))
    for share;
    if not found then
        raise exception using errcode = '42501', message = 'entitlement_required';
    end if;

    insert into public.marketplace_download_grants
        (entitlement_id, version_id, session_id, expires_at)
    values (selected_entitlement.id, selected_version.id, p_device_session_id, selected_expiry)
    returning id into grant_id;
    storage_path := selected_version.archive_storage_path;
    archive_sha256 := selected_version.archive_sha256;
    archive_size_bytes := selected_version.archive_size_bytes;
    expires_at := selected_expiry;
    return next;
end;
$$;

revoke all on function public.register_marketplace_device_session(text, text, text)
    from public, anon, authenticated;
revoke all on function public.issue_marketplace_download_grant(uuid, uuid, uuid)
    from public, anon, authenticated;
grant execute on function public.register_marketplace_device_session(text, text, text) to authenticated;
grant execute on function public.issue_marketplace_download_grant(uuid, uuid, uuid) to authenticated;

-- These trigger boundaries complement RLS: publishers can edit drafts without being able to self-publish, forge a
-- validator state, or modify another party's fields on a review.
create or replace function private.marketplace_privileged_actor()
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
    select (select auth.role()) = 'service_role' or private.is_platform_staff('moderator')
$$;

create or replace function private.enforce_marketplace_product_update()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
begin
    if private.marketplace_privileged_actor() then
        return new;
    end if;
    if old.publisher_id is distinct from new.publisher_id or
       old.state not in ('draft', 'changes_requested') or
       old.state is distinct from new.state or
       old.published_at is distinct from new.published_at or
       old.delisted_at is distinct from new.delisted_at then
        raise exception using errcode = '42501', message = 'marketplace_product_transition_forbidden';
    end if;
    return new;
end;
$$;

create trigger marketplace_products_protect_transitions
before update on public.marketplace_products
for each row execute function private.enforce_marketplace_product_update();

create or replace function private.enforce_marketplace_version_update()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
begin
    if private.marketplace_privileged_actor() then
        return new;
    end if;
    if old.product_id is distinct from new.product_id or
       old.state not in ('draft', 'validation_failed', 'changes_requested') or
       old.state is distinct from new.state or
       old.published_at is distinct from new.published_at or
       old.withdrawn_at is distinct from new.withdrawn_at then
        raise exception using errcode = '42501', message = 'marketplace_version_transition_forbidden';
    end if;
    return new;
end;
$$;

create trigger marketplace_versions_protect_transitions
before update on public.marketplace_product_versions
for each row execute function private.enforce_marketplace_version_update();

create or replace function private.enforce_marketplace_review_update()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
declare
    current_user_id uuid := (select auth.uid());
    manages_publisher boolean := exists (
        select 1 from public.marketplace_products product
        where product.id = old.product_id and private.can_manage_publisher(product.publisher_id));
begin
    if private.marketplace_privileged_actor() then
        return new;
    end if;
    if current_user_id = old.author_user_id then
        if old.product_id is distinct from new.product_id or
           old.author_user_id is distinct from new.author_user_id or
           old.owner_organization_id is distinct from new.owner_organization_id or
           old.publisher_reply is distinct from new.publisher_reply or
           old.hidden_at is distinct from new.hidden_at or
           old.created_at is distinct from new.created_at then
            raise exception using errcode = '42501', message = 'marketplace_review_fields_forbidden';
        end if;
        return new;
    end if;
    if manages_publisher then
        if old.product_id is distinct from new.product_id or
           old.author_user_id is distinct from new.author_user_id or
           old.owner_organization_id is distinct from new.owner_organization_id or
           old.rating is distinct from new.rating or
           old.title is distinct from new.title or
           old.body is distinct from new.body or
           old.hidden_at is distinct from new.hidden_at or
           old.created_at is distinct from new.created_at then
            raise exception using errcode = '42501', message = 'marketplace_review_fields_forbidden';
        end if;
        return new;
    end if;
    raise exception using errcode = '42501', message = 'marketplace_review_update_forbidden';
end;
$$;

create trigger marketplace_reviews_protect_fields
before update on public.marketplace_reviews
for each row execute function private.enforce_marketplace_review_update();

create policy reviews_publisher_reply_update on public.marketplace_reviews for update to authenticated
    using (exists (select 1 from public.marketplace_products product
                   where product.id = product_id and private.can_manage_publisher(product.publisher_id)))
    with check (exists (select 1 from public.marketplace_products product
                        where product.id = product_id and private.can_manage_publisher(product.publisher_id)));

revoke all on function private.marketplace_privileged_actor() from public, anon, authenticated;
revoke all on function private.enforce_marketplace_product_update() from public, anon, authenticated;
revoke all on function private.enforce_marketplace_version_update() from public, anon, authenticated;
revoke all on function private.enforce_marketplace_review_update() from public, anon, authenticated;

comment on function public.issue_marketplace_download_grant(uuid, uuid, uuid) is
    'Entitlement grants access; callers must independently verify the returned archive SHA-256 and marketplace signature.';
