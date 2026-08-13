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
        if (!caller.sessionId) throw new RequestError(400, "account.oauth_claims_required", "A Hub OAuth session is required.");
        const input = await readJson(request);
        const operation = stringField(input, "operation", 3, 64);
        if (operation === "session.register") {
            const { data, error } = await caller.admin.rpc("service_register_marketplace_device_session", {
                p_actor_user_id: caller.user.id,
                p_actor_session_id: caller.sessionId,
                p_device_name: stringField(input, "deviceName", 1, 128).trim(),
            });
            if (error) throw databaseFailure(error);
            return json(origin, 201, { data: { deviceSessionId: data, oauthSessionId: caller.sessionId } });
        }
        if (operation === "download.issue") {
            const { data, error } = await caller.admin.rpc("service_issue_marketplace_download_grant_v2", {
                p_actor_user_id: caller.user.id,
                p_actor_session_id: caller.sessionId,
                p_version_id: requiredUuid(input, "versionId"),
                p_device_session_id: requiredUuid(input, "deviceSessionId"),
                p_organization_id: optionalUuid(input, "organizationId"),
            }).single();
            if (error) throw databaseFailure(error);
            return json(origin, 201, { data });
        }
        throw new RequestError(400, "request.operation_invalid", "The marketplace operation is invalid.");
    } catch (error) {
        return handleError(origin, error);
    }
});
