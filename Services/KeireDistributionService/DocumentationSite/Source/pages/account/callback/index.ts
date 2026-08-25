import type { APIRoute } from "astro";
import { requireSupabase } from "../../../lib/api";
import {
    accountCallbackDestination,
    getAssuranceState,
    mfaChallengePath,
    oauthFailurePath,
    requiresMfaChallenge,
    safeLocalPath,
    type OAuthFailureReason,
} from "../../../lib/auth";

export const prerender = false;
export const GET: APIRoute = async (context) => {
    const next = safeLocalPath(context.url.searchParams.get("next"));
    const fail = (reason: OAuthFailureReason) =>
        context.redirect(oauthFailurePath(reason, next, context.locals.correlationId), 303);

    try {
        const code = context.url.searchParams.get("code");
        if (!code || code.length > 2_048) {
            return fail(context.url.searchParams.get("error") === "access_denied" ? "cancelled" : "code_missing");
        }
        const supabase = requireSupabase(context);
        const flowId = context.url.searchParams.get("sb_flow_id");
        const { error } = await supabase.auth.exchangeCodeForSession(code, flowId ? { flowId } : undefined);
        if (error) {
            console.warn(JSON.stringify({
                level: "warning",
                event: "account.oauth_exchange_failed",
                correlationId: context.locals.correlationId,
                errorCode: typeof error.code === "string" ? error.code : "unknown",
                flowCorrelated: Boolean(flowId),
            }));
            return fail("exchange_failed");
        }
        const assurance = await getAssuranceState(supabase);
        if (!assurance.available) throw new Error("Account assurance lookup failed after code exchange.");
        const destination = accountCallbackDestination(assurance, next);
        return context.redirect(requiresMfaChallenge(assurance) ? mfaChallengePath(destination) : destination, 303);
    } catch (error) {
        console.error(JSON.stringify({
            level: "error",
            event: "account.oauth_callback_failed",
            correlationId: context.locals.correlationId,
            error: error instanceof Error ? error.message : String(error),
        }));
        return fail("unavailable");
    }
};
