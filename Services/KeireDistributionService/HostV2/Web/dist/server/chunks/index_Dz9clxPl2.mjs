import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { A as createAstro, m as renderTemplate, o as renderComponent, v as maybeRenderHead } from "./server_DvX7bpsP.mjs";
import { t as createComponent } from "./astro-component_CT_H5Ga1.mjs";
import { t as $$PlatformLayout } from "./PlatformLayout_C-gGDb3o.mjs";
import "./compiler_CefTWzEM.mjs";
//#region Source/pages/publisher/index.astro
var publisher_exports = /* @__PURE__ */ __exportAll({
	default: () => $$Index,
	file: () => $$file,
	prerender: () => false,
	url: () => $$url
});
createAstro("https://keireengine.duckdns.org");
var $$Index = createComponent(async ($$result, $$props, $$slots) => {
	const Astro = $$result.createAstro($$props, $$slots);
	Astro.self = $$Index;
	const supabase = Astro.locals.supabase;
	const user = Astro.locals.user;
	let enabled = false;
	let applications = [];
	let products = [];
	if (supabase) {
		enabled = (await supabase.from("platform_feature_flags").select("enabled").eq("key", "publisher_portal_enabled").maybeSingle()).data?.enabled === true;
		if (user) {
			applications = (await supabase.from("publisher_applications").select("id,public_name,state,submitted_at,decision_note,updated_at").eq("applicant_user_id", user.id).order("updated_at", { ascending: false })).data ?? [];
			products = (await supabase.from("marketplace_products").select("id,slug,display_name,state,updated_at,publishers(display_name,slug)").order("updated_at", { ascending: false }).limit(100)).data ?? [];
		}
	}
	return renderTemplate`${renderComponent($$result, "PlatformLayout", $$PlatformLayout, {
		"title": "Publisher portal",
		"description": "Apply to publish, create draft listings, upload packages, review validation, and submit immutable Kéire Marketplace releases.",
		"active": "marketplace",
		"robots": "noindex"
	}, { "default": ($$result) => renderTemplate`${maybeRenderHead($$result)}<section class="platform-section"><div class="platform-shell"><div class="section-heading"><div><p class="eyebrow">Approved publishing</p><h1>From package draft to signed release.</h1></div><p>Applications, automated validation, staff moderation, and offline signing are explicit states. Published versions are immutable; fixes are new versions.</p></div>${!enabled ? renderTemplate`<div class="empty-state"><h2>Publisher workflows are staged privately.</h2><p>The portal is installed behind <code>publisher_portal_enabled</code>. Applications cannot be submitted until MFA, moderation, validation, legal, and signing gates pass.</p></div>` : !user ? renderTemplate`<div class="empty-state"><h2>Sign in to apply.</h2><p>Publisher applications are bound to a verified account and an organization.</p><a class="button" href="/account/sign-in/?next=/publisher/">Sign in</a></div>` : renderTemplate`<div class="dashboard-grid"><nav class="dashboard-nav"><a href="#overview" aria-current="page">Overview</a><a href="#products">Products</a><a href="#application">Application</a><a href="/docs/reference/asset-packages/">Package guide</a></nav><div id="overview"><h2>Release pipeline</h2><div class="workflow-line"><article><small>01</small><h3>Draft</h3><p>Define product, license, compatibility, media, and release notes.</p></article><article><small>02</small><h3>Validate</h3><p>Upload exact bytes to quarantine and receive structured diagnostics.</p></article><article><small>03</small><h3>Moderate</h3><p>Staff reviews content, provenance, safety, and store quality.</p></article><article><small>04</small><h3>Publish</h3><p>The same bytes are revalidated and signed offline.</p></article></div><section id="products" class="platform-section"><h2>Products</h2>${products.length ? renderTemplate`<table class="data-table"><thead><tr><th>Product</th><th>Publisher</th><th>State</th><th>Updated</th></tr></thead><tbody>${products.map((product) => renderTemplate`<tr><td>${product.display_name}</td><td>${product.publishers?.display_name}</td><td><span class="status-badge">${product.state}</span></td><td>${new Date(product.updated_at).toLocaleDateString()}</td></tr>`)}</tbody></table>` : renderTemplate`<p class="notice-panel">No publisher products are available for this account.</p>`}</section><section id="application"><h2>Applications</h2>${applications.length ? renderTemplate`<table class="data-table"><thead><tr><th>Public name</th><th>State</th><th>Updated</th></tr></thead><tbody>${applications.map((application) => renderTemplate`<tr><td>${application.public_name}</td><td><span class="status-badge">${application.state}</span></td><td>${new Date(application.updated_at).toLocaleDateString()}</td></tr>`)}</tbody></table>` : renderTemplate`<p class="notice-panel">No publisher application has been started.</p>`}</section></div></div>`}</div></section>` })}`;
}, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/publisher/index.astro", void 0);
var $$file = "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/publisher/index.astro";
var $$url = "/publisher/";
//#endregion
//#region \0virtual:astro:page:Source/pages/publisher/index@_@astro
var page = () => publisher_exports;
//#endregion
export { page };
