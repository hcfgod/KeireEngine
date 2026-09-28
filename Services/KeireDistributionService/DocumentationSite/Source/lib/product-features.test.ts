import { existsSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { allDocSources, sourcePathToSlug } from "../../doc-library.mjs";
import { productFeatures } from "./product-features";

describe("product discovery destinations", () => {
    it("provides unique, safe routes with a reachable next discipline", () => {
        const ids = new Set(productFeatures.map((feature) => feature.id));
        expect(ids.size).toBe(productFeatures.length);
        for (const feature of productFeatures) {
            expect(feature.id).toMatch(/^[a-z]+$/);
            expect(ids.has(feature.next)).toBe(true);
            expect(feature.next).not.toBe(feature.id);
        }
    });

    it("links every capability to a real canonical guide", () => {
        const routes = new Set(allDocSources.map((source: string) => `/docs/${sourcePathToSlug(source)}/`));
        for (const feature of productFeatures) {
            expect(feature.topics.length).toBeGreaterThan(0);
            for (const topic of feature.topics) expect(routes.has(topic.href), topic.href).toBe(true);
        }
    });

    it("uses existing local showcase images with accessible descriptions", () => {
        for (const feature of productFeatures) {
            expect(existsSync(new URL(`../media/outpost/${feature.image}.webp`, import.meta.url))).toBe(true);
            expect(feature.alt.trim().length).toBeGreaterThan(0);
        }
    });
});
