import assert from "node:assert/strict";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { decideLocalSubmission } from "../Source/lib/platform-review-decision.mjs";
import { handleLocalReviewRequest } from "../Source/lib/platform-review-http.mjs";
import { failLocalPublication, leaseLocalPublication, renewLocalPublication } from "../Source/lib/platform-publication-leases.mjs";

export async function verifyReviewDecision(pool, connection, actor, submissionId) {
    await connection.execute(`INSERT INTO platform_staff_members
        (user_id,role,active,appointed_at,updated_at)
        VALUES (?,'moderator',TRUE,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`, [actor.userId]);
    const authentication = (failAudit = false) => ({
        withSessionTransaction: (_token, action) => platformTransaction(pool, (transaction) => action({
            execute(sql, parameters) {
                if (failAudit && sql.includes("INSERT INTO platform_audit_events")) {
                    throw new Error("Synthetic review audit failure");
                }
                return transaction.execute(sql, parameters);
            },
        }, actor)),
    });
    const approve = { submissionId, decision: "approved_pending_signature" };
    await assert.rejects(decideLocalSubmission(authentication(), "fixture", approve),
        { code: "staff.administrator_required" });
    await connection.execute("UPDATE platform_staff_members SET role='administrator' WHERE user_id=?", [actor.userId]);
    const [[upload]] = await connection.execute("SELECT id,version_id FROM marketplace_uploads");
    await connection.execute("UPDATE marketplace_uploads SET version_id=UUID() WHERE id=?", [upload.id]);
    await assert.rejects(decideLocalSubmission(authentication(), "fixture", approve),
        { code: "staff.validation_required" });
    await connection.execute("UPDATE marketplace_uploads SET version_id=? WHERE id=?", [upload.version_id, upload.id]);
    await assert.rejects(decideLocalSubmission(authentication(true), "fixture", approve),
        /Synthetic review audit failure/);
    const [[rolledBack]] = await connection.execute("SELECT state FROM marketplace_submissions WHERE id=?", [submissionId]);
    assert.equal(rolledBack.state, "submitted");
    const [emptyJobs] = await connection.execute("SELECT id FROM marketplace_publication_jobs");
    assert.equal(emptyJobs.length, 0);
    const [emptyCounter] = await connection.execute("SELECT id FROM platform_publication_sequence");
    assert.equal(emptyCounter.length, 0);
    const response = await handleLocalReviewRequest(authentication(), new Request("https://qa.invalid/admin/marketplace/v1/actions/", {
        method: "POST", headers: { origin: "https://qa.invalid", "content-type": "application/json",
            cookie: `__Host-keire-session=${"a".repeat(43)}` },
        body: JSON.stringify({ operation: "submission.decide", ...approve }),
    }), "https://qa.invalid", "fixture");
    assert.equal(response.status, 200);
    assert.equal((await response.json()).data.submission_state, "approved_pending_signature");
    const [[job]] = await connection.execute("SELECT state,publication_sequence FROM marketplace_publication_jobs");
    assert.equal(job.state, "queued");
    assert.equal(String(job.publication_sequence), "1");
    await connection.execute(`INSERT INTO marketplace_signature_keys
        (key_id,algorithm,public_key_base64,fingerprint,active,valid_from,created_at)
        SELECT 'publication-fixture',algorithm,public_key_base64,fingerprint,TRUE,valid_from,UTC_TIMESTAMP(6)
        FROM marketplace_validator_attestation_keys LIMIT 1`);
    await assert.rejects(leaseLocalPublication(pool, "signer-fixture", "unknown-key"), { code: "publication.key_unavailable" });
    const lease = await leaseLocalPublication(pool, "signer-fixture", "publication-fixture");
    assert.equal(lease.publication_sequence, "1");
    assert.ok(lease.validation_attestation);
    assert.equal(await leaseLocalPublication(pool, "other-signer", "publication-fixture"), null);
    await assert.rejects(renewLocalPublication(pool, lease.job_id, "other-signer"), { code: "publication.lease_lost" });
    assert.ok(Date.parse((await renewLocalPublication(pool, lease.job_id, "signer-fixture")).leaseExpiresAt) > Date.now());
    await connection.execute("UPDATE marketplace_publication_jobs SET lease_expires_at=DATE_SUB(UTC_TIMESTAMP(6),INTERVAL 1 SECOND)");
    assert.equal(await leaseLocalPublication(pool, "other-signer", "publication-fixture"), null);
    const [[waiting]] = await connection.execute("SELECT state,lease_owner FROM marketplace_publication_jobs");
    assert.equal(waiting.state, "retry_wait");
    assert.equal(waiting.lease_owner, null);
    await connection.execute("UPDATE marketplace_publication_jobs SET retry_after=DATE_SUB(UTC_TIMESTAMP(6),INTERVAL 1 SECOND)");
    assert.equal((await leaseLocalPublication(pool, "other-signer", "publication-fixture")).job_id, lease.job_id);
    await assert.rejects(failLocalPublication(pool, lease.job_id, "signer-fixture", "publication.fixture", true),
        { code: "publication.lease_lost" });
    assert.equal((await failLocalPublication(pool, lease.job_id, "other-signer", "publication.fixture", true)).state, "retry_wait");
    const [[retry]] = await connection.execute(`SELECT last_error_code,lease_owner,
        TIMESTAMPDIFF(SECOND,UTC_TIMESTAMP(6),retry_after) AS remaining FROM marketplace_publication_jobs`);
    assert.equal(retry.last_error_code, "publication.fixture");
    assert.equal(retry.lease_owner, null);
    assert.ok(Number(retry.remaining) >= 295 && Number(retry.remaining) <= 300);
    assert.equal(await leaseLocalPublication(pool, "other-signer", "publication-fixture"), null);
    await connection.execute("UPDATE marketplace_publication_jobs SET retry_after=DATE_SUB(UTC_TIMESTAMP(6),INTERVAL 1 SECOND)");
    await leaseLocalPublication(pool, "other-signer", "publication-fixture");
    assert.equal((await failLocalPublication(pool, lease.job_id, "other-signer", "publication.permanent", false)).state, "failed");
    await connection.execute(`UPDATE marketplace_publication_jobs SET state='leased',lease_owner='other-signer',
        lease_expires_at=DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 1 MINUTE)`);
    await connection.execute(`UPDATE marketplace_publication_jobs SET attempt_count=5,
        lease_expires_at=DATE_SUB(UTC_TIMESTAMP(6),INTERVAL 1 SECOND)`);
    assert.equal(await leaseLocalPublication(pool, "signer-fixture", "publication-fixture"), null);
    const [[exhausted]] = await connection.execute("SELECT state FROM marketplace_publication_jobs");
    assert.equal(exhausted.state, "failed");
    await assert.rejects(decideLocalSubmission(authentication(), "fixture", { submissionId, decision: "in_review" }),
        { code: "staff.approval_withdrawal_required" });
    assert.equal((await decideLocalSubmission(authentication(), "fixture",
        { submissionId, decision: "rejected", note: "Withdraw fixture approval" })).state, "rejected");
    const [[cancelled]] = await connection.execute("SELECT state,lease_owner,lease_expires_at FROM marketplace_publication_jobs");
    assert.equal(cancelled.state, "cancelled");
    assert.equal(cancelled.lease_owner, null);
    assert.equal(cancelled.lease_expires_at, null);
    await assert.rejects(renewLocalPublication(pool, lease.job_id, "signer-fixture"), { code: "publication.lease_lost" });
    await assert.rejects(failLocalPublication(pool, lease.job_id, "other-signer", "publication.fixture", true),
        { code: "publication.lease_lost" });
    return 36;
}
