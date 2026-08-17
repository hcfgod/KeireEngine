import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { A as createAstro, b as addAttribute, m as renderTemplate, o as renderComponent, v as maybeRenderHead } from "./server_DvX7bpsP.mjs";
import { t as createComponent } from "./astro-component_CT_H5Ga1.mjs";
import { t as $$PlatformLayout } from "./PlatformLayout_C-gGDb3o.mjs";
import "./compiler_CefTWzEM.mjs";
import { n as marketplaceEnabled } from "./marketplace_C3fGUB9S.mjs";
import { t as $$MarketplaceCard } from "./MarketplaceCard_cKepxnhy.mjs";
//#region Source/pages/marketplace/index.astro
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
	const supabase = Astro.locals.supabase;
	const enabled = await marketplaceEnabled(supabase);
	const search = (Astro.url.searchParams.get("q") ?? "").trim().slice(0, 100);
	const category = (Astro.url.searchParams.get("category") ?? "").trim().slice(0, 64);
	let products = [];
	let categories = [];
	if (supabase && enabled) {
		let query = supabase.from("marketplace_catalog").select("id,slug,display_name,short_description,license_spdx,featured,publisher_slug,publisher_name,publisher_verified,category_slug,category_name,rating_average,rating_count,published_at").order("featured", { ascending: false }).order("published_at", { ascending: false }).limit(30);
		if (search) query = query.ilike("display_name", `%${search.replaceAll("%", "\\%").replaceAll("_", "\\_")}%`);
		if (category) query = query.eq("category_slug", category);
		const [{ data }, categoryResult] = await Promise.all([query, supabase.from("marketplace_categories").select("slug,display_name").eq("active", true).order("sort_order")]);
		products = data ?? [];
		categories = categoryResult.data ?? [];
	}
	Astro.response.headers.set("cache-control", Astro.locals.user ? "private, no-store" : "public, max-age=60, stale-while-revalidate=300");
	return renderTemplate`${renderComponent($$result, "PlatformLayout", $$PlatformLayout, {
		"title": "Marketplace",
		"description": "Discover verified free content, tools, shaders, VFX, and gameplay packages for Kéire Engine.",
		"active": "marketplace"
	}, { "default": ($$result) => renderTemplate`${maybeRenderHead($$result)}<section class="platform-section"><div class="platform-shell"><div class="section-heading"><div><p class="eyebrow">Kéire Marketplace</p><h1>Production content with visible provenance.</h1></div><p>Every published package is immutable, versioned, compatibility-scoped, validated, and independently verified by Hub. All 0.3.1 marketplace products are free.</p></div><form class="market-toolbar" method="get" role="search"><label class="search-field"><span>Search assets</span><input type="search" name="q"${addAttribute(search, "value")} placeholder="Shaders, VFX, gameplay, UI…" maxlength="100"></label><label class="field"><span>Category</span><select name="category"><option value="">All categories</option>${categories.map((item) => renderTemplate`<option${addAttribute(item.slug, "value")}${addAttribute(item.slug === category, "selected")}>${item.display_name}</option>`)}</select></label><button class="button" type="submit">Search</button></form>${!enabled ? renderTemplate`<div class="empty-state"><p class="eyebrow">Private staging</p><h2>The catalog is behind its release gate.</h2><p>Marketplace schema, package contracts, and workflows are installed behind <code>marketplace_enabled</code>. Public browsing remains disabled until identity, moderation, restore, legal, and platform-validation gates pass.</p></div>` : products.length === 0 ? renderTemplate`<div class="empty-state"><h2>No products matched.</h2><p>Try a broader search or remove the category filter.</p></div>` : renderTemplate`<div class="market-grid">${products.map((product) => renderTemplate`${renderComponent($$result, "MarketplaceCard", $$MarketplaceCard, { "product": product })}`)}</div>`}</div></section><section class="platform-section platform-section--line"><div class="platform-shell cta-band"><div><p class="eyebrow">Build for Kéire</p><h2>Publish free content through a reviewable pipeline.</h2></div><div class="actions"><a class="button" href="/publisher/">Publisher portal</a><a class="button button--secondary" href="/docs/reference/asset-packages/">Package documentation</a></div></div></section>` })}`;
}, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/marketplace/index.astro", void 0);
var $$file = "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/marketplace/index.astro";
var $$url = "/marketplace/";
//#endregion
//#region \0virtual:astro:page:Source/pages/marketplace/index@_@astro
var page = () => marketplace_exports;
//#endregion
export { page };
