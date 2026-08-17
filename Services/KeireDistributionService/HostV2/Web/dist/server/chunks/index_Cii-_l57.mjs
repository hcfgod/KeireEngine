import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { A as createAstro, m as renderTemplate, o as renderComponent, v as maybeRenderHead } from "./server_DvX7bpsP.mjs";
import { t as createComponent } from "./astro-component_CT_H5Ga1.mjs";
import { n as renderScript, t as $$PlatformLayout } from "./PlatformLayout_C-gGDb3o.mjs";
import "./compiler_CefTWzEM.mjs";
//#region Source/pages/account/update-password/index.astro
var update_password_exports = /* @__PURE__ */ __exportAll({
	default: () => $$Index,
	file: () => $$file,
	prerender: () => false,
	url: () => $$url
});
createAstro("https://keireengine.duckdns.org");
var $$Index = createComponent(($$result, $$props, $$slots) => {
	const Astro = $$result.createAstro($$props, $$slots);
	Astro.self = $$Index;
	if (!Astro.locals.user) return Astro.redirect("/account/sign-in/", 303);
	return renderTemplate`${renderComponent($$result, "PlatformLayout", $$PlatformLayout, {
		"title": "Update password",
		"description": "Choose a new password for your Kéire account.",
		"robots": "noindex"
	}, { "default": ($$result) => renderTemplate`${maybeRenderHead($$result)}<section class="platform-shell account-shell"><div><p class="eyebrow">Secure recovery</p><h1 class="display-title">Choose a new<br><span>account secret.</span></h1></div><div class="auth-card"><h1>New password</h1><form class="form-stack" data-password-form><label class="field"><span>New password</span><input type="password" name="password" minlength="10" maxlength="256" autocomplete="new-password" required></label><button class="button" type="submit">Update password</button><p class="form-message" aria-live="polite" data-password-message></p></form></div></section>` })}${renderScript($$result, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/account/update-password/index.astro?astro&type=script&index=0&lang.ts")}`;
}, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/account/update-password/index.astro", void 0);
var $$file = "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/account/update-password/index.astro";
var $$url = "/account/update-password/";
//#endregion
//#region \0virtual:astro:page:Source/pages/account/update-password/index@_@astro
var page = () => update_password_exports;
//#endregion
export { page };
