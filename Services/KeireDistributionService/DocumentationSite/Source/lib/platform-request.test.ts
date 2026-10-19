import { describe, expect, it, vi } from "vitest";
import { handleLocalPlatformRequest } from "./platform-request.mjs";

function fixture(pathname = "/account/sign-in/") {
    const authentication = {
        session: vi.fn().mockResolvedValue(null),
        pendingChallenge: vi.fn().mockResolvedValue({ factors: [{ id: "factor", friendly_name: "Phone" }], expiresAt: 1000 }),
    };
    const backend = { provider: "mysql", withBackend: async (action) => action({ authentication }) };
    const render = vi.fn().mockResolvedValue(new Response("rendered"));
    return { authentication, options: { backend, request: new Request(`https://example.test${pathname}`),
        siteOrigin: "https://example.test", clientIdentity: "trusted-client", correlationId: "id", render } };
}

describe("staged local platform request routing", () => {
    it("routes public product reads without treating browser cookies as ownership", async () => {
        const { options, authentication } = fixture("/marketplace/v1/products/00000000-0000-4000-8000-000000000001/");
        expect((await handleLocalPlatformRequest(options)).status).toBe(200);
        expect(options.render).toHaveBeenCalledWith({
            session: null, pendingChallenge: null, accountView: null, catalogPool: undefined,
        });
        expect(authentication.session).not.toHaveBeenCalled();
    });

    it("does not admit product mutations through the public read route", async () => {
        const { options } = fixture("/marketplace/v1/products/00000000-0000-4000-8000-000000000001/");
        options.request = new Request(options.request.url, { method: "POST" });
        expect((await handleLocalPlatformRequest(options)).status).toBe(503);
        expect(options.render).not.toHaveBeenCalled();
    });

    it("renders sign-in anonymously without inventing an authenticated user", async () => {
        const { options } = fixture();
        expect(await (await handleLocalPlatformRequest(options)).text()).toBe("rendered");
        expect(options.render).toHaveBeenCalledWith({ session: null, pendingChallenge: null, accountView: null, emailAvailable: false });
    });

    it("passes pending challenge metadata separately from authentication", async () => {
        const { options } = fixture("/account/mfa/challenge/");
        options.request = new Request(options.request.url, { headers: { cookie: `__Host-keire-challenge=${"a".repeat(43)}` } });
        expect((await handleLocalPlatformRequest(options)).status).toBe(200);
        expect(options.render).toHaveBeenCalledWith({ session: null,
            pendingChallenge: { factors: [{ id: "factor", friendly_name: "Phone" }], expiresAt: 1000 }, accountView: null, emailAvailable: false });
    });

    it("handles authentication API responses without rendering a page", async () => {
        const { options } = fixture("/account/session/");
        const response = await handleLocalPlatformRequest(options);
        expect(await response.json()).toEqual({ data: { signedIn: false, assuranceLevel: null } });
        expect(options.render).not.toHaveBeenCalled();
    });

    it("fails closed on unported routes without calling the previous backend", async () => {
        const { options } = fixture("/marketplace/v1/downloads/");
        expect((await handleLocalPlatformRequest(options)).status).toBe(503);
        expect(options.render).not.toHaveBeenCalled();
    });

    it("requires explicit initialized MySQL service ownership", async () => {
        const { options } = fixture();
        options.backend.provider = "supabase";
        expect((await handleLocalPlatformRequest(options)).status).toBe(503);
        expect(options.render).not.toHaveBeenCalled();
    });
});
