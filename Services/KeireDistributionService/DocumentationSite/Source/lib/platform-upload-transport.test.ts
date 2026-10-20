import { afterEach, describe, expect, it } from "vitest";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { PrivateUploadFileStore } from "./platform-upload-file-store.mjs";
import { createUploadTransport } from "./platform-upload-transport.mjs";

const roots: string[] = [];
const origin = "https://qa.invalid.example";

afterEach(async () => {
    for (const root of roots.splice(0)) {
        if (path.dirname(root) !== path.resolve(tmpdir()) || !path.basename(root).startsWith("keire-tus-")) {
            throw new Error("Unexpected test directory.");
        }
        await rm(root, { recursive: true, force: true });
    }
});

describe("local resumable transport", () => {
    it("supports a prepared reservation without allowing additional transfer files", async () => {
        const root = await mkdtemp(path.join(tmpdir(), "keire-tus-"));
        roots.push(root);
        const datastore = new PrivateUploadFileStore({ directory: root });
        const uploadId = "11111111-1111-4111-8111-111111111111";
        const id = uploadId.replaceAll("-", "");
        await datastore.create({ id, size: 6, offset: 0, metadata: { reservationId: uploadId } });
        const transport = createUploadTransport({
            datastore, siteOrigin: origin, allowCreation: false,
            authorize: async () => ({ uploadId, expectedSizeBytes: 6 }),
        });
        const headers = { origin, "tus-resumable": "1.0.0" };
        const extra = await transport.handle(new Request(`${origin}/publisher/v1/transfer`, {
            method: "POST", headers: { ...headers, "upload-length": "6" },
        }));
        expect(extra.status).toBe(405);
        const resumed = await transport.handle(new Request(`${origin}/publisher/v1/transfer/${id}`, {
            method: "HEAD", headers,
        }));
        expect(resumed.status).toBe(200);
        expect(resumed.headers.get("upload-offset")).toBe("0");
        const wrong = await transport.handle(new Request(`${origin}/publisher/v1/transfer/${"a".repeat(32)}`, {
            method: "HEAD", headers,
        }));
        expect(wrong.status).toBe(403);
    });

    it("retains offsets across requests and rejects another reservation", async () => {
        const root = await mkdtemp(path.join(tmpdir(), "keire-tus-"));
        roots.push(root);
        let reservationId = "reservation-one";
        const transport = createUploadTransport({
            datastore: new PrivateUploadFileStore({ directory: root }),
            siteOrigin: origin,
            authorize: async () => ({ uploadId: reservationId, expectedSizeBytes: 6 }),
        });
        const send = (url, method, headers = {}, body?) => transport.handle(new Request(url, {
            method, headers: { origin, "tus-resumable": "1.0.0", ...headers }, body,
        }));
        const created = await send(`${origin}/publisher/v1/transfer`, "POST", { "upload-length": "6" });
        expect(created.status).toBe(201);
        const location = new URL(created.headers.get("location"), origin).href;
        const patch = await send(location, "PATCH",
            { "upload-offset": "0", "content-type": "application/offset+octet-stream" }, "abc");
        expect(patch.status).toBe(204);
        expect((await send(location, "HEAD")).headers.get("upload-offset")).toBe("3");
        const browserHead = await transport.handle(new Request(location, {
            method: "HEAD", headers: { "tus-resumable": "1.0.0" },
        }));
        expect(browserHead.status).toBe(200);
        expect(browserHead.headers.get("upload-offset")).toBe("3");
        expect((await send(location, "PATCH",
            { "upload-offset": "0", "content-type": "application/offset+octet-stream" }, "def")).status).toBe(409);
        reservationId = "reservation-two";
        expect((await send(location, "HEAD")).status).toBe(403);
        reservationId = "reservation-one";
        expect((await send(location, "PATCH",
            { "upload-offset": "3", "content-type": "application/offset+octet-stream" }, "def")).status).toBe(204);
        expect((await send(location, "HEAD")).headers.get("upload-offset")).toBe("6");
        expect((await send(location, "DELETE")).status).toBe(405);
    });
});
