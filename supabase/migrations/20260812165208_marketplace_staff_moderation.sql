-- Audited marketplace staff authorization and moderation transitions.
-- Staff authorization is database-backed so role revocation takes effect immediately
-- instead of waiting for a browser JWT refresh.

create type public.platform_staff_role as enum ('moderator', 'administrator');

create table public.platform_staff_members (
    user_id uuid primary key references auth.users(id) on delete restrict,
    role public.platform_staff_role not null,
    active boolean not null default true,
    appointed_by uuid references auth.users(id) on delete restrict,
    appointed_at timestamptz not null default now(),
    updated_at timestamptz not null default now(),
    revoked_at timestamptz,
    constraint platform_staff_revocation_state check (
        (active and revoked_at is null) or (not active and revoked_at is not null)
    )
);

alter table public.platform_staff_members enable row level security;
alter table public.platform_staff_members force row level security;
revoke all on public.platform_staff_members from public, anon, authenticated;
grant select on public.platform_staff_members to authenticated;

create or replace function private.is_platform_staff(required_role text default null)
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
    select exists (
        select 1
        from public.platform_staff_members staff
        where staff.user_id = (select auth.uid())
          and staff.active
          and (required_role is null or staff.role::text = required_role or
               (staff.role = 'administrator' and required_role = 'moderator'))
    );
$$;

revoke all on function private.is_platform_staff(text) from public;
grant execute on function private.is_platform_staff(text) to anon, authenticated;

create or replace function private.can_manage_publisher(target_publisher uuid)
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
    select (select auth.uid()) is not null and exists (
        select 1
        from public.publishers publisher
        join public.organization_memberships membership
          on membership.organization_id = publisher.organization_id
        where publisher.id = target_publisher
          and membership.user_id = (select auth.uid())
          and membership.role in ('owner', 'admin')
          and publisher.suspended_at is null
    );
$$;

revoke all on function private.can_manage_publisher(uuid) from public;
grant execute on function private.can_manage_publisher(uuid) to anon, authenticated;

create policy platform_staff_members_staff_read
on public.platform_staff_members for select to authenticated
using (private.is_platform_staff('moderator'));

-- Browser staff sessions are read-only. All moderation writes cross the MFA-protected,
-- service-role Edge boundary below so they cannot bypass validation or audit events.
revoke insert on public.marketplace_submissions from authenticated;
drop policy if exists submissions_publisher_insert on public.marketplace_submissions;

drop policy if exists organizations_admin_update on public.organizations;
create policy organizations_admin_update on public.organizations for update to authenticated
using (private.is_organization_member(id, array['owner', 'admin']::public.organization_role[]))
with check (private.is_organization_member(id, array['owner', 'admin']::public.organization_role[]));

drop policy if exists organization_memberships_admin_insert on public.organization_memberships;
create policy organization_memberships_admin_insert on public.organization_memberships for insert to authenticated
with check (private.is_organization_member(
    organization_id, array['owner', 'admin']::public.organization_role[]));
drop policy if exists organization_memberships_admin_update on public.organization_memberships;
create policy organization_memberships_admin_update on public.organization_memberships for update to authenticated
using (private.is_organization_member(organization_id, array['owner']::public.organization_role[]))
with check (private.is_organization_member(organization_id, array['owner']::public.organization_role[]));

drop policy if exists publisher_applications_owner_or_staff_update on public.publisher_applications;
create policy publisher_applications_owner_update on public.publisher_applications for update to authenticated
using (applicant_user_id = (select auth.uid()) and state in ('draft', 'changes_requested'))
with check (
    applicant_user_id = (select auth.uid()) and state in ('draft', 'withdrawn') and
    organization_id is not null and
    private.is_organization_member(organization_id, array['owner', 'admin']::public.organization_role[])
);

drop policy if exists products_publisher_update on public.marketplace_products;
create policy products_publisher_update on public.marketplace_products for update to authenticated
using (private.can_manage_publisher(publisher_id))
with check (private.can_manage_publisher(publisher_id));

drop policy if exists media_publisher_update on public.marketplace_product_media;
create policy media_publisher_update on public.marketplace_product_media for update to authenticated
using (approved_at is null and exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_product_media.product_id
      and private.can_manage_publisher(product.publisher_id)
      and product.state in ('draft', 'changes_requested')
))
with check (approved_at is null and exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_product_media.product_id
      and private.can_manage_publisher(product.publisher_id)
      and product.state in ('draft', 'changes_requested')
));
drop policy if exists media_publisher_delete on public.marketplace_product_media;
create policy media_publisher_delete on public.marketplace_product_media for delete to authenticated
using (approved_at is null and exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_product_media.product_id
      and private.can_manage_publisher(product.publisher_id)
      and product.state in ('draft', 'changes_requested')
));

drop policy if exists reviews_author_publisher_or_staff_update on public.marketplace_reviews;
create policy reviews_author_or_publisher_update on public.marketplace_reviews for update to authenticated
using (author_user_id = (select auth.uid()) or exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_reviews.product_id
      and private.can_manage_publisher(product.publisher_id)
))
with check (author_user_id = (select auth.uid()) or exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_reviews.product_id
      and private.can_manage_publisher(product.publisher_id)
));

drop policy if exists sessions_owner_revoke on public.oauth_device_sessions;
create policy sessions_owner_revoke on public.oauth_device_sessions for update to authenticated
using (user_id = (select auth.uid()))
with check (user_id = (select auth.uid()));

create or replace function private.marketplace_privileged_actor()
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
    select coalesce((select auth.jwt() ->> 'role'), '') = 'service_role'
$$;

revoke all on function private.marketplace_privileged_actor()
    from public, anon, authenticated;

create or replace function private.service_actor_is_staff(target_user_id uuid, required_role text)
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
    select exists (
        select 1
        from public.platform_staff_members staff
        where staff.user_id = target_user_id
          and staff.active
          and (required_role is null or staff.role::text = required_role or
               (staff.role = 'administrator' and required_role = 'moderator'))
    );
$$;

revoke all on function private.service_actor_is_staff(uuid, text)
    from public, anon, authenticated;

create or replace function public.service_get_platform_staff_role(p_actor_user_id uuid)
returns text
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_role text;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' then
        raise exception using errcode = '42501', message = 'service_role_required';
    end if;
    select staff.role::text into selected_role
    from public.platform_staff_members staff
    where staff.user_id = p_actor_user_id and staff.active;
    if selected_role is null then
        raise exception using errcode = '42501', message = 'staff_authorization_required';
    end if;
    return selected_role;
end;
$$;

create or replace function public.service_set_platform_staff(
    p_actor_user_id uuid,
    p_target_user_id uuid,
    p_role text,
    p_active boolean,
    p_reason text
)
returns table (user_id uuid, role public.platform_staff_role, active boolean)
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_role public.platform_staff_role;
    target_was_last_administrator boolean;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       not private.service_actor_is_staff(p_actor_user_id, 'administrator') then
        raise exception using errcode = '42501', message = 'staff_administrator_required';
    end if;
    if p_role not in ('moderator', 'administrator') or
       char_length(btrim(coalesce(p_reason, ''))) not between 3 and 1000 or
       not exists (select 1 from auth.users account where account.id = p_target_user_id) then
        raise exception using errcode = '22023', message = 'staff_assignment_invalid';
    end if;
    selected_role := p_role::public.platform_staff_role;

    select exists (
        select 1
        from public.platform_staff_members staff
        where staff.user_id = p_target_user_id
          and staff.active and staff.role = 'administrator'
          and (not p_active or selected_role <> 'administrator')
          and (select count(*) from public.platform_staff_members current_staff
               where current_staff.active and current_staff.role = 'administrator') <= 1
    ) into target_was_last_administrator;
    if target_was_last_administrator then
        raise exception using errcode = '55000', message = 'staff_last_administrator_required';
    end if;

    insert into public.platform_staff_members (
        user_id, role, active, appointed_by, appointed_at, updated_at, revoked_at
    ) values (
        p_target_user_id, selected_role, p_active, p_actor_user_id, now(), now(),
        case when p_active then null else now() end
    )
    on conflict (user_id) do update
    set role = excluded.role,
        active = excluded.active,
        appointed_by = p_actor_user_id,
        updated_at = now(),
        revoked_at = case when excluded.active then null else now() end;

    insert into public.platform_audit_events (
        actor_user_id, action, target_type, target_id, metadata
    ) values (
        p_actor_user_id, 'staff.assignment_changed', 'platform_staff_member', p_target_user_id::text,
        jsonb_build_object('role', selected_role, 'active', p_active, 'reason', btrim(p_reason))
    );

    return query select staff.user_id, staff.role, staff.active
    from public.platform_staff_members staff
    where staff.user_id = p_target_user_id;
end;
$$;

create or replace function public.service_decide_publisher_application(
    p_actor_user_id uuid,
    p_application_id uuid,
    p_decision text,
    p_decision_note text,
    p_publisher_slug text
)
returns table (
    application_id uuid,
    application_state public.publisher_application_state,
    publisher_id uuid
)
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_application public.publisher_applications%rowtype;
    selected_publisher_id uuid;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       not private.service_actor_is_staff(p_actor_user_id, 'moderator') then
        raise exception using errcode = '42501', message = 'staff_moderator_required';
    end if;
    if p_decision not in ('changes_requested', 'approved', 'rejected') or
       char_length(btrim(coalesce(p_decision_note, ''))) not between 3 and 5000 or
       (p_decision = 'approved' and coalesce(p_publisher_slug, '') !~ '^[a-z0-9][a-z0-9-]{2,62}$') then
        raise exception using errcode = '22023', message = 'publisher_application_decision_invalid';
    end if;

    select application.* into selected_application
    from public.publisher_applications application
    where application.id = p_application_id and application.state = 'submitted'
    for update;
    if not found then
        raise exception using errcode = 'P0002', message = 'publisher_application_not_reviewable';
    end if;

    if p_decision = 'approved' then
        select publisher.id into selected_publisher_id
        from public.publishers publisher
        where publisher.organization_id = selected_application.organization_id
        for update;
        if found then
            update public.publishers publisher
            set display_name = selected_application.public_name,
                summary = left(selected_application.statement, 1000),
                website_url = selected_application.website_url,
                verified = true,
                suspended_at = null,
                updated_at = now()
            where publisher.id = selected_publisher_id;
        else
            insert into public.publishers (
                organization_id, slug, display_name, summary, website_url, verified
            ) values (
                selected_application.organization_id, p_publisher_slug,
                selected_application.public_name, left(selected_application.statement, 1000),
                selected_application.website_url, true
            ) returning id into selected_publisher_id;
        end if;
    end if;

    update public.publisher_applications application
    set state = p_decision::public.publisher_application_state,
        reviewed_at = now(),
        reviewed_by = p_actor_user_id,
        decision_note = btrim(p_decision_note),
        updated_at = now()
    where application.id = selected_application.id;

    insert into public.platform_audit_events (
        actor_user_id, action, target_type, target_id, metadata
    ) values (
        p_actor_user_id, 'publisher.application_decided', 'publisher_application',
        selected_application.id::text,
        jsonb_build_object('decision', p_decision, 'publisherId', selected_publisher_id,
                           'organizationId', selected_application.organization_id)
    );

    return query select selected_application.id, p_decision::public.publisher_application_state,
                        selected_publisher_id;
end;
$$;

create or replace function public.service_submit_marketplace_version(
    p_actor_user_id uuid,
    p_version_id uuid
)
returns table (submission_id uuid, submission_state public.marketplace_submission_state)
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_version public.marketplace_product_versions%rowtype;
    selected_report_id uuid;
    selected_submission_id uuid;
    selected_submission_state public.marketplace_submission_state;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       not exists (select 1 from auth.users account where account.id = p_actor_user_id) then
        raise exception using errcode = '42501', message = 'service_actor_invalid';
    end if;
    if not coalesce((select enabled from public.platform_feature_flags
                     where key = 'publisher_portal_enabled'), false) then
        raise exception using errcode = '55000', message = 'publisher_portal_disabled';
    end if;

    select version.* into selected_version
    from public.marketplace_product_versions version
    join public.marketplace_products product on product.id = version.product_id
    join public.publishers publisher on publisher.id = product.publisher_id
    join public.organization_memberships membership
      on membership.organization_id = publisher.organization_id
    where version.id = p_version_id
      and version.state = 'validated'
      and membership.user_id = p_actor_user_id
      and membership.role in ('owner', 'admin')
      and publisher.suspended_at is null
    for update of version;
    if not found then
        raise exception using errcode = 'P0002', message = 'publisher_version_not_submittable';
    end if;

    select report.id into selected_report_id
    from public.marketplace_validation_reports report
    join public.marketplace_uploads upload on upload.id = report.upload_id
    where upload.version_id = selected_version.id
      and upload.state = 'validated'
      and report.passed
      and report.package_sha256 = selected_version.archive_sha256
    order by report.completed_at desc
    limit 1;
    if selected_report_id is null then
        raise exception using errcode = '55000', message = 'publisher_validation_required';
    end if;

    select submission.id, submission.state
    into selected_submission_id, selected_submission_state
    from public.marketplace_submissions submission
    where submission.version_id = selected_version.id
      and submission.state in ('submitted', 'in_review', 'changes_requested', 'approved_pending_signature')
    for update;
    if found and selected_submission_state <> 'changes_requested' then
        raise exception using errcode = '55000', message = 'publisher_submission_already_active';
    end if;
    if found then
        update public.marketplace_submissions submission
        set validation_report_id = selected_report_id,
            submitted_by = p_actor_user_id,
            state = 'submitted',
            assigned_to = null,
            decision_note = null,
            submitted_at = now(),
            decided_at = null
        where submission.id = selected_submission_id;
    else
        insert into public.marketplace_submissions (
            version_id, validation_report_id, submitted_by, state
        ) values (
            selected_version.id, selected_report_id, p_actor_user_id, 'submitted'
        ) returning id into selected_submission_id;
    end if;

    update public.marketplace_product_versions version
    set state = 'submitted', updated_at = now()
    where version.id = selected_version.id;
    update public.marketplace_products product
    set state = 'submitted', updated_at = now()
    where product.id = selected_version.product_id;

    insert into public.platform_audit_events (
        actor_user_id, action, target_type, target_id, metadata
    ) values (
        p_actor_user_id, 'marketplace.submission_created', 'marketplace_submission',
        selected_submission_id::text,
        jsonb_build_object('versionId', selected_version.id, 'validationReportId', selected_report_id)
    );

    return query select selected_submission_id, 'submitted'::public.marketplace_submission_state;
end;
$$;

create or replace function public.service_decide_marketplace_submission(
    p_actor_user_id uuid,
    p_submission_id uuid,
    p_decision text,
    p_decision_note text
)
returns table (submission_id uuid, submission_state public.marketplace_submission_state)
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_submission public.marketplace_submissions%rowtype;
    selected_version public.marketplace_product_versions%rowtype;
    next_version_state public.marketplace_version_state;
    next_product_state public.marketplace_product_state;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       not private.service_actor_is_staff(p_actor_user_id, 'moderator') then
        raise exception using errcode = '42501', message = 'staff_moderator_required';
    end if;
    if p_decision not in ('in_review', 'changes_requested', 'approved_pending_signature', 'rejected') or
       char_length(coalesce(p_decision_note, '')) > 10000 or
       (p_decision in ('changes_requested', 'rejected') and
        char_length(btrim(coalesce(p_decision_note, ''))) < 3) then
        raise exception using errcode = '22023', message = 'marketplace_submission_decision_invalid';
    end if;

    select submission.* into selected_submission
    from public.marketplace_submissions submission
    where submission.id = p_submission_id
      and submission.state in ('submitted', 'in_review')
    for update;
    if not found then
        raise exception using errcode = 'P0002', message = 'marketplace_submission_not_reviewable';
    end if;
    select version.* into selected_version
    from public.marketplace_product_versions version
    where version.id = selected_submission.version_id
    for update;
    if not exists (
        select 1 from public.marketplace_validation_reports report
        where report.id = selected_submission.validation_report_id and report.passed
    ) then
        raise exception using errcode = '55000', message = 'marketplace_submission_validation_failed';
    end if;

    next_version_state := case p_decision
        when 'changes_requested' then 'changes_requested'::public.marketplace_version_state
        when 'approved_pending_signature' then 'approved_pending_signature'::public.marketplace_version_state
        when 'rejected' then 'withdrawn'::public.marketplace_version_state
        else 'submitted'::public.marketplace_version_state
    end;
    next_product_state := case p_decision
        when 'changes_requested' then 'changes_requested'::public.marketplace_product_state
        when 'approved_pending_signature' then 'approved_pending_signature'::public.marketplace_product_state
        when 'rejected' then 'changes_requested'::public.marketplace_product_state
        else 'submitted'::public.marketplace_product_state
    end;

    update public.marketplace_submissions submission
    set state = p_decision::public.marketplace_submission_state,
        assigned_to = p_actor_user_id,
        decision_note = nullif(btrim(coalesce(p_decision_note, '')), ''),
        decided_at = case when p_decision = 'in_review' then null else now() end
    where submission.id = selected_submission.id;
    update public.marketplace_product_versions version
    set state = next_version_state, updated_at = now()
    where version.id = selected_version.id;
    update public.marketplace_products product
    set state = next_product_state, updated_at = now()
    where product.id = selected_version.product_id;

    insert into public.platform_audit_events (
        actor_user_id, action, target_type, target_id, metadata
    ) values (
        p_actor_user_id, 'marketplace.submission_decided', 'marketplace_submission',
        selected_submission.id::text,
        jsonb_build_object('decision', p_decision, 'versionId', selected_version.id)
    );

    return query select selected_submission.id, p_decision::public.marketplace_submission_state;
end;
$$;

create or replace function public.service_decide_marketplace_report(
    p_actor_user_id uuid,
    p_report_id uuid,
    p_state text,
    p_resolution_note text
)
returns table (report_id uuid, report_state public.marketplace_report_state)
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_report public.marketplace_reports%rowtype;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       not private.service_actor_is_staff(p_actor_user_id, 'moderator') then
        raise exception using errcode = '42501', message = 'staff_moderator_required';
    end if;
    if p_state not in ('triaged', 'resolved', 'dismissed') or
       char_length(coalesce(p_resolution_note, '')) > 5000 or
       (p_state in ('resolved', 'dismissed') and
        char_length(btrim(coalesce(p_resolution_note, ''))) < 3) then
        raise exception using errcode = '22023', message = 'marketplace_report_decision_invalid';
    end if;
    select report.* into selected_report
    from public.marketplace_reports report
    where report.id = p_report_id and report.state in ('open', 'triaged')
    for update;
    if not found then
        raise exception using errcode = 'P0002', message = 'marketplace_report_not_reviewable';
    end if;

    update public.marketplace_reports report
    set state = p_state::public.marketplace_report_state,
        assigned_to = p_actor_user_id,
        resolution_note = nullif(btrim(coalesce(p_resolution_note, '')), ''),
        resolved_at = case when p_state in ('resolved', 'dismissed') then now() else null end
    where report.id = selected_report.id;
    insert into public.platform_audit_events (
        actor_user_id, action, target_type, target_id, metadata
    ) values (
        p_actor_user_id, 'marketplace.report_decided', 'marketplace_report', selected_report.id::text,
        jsonb_build_object('state', p_state)
    );
    return query select selected_report.id, p_state::public.marketplace_report_state;
end;
$$;

create or replace function public.service_set_platform_feature_flag(
    p_actor_user_id uuid,
    p_key text,
    p_enabled boolean,
    p_reason text
)
returns table (flag_key text, enabled boolean)
language plpgsql
security definer
set search_path = ''
as $$
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' or
       not private.service_actor_is_staff(p_actor_user_id, 'administrator') then
        raise exception using errcode = '42501', message = 'staff_administrator_required';
    end if;
    if char_length(btrim(coalesce(p_reason, ''))) not between 3 and 1000 or
       not exists (select 1 from public.platform_feature_flags flag where flag.key = p_key) or
       (p_key = 'paid_checkout_enabled' and p_enabled) then
        raise exception using errcode = '22023', message = 'platform_feature_flag_change_invalid';
    end if;
    update public.platform_feature_flags flag
    set enabled = p_enabled, updated_at = now(), updated_by = p_actor_user_id
    where flag.key = p_key;
    insert into public.platform_audit_events (
        actor_user_id, action, target_type, target_id, metadata
    ) values (
        p_actor_user_id, 'platform.feature_flag_changed', 'platform_feature_flag', p_key,
        jsonb_build_object('enabled', p_enabled, 'reason', btrim(p_reason))
    );
    return query select flag.key, flag.enabled
    from public.platform_feature_flags flag where flag.key = p_key;
end;
$$;

revoke all on function public.service_get_platform_staff_role(uuid)
    from public, anon, authenticated;
revoke all on function public.service_set_platform_staff(uuid, uuid, text, boolean, text)
    from public, anon, authenticated;
revoke all on function public.service_decide_publisher_application(uuid, uuid, text, text, text)
    from public, anon, authenticated;
revoke all on function public.service_submit_marketplace_version(uuid, uuid)
    from public, anon, authenticated;
revoke all on function public.service_decide_marketplace_submission(uuid, uuid, text, text)
    from public, anon, authenticated;
revoke all on function public.service_decide_marketplace_report(uuid, uuid, text, text)
    from public, anon, authenticated;
revoke all on function public.service_set_platform_feature_flag(uuid, text, boolean, text)
    from public, anon, authenticated;

grant execute on function public.service_get_platform_staff_role(uuid) to service_role;
grant execute on function public.service_set_platform_staff(uuid, uuid, text, boolean, text) to service_role;
grant execute on function public.service_decide_publisher_application(uuid, uuid, text, text, text) to service_role;
grant execute on function public.service_submit_marketplace_version(uuid, uuid) to service_role;
grant execute on function public.service_decide_marketplace_submission(uuid, uuid, text, text) to service_role;
grant execute on function public.service_decide_marketplace_report(uuid, uuid, text, text) to service_role;
grant execute on function public.service_set_platform_feature_flag(uuid, text, boolean, text) to service_role;

comment on table public.platform_staff_members is
    'Database-authoritative staff roles. Browser JWT metadata is never the moderation authorization source.';
comment on function public.service_decide_publisher_application(uuid, uuid, text, text, text) is
    'Service-only audited publisher decision and transactional publisher activation boundary.';
comment on function public.service_decide_marketplace_submission(uuid, uuid, text, text) is
    'Service-only audited package moderation boundary; approval stops before offline signing.';
