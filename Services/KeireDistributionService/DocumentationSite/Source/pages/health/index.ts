import type { APIRoute } from "astro";
import { apiResponse } from "../../lib/api";
import { runtimeEnvironment } from "../../lib/runtime-env";

export const prerender = false;
export const GET: APIRoute = async (context) => {
    const checks: Record<string, { status: "healthy" | "degraded" | "not_configured"; latencyMs?: number }> = {
        renderer: { status: "healthy" },
    };
    if (context.locals.supabase) {
        const began = performance.now();
        const { error } = await context.locals.supabase.from("platform_feature_flags").select("key").limit(1);
        checks.supabase = { status: error ? "degraded" : "healthy", latencyMs: Math.round(performance.now() - began) };
    } else checks.supabase = { status: "not_configured" };
    const probe = async (name: string, endpoint: string | undefined) => {
        if (!endpoint) { checks[name] = { status: "not_configured" }; return; }
        const began = performance.now();
        try {
            const response = await fetch(endpoint, { signal: AbortSignal.timeout(2500), headers: { accept: "application/json" } });
            checks[name] = { status: response.ok ? "healthy" : "degraded", latencyMs: Math.round(performance.now() - began) };
        } catch { checks[name] = { status: "degraded", latencyMs: Math.round(performance.now() - began) }; }
    };
    await Promise.all([
        probe("distribution", runtimeEnvironment("KEIRE_DISTRIBUTION_HEALTH_URL")),
        probe("validator", runtimeEnvironment("KEIRE_VALIDATOR_HEALTH_URL")),
    ]);
    const degraded = Object.values(checks).some(({ status }) => status === "degraded");
    return apiResponse(context, { status: degraded ? "degraded" : "healthy", service: "keire-web", version: "0.4.1", releaseState: "current", targetCatalogVersion: "0.4.1", catalogState: "active", activeCatalogVersion: "0.4.1", checks, correlationId: context.locals.correlationId }, degraded ? 503 : 200, "no-store");
};

export const HEAD: APIRoute = (context) => new Response(null, {
    status: 204,
    headers: {
        "cache-control": "no-store",
        "x-correlation-id": context.locals.correlationId,
    },
});
