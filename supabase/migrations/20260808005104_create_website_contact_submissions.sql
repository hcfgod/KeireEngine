create table public.website_contact_submissions (
    id uuid primary key default gen_random_uuid(),
    submitted_at timestamptz not null default now(),
    name text not null check (char_length(name) between 2 and 80),
    email text not null check (char_length(email) between 3 and 254),
    category text not null check (category in ('general', 'support', 'partnership', 'press', 'feedback')),
    subject text not null check (char_length(subject) between 3 and 120),
    message text not null check (char_length(message) between 10 and 5000),
    status text not null default 'new' check (status in ('new', 'reviewing', 'closed'))
);

comment on table public.website_contact_submissions is
    'Private website contact inbox. Browser roles have no table privileges; only the contact Edge Function writes.';

alter table public.website_contact_submissions enable row level security;
alter table public.website_contact_submissions force row level security;

revoke all on table public.website_contact_submissions from public, anon, authenticated;
grant select, insert, update, delete on table public.website_contact_submissions to service_role;

create index website_contact_submissions_submitted_at_idx
    on public.website_contact_submissions (submitted_at desc);

create table public.website_contact_rate_limits (
    id bigint generated always as identity primary key,
    attempted_at timestamptz not null default now(),
    ip_hash text not null check (ip_hash ~ '^[0-9a-f]{64}$')
);

comment on table public.website_contact_rate_limits is
    'Pseudonymous, short-lived abuse throttle records for the public website contact endpoint.';

alter table public.website_contact_rate_limits enable row level security;
alter table public.website_contact_rate_limits force row level security;

revoke all on table public.website_contact_rate_limits from public, anon, authenticated;
grant select, insert, delete on table public.website_contact_rate_limits to service_role;
grant usage, select on sequence public.website_contact_rate_limits_id_seq to service_role;

create index website_contact_rate_limits_lookup_idx
    on public.website_contact_rate_limits (ip_hash, attempted_at desc);
create index website_contact_rate_limits_expiry_idx
    on public.website_contact_rate_limits (attempted_at);

create function public.reserve_website_contact_submission(p_ip_hash text)
returns boolean
language plpgsql
security invoker
set search_path = ''
as $$
begin
    if p_ip_hash !~ '^[0-9a-f]{64}$' then
        raise exception 'invalid contact throttle identity';
    end if;

    perform pg_advisory_xact_lock(hashtextextended(p_ip_hash, 0));

    delete from public.website_contact_rate_limits
    where attempted_at < now() - interval '24 hours';

    if (
        select count(*)
        from public.website_contact_rate_limits
        where ip_hash = p_ip_hash
          and attempted_at >= now() - interval '1 hour'
    ) >= 3 then
        return false;
    end if;

    insert into public.website_contact_rate_limits (ip_hash)
    values (p_ip_hash);
    return true;
end;
$$;

revoke all on function public.reserve_website_contact_submission(text) from public, anon, authenticated;
grant execute on function public.reserve_website_contact_submission(text) to service_role;

comment on function public.reserve_website_contact_submission(text) is
    'Atomically reserves one contact submission within the three-per-hour pseudonymous throttle.';
