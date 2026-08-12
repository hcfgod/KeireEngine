-- Consolidate permissive policies so each role/action evaluates one authorization
-- expression. Published/approved listing content is immutable to publishers.
-- Rollback: restore the original *_publisher_write ALL policies and the two review
-- update policies from the foundation/storage migrations.

drop policy if exists product_tags_publisher_write on public.marketplace_product_tags;
create policy product_tags_publisher_insert on public.marketplace_product_tags
for insert to authenticated
with check (exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_product_tags.product_id
      and private.can_manage_publisher(product.publisher_id)
      and product.state in ('draft', 'changes_requested')
));
create policy product_tags_publisher_update on public.marketplace_product_tags
for update to authenticated
using (exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_product_tags.product_id
      and private.can_manage_publisher(product.publisher_id)
      and product.state in ('draft', 'changes_requested')
))
with check (exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_product_tags.product_id
      and private.can_manage_publisher(product.publisher_id)
      and product.state in ('draft', 'changes_requested')
));
create policy product_tags_publisher_delete on public.marketplace_product_tags
for delete to authenticated
using (exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_product_tags.product_id
      and private.can_manage_publisher(product.publisher_id)
      and product.state in ('draft', 'changes_requested')
));

drop policy if exists media_publisher_write on public.marketplace_product_media;
create policy media_publisher_insert on public.marketplace_product_media
for insert to authenticated
with check (approved_at is null and exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_product_media.product_id
      and private.can_manage_publisher(product.publisher_id)
      and product.state in ('draft', 'changes_requested')
));
create policy media_publisher_update on public.marketplace_product_media
for update to authenticated
using (private.is_platform_staff('moderator') or (approved_at is null and exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_product_media.product_id
      and private.can_manage_publisher(product.publisher_id)
      and product.state in ('draft', 'changes_requested')
)))
with check (private.is_platform_staff('moderator') or (approved_at is null and exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_product_media.product_id
      and private.can_manage_publisher(product.publisher_id)
      and product.state in ('draft', 'changes_requested')
)));
create policy media_publisher_delete on public.marketplace_product_media
for delete to authenticated
using (private.is_platform_staff('moderator') or (approved_at is null and exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_product_media.product_id
      and private.can_manage_publisher(product.publisher_id)
      and product.state in ('draft', 'changes_requested')
)));

drop policy if exists versions_publisher_write on public.marketplace_product_versions;
create policy versions_publisher_insert on public.marketplace_product_versions
for insert to authenticated
with check (state = 'draft' and exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_product_versions.product_id
      and private.can_manage_publisher(product.publisher_id)
));
create policy versions_publisher_update on public.marketplace_product_versions
for update to authenticated
using (exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_product_versions.product_id
      and private.can_manage_publisher(product.publisher_id)
) and state not in ('published', 'withdrawn', 'security_revoked'))
with check (exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_product_versions.product_id
      and private.can_manage_publisher(product.publisher_id)
) and state not in ('published', 'withdrawn', 'security_revoked'));
create policy versions_publisher_delete on public.marketplace_product_versions
for delete to authenticated
using (state = 'draft' and exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_product_versions.product_id
      and private.can_manage_publisher(product.publisher_id)
));

drop policy if exists dependencies_publisher_write on public.marketplace_version_dependencies;
create policy dependencies_publisher_insert on public.marketplace_version_dependencies
for insert to authenticated
with check (exists (
    select 1
    from public.marketplace_product_versions version
    join public.marketplace_products product on product.id = version.product_id
    where version.id = marketplace_version_dependencies.version_id
      and private.can_manage_publisher(product.publisher_id)
      and version.state in ('draft', 'validation_failed', 'changes_requested')
));
create policy dependencies_publisher_update on public.marketplace_version_dependencies
for update to authenticated
using (exists (
    select 1
    from public.marketplace_product_versions version
    join public.marketplace_products product on product.id = version.product_id
    where version.id = marketplace_version_dependencies.version_id
      and private.can_manage_publisher(product.publisher_id)
      and version.state in ('draft', 'validation_failed', 'changes_requested')
))
with check (exists (
    select 1
    from public.marketplace_product_versions version
    join public.marketplace_products product on product.id = version.product_id
    where version.id = marketplace_version_dependencies.version_id
      and private.can_manage_publisher(product.publisher_id)
      and version.state in ('draft', 'validation_failed', 'changes_requested')
));
create policy dependencies_publisher_delete on public.marketplace_version_dependencies
for delete to authenticated
using (exists (
    select 1
    from public.marketplace_product_versions version
    join public.marketplace_products product on product.id = version.product_id
    where version.id = marketplace_version_dependencies.version_id
      and private.can_manage_publisher(product.publisher_id)
      and version.state in ('draft', 'validation_failed', 'changes_requested')
));

drop policy if exists reviews_author_update on public.marketplace_reviews;
drop policy if exists reviews_publisher_reply_update on public.marketplace_reviews;
create policy reviews_author_publisher_or_staff_update on public.marketplace_reviews
for update to authenticated
using (author_user_id = (select auth.uid()) or private.is_platform_staff('moderator') or exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_reviews.product_id
      and private.can_manage_publisher(product.publisher_id)
))
with check (author_user_id = (select auth.uid()) or private.is_platform_staff('moderator') or exists (
    select 1 from public.marketplace_products product
    where product.id = marketplace_reviews.product_id
      and private.can_manage_publisher(product.publisher_id)
));
