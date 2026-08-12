-- Keep terminal moderation decisions out of the active queue while preserving immutable
-- review evidence. Approval for offline signing may be withdrawn only by an administrator
-- before publication. Rollback: restore service_decide_marketplace_submission from
-- 20260812165208_marketplace_staff_moderation.sql.

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
      and submission.state in ('submitted', 'in_review', 'approved_pending_signature')
    for update;
    if not found then
        raise exception using errcode = 'P0002', message = 'marketplace_submission_not_reviewable';
    end if;
    if selected_submission.state = 'approved_pending_signature' and
       (p_decision <> 'rejected' or
        not private.service_actor_is_staff(p_actor_user_id, 'administrator')) then
        raise exception using errcode = '42501', message = 'marketplace_signing_approval_withdrawal_requires_administrator';
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
        p_actor_user_id,
        case when selected_submission.state = 'approved_pending_signature'
            then 'marketplace.signing_approval_withdrawn'
            else 'marketplace.submission_decided'
        end,
        'marketplace_submission',
        selected_submission.id::text,
        jsonb_build_object(
            'decision', p_decision,
            'previousState', selected_submission.state,
            'versionId', selected_version.id
        )
    );

    return query select selected_submission.id, p_decision::public.marketplace_submission_state;
end;
$$;

revoke all on function public.service_decide_marketplace_submission(uuid, uuid, text, text)
    from public, anon, authenticated;
grant execute on function public.service_decide_marketplace_submission(uuid, uuid, text, text)
    to service_role;

comment on function public.service_decide_marketplace_submission(uuid, uuid, text, text) is
    'Service-only audited package moderation. Terminal decisions leave the active queue; administrators may withdraw pre-publication signing approval.';
