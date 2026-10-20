import { describe, expect, it } from "vitest";
import { publisherUploadTransport } from "./publisher-upload-client";

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
