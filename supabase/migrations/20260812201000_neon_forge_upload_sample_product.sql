-- Seed the deterministic mixed-content package as an unpublished upload target for the
-- approved first-party staging publisher. Rollback: delete this draft only while it has
-- no versions, submissions, publications, orders, or entitlements.

insert into public.marketplace_products (
    id,
    publisher_id,
    category_id,
    slug,
    display_name,
    short_description,
    description_markdown,
    state,
    license_spdx,
    license_revision,
    support_url,
    repository_url,
    featured
)
select
    '03100000-0000-4000-8000-000000000191'::uuid,
    publisher.id,
    category.id,
    'neon-forge-creator-pack',
    'Neon Forge Creator Pack',
    'Mixed-content upload sample with VFX, Shader Graph, Material Graph, and managed C# assets.',
    E'A deterministic first-party package for exercising the complete Marketplace upload and validation path.\n\n'
        || E'The 1.0.0 package contains two VFX graphs, two Shader Graphs, two Material Graphs, and one runtime C# behaviour.',
    'draft'::public.marketplace_product_state,
    'MIT',
    '1',
    'https://keireengine.duckdns.org/contact/',
    'https://github.com/hcfgod/KeireEngine',
    false
from public.publishers publisher
join public.marketplace_categories category on category.slug = 'rendering'
where publisher.slug = 'keire-engine'
on conflict (id) do nothing;

do $$
begin
    if not exists (
        select 1
        from public.marketplace_products product
        where product.id = '03100000-0000-4000-8000-000000000191'::uuid
          and product.slug = 'neon-forge-creator-pack'
          and product.state = 'draft'
    ) then
        raise exception using errcode = '23514', message = 'neon_forge_upload_sample_seed_failed';
    end if;
end;
$$;
