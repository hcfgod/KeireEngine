import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { A as createAstro, b as addAttribute, c as Fragment, m as renderTemplate, o as renderComponent, v as maybeRenderHead } from "./server_DvX7bpsP.mjs";
import { t as createComponent } from "./astro-component_CT_H5Ga1.mjs";
import { n as renderScript, t as $$PlatformLayout } from "./PlatformLayout_C-gGDb3o.mjs";
import "./compiler_CefTWzEM.mjs";
import { n as marketplaceEnabled } from "./marketplace_C3fGUB9S.mjs";
//#region Source/pages/marketplace/[publisher]/[product].astro
var _product__exports = /* @__PURE__ */ __exportAll({
	default: () => $$Product,
	file: () => $$file,
	prerender: () => false,
	url: () => $$url
});
createAstro("https://keireengine.duckdns.org");
var $$Product = createComponent(async ($$result, $$props, $$slots) => {
	const Astro = $$result.createAstro($$props, $$slots);
	Astro.self = $$Product;
	const { publisher, product } = Astro.params;
	const supabase = Astro.locals.supabase;
	let listing = null;
	let versions = [];
	if (supabase && await marketplaceEnabled(supabase)) {
		listing = (await supabase.from("marketplace_catalog").select("*").eq("publisher_slug", publisher).eq("slug", product).maybeSingle()).data;
		if (listing) versions = (await supabase.from("marketplace_product_versions").select("id,version,install_kind,minimum_engine_version,maximum_engine_version,platforms,architectures,release_notes_markdown,published_at,state").eq("product_id", listing.id).in("state", [
			"published",
			"withdrawn",
			"security_revoked"
		]).order("published_at", { ascending: false })).data ?? [];
	}
	if (!listing) Astro.response.status = 404;
	const structuredData = listing ? {
		"@context": "https://schema.org",
		"@type": "Product",
		name: listing.display_name,
		description: listing.short_description,
		brand: {
			"@type": "Organization",
			name: listing.publisher_name
		},
		offers: {
			"@type": "Offer",
			price: "0",
			priceCurrency: "USD",
			availability: "https://schema.org/InStock"
		},
		aggregateRating: listing.rating_count > 0 ? {
			"@type": "AggregateRating",
			ratingValue: listing.rating_average,
			reviewCount: listing.rating_count
		} : void 0
	} : void 0;
	return renderTemplate`${renderComponent($$result, "PlatformLayout", $$PlatformLayout, {
		"title": listing?.display_name ?? "Product not found",
		"description": listing?.short_description ?? "The requested Kéire Marketplace product is unavailable.",
		"active": "marketplace",
		"robots": listing ? "index,follow" : "noindex",
		"structuredData": structuredData
	}, { "default": ($$result) => renderTemplate`${listing ? renderTemplate`${renderComponent($$result, "Fragment", Fragment, {}, { "default": ($$result) => renderTemplate`${maybeRenderHead($$result)}<section class="platform-shell product-hero"><div><p class="eyebrow">${listing.category_name} · ${listing.license_spdx}</p><h1>${listing.display_name}</h1><p class="lead">${listing.short_description}</p><p>By <a${addAttribute(`/marketplace/publishers/${listing.publisher_slug}/`, "href")}>${listing.publisher_name}</a>${listing.publisher_verified ? " · Verified publisher" : ""}</p></div><aside class="product-buybox" aria-label="Get this asset"><span class="product-buybox__price">Free</span><button class="button" type="button"${addAttribute(listing.id, "data-claim-product")}>Get asset</button><p class="form-message" aria-live="polite" data-claim-message>Choose personal or organization ownership after signing in.</p><a class="button button--secondary"${addAttribute(`keirehub://marketplace/product/${listing.id}`, "href")}>Open in Hub</a></aside></section><section class="platform-shell product-details"><article class="prose"><h2>About this asset</h2><p class="preserve-lines">${listing.description_markdown}</p><h2>Release history</h2>${versions.length ? versions.map((version) => renderTemplate`<section><h3>${version.version}</h3><p>${version.release_notes_markdown || "No release notes were supplied."}</p><span${addAttribute(`status-badge ${version.state === "published" ? "status-badge--ready" : ""}`, "class")}>${version.state}</span></section>`) : renderTemplate`<p>No compatible published version is currently available.</p>`}</article><aside><dl class="fact-list"><div><dt>Publisher</dt><dd>${listing.publisher_name}</dd></div><div><dt>License</dt><dd>${listing.license_spdx} (${listing.license_revision})</dd></div><div><dt>Rating</dt><dd>${Number(listing.rating_average).toFixed(1)} / 5 (${listing.rating_count})</dd></div><div><dt>Ownership</dt><dd>Perpetual personal or organization entitlement</dd></div></dl></aside></section>` })}` : renderTemplate`<section class="platform-section"><div class="platform-shell empty-state"><p class="eyebrow">404</p><h1>Product unavailable.</h1><p>The listing may not exist, may still be in review, or may have been delisted.</p><a class="button" href="/marketplace/">Return to Marketplace</a></div></section>`}` })}${renderScript($$result, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/marketplace/[publisher]/[product].astro?astro&type=script&index=0&lang.ts")}`;
}, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/marketplace/[publisher]/[product].astro", void 0);
var $$file = "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/marketplace/[publisher]/[product].astro";
var $$url = "/marketplace/[publisher]/[product]/";
//#endregion
//#region \0virtual:astro:page:Source/pages/marketplace/[publisher]/[product]@_@astro
var page = () => _product__exports;
//#endregion
export { page };
