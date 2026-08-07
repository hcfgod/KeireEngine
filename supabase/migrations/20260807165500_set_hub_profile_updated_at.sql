create function public.set_hub_profile_updated_at()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
begin
    new.updated_at = now();
    return new;
end;
$$;

revoke all on function public.set_hub_profile_updated_at() from public, anon, authenticated;

create trigger set_hub_profile_updated_at
before update on public.profiles
for each row
execute function public.set_hub_profile_updated_at();
