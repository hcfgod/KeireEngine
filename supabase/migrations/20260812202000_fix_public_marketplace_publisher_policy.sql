-- Published publishers must remain readable to anonymous catalog clients without
-- granting those clients direct access to protected organization memberships.
-- Rollback: restore publishers_public_read from 20260812032747_marketplace_foundation.sql.

drop policy if exists publishers_public_read on public.publishers;
create policy publishers_public_read on public.publishers for select to anon, authenticated
    using (suspended_at is null or private.can_manage_publisher(id) or private.is_platform_staff('moderator'));

comment on policy publishers_public_read on public.publishers is
    'Public active publishers plus protected owner/staff access, without exposing organization memberships.';
