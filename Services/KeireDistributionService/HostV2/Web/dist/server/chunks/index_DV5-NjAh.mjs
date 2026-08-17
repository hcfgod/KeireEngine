import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { A as createAstro, b as addAttribute, m as renderTemplate, o as renderComponent, v as maybeRenderHead } from "./server_DvX7bpsP.mjs";
import { t as createComponent } from "./astro-component_CT_H5Ga1.mjs";
import { t as $$PlatformLayout } from "./PlatformLayout_C-gGDb3o.mjs";
import "./compiler_CefTWzEM.mjs";
//#region Source/pages/account/index.astro
var account_exports = /* @__PURE__ */ __exportAll({
	default: () => $$Index,
	file: () => $$file,
	prerender: () => false,
	url: () => $$url
});
createAstro("https://keireengine.duckdns.org");
var $$Index = createComponent(async ($$result, $$props, $$slots) => {
	const Astro = $$result.createAstro($$props, $$slots);
	Astro.self = $$Index;
	if (!Astro.locals.user) return Astro.redirect(`/account/sign-in/?next=${encodeURIComponent(Astro.url.pathname)}`, 303);
	const user = Astro.locals.user;
	const supabase = Astro.locals.supabase;
	const [profileResult, entitlementsResult, organizationsResult, sessionsResult] = await Promise.all([
		supabase.from("profiles").select("display_name,handle,avatar_url,biography,locale").eq("user_id", user.id).maybeSingle(),
		supabase.from("marketplace_entitlements").select("id,product_id,organization_id,granted_at,marketplace_products(display_name,slug,publishers(slug))").is("revoked_at", null).order("granted_at", { ascending: false }).limit(12),
		supabase.from("organization_memberships").select("role,organizations(id,slug,display_name)").eq("user_id", user.id),
		supabase.from("oauth_device_sessions").select("id,client_type,device_name,created_at,last_used_at,revoked_at").eq("user_id", user.id).order("last_used_at", { ascending: false }).limit(20)
	]);
	const profile = profileResult.data;
	const entitlements = entitlementsResult.data ?? [];
	const organizations = organizationsResult.data ?? [];
	const sessions = sessionsResult.data ?? [];
	return renderTemplate`${renderComponent($$result, "PlatformLayout", $$PlatformLayout, {
		"title": "Account",
		"description": "Manage your Kéire profile, asset library, organizations, and connected sessions.",
		"robots": "noindex"
	}, { "default": ($$result) => renderTemplate`${maybeRenderHead($$result)}<section class="platform-section"><div class="platform-shell dashboard-grid"><nav class="dashboard-nav" aria-label="Account"><a href="/account/" aria-current="page">Overview</a><a href="#library">My assets</a><a href="#organizations">Organizations</a><a href="#security">Security</a><a href="/account/data/">Data and privacy</a><form method="post" action="/account/session/delete/"><button class="button button--quiet" type="submit">Sign out</button></form></nav><div><p class="eyebrow">Account overview</p><h1>${profile?.display_name ?? user.email ?? "Kéire developer"}</h1><p class="lead">${profile?.biography ?? "Your identity is shared across Kéire services; each browser and Hub session remains independently revocable."}</p><section id="library"><h2>My assets</h2>${entitlements.length ? renderTemplate`<table class="data-table"><thead><tr><th>Asset</th><th>Owner</th><th>Claimed</th></tr></thead><tbody>${entitlements.map((item) => renderTemplate`<tr><td>${item.marketplace_products?.display_name ?? item.product_id}</td><td>${item.organization_id ? "Organization" : "Personal"}</td><td>${new Date(item.granted_at).toLocaleDateString()}</td></tr>`)}</tbody></table>` : renderTemplate`<div class="empty-state"><h3>No claimed assets yet.</h3><p>Free marketplace assets you claim personally or for an organization appear here and in Hub.</p><a class="button button--secondary" href="/marketplace/">Browse Marketplace</a></div>`}</section><section id="organizations" class="platform-section"><h2>Organizations</h2>${organizations.length ? renderTemplate`<table class="data-table"><thead><tr><th>Organization</th><th>Role</th></tr></thead><tbody>${organizations.map((membership) => renderTemplate`<tr><td>${membership.organizations?.display_name}</td><td>${membership.role}</td></tr>`)}</tbody></table>` : renderTemplate`<p class="notice-panel">You do not belong to an organization yet.</p>`}</section><section id="security"><h2>Connected devices</h2>${sessions.length ? renderTemplate`<table class="data-table"><thead><tr><th>Device</th><th>Client</th><th>Last used</th><th>Status</th></tr></thead><tbody>${sessions.map((session) => renderTemplate`<tr><td>${session.device_name}</td><td>${session.client_type}</td><td>${new Date(session.last_used_at).toLocaleString()}</td><td><span${addAttribute(`status-badge ${session.revoked_at ? "" : "status-badge--ready"}`, "class")}>${session.revoked_at ? "Revoked" : "Active"}</span></td></tr>`)}</tbody></table>` : renderTemplate`<p class="notice-panel">No registered Hub OAuth sessions. Browser sign-in remains active independently.</p>`}</section></div></div></section>` })}`;
}, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/account/index.astro", void 0);
var $$file = "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/account/index.astro";
var $$url = "/account/";
//#endregion
//#region \0virtual:astro:page:Source/pages/account/index@_@astro
var page = () => account_exports;
//#endregion
export { page };
