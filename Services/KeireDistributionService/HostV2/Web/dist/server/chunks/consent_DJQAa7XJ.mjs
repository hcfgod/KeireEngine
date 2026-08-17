import { t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { A as createAstro, b as addAttribute, m as renderTemplate, o as renderComponent, v as maybeRenderHead } from "./server_DvX7bpsP.mjs";
import { t as createComponent } from "./astro-component_CT_H5Ga1.mjs";
import { t as $$PlatformLayout } from "./PlatformLayout_C-gGDb3o.mjs";
import "./compiler_CefTWzEM.mjs";
import { t as featureEnabled } from "./marketplace_C3fGUB9S.mjs";
//#region Source/pages/oauth/consent.astro
var consent_exports = /* @__PURE__ */ __exportAll({
	default: () => $$Consent,
	file: () => $$file,
	prerender: () => false,
	url: () => $$url
});
createAstro("https://keireengine.duckdns.org");
var $$Consent = createComponent(async ($$result, $$props, $$slots) => {
	const Astro = $$result.createAstro($$props, $$slots);
	Astro.self = $$Consent;
	const authorizationId = Astro.url.searchParams.get("authorization_id") ?? "";
	if (!/^[A-Za-z0-9._~-]{16,2048}$/.test(authorizationId)) return new Response("The OAuth authorization request is missing or invalid.", { status: 400 });
	if (!await featureEnabled(Astro.locals.supabase, "hub_oauth_sso_enabled")) return new Response("Browser sign-in for Kéire Hub is not enabled in this environment.", { status: 503 });
	if (!Astro.locals.user) {
		const next = `/oauth/consent/?authorization_id=${encodeURIComponent(authorizationId)}`;
		return Astro.redirect(`/account/sign-in/?next=${encodeURIComponent(next)}`, 303);
	}
	if (!Astro.locals.supabase) return new Response("Account services are unavailable.", { status: 503 });
	const { data, error } = await Astro.locals.supabase.auth.oauth.getAuthorizationDetails(authorizationId);
	if (error || !data) return new Response("The authorization request is invalid or expired.", { status: 400 });
	if (!("authorization_id" in data)) return Astro.redirect(data.redirect_url, 303);
	const details = data;
	const scopes = details.scope?.split(" ").filter(Boolean) ?? [];
	return renderTemplate`${renderComponent($$result, "PlatformLayout", $$PlatformLayout, {
		"title": "Authorize Kéire Hub",
		"description": "Review and approve a separate Kéire Hub session.",
		"robots": "noindex,nofollow"
	}, { "default": ($$result) => renderTemplate`${maybeRenderHead($$result)}<section class="platform-shell account-shell"><div><p class="eyebrow">Desktop authorization</p><h1 class="display-title">Connect <span>${details.client.name}</span></h1><p class="lead">Kéire Hub receives its own independently revocable access and refresh tokens. Your website cookies, password, and browser refresh token remain in this browser.</p></div><div class="auth-card"><span class="status-badge status-badge--ready">Public PKCE client</span><h2>Review access</h2><dl class="definition-list"><div><dt>Application</dt><dd>${details.client.name}</dd></div><div><dt>Callback</dt><dd>${details.redirect_uri}</dd></div></dl><h3>Requested identity information</h3><ul>${scopes.map((scope) => renderTemplate`<li>${scope}</li>`)}</ul><form method="post" action="/oauth/decision/" class="actions"><input type="hidden" name="authorization_id"${addAttribute(authorizationId, "value")}><button class="button" type="submit" name="decision" value="approve">Authorize Kéire Hub</button><button class="button button--secondary" type="submit" name="decision" value="deny">Cancel</button></form><p class="fine-print">You can revoke this Hub session later under Account → Connected devices without signing out of the website.</p></div></section>` })}`;
}, "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/oauth/consent.astro", void 0);
var $$file = "C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/pages/oauth/consent.astro";
var $$url = "/oauth/consent/";
//#endregion
//#region \0virtual:astro:page:Source/pages/oauth/consent@_@astro
var page = () => consent_exports;
//#endregion
export { page };
