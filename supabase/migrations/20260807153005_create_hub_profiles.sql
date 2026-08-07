create table public.profiles (
    user_id uuid primary key references auth.users(id) on delete cascade,
    display_name text,
    avatar_url text,
    created_at timestamptz not null default now(),
    updated_at timestamptz not null default now(),
    constraint profiles_display_name_length
        check (display_name is null or char_length(display_name) between 1 and 64),
    constraint profiles_avatar_url
        check (
            avatar_url is null
            or (
                char_length(avatar_url) between 1 and 2048
                and avatar_url like 'https://%'
            )
        )
);

alter table public.profiles enable row level security;
alter table public.profiles force row level security;

revoke all on table public.profiles from anon, authenticated;
grant usage on schema public to authenticated;
grant select, insert, update on table public.profiles to authenticated;

create policy profiles_select_own
    on public.profiles
    for select
    to authenticated
    using ((select auth.uid()) = user_id);

create policy profiles_insert_own
    on public.profiles
    for insert
    to authenticated
    with check ((select auth.uid()) = user_id);

create policy profiles_update_own
    on public.profiles
    for update
    to authenticated
    using ((select auth.uid()) = user_id)
    with check ((select auth.uid()) = user_id);

comment on table public.profiles is
    'Optional Kéire Hub profile data. Authentication identity remains in auth.users.';
