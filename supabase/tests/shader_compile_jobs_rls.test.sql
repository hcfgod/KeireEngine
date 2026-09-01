begin;

select plan(10);

insert into auth.users (id, email)
values
    ('31000000-0000-4000-8000-000000000001', 'shader-owner@example.com'),
    ('31000000-0000-4000-8000-000000000002', 'shader-other@example.com');

insert into public.shader_compile_jobs (owner_user_id, work_key, manifest)
values (
    '31000000-0000-4000-8000-000000000001',
    repeat('a', 64),
    jsonb_build_object(
        'schemaVersion', 1,
        'toolchainSha256', repeat('b', 64),
        'sourceSha256', repeat('c', 64)
    )
);

select ok(has_table_privilege('authenticated', 'public.shader_compile_jobs', 'select'),
          'authenticated users can read their visible compilation jobs');
select ok(not has_table_privilege('anon', 'public.shader_compile_jobs', 'select'),
          'anonymous users cannot read compilation jobs');
select ok(not has_table_privilege('authenticated', 'public.shader_compile_jobs', 'insert'),
          'desktop users cannot bypass the coordinator to enqueue work');
select ok(not has_table_privilege('authenticated', 'public.shader_compile_jobs', 'update'),
          'desktop users cannot forge job state transitions');
select ok(not has_table_privilege('authenticated', 'public.shader_compile_jobs', 'delete'),
          'desktop users cannot delete compilation audit state');

set local role authenticated;
select set_config('request.jwt.claims',
                  '{"sub":"31000000-0000-4000-8000-000000000001","role":"authenticated"}', true);
select results_eq(
    $$select count(*)::bigint from public.shader_compile_jobs$$,
    array[1::bigint],
    'the owner can read the owned compilation job'
);
select throws_ok(
    $$insert into public.shader_compile_jobs (owner_user_id, work_key, manifest)
      values ('31000000-0000-4000-8000-000000000001', repeat('d', 64), '{}'::jsonb)$$,
    '42501', null,
    'the owner cannot insert directly'
);

reset role;
set local role authenticated;
select set_config('request.jwt.claims',
                  '{"sub":"31000000-0000-4000-8000-000000000002","role":"authenticated"}', true);
select results_eq(
    $$select count(*)::bigint from public.shader_compile_jobs$$,
    array[0::bigint],
    'another user cannot read the owner compilation job'
);
select throws_ok(
    $$update public.shader_compile_jobs set priority = 0$$,
    '42501', null,
    'another user cannot update compilation jobs'
);
select throws_ok(
    $$delete from public.shader_compile_jobs$$,
    '42501', null,
    'another user cannot delete compilation jobs'
);

select * from finish();
rollback;
