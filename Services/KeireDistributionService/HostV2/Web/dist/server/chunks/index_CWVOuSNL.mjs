import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { t as runtimeEnvironment } from "./runtime-env_-CSLWzci.mjs";
import { r as apiResponse } from "./api_CeIcXSPx.mjs";
//#region Source/pages/health/index.ts
var health_exports = /* @__PURE__ */ __exportAll({
	GET: () => GET,
	prerender: () => false
});
var GET = async (context) => {
	const checks = { renderer: { status: "healthy" } };
	if (context.locals.supabase) {
		const began = performance.now();
		const { error } = await context.locals.supabase.from("platform_feature_flags").select("key").limit(1);
		checks.supabase = {
			status: error ? "degraded" : "healthy",
			latencyMs: Math.round(performance.now() - began)
		};
	} else checks.supabase = { status: "not_configured" };
	const probe = async (name, endpoint) => {
		if (!endpoint) {
			checks[name] = { status: "not_configured" };
			return;
		}
		const began = performance.now();
		try {
			const response = await fetch(endpoint, {
				signal: AbortSignal.timeout(2500),
				headers: { accept: "application/json" }
			});
			checks[name] = {
				status: response.ok ? "healthy" : "degraded",
				latencyMs: Math.round(performance.now() - began)
			};
		} catch {
			checks[name] = {
				status: "degraded",
				latencyMs: Math.round(performance.now() - began)
			};
		}
	};
	await Promise.all([probe("distribution", runtimeEnvironment("KEIRE_DISTRIBUTION_HEALTH_URL")), probe("validator", runtimeEnvironment("KEIRE_VALIDATOR_HEALTH_URL"))]);
	const degraded = Object.values(checks).some(({ status }) => status === "degraded");
	return apiResponse(context, {
		status: degraded ? "degraded" : "healthy",
		service: "keire-web",
		version: "0.3.1",
		checks,
		correlationId: context.locals.correlationId
	}, degraded ? 503 : 200, "no-store");
};
//#endregion
//#region \0virtual:astro:page:Source/pages/health/index@_@ts
var page = () => health_exports;
//#endregion
export { page };
