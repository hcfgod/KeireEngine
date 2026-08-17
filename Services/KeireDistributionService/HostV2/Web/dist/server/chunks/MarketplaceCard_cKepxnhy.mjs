import { A as createAstro, b as addAttribute, m as renderTemplate, v as maybeRenderHead } from "./server_DvX7bpsP.mjs";
import { t as createComponent } from "./astro-component_CT_H5Ga1.mjs";
import "./compiler_CefTWzEM.mjs";
//#region Source/components/MarketplaceCard.astro
createAstro("https://keireengine.duckdns.org");
var $$MarketplaceCard = createComponent(($$result, $$props, $$slots) => {
	const Astro = $$result.createAstro($$props, $$slots);
	Astro.self = $$MarketplaceCard;
	const { product } = Astro.props;
	return renderTemplate`${maybeRenderHead($$result)}<article class="market-card"><a class="market-card__media"${addAttribute(`/marketplace/${product.publisher_slug}/${product.slug}/`, "href")} tabindex="-1"><span class="market-card__monogram" aria-hidden="true">${product.display_name.slice(0, 2).toUpperCase()}</span>${product.featured && renderTemplate`<span class="market-card__featured">Featured</span>`}</a><div class="market-card__body"><p class="market-card__category">${product.category_name}</p><h3><a${addAttribute(`/marketplace/${product.publisher_slug}/${product.slug}/`, "href")}>${product.display_name}</a></h3><p>${product.short_description}</p><div class="market-card__meta"><span>${product.publisher_name}${product.publisher_verified ? " · Verified" : ""}</span><span>${Number(product.rating_average).toFixed(1)} / 5 · ${product.rating_count}</span></div><div class="market-card__footer"><span>${product.license_spdx}</span><strong>Free</strong></div></div></article>`;
}, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/components/MarketplaceCard.astro", void 0);
//#endregion
export { $$MarketplaceCard as t };
