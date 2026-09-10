import { describe, expect, it } from "vitest";
import { publicationState } from "./publication-state";

describe("website publication state", () => {
    it("does not advertise a source candidate as an available download", () => {
        expect(publicationState("PROJECT_VERSION=0.4.4\r\n", {
            state: "preparing", version: "0.4.4", activeCatalogVersion: "0.4.2",
        })).toEqual({ sourceVersion: "0.4.4", publicationVersion: "0.4.4", preparing: true, availableVersion: "0.4.2" });
    });
    it("follows an activated catalog even when source development has advanced", () => {
        expect(publicationState("PROJECT_VERSION=0.5.0\n", {
            state: "active", version: "0.4.4", activeCatalogVersion: "0.4.2",
        })).toMatchObject({ sourceVersion: "0.5.0", preparing: false, availableVersion: "0.4.4" });
    });
    it.each([undefined, {}, { state: "failed", version: "0.4.4" },
        { state: "preparing", version: "0.4.4" }, { state: "active", version: "next" }])(
        "rejects ambiguous publication metadata instead of inventing availability", (state) => {
            expect(() => publicationState("PROJECT_VERSION=0.4.4", state)).toThrow();
        });
    it("requires the authoritative source version", () => {
        expect(() => publicationState("", { state: "active", version: "0.4.4" })).toThrow();
    });
});
