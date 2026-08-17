import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { A as createAstro, m as renderTemplate, o as renderComponent, v as maybeRenderHead } from "./server_DvX7bpsP.mjs";
import { t as createComponent } from "./astro-component_CT_H5Ga1.mjs";
import { n as renderScript, t as $$PlatformLayout } from "./PlatformLayout_C-gGDb3o.mjs";
import "./compiler_CefTWzEM.mjs";
//#region Source/pages/account/data/index.astro
var data_exports = /* @__PURE__ */ __exportAll({
	default: () => $$Index,
	file: () => $$file,
	prerender: () => false,
	url: () => $$url
});
createAstro("https://keireengine.duckdns.org");
var $$Index = createComponent(($$result, $$props, $$slots) => {
	const Astro = $$result.createAstro($$props, $$slots);
	Astro.self = $$Index;
	if (!Astro.locals.user) return Astro.redirect("/account/sign-in/?next=/account/data/", 303);
	return renderTemplate`${renderComponent($$result, "PlatformLayout", $$PlatformLayout, {
		"title": "Account data",
		"description": "Request Kéire account data export or deletion.",
		"robots": "noindex"
	}, { "default": ($$result) => renderTemplate`${maybeRenderHead($$result)}<section class="platform-section"><div class="platform-shell product-details"><article class="prose"><p class="eyebrow">Data and privacy</p><h1>Export or delete your account.</h1><p>Requests are timestamped on your protected profile. Export and deletion fulfillment remains an administrative workflow until automated delivery passes release review.</p><div class="actions"><button class="button button--secondary" data-data-request="export">Request export</button><button class="button button--secondary" data-data-request="deletion">Request deletion</button></div><p class="form-message" aria-live="polite" data-data-message></p></article><aside class="notice-panel"><strong>Deletion consequences</strong><p>Active Hub sessions are revoked. Legal, security, license-acceptance, publication, and audit records may require limited retention.</p></aside></div></section>` })}${renderScript($$result, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/account/data/index.astro?astro&type=script&index=0&lang.ts")}`;
}, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/account/data/index.astro", void 0);
var $$file = "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/account/data/index.astro";
var $$url = "/account/data/";
//#endregion
//#region \0virtual:astro:page:Source/pages/account/data/index@_@astro
var page = () => data_exports;
//#endregion
export { page };
