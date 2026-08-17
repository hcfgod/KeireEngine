import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { A as createAstro, m as renderTemplate, o as renderComponent, v as maybeRenderHead } from "./server_DvX7bpsP.mjs";
import { t as createComponent } from "./astro-component_CT_H5Ga1.mjs";
import { t as $$PlatformLayout } from "./PlatformLayout_C-gGDb3o.mjs";
import "./compiler_CefTWzEM.mjs";
import { n as marketplaceEnabled } from "./marketplace_C3fGUB9S.mjs";
import { t as $$MarketplaceCard } from "./MarketplaceCard_cKepxnhy.mjs";
//#region Source/pages/marketplace/publishers/[publisher].astro
var _publisher__exports = /* @__PURE__ */ __exportAll({
	default: () => $$Publisher,
	file: () => $$file,
	prerender: () => false,
	url: () => $$url
});
createAstro("https://keireengine.duckdns.org");
var $$Publisher = createComponent(async ($$result, $$props, $$slots) => {
	const Astro = $$result.createAstro($$props, $$slots);
	Astro.self = $$Publisher;
	const supabase = Astro.locals.supabase;
	let publisher = null;
	let products = [];
	if (supabase && await marketplaceEnabled(supabase)) {
		publisher = (await supabase.from("publishers").select("id,slug,display_name,summary,website_url,verified,created_at").eq("slug", Astro.params.publisher).maybeSingle()).data;
		if (publisher) products = (await supabase.from("marketplace_catalog").select("*").eq("publisher_id", publisher.id).order("published_at", { ascending: false })).data ?? [];
	}
	if (!publisher) Astro.response.status = 404;
	return renderTemplate`${renderComponent($$result, "PlatformLayout", $$PlatformLayout, {
		"title": publisher?.display_name ?? "Publisher not found",
		"description": publisher?.summary ?? "The requested Kéire Marketplace publisher is unavailable.",
		"active": "marketplace",
		"robots": publisher ? "index,follow" : "noindex"
	}, { "default": ($$result) => renderTemplate`${publisher ? renderTemplate`${maybeRenderHead($$result)}<section class="platform-section"><div class="platform-shell"><div class="section-heading"><div><p class="eyebrow">${publisher.verified ? "Verified publisher" : "Publisher"}</p><h1>${publisher.display_name}</h1></div><p>${publisher.summary || "This publisher has not added a public biography."}</p></div>${products.length ? renderTemplate`<div class="market-grid">${products.map((product) => renderTemplate`${renderComponent($$result, "MarketplaceCard", $$MarketplaceCard, { "product": product })}`)}</div>` : renderTemplate`<p class="empty-state">No published products.</p>`}</div></section>` : renderTemplate`<section class="platform-section"><div class="platform-shell empty-state"><h1>Publisher unavailable.</h1><a class="button" href="/marketplace/">Return to Marketplace</a></div></section>`}` })}`;
}, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/marketplace/publishers/[publisher].astro", void 0);
var $$file = "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/marketplace/publishers/[publisher].astro";
var $$url = "/marketplace/publishers/[publisher]/";
//#endregion
//#region \0virtual:astro:page:Source/pages/marketplace/publishers/[publisher]@_@astro
var page = () => _publisher__exports;
//#endregion
export { page };
