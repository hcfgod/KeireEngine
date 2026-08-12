create index idx_platform_staff_members_appointed_by
    on public.platform_staff_members (appointed_by)
    where appointed_by is not null;
