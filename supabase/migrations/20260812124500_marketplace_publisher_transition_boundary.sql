-- MFA-protected publisher application transition used only through the JWT-verifying Edge Function.
-- Rollback: disable publisher_portal_enabled, revoke this function, and drop it.

create or replace function public.service_submit_publisher_application(
    p_actor_user_id uuid,
    p_application_id uuid
)
returns uuid
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_organization_id uuid;
begin
    if (select auth.role()) <> 'service_role' or
       not exists (select 1 from auth.users where id = p_actor_user_id) then
        raise exception using errcode = '42501', message = 'service_actor_invalid';
    end if;
    if not coalesce((select enabled from public.platform_feature_flags
                     where key = 'publisher_portal_enabled'), false) then
        raise exception using errcode = '55000', message = 'publisher_portal_disabled';
    end if;

    select application.organization_id into selected_organization_id
    from public.publisher_applications application
    where application.id = p_application_id and
          application.applicant_user_id = p_actor_user_id and
          application.state in ('draft', 'changes_requested')
    for update;
    if not found then
        raise exception using errcode = 'P0002', message = 'publisher_application_not_editable';
    end if;
    if selected_organization_id is null or not exists (
        select 1 from public.organization_memberships membership
        where membership.organization_id = selected_organization_id and
              membership.user_id = p_actor_user_id and
              membership.role in ('owner', 'admin')
    ) then
        raise exception using errcode = '42501', message = 'organization_authorization_required';
    end if;

    update public.publisher_applications
    set state = 'submitted', submitted_at = now(), reviewed_at = null,
        reviewed_by = null, decision_note = null, updated_at = now()
    where id = p_application_id;

    insert into public.platform_audit_events (actor_user_id, action, target_type, target_id)
    values (p_actor_user_id, 'publisher.application_submitted', 'publisher_application', p_application_id::text);
    return p_application_id;
end;
$$;

revoke all on function public.service_submit_publisher_application(uuid, uuid)
    from public, anon, authenticated;
grant execute on function public.service_submit_publisher_application(uuid, uuid) to service_role;

comment on function public.service_submit_publisher_application(uuid, uuid) is
    'Service-role-only transition. The publisher Edge Function must verify the caller JWT and AAL2.';

drop policy if exists publisher_applications_owner_insert on public.publisher_applications;
drop policy if exists publisher_applications_owner_update on public.publisher_applications;
create policy publisher_applications_owner_insert on public.publisher_applications
for insert to authenticated
with check (
    applicant_user_id = (select auth.uid()) and state = 'draft' and organization_id is not null and
    private.is_organization_member(organization_id, array['owner', 'admin']::public.organization_role[])
);
create policy publisher_applications_owner_or_staff_update on public.publisher_applications
for update to authenticated
using (
    (applicant_user_id = (select auth.uid()) and state in ('draft', 'changes_requested')) or
    private.is_platform_staff('moderator')
)
with check (
    (applicant_user_id = (select auth.uid()) and state in ('draft', 'withdrawn') and
     organization_id is not null and
     private.is_organization_member(organization_id, array['owner', 'admin']::public.organization_role[])) or
    private.is_platform_staff('moderator')
);
