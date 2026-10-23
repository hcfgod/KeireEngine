import { createHash } from "node:crypto";
import { describe, expect, it, vi } from "vitest";
import { createValidatorReportFixture } from "../../scripts/validator-report-fixture.mjs";
import { readLocalAdminEvidence } from "./platform-admin-evidence.mjs";
import { handleLocalAdminEvidenceRequest } from "./platform-admin-evidence-http.mjs";

const reportId = "33333333-3333-4333-8333-333333333333";

function fixture() {
    const signed = createValidatorReportFixture();
    const bytes = Buffer.from('{"schemaVersion":1,"inventory":null}');
    signed.report.evidenceSha256 = createHash("sha256").update(bytes).digest("hex");
    signed.report.evidenceSizeBytes = bytes.length;
    signed.document.evidenceSha256 = signed.report.evidenceSha256;
    signed.document.evidenceSizeBytes = bytes.length;
    signed.seal();
    const key = { ...signed.trustedKey, fingerprint: `sha256:${createHash("sha256")
        .update(Buffer.from(signed.trustedKey.public_key_base64, "base64")).digest("hex")}` };
    const saved: Record<string, any> = Object.fromEntries(Object.entries(signed.report)
        .map(([name, value]) => [name.replace(/[A-Z]/g, (letter) => `_${letter.toLowerCase()}`), value]));
    Object.assign(saved, { id: reportId, upload_id: signed.upload.id, code_fingerprint: null,
        attestation_key_id: key.key_id, signed_attestation: JSON.stringify(signed.report.attestation),
        completed_at: "2026-10-20 12:00:00.000000" });
    const session = { userId: "staff", assurance: 2 };
    const staff = { role: "moderator", active: 1, revoked_at: null };
    const execute = vi.fn(async (sql: string) => {
        if (sql.includes("FROM platform_staff_members")) return [[staff]];
        if (sql.includes("FROM marketplace_validation_reports")) return [[saved]];
        if (sql.includes("FROM marketplace_uploads")) return [[signed.upload]];
        if (sql.includes("FROM marketplace_validator_attestation_keys")) return [[key]];
        throw new Error("Unexpected query");
    });
    const authentication = { withSessionTransaction: vi.fn(async (_token: string, action: Function) => action({ execute }, session)) };
    const store = { read: vi.fn(async function* () { yield bytes; }) };
    return { authentication, store, session, staff, saved, key, execute, bytes };
}

const request = (method = "GET", headers: Record<string, string> = {}) => new Request(
    `https://example.test/admin/marketplace/v1/evidence/${reportId}/`, {
        method, headers: { cookie: `__Host-keire-session=${"a".repeat(43)}`, ...headers },
    });

describe("staff signed evidence", () => {
    it("verifies Ed25519, persisted report binding and exact private bytes", async () => {
        const test = fixture();
        const evidence = await readLocalAdminEvidence(test.authentication, test.store, "token", reportId);
        expect(evidence.bytes).toEqual(test.bytes);
        expect(evidence.fingerprint).toBe(test.key.fingerprint);
        expect(test.store.read).toHaveBeenCalledWith({ sha256: test.saved.evidence_sha256, sizeBytes: test.bytes.length });
    });
    it.each(["evidence_sha256", "package_sha256", "manifest_sha256", "validator_version", "completed_at"])("rejects altered stored %s before reading bytes", async (field) => {
        const test = fixture();
        test.saved[field] = field === "completed_at" ? "2026-10-21 12:00:00" : "altered";
        await expect(readLocalAdminEvidence(test.authentication, test.store, "token", reportId))
            .rejects.toMatchObject({ code: "evidence.attestation_invalid" });
        expect(test.store.read).not.toHaveBeenCalled();
    });
    it("rejects revoked trusted keys and fingerprints", async () => {
        const test = fixture();
        test.key.active = false;
        await expect(readLocalAdminEvidence(test.authentication, test.store, "token", reportId)).rejects.toMatchObject({ status: 409 });
        test.key.active = true;
        test.key.fingerprint = "sha256:wrong";
        await expect(readLocalAdminEvidence(test.authentication, test.store, "token", reportId)).rejects.toMatchObject({ status: 409 });
        expect(test.store.read).not.toHaveBeenCalled();
    });
    it("rejects truncated or changed bytes even when a store returns them", async () => {
        const test = fixture();
        test.store.read.mockImplementation(async function* () { yield Buffer.from("altered"); });
        await expect(readLocalAdminEvidence(test.authentication, test.store, "token", reportId))
            .rejects.toMatchObject({ code: "evidence.storage_unavailable" });
    });
    it("rechecks session, assurance and active staff authorization for each read", async () => {
        const test = fixture();
        test.session.assurance = 1;
        await expect(readLocalAdminEvidence(test.authentication, test.store, "token", reportId)).rejects.toMatchObject({ status: 403 });
        expect(test.execute).not.toHaveBeenCalled();
        test.session.assurance = 2;
        test.staff.active = 0;
        await expect(readLocalAdminEvidence(test.authentication, test.store, "token", reportId)).rejects.toMatchObject({ status: 403 });
        expect(test.store.read).not.toHaveBeenCalled();
        await expect(readLocalAdminEvidence({ withSessionTransaction: async () => null }, test.store, "expired", reportId))
            .rejects.toMatchObject({ status: 401 });
    });
    it("fails closed when evidence storage has not been configured", async () => {
        const test = fixture();
        await expect(readLocalAdminEvidence(test.authentication, null, "token", reportId)).rejects.toMatchObject({ status: 503 });
        expect(test.execute).toHaveBeenCalledTimes(1);
    });
});

describe("staff evidence delivery", () => {
    it("serves bounded no-store attachments only to the current browser session", async () => {
        const test = fixture();
        const response = await handleLocalAdminEvidenceRequest({ authentication: test.authentication, adminEvidenceStore: test.store }, request());
        expect(response.status).toBe(200);
        expect(Buffer.from(await response.arrayBuffer())).toEqual(test.bytes);
        expect(response.headers.get("content-disposition")).toContain("attachment");
        expect(response.headers.get("cache-control")).toBe("private, no-store");
        expect(response.headers.get("cross-origin-resource-policy")).toBe("same-origin");
    });
    it("HEAD verifies the same authorization without returning bytes", async () => {
        const test = fixture();
        const response = await handleLocalAdminEvidenceRequest({ authentication: test.authentication, adminEvidenceStore: test.store }, request("HEAD"));
        expect(response.status).toBe(200);
        expect(await response.text()).toBe("");
        expect(response.headers.get("content-length")).toBe(String(test.bytes.length));
    });
    it("rejects anonymous/bearer delivery and unsupported methods", async () => {
        const test = fixture();
        const service = { authentication: test.authentication, adminEvidenceStore: test.store };
        expect((await handleLocalAdminEvidenceRequest(service, request("GET", { cookie: "" }))).status).toBe(401);
        expect((await handleLocalAdminEvidenceRequest(service, request("GET", { authorization: "Bearer token" }))).status).toBe(401);
        expect((await handleLocalAdminEvidenceRequest(service, request("POST"))).status).toBe(405);
        expect(test.execute).not.toHaveBeenCalled();
    });
});
