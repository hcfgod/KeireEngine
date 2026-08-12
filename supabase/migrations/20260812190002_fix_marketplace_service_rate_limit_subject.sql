-- Attribute marketplace write throttles to the actor carried by each trusted row instead of requiring an end-user
-- JWT inside service-role transition functions. Rollback: restore the three-argument consume function and the
-- auth.uid()-only trigger body from 20260812061500_marketplace_storage_and_downloads.sql.

create or replace function private.consume_marketplace_rate_limit(
    p_subject uuid,
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
    current_count integer;
begin
    if p_subject is null or p_limit not between 1 and 100000 or
       p_window < interval '1 minute' or p_window > interval '1 day' then
        return false;
    end if;

    insert into private.marketplace_rate_limits (bucket, subject, window_started_at, request_count)
    values (p_bucket, p_subject, now(), 1)
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

revoke all on function private.consume_marketplace_rate_limit(uuid, text, integer, interval)
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
    selected_subject uuid;
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
    selected_subject := case tg_table_name
        when 'marketplace_orders' then nullif(to_jsonb(new) ->> 'user_id', '')::uuid
        when 'marketplace_reviews' then coalesce(
            nullif(to_jsonb(new) ->> 'author_user_id', '')::uuid,
            (select auth.uid()))
        when 'marketplace_reports' then nullif(to_jsonb(new) ->> 'reporter_user_id', '')::uuid
        when 'marketplace_uploads' then nullif(to_jsonb(new) ->> 'created_by', '')::uuid
        when 'marketplace_download_grants' then (
            select session.user_id
            from public.oauth_device_sessions as session
            where session.id = nullif(to_jsonb(new) ->> 'session_id', '')::uuid)
        else null
    end;
    if selected_limit = 0 or selected_subject is null or not private.consume_marketplace_rate_limit(
        selected_subject, selected_bucket, selected_limit, interval '1 hour') then
        raise exception using errcode = 'P0001', message = 'marketplace_rate_limited';
    end if;
    return new;
end;
$$;

revoke all on function private.enforce_marketplace_write_rate() from public, anon, authenticated;
drop function private.consume_marketplace_rate_limit(text, integer, interval);
