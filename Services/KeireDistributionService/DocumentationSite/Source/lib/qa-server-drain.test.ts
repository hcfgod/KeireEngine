import { createServer, get } from "node:http";
import { connect } from "node:net";
import { once } from "node:events";
import { describe, expect, it } from "vitest";
import { createQaServerDrain } from "../../scripts/qa-server-drain.mjs";

describe("QA listener shutdown", () => {
    it("closes unused connections but waits for an active response", async () => {
        let finishResponse: () => void = () => {};
        let acceptedRequest: () => void = () => {};
        const started = new Promise<void>((resolve) => { acceptedRequest = resolve; });
        const server = createServer((_request, response) => {
            finishResponse = () => response.end("completed");
            acceptedRequest();
        });
        const drain = createQaServerDrain(server, false);
        server.listen(0, "127.0.0.1");
        await once(server, "listening");
        const address = server.address();
        if (!address || typeof address === "string") {
            throw new Error("Missing QA listener address");
        }
        const spare = connect(address.port, "127.0.0.1");
        await once(spare, "connect");
        const spareClosed = once(spare, "close");
        let client;
        const result = new Promise<string>((resolve, reject) => {
            client = get({ host: "127.0.0.1", port: address.port, path: "/", agent: false }, (response) => {
                let body = "";
                response.setEncoding("utf8");
                response.on("data", (chunk) => { body += chunk; });
                response.on("end", () => resolve(body));
            });
            client.on("error", reject);
        });
        try {
            await started;
            const closing = drain();
            expect(drain()).toBe(closing);
            let closed = false;
            void closing.then(() => { closed = true; });
            await spareClosed;
            expect(closed).toBe(false);
            finishResponse();
            expect(await result).toBe("completed");
            await closing;
            expect(closed).toBe(true);
        } finally {
            spare.destroy();
            client?.destroy();
            server.closeAllConnections();
            await drain();
        }
    });
});
