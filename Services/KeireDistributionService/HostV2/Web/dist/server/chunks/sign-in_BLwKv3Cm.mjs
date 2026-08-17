import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { A as createAstro, b as addAttribute, m as renderTemplate, o as renderComponent, v as maybeRenderHead } from "./server_DvX7bpsP.mjs";
import { t as createComponent } from "./astro-component_CT_H5Ga1.mjs";
import { n as renderScript, t as $$PlatformLayout } from "./PlatformLayout_C-gGDb3o.mjs";
import "./compiler_CefTWzEM.mjs";
//#region Source/pages/account/sign-in.astro
var sign_in_exports = /* @__PURE__ */ __exportAll({
	default: () => $$SignIn,
	file: () => $$file,
	prerender: () => false,
	url: () => $$url
});
createAstro("https://keireengine.duckdns.org");
var $$SignIn = createComponent(($$result, $$props, $$slots) => {
	const Astro = $$result.createAstro($$props, $$slots);
	Astro.self = $$SignIn;
	if (Astro.locals.user) return Astro.redirect("/account/", 303);
	const next = Astro.url.searchParams.get("next") ?? "/account/";
	const safeNext = next.startsWith("/") && !next.startsWith("//") ? next : "/account/";
	return renderTemplate`${renderComponent($$result, "PlatformLayout", $$PlatformLayout, {
		"title": "Sign in",
		"description": "Sign in to your Kéire account for website, Hub authorization, organizations, and Marketplace library access.",
		"robots": "noindex"
	}, { "default": ($$result) => renderTemplate`${maybeRenderHead($$result)}<section class="platform-shell account-shell"><div><p class="eyebrow">One identity, separate sessions</p><h1 class="display-title">Your Kéire account.<br><span>Your devices.</span></h1><p class="lead">The website and Hub use the same identity authority, while each keeps an independently revocable session. Browser cookies and refresh tokens never transfer to Hub.</p></div><div class="auth-card"><h1>Sign in</h1><p>Continue to your library, organizations, and connected devices.</p><a class="button button--secondary"${addAttribute(`/account/github/?next=${encodeURIComponent(safeNext)}`, "href")}>Continue with GitHub</a><div class="divider">or use email</div><form class="form-stack" data-auth-form${addAttribute(safeNext, "data-next")}><label class="field"><span>Email address</span><input type="email" name="email" autocomplete="email" maxlength="254" required></label><label class="field"><span>Password</span><input type="password" name="password" autocomplete="current-password" minlength="8" maxlength="256" required></label><button class="button" type="submit">Sign in securely</button><p class="form-message" aria-live="polite" data-auth-message></p></form><p><a href="/account/recover/">Forgot your password?</a> · <a href="/account/create/">Create an account</a></p></div></section>` })}${renderScript($$result, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/account/sign-in.astro?astro&type=script&index=0&lang.ts")}`;
}, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/account/sign-in.astro", void 0);
var $$file = "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/account/sign-in.astro";
var $$url = "/account/sign-in/";
//#endregion
//#region \0virtual:astro:page:Source/pages/account/sign-in@_@astro
var page = () => sign_in_exports;
//#endregion
export { page };
