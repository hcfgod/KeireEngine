import { describe, expect, it } from "vitest";
import { localUploadInput, localUploadProductSlugs, maximumPackageBytes } from "./platform-upload-input.mjs";

const valid = {
    publisherId: "A1111111-1111-4111-8111-111111111111",
    categoryId: "b1111111-1111-4111-8111-111111111111",
    productName: " Example package ", productSummary: " A descriptive package summary. ",
    licenseSpdx: "MIT", licenseRevision: "1", version: "1.2.3", minimumEngineVersion: "0.4.7",
    installKind: "asset_import", platforms: ["windows"], architectures: ["x64"], rendererCapabilities: [],
    expectedSha256: "a".repeat(64), expectedSizeBytes: 1,
};

describe("local upload reservation input", () => {
    it("preserves the release contract and normalizes only identity and display fields", () => {
        const result = localUploadInput(valid);
        expect(result.publisherId).toBe(valid.publisherId.toLowerCase());
        expect(result.productName).toBe("Example package");
        expect(result.productId).toBeNull();
        expect(result.maximumEngineVersion).toBeNull();
        expect(result.releaseNotes).toBe("");
        expect(result.platforms).not.toBe(valid.platforms);
        for (const installKind of ["registry", "asset_import", "complete_project"]) {
            expect(localUploadInput({ ...valid, installKind, expectedSizeBytes: maximumPackageBytes }).installKind)
                .toBe(installKind);
        }
    });

    it("rejects invalid and oversized package metadata before reservation", () => {
        for (const changes of [
            { expectedSizeBytes: 0 }, { expectedSizeBytes: maximumPackageBytes + 1 },
            { expectedSizeBytes: "1024" }, { expectedSizeBytes: 1.5 },
            { expectedSha256: "A".repeat(64) }, { productId: "../../file" },
            { productName: " " }, { productSummary: "short" },
            { version: "01.2.3" }, { maximumEngineVersion: "latest" },
            { installKind: "executable" }, { platforms: ["windows", "windows"] },
            { architectures: ["../x64"] }, { rendererCapabilities: Array(33).fill("vulkan") },
            { releaseNotes: "x".repeat(50001) }, { managedApiVersion: "x".repeat(129) },
        ]) {
            expect(() => localUploadInput({ ...valid, ...changes })).toThrow();
        }
        for (const input of [null, [], "invalid"]) {
            expect(() => localUploadInput(input)).toThrow();
        }
    });

    it("retains deterministic SQL-compatible slugs for punctuation and Unicode names", () => {
        expect(localUploadProductSlugs(" Example Package! ")[0]).toBe("example-package");
        expect(localUploadProductSlugs("蜘蛛")[0]).toMatch(/^asset-[0-9a-f]{12}$/);
        const [slug, collision] = localUploadProductSlugs("a".repeat(128));
        expect(slug.length).toBe(96);
        expect(collision.length).toBe(95);
        expect(collision).toMatch(/-[0-9a-f]{8}$/);
    });
});
