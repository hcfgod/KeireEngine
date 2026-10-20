import { describe, expect, it, vi } from "vitest";
import { confirmLocalUpload, publisherUploadTransport } from "./publisher-upload-client";

const origin = "https://qa.invalid.example";
const uploadId = "11111111-1111-4111-8111-111111111111";
const uploadUrl = `/publisher/v1/transfer/${uploadId.replaceAll("-", "")}/`;

describe("publisher upload provider selection", () => {
    it("uses the prepared local URL with current browser credentials", () => {
        expect(publisherUploadTransport({ uploadId, uploadUrl }, true, origin)).toEqual({
            uploadUrl: origin + uploadUrl, headers: { "x-signature": uploadId },
            withCredentials: true, uploadDataDuringCreation: false,
        });
    });

    it("preserves the Supabase upload endpoint and grant", () => {
        expect(publisherUploadTransport({
            resumableEndpoint: "https://storage.example/upload", uploadToken: "synthetic-grant",
        }, false, origin)).toEqual({
            endpoint: "https://storage.example/upload", headers: { "x-signature": "synthetic-grant" },
            uploadDataDuringCreation: true,
        });
    });

    it.each([
        `https://other.invalid${uploadUrl}`,
        `${uploadUrl}?redirect=other`,
        `${uploadUrl}#fragment`,
        "/publisher/v1/transfer/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/",
        `https://user:password@qa.invalid.example${uploadUrl}`,
    ])("rejects an unexpected local destination: %s", (address) => {
        expect(() => publisherUploadTransport({ uploadId, uploadUrl: address }, true, origin)).toThrow();
    });

    it("rejects incomplete local and Supabase responses", () => {
        expect(() => publisherUploadTransport({}, true, origin)).toThrow("incomplete");
        expect(() => publisherUploadTransport({}, false, origin)).toThrow("incomplete");
    });
});

describe("upload confirmation recovery", () => {
    it("retries the same reservation after a lost response without deleting or uploading bytes", async () => {
        const request = vi.fn()
            .mockRejectedValueOnce(new TypeError("Disconnected"))
            .mockResolvedValueOnce(Response.json({ data: { uploadId, state: "uploaded" } }));
        await expect(confirmLocalUpload(uploadId, request)).rejects.toThrow("Disconnected");
        await expect(confirmLocalUpload(uploadId, request)).resolves.toBeUndefined();
        expect(request.mock.calls).toEqual(Array(2).fill([
            `/publisher/v1/uploads/${uploadId}/complete/`,
            { method: "POST", credentials: "same-origin", headers: { "Content-Type": "application/json" }, body: "{}" },
        ]));
    });

    it.each(["leased", "validated", "failed"])("accepts a receipt after validation advanced to %s", async (state) => {
        const request = vi.fn().mockResolvedValue(Response.json({ data: { uploadId, state } }));
        await expect(confirmLocalUpload(uploadId, request)).resolves.toBeUndefined();
    });

    it.each([
        { data: { uploadId: "another-upload", state: "uploaded" } },
        { data: { uploadId, state: "uploading" } },
        {},
    ])("rejects a response that does not confirm this upload", async (payload) => {
        await expect(confirmLocalUpload(uploadId, vi.fn().mockResolvedValue(Response.json(payload))))
            .rejects.toThrow("did not confirm");
    });

    it("preserves an authentication error without claiming success", async () => {
        const request = vi.fn().mockResolvedValue(Response.json(
            { error: { message: "Sign in again." } }, { status: 401 }));
        await expect(confirmLocalUpload(uploadId, request)).rejects.toThrow("Sign in again.");
    });
});
