import {
    authenticate, databaseFailure, handleError, json, preflight, readJson, RequestError, requiredUuid,
    stringField, validateOrigin,
} from "../_shared/marketplace.ts";

function booleanField(input: Record<string, unknown>, name: string): boolean {
    if (typeof input[name] !== "boolean") {
        throw new RequestError(400, "request.invalid_field", `${name} must be a boolean.`);
    }
    return input[name] as boolean;
}

async function staffRole(caller: Awaited<ReturnType<typeof authenticate>>): Promise<string> {
    const { data, error } = await caller.admin.rpc("service_get_platform_staff_role", {
        p_actor_user_id: caller.user.id,
    });
    if (error) throw databaseFailure(error);
    if (data !== "moderator" && data !== "administrator") {
        throw new RequestError(403, "staff.authorization_required", "An active staff role is required.");
    }
    return data;
}

function requireAdministrator(role: string): void {
    if (role !== "administrator") {
        throw new RequestError(403, "staff.administrator_required", "An active administrator role is required.");
    }
}

Deno.serve(async (request: Request) => {
    let origin = "";
    try {
        origin = validateOrigin(request);
        if (request.method === "OPTIONS") return preflight(origin);
        if (request.method !== "POST") throw new RequestError(405, "request.method_not_allowed", "POST is required.");
        const caller = await authenticate(request);
        if (caller.assuranceLevel !== "aal2") {
            throw new RequestError(403, "account.mfa_required", "A verified second factor is required.");
        }
        const role = await staffRole(caller);
        const input = await readJson(request);
        const operation = stringField(input, "operation", 3, 64);

        if (operation === "application.decide") {
            const { data, error } = await caller.admin.rpc("service_decide_publisher_application", {
                p_actor_user_id: caller.user.id,
                p_application_id: requiredUuid(input, "applicationId"),
                p_decision: stringField(input, "decision", 8, 32),
                p_decision_note: stringField(input, "decisionNote", 3, 5000),
                p_publisher_slug: typeof input.publisherSlug === "string" ? input.publisherSlug : "",
            });
            if (error) throw databaseFailure(error);
            const decision = Array.isArray(data) ? data[0] : data;
            return json(origin, 200, { data: decision });
        }
        if (operation === "submission.decide") {
            const { data, error } = await caller.admin.rpc("service_decide_marketplace_submission", {
                p_actor_user_id: caller.user.id,
                p_submission_id: requiredUuid(input, "submissionId"),
                p_decision: stringField(input, "decision", 8, 32),
                p_decision_note: typeof input.decisionNote === "string" ? input.decisionNote : "",
            });
            if (error) throw databaseFailure(error);
            const decision = Array.isArray(data) ? data[0] : data;
            return json(origin, 200, { data: decision });
        }
        if (operation === "report.decide") {
            const { data, error } = await caller.admin.rpc("service_decide_marketplace_report", {
                p_actor_user_id: caller.user.id,
                p_report_id: requiredUuid(input, "reportId"),
                p_state: stringField(input, "state", 7, 16),
                p_resolution_note: typeof input.resolutionNote === "string" ? input.resolutionNote : "",
            });
            if (error) throw databaseFailure(error);
            const decision = Array.isArray(data) ? data[0] : data;
            return json(origin, 200, { data: decision });
        }
        if (operation === "staff.set") {
            requireAdministrator(role);
            const { data, error } = await caller.admin.rpc("service_set_platform_staff", {
                p_actor_user_id: caller.user.id,
                p_target_user_id: requiredUuid(input, "targetUserId"),
                p_role: stringField(input, "role", 9, 16),
                p_active: booleanField(input, "active"),
                p_reason: stringField(input, "reason", 3, 1000),
            });
            if (error) throw databaseFailure(error);
            const assignment = Array.isArray(data) ? data[0] : data;
            return json(origin, 200, { data: assignment });
        }
        if (operation === "feature.set") {
            requireAdministrator(role);
            const { data, error } = await caller.admin.rpc("service_set_platform_feature_flag", {
                p_actor_user_id: caller.user.id,
                p_key: stringField(input, "key", 3, 64),
                p_enabled: booleanField(input, "enabled"),
                p_reason: stringField(input, "reason", 3, 1000),
            });
            if (error) throw databaseFailure(error);
            const flag = Array.isArray(data) ? data[0] : data;
            return json(origin, 200, { data: flag });
        }
        throw new RequestError(400, "request.operation_invalid", "The moderation operation is invalid.");
    } catch (error) {
        return handleError(origin, error);
    }
});
