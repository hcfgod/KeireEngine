import { generateKeyPairSync } from "node:crypto";
import { describe, expect, it, vi } from "vitest";
import { createLocalNativeService } from "./platform-native-service.mjs";

const origin = "https://platform.invalid";

function fixture() {
    const pool = { getConnection: vi.fn(), execute: vi.fn(async () => [[{
        client_id: "keire-hub-desktop", redirect_uri: `${origin}/oauth/hub/callback/`, scope: "openid email",
    }]]), end: vi.fn() };
    const authentication = { session: vi.fn(async () => ({ userId: "fixture", assurance: 1 })),
        withSessionTransaction: vi.fn(), pendingChallenge: vi.fn(), close: vi.fn() };
    const service = createLocalNativeService({ pool, authentication, siteOrigin: origin,
        clientId: "keire-hub-desktop", privateKey: generateKeyPairSync("ed25519").privateKey });
    return { pool, authentication, service };
}

describe("owned native authorization service", () => {
    it("publishes issuer discovery and public keys without accessing private accounts", async () => {
        const { service, pool, authentication } = fixture();
        try {
            const discovery = await service.handle(new Request(`${origin}/auth/v1/.well-known/openid-configuration`));
            const configuration = await discovery.json();
            expect(configuration.issuer).toBe(`${origin}/auth/v1`);
            expect(configuration.token_endpoint).toBe(`${origin}/auth/v1/oauth/token`);
            expect(configuration.code_challenge_methods_supported).toEqual(["S256"]);
            const keys = await (await service.handle(new Request(configuration.jwks_uri))).json();
            expect(keys.keys).toHaveLength(1);
            expect(keys.keys[0]).not.toHaveProperty("d");
            expect(pool.getConnection).not.toHaveBeenCalled();
            expect(authentication.session).not.toHaveBeenCalled();
        } finally {
            await service.close();
        }
    });

    it("renders authenticated consent and applies private response headers", async () => {
        const { service } = fixture();
        const render = vi.fn(async () => new Response("approved application details"));
        const response = await service.handle(new Request(`${origin}/oauth/consent/?authorization_id=${"a".repeat(43)}`, {
            headers: { cookie: `__Host-keire-session=${"b".repeat(43)}` },
        }), "trusted-client", render);
        expect(response.status).toBe(200);
        expect(render.mock.calls[0][0].nativeConsent.client.name).toBe("Kéire Hub");
        expect(response.headers.get("cache-control")).toBe("private, no-store");
        await service.close();
    });

    it("drains accepted consent reads without closing borrowed dependencies", async () => {
        const { service, pool, authentication } = fixture();
        let resolve;
        pool.execute.mockImplementationOnce(() => new Promise((complete) => { resolve = complete; }));
        const incoming = new Request(`${origin}/oauth/consent/?authorization_id=${"a".repeat(43)}`, {
            headers: { cookie: `__Host-keire-session=${"b".repeat(43)}` },
        });
        const pending = service.handle(incoming, "trusted-client", async () => new Response("consent"));
        await vi.waitFor(() => expect(resolve).toBeTypeOf("function"));
        let closed = false;
        const shutdown = service.close().then(() => { closed = true; });
        expect((await service.handle(incoming)).status).toBe(503);
        expect(closed).toBe(false);
        resolve([[{ client_id: "keire-hub-desktop", redirect_uri: `${origin}/oauth/hub/callback/`, scope: "openid" }]]);
        expect((await pending).status).toBe(200);
        await shutdown;
        expect(closed).toBe(true);
        expect(pool.end).not.toHaveBeenCalled();
        expect(authentication.close).not.toHaveBeenCalled();
    });

    it("leaves other routes alone and rejects unknown native endpoints", async () => {
        const { service } = fixture();
        expect(await service.handle(new Request(`${origin}/downloads/`))).toBeNull();
        expect((await service.handle(new Request(`${origin}/auth/v1/oauth/unknown`))).status).toBe(404);
        expect((await service.handle(new Request(`${origin}/auth/v1/.well-known/jwks.json?other=1`))).status).toBe(400);
        await service.close();
    });
});
