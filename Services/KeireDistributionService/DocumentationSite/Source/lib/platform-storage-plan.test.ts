import { describe, expect, it } from "vitest";
import { planStorageRecovery } from "./platform-storage-plan.mjs";

function fixture() {
    return {
        project: "khjduyjamzwumhducmou",
        tables: {
            "storage.buckets": [{ id: "private", public: false, type: "STANDARD", versioning_status: "DISABLED" }],
            "storage.objects": [{ id: "object", bucket_id: "private", name: "opaque/name", metadata: { size: 3 },
                is_versioned: false, is_delete_marker: false, archived_at: null, owner_id: "owner" }],
            "public.marketplace_uploads": [{ id: "upload", storage_bucket: "private", storage_path: "opaque/name",
                expected_sha256: "a".repeat(64), expected_size_bytes: 3 }],
            "public.marketplace_product_versions": [],
            "public.marketplace_validation_reports": [],
            "public.marketplace_product_media": [],
        },
    };
}

describe("storage recovery planning", () => {
    it("preserves private metadata without claiming bytes recovered", () => {
        const source = fixture();
        const plan = planStorageRecovery(source);
        expect(plan.objects[0].source).toEqual(source.tables["storage.objects"][0]);
        expect(plan.buckets[0].public).toBe(false);
        expect(plan.summary.withExpectedHash).toBe(1);
        expect(plan.summary.recoveredObjects).toBe(0);
        expect(plan.objects[0].recoveryStatus).toBe("missing");
        plan.objects[0].source.owner_id = "changed";
        expect(source.tables["storage.objects"][0].owner_id).toBe("owner");
    });

    it("retains unreferenced objects instead of deleting them", () => {
        const source = fixture();
        source.tables["public.marketplace_uploads"] = [];
        expect(planStorageRecovery(source).summary.withoutExpectedHash).toBe(1);
    });

    it("records missing historical references separately", () => {
        const source = fixture();
        source.tables["storage.objects"] = [];
        expect(planStorageRecovery(source).summary.missingReferencedObjects).toBe(1);
    });

    it("rejects conflicting expected hashes", () => {
        const source = fixture();
        source.tables["public.marketplace_uploads"].push({ ...source.tables["public.marketplace_uploads"][0],
            id: "other", expected_sha256: "b".repeat(64) });
        expect(() => planStorageRecovery(source)).toThrow("Conflicting");
    });

    it("rejects source size mismatches", () => {
        const source = fixture();
        source.tables["storage.objects"][0].metadata.size = 4;
        expect(() => planStorageRecovery(source)).toThrow("Conflicting");
    });

    it("rejects duplicate identities and unreviewed versioning", () => {
        const source = fixture();
        source.tables["storage.objects"].push(source.tables["storage.objects"][0]);
        expect(() => planStorageRecovery(source)).toThrow("Duplicate");
        source.tables["storage.objects"].pop();
        source.tables["storage.buckets"][0].versioning_status = "ENABLED";
        expect(() => planStorageRecovery(source)).toThrow("versioning");
    });
});
