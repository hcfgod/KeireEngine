import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { m as renderTemplate, o as renderComponent, v as maybeRenderHead } from "./server_DvX7bpsP.mjs";
import { t as createComponent } from "./astro-component_CT_H5Ga1.mjs";
import { n as renderScript, t as $$PlatformLayout } from "./PlatformLayout_C-gGDb3o.mjs";
import "./compiler_CefTWzEM.mjs";
//#region Source/pages/account/recover/index.astro
var recover_exports = /* @__PURE__ */ __exportAll({
	default: () => $$Index,
	file: () => $$file,
	prerender: () => false,
	url: () => $$url
});
var $$Index = createComponent(($$result, $$props, $$slots) => {
	return renderTemplate`${renderComponent($$result, "PlatformLayout", $$PlatformLayout, {
		"title": "Recover account",
		"description": "Request a secure Kéire account password recovery email.",
		"robots": "noindex"
	}, { "default": ($$result) => renderTemplate`${maybeRenderHead($$result)}<section class="platform-shell account-shell"><div><p class="eyebrow">Account recovery</p><h1 class="display-title">Recover access.<br><span>Keep sessions separate.</span></h1><p class="lead">Resetting the website password does not silently reactivate revoked Hub device sessions.</p></div><div class="auth-card"><h1>Reset password</h1><form class="form-stack" data-recovery-form><label class="field"><span>Email address</span><input type="email" name="email" maxlength="254" autocomplete="email" required></label><button class="button" type="submit">Send recovery email</button><p class="form-message" aria-live="polite" data-recovery-message></p></form></div></section>` })}${renderScript($$result, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/account/recover/index.astro?astro&type=script&index=0&lang.ts")}`;
}, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/account/recover/index.astro", void 0);
var $$file = "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/account/recover/index.astro";
var $$url = "/account/recover/";
//#endregion
//#region \0virtual:astro:page:Source/pages/account/recover/index@_@astro
var page = () => recover_exports;
//#endregion
export { page };
