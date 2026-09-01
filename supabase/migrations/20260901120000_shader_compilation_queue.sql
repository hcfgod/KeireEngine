-- Remote shader compilation is additive and disabled by default. The desktop client never receives service-role
-- credentials; a networked coordinator owns these RPCs and hands content-addressed work to isolated executors.

insert into public.platform_feature_flags (key, enabled, description)
values ('remote_shader_compilation_enabled', false,
        'Allows the trusted shader compiler coordinator to enqueue remote content-addressed compilation work.')
on conflict (key) do nothing;

create type public.shader_compile_job_state as enum (
    'queued',
    'leased',
    'succeeded',
    'failed',
    'cancelled'
);

create table public.shader_compile_jobs (
    id uuid primary key default gen_random_uuid(),
    owner_user_id uuid not null references auth.users(id) on delete cascade,
    work_key text not null,
    manifest jsonb not null,
    priority smallint not null default 1,
    state public.shader_compile_job_state not null default 'queued',
    attempts smallint not null default 0,
    maximum_attempts smallint not null default 3,
    lease_owner text,
    lease_token uuid,
    lease_expires_at timestamptz,
    artifact_storage_path text,
    artifact_sha256 text,
    artifact_size_bytes bigint,
    diagnostics jsonb not null default '[]'::jsonb,
    completed_at timestamptz,
    created_at timestamptz not null default now(),
    updated_at timestamptz not null default now(),
    constraint shader_compile_jobs_owner_work_unique unique (owner_user_id, work_key),
    constraint shader_compile_jobs_work_key check (work_key ~ '^[0-9a-f]{64}$'),
    constraint shader_compile_jobs_manifest check (
        jsonb_typeof(manifest) = 'object' and
        octet_length(manifest::text) between 2 and 1048576 and
        manifest ->> 'schemaVersion' = '1' and
        coalesce(manifest ->> 'toolchainSha256', '') ~ '^[0-9a-f]{64}$' and
        coalesce(manifest ->> 'sourceSha256', '') ~ '^[0-9a-f]{64}$'
    ),
    constraint shader_compile_jobs_priority check (priority between 0 and 2),
    constraint shader_compile_jobs_attempts check (
        attempts between 0 and 10 and maximum_attempts between 1 and 10 and attempts <= maximum_attempts
    ),
    constraint shader_compile_jobs_lease check (
        (state = 'leased' and lease_owner is not null and lease_token is not null and lease_expires_at is not null) or
        (state <> 'leased' and lease_owner is null and lease_token is null and lease_expires_at is null)
    ),
    constraint shader_compile_jobs_artifact check (
        (state = 'succeeded' and artifact_storage_path is not null and
         artifact_sha256 ~ '^[0-9a-f]{64}$' and artifact_size_bytes between 1 and 67108864 and
         completed_at is not null) or
        (state <> 'succeeded' and artifact_storage_path is null and artifact_sha256 is null and
         artifact_size_bytes is null)
    ),
    constraint shader_compile_jobs_terminal check (
        (state in ('succeeded', 'failed', 'cancelled') and completed_at is not null) or
        (state in ('queued', 'leased') and completed_at is null)
    ),
    constraint shader_compile_jobs_diagnostics check (
        jsonb_typeof(diagnostics) = 'array' and jsonb_array_length(diagnostics) <= 512 and
        octet_length(diagnostics::text) <= 1048576
    ),
    constraint shader_compile_jobs_storage_path check (
        artifact_storage_path is null or
        (char_length(artifact_storage_path) between 76 and 512 and artifact_storage_path !~ '(^|/)\.\.(/|$)')
    )
);

create index idx_shader_compile_jobs_queue
on public.shader_compile_jobs (priority, created_at, id)
where state = 'queued';

create index idx_shader_compile_jobs_stale_leases
on public.shader_compile_jobs (lease_expires_at, id)
where state = 'leased';

create index idx_shader_compile_jobs_owner_recent
on public.shader_compile_jobs (owner_user_id, created_at desc, id desc);

alter table public.shader_compile_jobs enable row level security;
alter table public.shader_compile_jobs force row level security;
revoke all on table public.shader_compile_jobs from anon, authenticated;
grant select on table public.shader_compile_jobs to authenticated;
grant select, insert, update, delete on table public.shader_compile_jobs to service_role;

create policy shader_compile_jobs_owner_read
on public.shader_compile_jobs for select to authenticated
using ((select auth.uid()) = owner_user_id);

insert into storage.buckets (id, name, public, file_size_limit, allowed_mime_types)
values ('shader-compiler-artifacts', 'shader-compiler-artifacts', false, 67108864,
        array['application/octet-stream', 'application/vnd.keire.shader-artifact'])
on conflict (id) do update
set public = false,
    file_size_limit = excluded.file_size_limit,
    allowed_mime_types = excluded.allowed_mime_types;

create or replace function public.service_enqueue_shader_compile_job(
    p_owner_user_id uuid,
    p_work_key text,
    p_manifest jsonb,
    p_priority smallint default 1
)
returns table (
    job_id uuid,
    job_state public.shader_compile_job_state,
    artifact_storage_path text,
    artifact_sha256 text,
    artifact_size_bytes bigint
)
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_job public.shader_compile_jobs%rowtype;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' then
        raise exception using errcode = '42501', message = 'service_role_required';
    end if;
    if not coalesce((select enabled from public.platform_feature_flags
                     where key = 'remote_shader_compilation_enabled'), false) then
        raise exception using errcode = '55000', message = 'remote_shader_compilation_disabled';
    end if;
    if not exists (select 1 from auth.users where id = p_owner_user_id) or
       p_work_key !~ '^[0-9a-f]{64}$' or p_priority not between 0 and 2 or
       coalesce(jsonb_typeof(p_manifest), 'missing') <> 'object' or
       octet_length(p_manifest::text) not between 2 and 1048576 or
       p_manifest ->> 'schemaVersion' <> '1' or
       coalesce(p_manifest ->> 'toolchainSha256', '') !~ '^[0-9a-f]{64}$' or
       coalesce(p_manifest ->> 'sourceSha256', '') !~ '^[0-9a-f]{64}$' then
        raise exception using errcode = '22023', message = 'shader_compile_request_invalid';
    end if;

    insert into public.shader_compile_jobs (owner_user_id, work_key, manifest, priority)
    values (p_owner_user_id, p_work_key, p_manifest, p_priority)
    on conflict (owner_user_id, work_key) do nothing;

    select job.* into selected_job
    from public.shader_compile_jobs as job
    where job.owner_user_id = p_owner_user_id and job.work_key = p_work_key
    for update;
    if selected_job.manifest <> p_manifest then
        raise exception using errcode = '22000', message = 'shader_compile_work_key_collision';
    end if;

    if selected_job.state in ('failed', 'cancelled') then
        update public.shader_compile_jobs as job
        set state = 'queued', priority = least(job.priority, p_priority), attempts = 0,
            diagnostics = '[]'::jsonb, completed_at = null, updated_at = now()
        where job.id = selected_job.id
        returning job.* into selected_job;
    elsif selected_job.state = 'queued' and p_priority < selected_job.priority then
        update public.shader_compile_jobs as job
        set priority = p_priority, updated_at = now()
        where job.id = selected_job.id
        returning job.* into selected_job;
    end if;

    return query select selected_job.id, selected_job.state, selected_job.artifact_storage_path,
                        selected_job.artifact_sha256, selected_job.artifact_size_bytes;
end;
$$;

create or replace function public.service_lease_shader_compile_job(
    p_worker_id text,
    p_lease_seconds integer default 300
)
returns table (
    job_id uuid,
    owner_user_id uuid,
    work_key text,
    manifest jsonb,
    lease_token uuid,
    lease_expires_at timestamptz
)
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_job public.shader_compile_jobs%rowtype;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' then
        raise exception using errcode = '42501', message = 'service_role_required';
    end if;
    if p_worker_id !~ '^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$' or p_lease_seconds not between 30 and 900 then
        raise exception using errcode = '22023', message = 'shader_compile_lease_invalid';
    end if;

    update public.shader_compile_jobs as stale
    set state = case when stale.attempts >= stale.maximum_attempts
                     then 'failed'::public.shader_compile_job_state
                     else 'queued'::public.shader_compile_job_state end,
        diagnostics = case when stale.attempts >= stale.maximum_attempts
                           then jsonb_build_array(jsonb_build_object(
                               'code', 'REMOTE_RETRY_EXHAUSTED',
                               'message', 'Remote shader compilation exhausted its lease attempts.'))
                           else stale.diagnostics end,
        completed_at = case when stale.attempts >= stale.maximum_attempts then now() else null end,
        lease_owner = null, lease_token = null, lease_expires_at = null, updated_at = now()
    where stale.state = 'leased' and stale.lease_expires_at <= now();

    select job.* into selected_job
    from public.shader_compile_jobs as job
    where job.state = 'queued' and job.attempts < job.maximum_attempts
    order by job.priority, job.created_at, job.id
    for update skip locked
    limit 1;
    if not found then
        return;
    end if;

    update public.shader_compile_jobs as job
    set state = 'leased', attempts = job.attempts + 1, lease_owner = p_worker_id,
        lease_token = gen_random_uuid(), lease_expires_at = now() + make_interval(secs => p_lease_seconds),
        updated_at = now()
    where job.id = selected_job.id
    returning job.* into selected_job;

    return query select selected_job.id, selected_job.owner_user_id, selected_job.work_key, selected_job.manifest,
                        selected_job.lease_token, selected_job.lease_expires_at;
end;
$$;

create or replace function public.service_renew_shader_compile_lease(
    p_job_id uuid,
    p_worker_id text,
    p_lease_token uuid,
    p_lease_seconds integer default 300
)
returns timestamptz
language plpgsql
security definer
set search_path = ''
as $$
declare
    renewed_until timestamptz;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' then
        raise exception using errcode = '42501', message = 'service_role_required';
    end if;
    if p_worker_id !~ '^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$' or p_lease_seconds not between 30 and 900 then
        raise exception using errcode = '22023', message = 'shader_compile_lease_invalid';
    end if;
    update public.shader_compile_jobs as job
    set lease_expires_at = now() + make_interval(secs => p_lease_seconds), updated_at = now()
    where job.id = p_job_id and job.state = 'leased' and job.lease_owner = p_worker_id and
          job.lease_token = p_lease_token and job.lease_expires_at > now()
    returning job.lease_expires_at into renewed_until;
    if not found then
        raise exception using errcode = '55000', message = 'shader_compile_lease_lost';
    end if;
    return renewed_until;
end;
$$;

create or replace function public.service_complete_shader_compile_job(
    p_job_id uuid,
    p_worker_id text,
    p_lease_token uuid,
    p_artifact_storage_path text,
    p_artifact_sha256 text,
    p_artifact_size_bytes bigint,
    p_diagnostics jsonb default '[]'::jsonb
)
returns uuid
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_job public.shader_compile_jobs%rowtype;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' then
        raise exception using errcode = '42501', message = 'service_role_required';
    end if;
    if p_worker_id !~ '^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$' or
       p_artifact_sha256 !~ '^[0-9a-f]{64}$' or p_artifact_size_bytes not between 1 and 67108864 or
       coalesce(jsonb_typeof(p_diagnostics), 'missing') <> 'array' or
       jsonb_array_length(p_diagnostics) > 512 or octet_length(p_diagnostics::text) > 1048576 then
        raise exception using errcode = '22023', message = 'shader_compile_result_invalid';
    end if;

    select job.* into selected_job
    from public.shader_compile_jobs as job
    where job.id = p_job_id
    for update;
    if not found or selected_job.state <> 'leased' or selected_job.lease_owner <> p_worker_id or
       selected_job.lease_token <> p_lease_token or selected_job.lease_expires_at <= now() then
        raise exception using errcode = '55000', message = 'shader_compile_lease_lost';
    end if;
    if not starts_with(p_artifact_storage_path, 'artifacts/' || selected_job.work_key || '/') or
       char_length(p_artifact_storage_path) > 512 or p_artifact_storage_path ~ '(^|/)\.\.(/|$)' then
        raise exception using errcode = '22023', message = 'shader_compile_artifact_path_invalid';
    end if;

    update public.shader_compile_jobs as job
    set state = 'succeeded', artifact_storage_path = p_artifact_storage_path,
        artifact_sha256 = p_artifact_sha256, artifact_size_bytes = p_artifact_size_bytes,
        diagnostics = p_diagnostics, completed_at = now(), lease_owner = null, lease_token = null,
        lease_expires_at = null, updated_at = now()
    where job.id = selected_job.id;
    return selected_job.id;
end;
$$;

create or replace function public.service_fail_shader_compile_job(
    p_job_id uuid,
    p_worker_id text,
    p_lease_token uuid,
    p_retryable boolean,
    p_diagnostics jsonb
)
returns public.shader_compile_job_state
language plpgsql
security definer
set search_path = ''
as $$
declare
    selected_job public.shader_compile_jobs%rowtype;
    result_state public.shader_compile_job_state;
begin
    if coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role' then
        raise exception using errcode = '42501', message = 'service_role_required';
    end if;
    if p_worker_id !~ '^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$' or
       coalesce(jsonb_typeof(p_diagnostics), 'missing') <> 'array' or
       jsonb_array_length(p_diagnostics) > 512 or octet_length(p_diagnostics::text) > 1048576 then
        raise exception using errcode = '22023', message = 'shader_compile_failure_invalid';
    end if;
    select job.* into selected_job
    from public.shader_compile_jobs as job
    where job.id = p_job_id
    for update;
    if not found or selected_job.state <> 'leased' or selected_job.lease_owner <> p_worker_id or
       selected_job.lease_token <> p_lease_token or selected_job.lease_expires_at <= now() then
        raise exception using errcode = '55000', message = 'shader_compile_lease_lost';
    end if;
    result_state := case when p_retryable and selected_job.attempts < selected_job.maximum_attempts
                         then 'queued'::public.shader_compile_job_state
                         else 'failed'::public.shader_compile_job_state end;
    update public.shader_compile_jobs as job
    set state = result_state, diagnostics = p_diagnostics,
        completed_at = case when result_state = 'failed' then now() else null end,
        lease_owner = null, lease_token = null, lease_expires_at = null, updated_at = now()
    where job.id = selected_job.id;
    return result_state;
end;
$$;

revoke all on function public.service_enqueue_shader_compile_job(uuid, text, jsonb, smallint)
    from public, anon, authenticated;
revoke all on function public.service_lease_shader_compile_job(text, integer)
    from public, anon, authenticated;
revoke all on function public.service_renew_shader_compile_lease(uuid, text, uuid, integer)
    from public, anon, authenticated;
revoke all on function public.service_complete_shader_compile_job(uuid, text, uuid, text, text, bigint, jsonb)
    from public, anon, authenticated;
revoke all on function public.service_fail_shader_compile_job(uuid, text, uuid, boolean, jsonb)
    from public, anon, authenticated;
grant execute on function public.service_enqueue_shader_compile_job(uuid, text, jsonb, smallint) to service_role;
grant execute on function public.service_lease_shader_compile_job(text, integer) to service_role;
grant execute on function public.service_renew_shader_compile_lease(uuid, text, uuid, integer) to service_role;
grant execute on function public.service_complete_shader_compile_job(uuid, text, uuid, text, text, bigint, jsonb)
    to service_role;
grant execute on function public.service_fail_shader_compile_job(uuid, text, uuid, boolean, jsonb) to service_role;

comment on table public.shader_compile_jobs is
    'Tenant-scoped metadata for content-addressed remote shader compilation. Artifact payloads remain private.';
comment on function public.service_lease_shader_compile_job(text, integer) is
    'Service-role-only priority lease with stale recovery. Networkless executors never receive this credential.';
