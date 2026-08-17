import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { A as createAstro, b as addAttribute, m as renderTemplate, o as renderComponent, v as maybeRenderHead } from "./server_DvX7bpsP.mjs";
import { t as createComponent } from "./astro-component_CT_H5Ga1.mjs";
import { t as $$PlatformLayout } from "./PlatformLayout_C-gGDb3o.mjs";
import "./compiler_CefTWzEM.mjs";
//#region Source/pages/oauth/hub/callback.astro
var callback_exports = /* @__PURE__ */ __exportAll({
	default: () => $$Callback,
	file: () => $$file,
	prerender: () => false,
	url: () => $$url
});
createAstro("https://keireengine.duckdns.org");
var $$Callback = createComponent(($$result, $$props, $$slots) => {
	const Astro = $$result.createAstro($$props, $$slots);
	Astro.self = $$Callback;
	const code = Astro.url.searchParams.get("code") ?? "";
	const state = Astro.url.searchParams.get("state") ?? "";
	const oauthError = Astro.url.searchParams.get("error") ?? "";
	const errorDescription = (Astro.url.searchParams.get("error_description") ?? "Authorization was denied.").slice(0, 300);
	const validCode = /^[\x21-\x7e]{16,2048}$/.test(code);
	const validState = /^[A-Za-z0-9_-]{32,128}$/.test(state);
	const validError = oauthError && /^[A-Za-z0-9_.-]{1,64}$/.test(oauthError);
	const parameters = new URLSearchParams();
	if (validCode && validState) {
		parameters.set("code", code);
		parameters.set("state", state);
	} else if (validError && validState) {
		parameters.set("error", oauthError);
		parameters.set("state", state);
	}
	const hubUrl = parameters.size ? `keirehub://oauth/callback?${parameters.toString()}` : "";
	const success = Boolean(validCode && validState);
	return renderTemplate`${renderComponent($$result, "PlatformLayout", $$PlatformLayout, {
		"title": success ? "Continue in Kéire Hub" : "Hub authorization stopped",
		"description": "Complete Kéire Hub browser authorization.",
		"robots": "noindex,nofollow"
	}, { "default": ($$result) => renderTemplate`${maybeRenderHead($$result)}<section class="platform-shell narrow-shell"><div class="auth-card"><span${addAttribute(`status-badge ${success ? "status-badge--ready" : ""}`, "class")}>${success ? "Authorization approved" : "Authorization not completed"}</span><h1>${success ? "Return securely to Kéire Hub" : "Kéire Hub was not authorized"}</h1><p>${success ? "The single-use authorization code is ready. Kéire Hub will exchange it with Supabase using the PKCE verifier that never left the app." : errorDescription}</p>${hubUrl && renderTemplate`<a class="button"${addAttribute(hubUrl, "href")} rel="noreferrer">Open Kéire Hub</a>`}<p class="fine-print">If the Hub does not open, start Kéire Hub and begin browser sign-in again. This page never exchanges the code or receives Hub tokens.</p></div></section>` })}`;
}, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/oauth/hub/callback.astro", void 0);
var $$file = "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/oauth/hub/callback.astro";
var $$url = "/oauth/hub/callback/";
//#endregion
//#region \0virtual:astro:page:Source/pages/oauth/hub/callback@_@astro
var page = () => callback_exports;
//#endregion
export { page };
