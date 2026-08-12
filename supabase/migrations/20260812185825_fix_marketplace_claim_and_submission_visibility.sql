-- Keep PL/pgSQL locals distinct from marketplace table columns. The original
-- order_id local made the marketplace_order_items conflict target ambiguous,
-- aborting every first-time free claim after the order row was created.
create or replace function public.claim_free_marketplace_product(
    p_product_id uuid,
    p_organization_id uuid,
    p_idempotency_key text,
    p_license_spdx text,
    p_license_revision text,
    p_accepted_license_snapshot text
)
returns uuid
language plpgsql
security definer
set search_path = ''
as $$
declare
    current_user_id uuid := (select auth.uid());
    selected_product public.marketplace_products%rowtype;
    selected_offer public.marketplace_offers%rowtype;
    expected_license_snapshot text;
    selected_order_id uuid;
    selected_order_item_id uuid;
    selected_entitlement_id uuid;
begin
    if current_user_id is null then
        raise exception using errcode = '42501', message = 'authentication_required';
    end if;
    if not coalesce((select enabled from public.platform_feature_flags where key = 'marketplace_enabled'), false) then
        raise exception using errcode = '55000', message = 'marketplace_disabled';
    end if;
    if char_length(p_idempotency_key) not between 16 and 128 then
        raise exception using errcode = '22023', message = 'invalid_idempotency_key';
    end if;
    if p_organization_id is not null and not private.is_organization_member(
        p_organization_id, array['owner', 'admin']::public.organization_role[]) then
        raise exception using errcode = '42501', message = 'organization_authorization_required';
    end if;

    select * into selected_product
    from public.marketplace_products product
    where product.id = p_product_id and product.state = 'published'
    for share;
    if not found then
        raise exception using errcode = 'P0002', message = 'product_not_found';
    end if;
    if selected_product.license_spdx <> p_license_spdx or
       selected_product.license_revision <> p_license_revision then
        raise exception using errcode = '40001', message = 'license_revision_changed';
    end if;
    select license.acceptance_snapshot into expected_license_snapshot
    from public.marketplace_license_revisions license
    where license.license_id = selected_product.license_spdx and
          license.revision = selected_product.license_revision;
    if expected_license_snapshot is null or expected_license_snapshot <> p_accepted_license_snapshot then
        raise exception using errcode = '40001', message = 'license_revision_changed';
    end if;

    select * into selected_offer
    from public.marketplace_offers offer
    where offer.product_id = p_product_id and offer.active and offer.amount_minor = 0
    for share;
    if not found then
        raise exception using errcode = '55000', message = 'free_offer_unavailable';
    end if;

    perform pg_advisory_xact_lock(hashtextextended(
        current_user_id::text || ':' || coalesce(p_organization_id::text, 'personal') || ':' || p_product_id::text, 0));

    select entitlement.id into selected_entitlement_id
    from public.marketplace_entitlements entitlement
    where entitlement.product_id = p_product_id
      and ((p_organization_id is null and entitlement.user_id = current_user_id) or
           (p_organization_id is not null and entitlement.organization_id = p_organization_id));
    if selected_entitlement_id is not null then
        return selected_entitlement_id;
    end if;

    insert into public.marketplace_orders
        (user_id, organization_id, currency, total_minor, status, idempotency_key)
    values
        (current_user_id, p_organization_id, selected_offer.currency, 0, 'completed', p_idempotency_key)
    on conflict (user_id, idempotency_key) do nothing
    returning id into selected_order_id;
    if selected_order_id is null then
        select marketplace_order.id into selected_order_id
        from public.marketplace_orders marketplace_order
        where marketplace_order.user_id = current_user_id and
              marketplace_order.idempotency_key = p_idempotency_key and
              marketplace_order.organization_id is not distinct from p_organization_id;
        if selected_order_id is null then
            raise exception using errcode = '23505', message = 'idempotency_key_reused';
        end if;
    end if;

    insert into public.marketplace_order_items
        (order_id, product_id, offer_id, amount_minor, license_spdx, license_revision, accepted_license_snapshot)
    values
        (selected_order_id, p_product_id, selected_offer.id, 0, p_license_spdx, p_license_revision,
         p_accepted_license_snapshot)
    on conflict (order_id, product_id) do update
    set product_id = excluded.product_id
    returning id into selected_order_item_id;

    insert into public.marketplace_entitlements (product_id, user_id, organization_id, order_item_id)
    values (p_product_id,
            case when p_organization_id is null then current_user_id else null end,
            p_organization_id,
            selected_order_item_id)
    returning id into selected_entitlement_id;

    insert into public.platform_audit_events
        (actor_user_id, action, target_type, target_id, metadata)
    values
        (current_user_id, 'marketplace.product_claimed', 'entitlement', selected_entitlement_id::text,
         jsonb_build_object('productId', p_product_id, 'organizationId', p_organization_id));
    return selected_entitlement_id;
end;
$$;

revoke all on function public.claim_free_marketplace_product(uuid, uuid, text, text, text, text)
    from public, anon, authenticated;
grant execute on function public.claim_free_marketplace_product(uuid, uuid, text, text, text, text) to service_role;
