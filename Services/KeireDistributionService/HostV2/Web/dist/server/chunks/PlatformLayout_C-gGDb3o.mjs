import { A as createAstro, O as unescapeHTML, b as addAttribute, d as renderSlot, m as renderTemplate, o as renderComponent, v as maybeRenderHead, x as createRenderInstruction, y as renderHead } from "./server_DvX7bpsP.mjs";
import { t as createComponent } from "./astro-component_CT_H5Ga1.mjs";
import "./compiler_CefTWzEM.mjs";
//#region node_modules/astro/dist/runtime/server/render/script.js
async function renderScript(result, id) {
	const inlined = result.inlinedScripts.get(id);
	let content = "";
	if (inlined != null) {
		if (inlined) content = `<script type="module">${inlined}<\/script>`;
	} else {
		const resolved = await result.resolve(id);
		content = `<script type="module" src="${result.userAssetsBase ? (result.base === "/" ? "" : result.base) + result.userAssetsBase : ""}${resolved}"><\/script>`;
	}
	return createRenderInstruction({
		type: "script",
		id,
		content
	});
}
//#endregion
//#region Source/components/PlatformFooter.astro
var $$PlatformFooter = createComponent(($$result, $$props, $$slots) => {
	return renderTemplate`${maybeRenderHead($$result)}<footer class="platform-footer"><div class="platform-shell platform-footer__grid"><div class="platform-footer__statement"><a class="platform-brand" href="/"><img src="/assets/keire.png" width="36" height="36" alt=""><span><strong>Kéire</strong><small>ENGINE</small></span></a><p>Open game technology built around explicit ownership, observable systems, and boring reliability.</p><span class="release-chip"><i></i>Kéire 0.3.1 is pre-1.0 software</span></div><nav aria-label="Product links"><strong>Product</strong><a href="/features/">Engine</a><a href="/marketplace/">Marketplace</a><a href="/downloads/">Downloads</a><a href="/showcase/">Showcase</a></nav><nav aria-label="Developer links"><strong>Developers</strong><a href="/docs/">Documentation</a><a href="/docs/reference/getting-started/">Get started</a><a href="/publisher/">Publish assets</a><a href="https://github.com/hcfgod/KeireEngine">GitHub</a></nav><nav aria-label="Company and trust links"><strong>Trust</strong><a href="/news/">News</a><a href="/contact/">Contact</a><a href="/policies/privacy/">Privacy</a><a href="/policies/terms/">Terms</a><a href="/security/">Security</a></nav></div><div class="platform-shell platform-footer__legal"><span>© 2026 Kéire Engine contributors.</span><span>DuckDNS staging · Public launch gates remain open</span></div></footer>`;
}, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/components/PlatformFooter.astro", void 0);
//#endregion
//#region Source/components/PlatformHeader.astro
createAstro("https://keireengine.duckdns.org");
var $$PlatformHeader = createComponent(($$result, $$props, $$slots) => {
	const Astro = $$result.createAstro($$props, $$slots);
	Astro.self = $$PlatformHeader;
	const { active, signedIn = false } = Astro.props;
	return renderTemplate`${maybeRenderHead($$result)}<a class="skip-link" href="#main">Skip to content</a><header class="platform-header" data-platform-header><div class="platform-shell platform-header__inner"><a class="platform-brand" href="/" aria-label="Kéire Engine home"><img src="/assets/keire.png" width="36" height="36" alt=""><span><strong>Kéire</strong><small>ENGINE</small></span></a><button class="platform-nav-toggle" type="button" aria-label="Open navigation" aria-expanded="false" aria-controls="platform-navigation" data-platform-nav-toggle><span></span><span></span></button><nav id="platform-navigation" class="platform-nav" aria-label="Primary navigation" data-platform-nav><a href="/features/"${addAttribute(active === "product" ? "page" : void 0, "aria-current")}>Product</a><a href="/marketplace/"${addAttribute(active === "marketplace" ? "page" : void 0, "aria-current")}>Marketplace</a><a href="/downloads/"${addAttribute(active === "downloads" ? "page" : void 0, "aria-current")}>Downloads</a><a href="/docs/"${addAttribute(active === "docs" ? "page" : void 0, "aria-current")}>Documentation</a><a href="/community/"${addAttribute(active === "community" ? "page" : void 0, "aria-current")}>Community</a></nav><div class="platform-header__actions"><a class="text-action"${addAttribute(signedIn ? "/account/" : "/account/sign-in/", "href")}>${signedIn ? "Account" : "Sign in"}</a><a class="compact-button" href="/downloads/">Get Kéire Hub</a></div></div></header>${renderScript($$result, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/components/PlatformHeader.astro?astro&type=script&index=0&lang.ts")}`;
}, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/components/PlatformHeader.astro", void 0);
//#endregion
//#region Source/layouts/PlatformLayout.astro
createAstro("https://keireengine.duckdns.org");
var $$PlatformLayout = createComponent(($$result, $$props, $$slots) => {
	const Astro = $$result.createAstro($$props, $$slots);
	Astro.self = $$PlatformLayout;
	const { title, description, active, image = "/assets/hero-cinematic.png", robots = "index,follow", structuredData } = Astro.props;
	const canonical = new URL(Astro.url.pathname, Astro.site);
	const fullTitle = title === "Kéire Engine" ? title : `${title} — Kéire Engine`;
	return renderTemplate`<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><meta name="theme-color" content="#07101e"><meta name="description"${addAttribute(description, "content")}><meta name="robots"${addAttribute(robots, "content")}><meta property="og:type" content="website"><meta property="og:title"${addAttribute(fullTitle, "content")}><meta property="og:description"${addAttribute(description, "content")}><meta property="og:image"${addAttribute(new URL(image, Astro.site), "content")}><meta property="og:url"${addAttribute(canonical, "content")}><meta name="twitter:card" content="summary_large_image"><link rel="canonical"${addAttribute(canonical, "href")}><link rel="icon" href="/assets/keire.png"><link rel="manifest" href="/site.webmanifest"><title>${fullTitle}</title>${structuredData && renderTemplate`<script type="application/ld+json">${unescapeHTML(JSON.stringify(structuredData))}<\/script>`}${renderHead($$result)}</head><body>${renderComponent($$result, "PlatformHeader", $$PlatformHeader, {
		"active": active,
		"signedIn": Boolean(Astro.locals.user)
	})}<main id="main">${renderSlot($$result, $$slots["default"])}</main>${renderComponent($$result, "PlatformFooter", $$PlatformFooter, {})}</body></html>`;
}, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/layouts/PlatformLayout.astro", void 0);
//#endregion
export { renderScript as n, $$PlatformLayout as t };
