import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { A as createAstro, m as renderTemplate, o as renderComponent, v as maybeRenderHead } from "./server_DvX7bpsP.mjs";
import { t as createComponent } from "./astro-component_CT_H5Ga1.mjs";
import { t as $$PlatformLayout } from "./PlatformLayout_C-gGDb3o.mjs";
import "./compiler_CefTWzEM.mjs";
//#region Source/pages/admin/marketplace/index.astro
var marketplace_exports = /* @__PURE__ */ __exportAll({
	default: () => $$Index,
	file: () => $$file,
	prerender: () => false,
	url: () => $$url
});
createAstro("https://keireengine.duckdns.org");
var $$Index = createComponent(async ($$result, $$props, $$slots) => {
	const Astro = $$result.createAstro($$props, $$slots);
	Astro.self = $$Index;
	const role = Astro.locals.user?.app_metadata?.platform_role;
	if (!Astro.locals.user || !(/* @__PURE__ */ new Set(["moderator", "administrator"])).has(role)) Astro.response.status = 404;
	const allowed = Astro.response.status !== 404;
	const supabase = Astro.locals.supabase;
	let submissions = [];
	let reports = [];
	if (allowed && supabase) {
		const [submissionResult, reportResult] = await Promise.all([supabase.from("marketplace_submissions").select("id,state,submitted_at,version_id,submitted_by,assigned_to").in("state", [
			"submitted",
			"in_review",
			"changes_requested",
			"approved_pending_signature"
		]).order("submitted_at"), supabase.from("marketplace_reports").select("id,reason,state,created_at,product_id,review_id,assigned_to").in("state", ["open", "triaged"]).order("created_at")]);
		submissions = submissionResult.data ?? [];
		reports = reportResult.data ?? [];
	}
	return renderTemplate`${renderComponent($$result, "PlatformLayout", $$PlatformLayout, {
		"title": allowed ? "Marketplace moderation" : "Page not found",
		"description": "Kéire Marketplace staff moderation queue.",
		"robots": "noindex"
	}, { "default": ($$result) => renderTemplate`${allowed ? renderTemplate`${maybeRenderHead($$result)}<section class="platform-section"><div class="platform-shell"><p class="eyebrow">Staff · audited operations</p><h1>Marketplace moderation</h1><div class="dashboard-grid"><nav class="dashboard-nav"><a href="#submissions" aria-current="page">Submissions</a><a href="#reports">Reports</a><a href="#security">Security revocations</a></nav><div><section id="submissions"><h2>Active submissions</h2>${submissions.length ? renderTemplate`<table class="data-table"><thead><tr><th>ID</th><th>State</th><th>Submitted</th></tr></thead><tbody>${submissions.map((item) => renderTemplate`<tr><td>${item.id}</td><td>${item.state}</td><td>${new Date(item.submitted_at).toLocaleString()}</td></tr>`)}</tbody></table>` : renderTemplate`<p class="notice-panel">No active submissions.</p>`}</section><section id="reports" class="platform-section"><h2>Open reports</h2>${reports.length ? renderTemplate`<table class="data-table"><thead><tr><th>ID</th><th>Reason</th><th>State</th></tr></thead><tbody>${reports.map((item) => renderTemplate`<tr><td>${item.id}</td><td>${item.reason}</td><td>${item.state}</td></tr>`)}</tbody></table>` : renderTemplate`<p class="notice-panel">No open reports.</p>`}</section></div></div></div></section>` : renderTemplate`<section class="platform-section"><div class="platform-shell empty-state"><h1>Page not found.</h1></div></section>`}` })}`;
}, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/admin/marketplace/index.astro", void 0);
var $$file = "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/admin/marketplace/index.astro";
var $$url = "/admin/marketplace/";
//#endregion
//#region \0virtual:astro:page:Source/pages/admin/marketplace/index@_@astro
var page = () => marketplace_exports;
//#endregion
export { page };
