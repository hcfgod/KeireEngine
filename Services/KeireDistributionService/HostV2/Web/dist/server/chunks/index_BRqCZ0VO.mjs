import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { A as createAstro, m as renderTemplate, o as renderComponent, v as maybeRenderHead } from "./server_DvX7bpsP.mjs";
import { t as createComponent } from "./astro-component_CT_H5Ga1.mjs";
import { n as renderScript, t as $$PlatformLayout } from "./PlatformLayout_C-gGDb3o.mjs";
import "./compiler_CefTWzEM.mjs";
//#region Source/pages/account/create/index.astro
var create_exports = /* @__PURE__ */ __exportAll({
	default: () => $$Index,
	file: () => $$file,
	prerender: () => false,
	url: () => $$url
});
createAstro("https://keireengine.duckdns.org");
var $$Index = createComponent(($$result, $$props, $$slots) => {
	const Astro = $$result.createAstro($$props, $$slots);
	Astro.self = $$Index;
	if (Astro.locals.user) return Astro.redirect("/account/", 303);
	return renderTemplate`${renderComponent($$result, "PlatformLayout", $$PlatformLayout, {
		"title": "Create account",
		"description": "Create a verified Kéire account for Hub authorization, organizations, Marketplace assets, and publishing.",
		"robots": "noindex"
	}, { "default": ($$result) => renderTemplate`${maybeRenderHead($$result)}<section class="platform-shell account-shell"><div><p class="eyebrow">One account</p><h1 class="display-title">Build.<br><span>Publish. Belong.</span></h1><p class="lead">Claim free assets, authorize Hub without sharing browser tokens, and collaborate through personal or organization ownership.</p></div><div class="auth-card"><h1>Create account</h1><form class="form-stack" data-create-account><label class="field"><span>Display name</span><input name="displayName" minlength="1" maxlength="64" autocomplete="name" required></label><label class="field"><span>Email address</span><input type="email" name="email" maxlength="254" autocomplete="email" required></label><label class="field"><span>Password</span><input type="password" name="password" minlength="10" maxlength="256" autocomplete="new-password" required></label><label><input type="checkbox" name="accepted" required> I accept the <a href="/policies/terms/">platform terms</a> and <a href="/policies/privacy/">privacy notice</a>.</label><button class="button" type="submit">Create verified account</button><p class="form-message" aria-live="polite" data-create-message></p></form><p>Already registered? <a href="/account/sign-in/">Sign in</a>.</p></div></section>` })}${renderScript($$result, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/account/create/index.astro?astro&type=script&index=0&lang.ts")}`;
}, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/account/create/index.astro", void 0);
var $$file = "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/account/create/index.astro";
var $$url = "/account/create/";
//#endregion
//#region \0virtual:astro:page:Source/pages/account/create/index@_@astro
var page = () => create_exports;
//#endregion
export { page };
