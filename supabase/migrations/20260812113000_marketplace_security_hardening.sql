-- Harden the disabled 0.3.1 marketplace before any public feature flag is enabled.
-- Rollback: drop the indexes and read policies below, restore publishers_public_read,
-- and only restore authenticated RPC execution after the bounded Edge Function adapters exist.

-- Browser and desktop clients must enter privileged, multi-table transitions through
-- narrowly scoped Edge Functions. The service role is the only direct RPC caller.
revoke all on function public.create_marketplace_organization(text, text)
    from public, anon, authenticated;
revoke all on function public.claim_free_marketplace_product(uuid, uuid, text, text, text, text)
    from public, anon, authenticated;
revoke all on function public.register_marketplace_device_session(text, text, text)
    from public, anon, authenticated;
revoke all on function public.issue_marketplace_download_grant(uuid, uuid, uuid)
    from public, anon, authenticated;
grant execute on function public.create_marketplace_organization(text, text) to service_role;
grant execute on function public.claim_free_marketplace_product(uuid, uuid, text, text, text, text) to service_role;
grant execute on function public.register_marketplace_device_session(text, text, text) to service_role;
grant execute on function public.issue_marketplace_download_grant(uuid, uuid, uuid) to service_role;

comment on function public.create_marketplace_organization(text, text) is
    'Internal marketplace transition. Invoke only through the authenticated marketplace Edge Function boundary.';
comment on function public.claim_free_marketplace_product(uuid, uuid, text, text, text, text) is
    'Internal free-entitlement transition. Invoke only through the authenticated marketplace Edge Function boundary.';
comment on function public.register_marketplace_device_session(text, text, text) is
    'Internal Hub session transition. Invoke only through the authenticated marketplace Edge Function boundary.';
comment on function public.issue_marketplace_download_grant(uuid, uuid, uuid) is
    'Internal download transition. Entitlement and artifact signature verification remain independent.';

-- Qualify the outer publisher row explicitly. The original expression could resolve
-- organization_id to the inner membership row and broaden suspended-publisher reads.
drop policy if exists publishers_public_read on public.publishers;
create policy publishers_public_read on public.publishers for select to anon, authenticated
    using (suspended_at is null or private.is_platform_staff('moderator') or exists (
        select 1 from public.organization_memberships membership
        where membership.organization_id = publishers.organization_id
          and membership.user_id = (select auth.uid())
          and membership.role in ('owner', 'admin')));

grant select on public.marketplace_validation_reports, public.marketplace_publications,
    public.platform_audit_events to authenticated;

create policy validation_reports_publisher_or_staff_read
on public.marketplace_validation_reports for select to authenticated
using (private.is_platform_staff('moderator') or exists (
    select 1
    from public.marketplace_uploads upload
    join public.marketplace_product_versions version on version.id = upload.version_id
    join public.marketplace_products product on product.id = version.product_id
    where upload.id = marketplace_validation_reports.upload_id
      and private.can_manage_publisher(product.publisher_id)
));

create policy publications_publisher_or_staff_read
on public.marketplace_publications for select to authenticated
using (private.is_platform_staff('moderator') or exists (
    select 1
    from public.marketplace_product_versions version
    join public.marketplace_products product on product.id = version.product_id
    where version.id = marketplace_publications.version_id
      and private.can_manage_publisher(product.publisher_id)
));

create policy audit_events_staff_read
on public.platform_audit_events for select to authenticated
using (private.is_platform_staff('moderator'));

-- Every foreign-key target used by deletes, joins, authorization checks, and moderation
-- queues needs a covering index before production-sized catalogs are admitted.
create index if not exists idx_marketplace_download_grants_entitlement
    on public.marketplace_download_grants (entitlement_id);
create index if not exists idx_marketplace_download_grants_session
    on public.marketplace_download_grants (session_id);
create index if not exists idx_marketplace_download_grants_version
    on public.marketplace_download_grants (version_id);
create index if not exists idx_marketplace_entitlements_order_item
    on public.marketplace_entitlements (order_item_id);
create index if not exists idx_marketplace_entitlements_organization
    on public.marketplace_entitlements (organization_id);
create index if not exists idx_marketplace_entitlements_user
    on public.marketplace_entitlements (user_id);
create index if not exists idx_marketplace_license_revisions_approved_by
    on public.marketplace_license_revisions (approved_by);
create index if not exists idx_marketplace_order_items_offer
    on public.marketplace_order_items (offer_id);
create index if not exists idx_marketplace_order_items_product
    on public.marketplace_order_items (product_id);
create index if not exists idx_marketplace_orders_organization
    on public.marketplace_orders (organization_id);
create index if not exists idx_marketplace_product_media_product
    on public.marketplace_product_media (product_id);
create index if not exists idx_marketplace_product_tags_tag
    on public.marketplace_product_tags (tag_id);
create index if not exists idx_marketplace_products_category
    on public.marketplace_products (category_id);
create index if not exists idx_marketplace_products_license
    on public.marketplace_products (license_spdx, license_revision);
create index if not exists idx_marketplace_publications_approved_by
    on public.marketplace_publications (approved_by);
create index if not exists idx_marketplace_reports_assigned_to
    on public.marketplace_reports (assigned_to);
create index if not exists idx_marketplace_reports_product
    on public.marketplace_reports (product_id);
create index if not exists idx_marketplace_reports_reporter
    on public.marketplace_reports (reporter_user_id);
create index if not exists idx_marketplace_reports_review
    on public.marketplace_reports (review_id);
create index if not exists idx_marketplace_reviews_author
    on public.marketplace_reviews (author_user_id);
create index if not exists idx_marketplace_reviews_owner_organization
    on public.marketplace_reviews (owner_organization_id);
create index if not exists idx_marketplace_submissions_assigned_to
    on public.marketplace_submissions (assigned_to);
create index if not exists idx_marketplace_submissions_submitted_by
    on public.marketplace_submissions (submitted_by);
create index if not exists idx_marketplace_submissions_validation_report
    on public.marketplace_submissions (validation_report_id);
create index if not exists idx_marketplace_uploads_created_by
    on public.marketplace_uploads (created_by);
create index if not exists idx_marketplace_uploads_version
    on public.marketplace_uploads (version_id);
create index if not exists idx_marketplace_wishlist_product
    on public.marketplace_wishlist_items (product_id);
create index if not exists idx_oauth_device_sessions_user
    on public.oauth_device_sessions (user_id);
create index if not exists idx_organization_memberships_invited_by
    on public.organization_memberships (invited_by);
create index if not exists idx_organization_memberships_user
    on public.organization_memberships (user_id);
create index if not exists idx_organizations_created_by
    on public.organizations (created_by);
create index if not exists idx_platform_audit_events_actor_session
    on public.platform_audit_events (actor_session_id);
create index if not exists idx_platform_audit_events_actor_user
    on public.platform_audit_events (actor_user_id);
create index if not exists idx_platform_feature_flags_updated_by
    on public.platform_feature_flags (updated_by);
create index if not exists idx_publisher_applications_applicant
    on public.publisher_applications (applicant_user_id);
create index if not exists idx_publisher_applications_organization
    on public.publisher_applications (organization_id);
create index if not exists idx_publisher_applications_reviewed_by
    on public.publisher_applications (reviewed_by);
