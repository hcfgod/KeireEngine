import assert from "node:assert/strict";
import { createHash, randomBytes, randomUUID } from "node:crypto";
import { writeFile, unlink } from "node:fs/promises";
import path from "node:path";
import { createDownloadHttpFixture } from "./platform-download-http-fixture.mjs";
import { createValidatorReportFixture } from "./validator-report-fixture.mjs";
import { tokenDigest } from "../Source/lib/platform-auth-crypto.mjs";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";

/** Only this run's disposable HTTP account, keys, rows, session and exact byte files are modified. */
export async function verifyAdminEvidenceHttp({ pool, userId, request, packageRoot, evidenceRoot, setStage }) {
    assert.ok(path.isAbsolute(packageRoot) && path.isAbsolute(evidenceRoot));
    const [[account]] = await pool.execute("SELECT email_key FROM platform_accounts WHERE id=?", [userId]);
    assert.equal(account?.email_key, `http-qa-${userId}@invalid.example`);
    const [[membership]] = await pool.execute("SELECT COUNT(*) AS total FROM platform_staff_members WHERE user_id=?", [userId]);
    assert.equal(Number(membership.total), 0);
    let base;
    const uploadId = randomUUID();
    const reportId = randomUUID();
    const keyId = `qa-evidence-${randomUUID()}`;
    const sessionToken = randomBytes(32).toString("base64url");
    const sessionHash = tokenDigest(sessionToken);
    const bytes = Buffer.from(JSON.stringify({ schemaVersion: 1, inventory: null, fixture: randomUUID() }));
    const sha256 = createHash("sha256").update(bytes).digest("hex");
    const file = path.join(evidenceRoot, sha256);
    let fileCreated = false;
    try {
        setStage("admin-evidence-fixture");
        base = await createDownloadHttpFixture(pool, userId, packageRoot);
        const signed = createValidatorReportFixture({ id: uploadId, version_id: base.version,
            expected_sha256: base.sha256, expected_size_bytes: String(base.bytes.length), storage_path: `qa/${uploadId}/package` });
        signed.trustedKey.key_id = keyId;
        signed.document.keyId = keyId;
        signed.report.evidenceSha256 = sha256;
        signed.document.evidenceSha256 = sha256;
        signed.report.evidenceSizeBytes = bytes.length;
        signed.document.evidenceSizeBytes = bytes.length;
        signed.seal();
        const fingerprint = `sha256:${createHash("sha256").update(Buffer.from(signed.trustedKey.public_key_base64, "base64")).digest("hex")}`;
        await writeFile(file, bytes, { flag: "wx", mode: 0o600 });
        fileCreated = true;
        await platformTransaction(pool, async (connection) => {
            await connection.execute(`INSERT INTO platform_staff_members
                (user_id,role,active,appointed_by,appointed_at,updated_at)
                VALUES (?,'moderator',TRUE,?,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`, [userId, userId]);
            await connection.execute(`INSERT INTO marketplace_validator_attestation_keys
                (key_id,algorithm,public_key_base64,fingerprint,active,valid_from,created_at)
                VALUES (?,'ed25519',?,?,TRUE,'2020-01-01 00:00:00',UTC_TIMESTAMP(6))`,
            [keyId, signed.trustedKey.public_key_base64, fingerprint]);
            await connection.execute(`INSERT INTO marketplace_uploads
                (id,version_id,created_by,storage_path,expected_size_bytes,expected_sha256,state,uploaded_at,
                created_at,expires_at,validation_attempts,storage_bucket)
                VALUES (?,?,?,?,?,?,'validated',UTC_TIMESTAMP(6),UTC_TIMESTAMP(6),DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 1 HOUR),0,'marketplace-quarantine')`,
            [uploadId, base.version, userId, signed.upload.storage_path, base.bytes.length, base.sha256]);
            const report = signed.report;
            await connection.execute(`INSERT INTO marketplace_validation_reports
                (id,upload_id,validator_version,validator_fingerprint_sha256,policy_version,package_sha256,
                manifest_sha256,passed,diagnostics,malware_scan_result,secret_scan_result,managed_validation_result,
                evidence_storage_path,evidence_sha256,evidence_size_bytes,attestation_key_id,signed_attestation,completed_at)
                VALUES (?,?,?,?,?,?,?,TRUE,'[]','clean','clean','not_applicable',?,?,?,?,?,'2026-10-20 12:00:00')`,
            [reportId, uploadId, report.validatorVersion, report.validatorFingerprintSha256, report.policyVersion,
                base.sha256, report.manifestSha256, report.evidenceStoragePath, sha256, bytes.length, keyId, JSON.stringify(report.attestation)]);
            const [created] = await connection.execute(`INSERT INTO platform_auth_sessions
                (token_hash,user_id,password_digest,assurance_level,factor_id,expires_ms)
                SELECT ?,user_id,password_digest,assurance_level,factor_id,expires_ms FROM platform_auth_sessions
                WHERE user_id=? AND assurance_level=2 AND expires_ms>? LIMIT 1`, [sessionHash, userId, Date.now()]);
            assert.equal(created.affectedRows, 1);
        });
        setStage("admin-evidence-issue");
        const issued = await request("/admin/marketplace/v1/actions/", { operation: "evidence.issue", reportId });
        assert.equal(issued.status, 200);
        const { data: grant } = await issued.json();
        assert.equal(grant.sha256, sha256);
        assert.equal(grant.attestationFingerprint, fingerprint);
        assert.equal(grant.url, `/admin/marketplace/v1/evidence/${reportId}/`);
        const delivered = await request(grant.url);
        assert.equal(delivered.status, 200);
        assert.match(delivered.headers.get("cache-control"), /no-store/);
        assert.deepEqual(Buffer.from(await delivered.arrayBuffer()), bytes);
        const anonymous = await request(grant.url, undefined, { cookie: "" });
        assert.equal(anonymous.status, 401);
        await anonymous.arrayBuffer();
        setStage("admin-evidence-session-revocation");
        const alternateCookie = { cookie: `__Host-keire-session=${sessionToken}` };
        const before = await request(grant.url, undefined, alternateCookie);
        assert.equal(before.status, 200);
        await before.arrayBuffer();
        await pool.execute("DELETE FROM platform_auth_sessions WHERE token_hash=? AND user_id=?", [sessionHash, userId]);
        const expired = await request(grant.url, undefined, alternateCookie);
        assert.equal(expired.status, 401);
        await expired.arrayBuffer();
        setStage("admin-evidence-role-revocation");
        await pool.execute("UPDATE platform_staff_members SET active=FALSE,revoked_at=UTC_TIMESTAMP(6) WHERE user_id=?", [userId]);
        const denied = await request(grant.url);
        assert.equal(denied.status, 403);
        await denied.arrayBuffer();
        await pool.execute("UPDATE platform_staff_members SET active=TRUE,revoked_at=NULL WHERE user_id=?", [userId]);
        setStage("admin-evidence-key-revocation");
        await pool.execute("UPDATE marketplace_validator_attestation_keys SET active=FALSE WHERE key_id=?", [keyId]);
        const badKey = await request(grant.url);
        assert.equal(badKey.status, 409);
        await badKey.arrayBuffer();
        await pool.execute("UPDATE marketplace_validator_attestation_keys SET active=TRUE WHERE key_id=?", [keyId]);
        setStage("admin-evidence-missing-bytes");
        await unlink(file);
        fileCreated = false;
        const missing = await request(grant.url);
        assert.equal(missing.status, 503);
        assert.match((await missing.json()).error.message, /unavailable/);
        return 14;
    } finally {
        await platformTransaction(pool, async (connection) => {
            await connection.execute("DELETE FROM platform_auth_sessions WHERE token_hash=? AND user_id=?", [sessionHash, userId]);
            await connection.execute("DELETE FROM marketplace_validation_reports WHERE id=?", [reportId]);
            await connection.execute("DELETE FROM marketplace_uploads WHERE id=?", [uploadId]);
            await connection.execute("DELETE FROM marketplace_validator_attestation_keys WHERE key_id=?", [keyId]);
            await connection.execute("DELETE FROM platform_staff_members WHERE user_id=?", [userId]);
        });
        if (fileCreated) {
            await unlink(file);
        }
        await base?.close();
    }
}
