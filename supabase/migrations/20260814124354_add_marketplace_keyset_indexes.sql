-- Keep public catalog and authenticated library pagination index-backed at every page depth.
-- Rollback: drop idx_marketplace_products_public_keyset,
-- idx_marketplace_entitlements_personal_keyset, and idx_marketplace_entitlements_organization_keyset.

create index if not exists idx_marketplace_products_public_keyset
    on public.marketplace_products (featured desc, published_at desc, id)
    where state = 'published';

create index if not exists idx_marketplace_entitlements_personal_keyset
    on public.marketplace_entitlements (user_id, granted_at desc, id)
    where revoked_at is null and organization_id is null;

create index if not exists idx_marketplace_entitlements_organization_keyset
    on public.marketplace_entitlements (organization_id, granted_at desc, id)
    where revoked_at is null;
