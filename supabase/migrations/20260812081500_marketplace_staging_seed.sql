-- Additive staging-only catalog seeding helper. This creates drafts and free offers; it never
-- publishes an unsigned version or enables a feature flag. Rollback: drop this private function.

create or replace function private.seed_official_marketplace_drafts(p_owner_user_id uuid)
returns table (product_id uuid, product_slug text)
language plpgsql
security definer
set search_path = ''
as $$
declare
    v_organization_id constant uuid := '03100000-0000-4000-8000-000000000001';
    v_publisher_id constant uuid := '03100000-0000-4000-8000-000000000002';
begin
    if not exists (select 1 from auth.users where id = p_owner_user_id) then
        raise exception using errcode = '22023', message = 'owner_user_missing';
    end if;

    insert into public.organizations (id, slug, display_name, created_by)
    values (v_organization_id, 'keire-engine', 'Kéire Engine', p_owner_user_id)
    on conflict (id) do update
    set display_name = excluded.display_name,
        updated_at = now();

    insert into public.organization_memberships (organization_id, user_id, role)
    values (v_organization_id, p_owner_user_id, 'owner')
    on conflict (organization_id, user_id) do update set role = 'owner';

    insert into public.publishers
        (id, organization_id, slug, display_name, summary, website_url, verified)
    values
        (v_publisher_id, v_organization_id, 'keire-engine', 'Kéire Engine',
         'Official, versioned content maintained with the Kéire Engine release process.',
         'https://keireengine.duckdns.org/', true)
    on conflict (id) do update
    set display_name = excluded.display_name,
        summary = excluded.summary,
        website_url = excluded.website_url,
        verified = true,
        updated_at = now();

    insert into public.marketplace_categories (id, slug, display_name, description, sort_order)
    values
        ('03100000-0000-4000-8000-000000000010', 'environment-content', 'Environment Content',
         'Production-ready scenes, meshes, textures, and example projects.', 10),
        ('03100000-0000-4000-8000-000000000011', 'rendering', 'Rendering',
         'Shader Graph, Material Graph, and renderer-focused content.', 20),
        ('03100000-0000-4000-8000-000000000012', 'vfx', 'Visual Effects',
         'VFX Graph systems, effects, and reusable authoring examples.', 30),
        ('03100000-0000-4000-8000-000000000013', 'code', 'Code and Gameplay',
         'Managed gameplay, editor tooling, and integration samples.', 40),
        ('03100000-0000-4000-8000-000000000014', 'ui-input', 'UI and Input',
         'User-interface, navigation, and input-action foundations.', 50)
    on conflict (id) do update
    set display_name = excluded.display_name,
        description = excluded.description,
        sort_order = excluded.sort_order,
        active = true;

    insert into public.marketplace_products
        (id, publisher_id, category_id, slug, display_name, short_description, description_markdown, state,
         license_spdx, license_revision, support_url, repository_url, featured)
    values
        ('03100000-0000-4000-8000-000000000101', v_publisher_id,
         '03100000-0000-4000-8000-000000000010', 'keire-sandbox-content-pack',
         'Kéire Sandbox Content Pack',
         'The organized production sandbox scene, reusable assets, and project examples maintained with Kéire.',
         'A complete official content baseline for validating asset import, rendering, scripting, and project upgrades.',
         'draft', 'MIT', '1', 'https://keireengine.duckdns.org/contact/',
         'https://github.com/hcfgod/KeireEngine', true),
        ('03100000-0000-4000-8000-000000000102', v_publisher_id,
         '03100000-0000-4000-8000-000000000011', 'shader-material-graph-showcase',
         'Shader and Material Graph Showcase',
         'Official custom Shader Graphs and Material Graphs ranging from foundational to advanced production examples.',
         'Demonstrates the separation between reusable shader programs, material authoring, and lightweight instances.',
         'draft', 'MIT', '1', 'https://keireengine.duckdns.org/contact/',
         'https://github.com/hcfgod/KeireEngine', true),
        ('03100000-0000-4000-8000-000000000103', v_publisher_id,
         '03100000-0000-4000-8000-000000000012', 'vfx-starter-pack', 'VFX Starter Pack',
         'Reusable VFX Graph examples with deterministic simulation, events, debugging, and profiling scenarios.',
         'A focused set of effects designed to exercise Kéire VFX authoring and production diagnostics.',
         'draft', 'MIT', '1', 'https://keireengine.duckdns.org/contact/',
         'https://github.com/hcfgod/KeireEngine', false),
        ('03100000-0000-4000-8000-000000000104', v_publisher_id,
         '03100000-0000-4000-8000-000000000013', 'gameplay-csharp-samples', 'Gameplay and C# Samples',
         'Managed gameplay examples with explicit runtime/editor assembly boundaries and package-code trust metadata.',
         'Samples for component lifecycle, asset references, input, diagnostics, and safe managed-code package consent.',
         'draft', 'MIT', '1', 'https://keireengine.duckdns.org/contact/',
         'https://github.com/hcfgod/KeireEngine', false),
        ('03100000-0000-4000-8000-000000000105', v_publisher_id,
         '03100000-0000-4000-8000-000000000014', 'starter-ui-input-assets', 'Starter UI and Input Assets',
         'Accessible interface foundations and input-action examples for keyboard, pointer, and controller workflows.',
         'A starter set for consistent UI navigation, action maps, icons, prompts, and cross-device interaction testing.',
         'draft', 'MIT', '1', 'https://keireengine.duckdns.org/contact/',
         'https://github.com/hcfgod/KeireEngine', false)
    on conflict (id) do update
    set category_id = excluded.category_id,
        display_name = excluded.display_name,
        short_description = excluded.short_description,
        description_markdown = excluded.description_markdown,
        license_spdx = excluded.license_spdx,
        license_revision = excluded.license_revision,
        support_url = excluded.support_url,
        repository_url = excluded.repository_url,
        featured = excluded.featured,
        updated_at = now();

    insert into public.marketplace_offers (product_id, currency, amount_minor, active)
    select product.id, 'USD', 0, true
    from public.marketplace_products as product
    where product.publisher_id = v_publisher_id
      and product.id in
          ('03100000-0000-4000-8000-000000000101', '03100000-0000-4000-8000-000000000102',
           '03100000-0000-4000-8000-000000000103', '03100000-0000-4000-8000-000000000104',
           '03100000-0000-4000-8000-000000000105')
      and not exists (
          select 1 from public.marketplace_offers as offer
          where offer.product_id = product.id and offer.active
      );

    return query
    select product.id, product.slug
    from public.marketplace_products as product
    where product.publisher_id = v_publisher_id
      and product.id in
          ('03100000-0000-4000-8000-000000000101', '03100000-0000-4000-8000-000000000102',
           '03100000-0000-4000-8000-000000000103', '03100000-0000-4000-8000-000000000104',
           '03100000-0000-4000-8000-000000000105')
    order by product.slug;
end;
$$;

revoke all on function private.seed_official_marketplace_drafts(uuid) from public, anon, authenticated;

comment on function private.seed_official_marketplace_drafts(uuid) is
    'Seeds idempotent official staging drafts. It never publishes artifacts or changes feature flags.';
