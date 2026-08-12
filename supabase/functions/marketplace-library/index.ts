import {
    authenticate, databaseFailure, handleError, json, optionalUuid, preflight, readJson, RequestError,
    requiredUuid, stringField, validateOrigin,
} from "../_shared/marketplace.ts";

Deno.serve(async (request: Request) => {
    let origin = "";
    try {
        origin = validateOrigin(request);
        if (request.method === "OPTIONS") return preflight(origin);
        if (request.method !== "POST") throw new RequestError(405, "request.method_not_allowed", "POST is required.");
        const caller = await authenticate(request);
        const input = await readJson(request);
        const operation = stringField(input, "operation", 3, 64);
        if (operation === "organization.create") {
            if (caller.assuranceLevel !== "aal2") {
                throw new RequestError(403, "account.mfa_required", "A verified second factor is required.");
            }
            const { data, error } = await caller.admin.rpc("service_create_marketplace_organization", {
                p_actor_user_id: caller.user.id,
                p_slug: stringField(input, "slug", 3, 63).toLowerCase(),
                p_display_name: stringField(input, "displayName", 1, 96).trim(),
            });
            if (error) throw databaseFailure(error);
            return json(origin, 201, { data: { organizationId: data } });
        }
        if (operation === "claim.create") {
            const { data, error } = await caller.admin.rpc("service_claim_free_marketplace_product", {
                p_actor_user_id: caller.user.id,
                p_product_id: requiredUuid(input, "productId"),
                p_organization_id: optionalUuid(input, "organizationId"),
                p_idempotency_key: stringField(input, "idempotencyKey", 16, 128),
                p_license_spdx: stringField(input, "licenseSpdx", 2, 64),
                p_license_revision: stringField(input, "licenseRevision", 1, 64),
                p_accepted_license_snapshot: stringField(input, "acceptedLicenseSnapshot", 1, 100_000),
            });
            if (error) throw databaseFailure(error);
            return json(origin, 201, { data: { entitlementId: data } });
        }
        throw new RequestError(400, "request.operation_invalid", "The marketplace operation is invalid.");
    } catch (error) {
        return handleError(origin, error);
    }
});
