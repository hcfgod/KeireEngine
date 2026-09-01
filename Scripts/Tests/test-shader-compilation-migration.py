#!/usr/bin/env python3
"""Static security-contract checks for the remote shader compilation queue."""

from __future__ import annotations

import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[2]
MIGRATION = (
    ROOT
    / "supabase/migrations/20260901120000_shader_compilation_queue.sql"
)
RLS_TEST = ROOT / "supabase/tests/shader_compile_jobs_rls.test.sql"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


source = MIGRATION.read_text(encoding="utf-8")
rls_test = RLS_TEST.read_text(encoding="utf-8")

require(
    "('remote_shader_compilation_enabled', false" in source,
    "Remote shader compilation must remain disabled by default.",
)
require(
    "('shader-compiler-artifacts', 'shader-compiler-artifacts', false" in source,
    "The shader artifact bucket must remain private.",
)
require(
    "alter table public.shader_compile_jobs enable row level security;" in source
    and "alter table public.shader_compile_jobs force row level security;" in source,
    "Shader compilation jobs must enable and force RLS.",
)
require(
    "revoke all on table public.shader_compile_jobs from anon, authenticated;"
    in source,
    "Client table privileges must be revoked before the owner read grant.",
)
require(
    "grant select on table public.shader_compile_jobs to authenticated;" in source,
    "Authenticated users need only owner-scoped read access.",
)
require(
    not re.search(
        r"grant\s+(?:insert|update|delete|all).*shader_compile_jobs.*authenticated",
        source,
        flags=re.IGNORECASE,
    ),
    "Authenticated clients must not mutate compilation jobs directly.",
)
require(
    "using ((select auth.uid()) = owner_user_id);" in source,
    "The owner read policy must bind rows to auth.uid().",
)
require(
    "order by job.priority, job.created_at, job.id" in source
    and "for update skip locked" in source,
    "Leasing must retain deterministic priority order and skip locked workers.",
)
require(
    "on conflict (owner_user_id, work_key) do nothing" in source
    and "shader_compile_work_key_collision" in source,
    "Enqueue must be idempotent without accepting a manifest collision.",
)
require(
    not re.search(r"\bauth\.role\s*\(", source),
    "New migrations must not use deprecated auth.role().",
)

service_functions = (
    "service_enqueue_shader_compile_job",
    "service_lease_shader_compile_job",
    "service_renew_shader_compile_lease",
    "service_complete_shader_compile_job",
    "service_fail_shader_compile_job",
)
for function in service_functions:
    definition = re.search(
        rf"create or replace function public\.{function}\(.*?\$\$;",
        source,
        flags=re.DOTALL,
    )
    require(definition is not None, f"Missing service RPC: {function}.")
    body = definition.group(0)
    require(
        "security definer" in body and "set search_path = ''" in body,
        f"{function} must pin its SECURITY DEFINER search path.",
    )
    require(
        "coalesce((select auth.jwt() ->> 'role'), '') <> 'service_role'" in body,
        f"{function} must assert the service-role JWT.",
    )
    require(
        re.search(
            rf"revoke all on function public\.{function}\([^;]+\)\s+"
            r"from public, anon, authenticated;",
            source,
        )
        is not None,
        f"{function} must revoke client execution.",
    )
    require(
        re.search(
            rf"grant execute on function public\.{function}\([^;]+\)\s+"
            r"to service_role;",
            source,
        )
        is not None,
        f"{function} must grant only service-role execution.",
    )

require("select plan(10);" in rls_test, "The RLS test plan changed unexpectedly.")
for contract in (
    "the owner can read the owned compilation job",
    "another user cannot read the owner compilation job",
    "the owner cannot insert directly",
    "another user cannot update compilation jobs",
    "another user cannot delete compilation jobs",
):
    require(contract in rls_test, f"RLS regression coverage is missing: {contract}.")

print("Shader compilation migration security checks passed.")
