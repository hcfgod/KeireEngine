import { cookieToken, readObject } from "./platform-auth-http.mjs";
import { claimLocalProduct, LocalClaimError } from "./platform-claims.mjs";

/** Browser claims only; native bearer credentials require their own OAuth adapter. */
export async function handleLocalClaimRequest(authentication, request, siteOrigin, clientIdentity, correlationId) {
    const headers = new Headers({
        "content-type": "application/json; charset=utf-8",
        "cache-control": "private, no-store",
        "vary": "Cookie, Authorization",
        "x-content-type-options": "nosniff",
    });
    const respond = (body, status) => new Response(JSON.stringify(body), { status, headers });
    const fail = (code, message, status) => respond({ error: { code, message, correlationId } }, status);
    if (request.method !== "POST") {
        headers.set("allow", "POST");
        return fail("request.method_not_allowed", "Use POST to acquire an asset.", 405);
    }
    if (request.headers.has("authorization")) {
        return fail("account.invalid_authorization", "Use browser sign-in for this endpoint.", 401);
    }
    const configured = new URL(siteOrigin);
    if (configured.protocol !== "https:" || configured.origin !== siteOrigin || request.headers.get("origin") !== siteOrigin) {
        return fail("request.origin_rejected", "The request origin could not be verified.", 403);
    }
    if (typeof clientIdentity !== "string" || !clientIdentity || clientIdentity.length > 256) {
        return fail("marketplace.unavailable", "Asset acquisition is temporarily unavailable. Try again.", 503);
    }
    const token = cookieToken(request, "__Host-keire-session");
    if (!token) {
        return fail("marketplace.authentication_required", "Sign in to acquire this asset.", 401);
    }
    let input;
    try {
        // A license can contain 100,000 Unicode characters; bound the actual stream before parsing it.
        input = await readObject(request, 512 * 1024);
    } catch {
        return fail("request.invalid_body", "The acquisition request is invalid or too large.", 400);
    }
    if (!["personal", "organization"].includes(input.ownership)) {
        return fail("marketplace.invalid_claim", "Select personal or organization ownership.", 400);
    }
    if (input.ownership === "organization" && typeof input.organizationId !== "string") {
        return fail("marketplace.invalid_claim", "Select the organization that should own this asset.", 400);
    }
    try {
        const acquired = await claimLocalProduct(authentication, token, {
            productId: input.productId,
            organizationId: input.ownership === "organization" ? input.organizationId : null,
            idempotencyKey: request.headers.get("idempotency-key"),
            licenseSpdx: input.licenseSpdx,
            licenseRevision: input.licenseRevision,
            acceptedLicenseSnapshot: input.acceptedLicenseSnapshot,
        });
        return respond({ data: { ...acquired, ownership: input.ownership },
            meta: { apiVersion: "marketplace/v1", correlationId } }, 201);
    } catch (error) {
        if (error instanceof LocalClaimError) {
            return fail(error.code, error.message, error.status);
        }
        return fail("marketplace.unavailable", "Acquisition could not be confirmed. Retry the same request safely.", 503);
    }
}
