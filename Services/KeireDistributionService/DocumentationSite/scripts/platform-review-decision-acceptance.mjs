import assert from "node:assert/strict";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { decideLocalSubmission } from "../Source/lib/platform-review-decision.mjs";

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
    await assert.rejects(decideLocalSubmission(authentication(true), "fixture", approve),
        /Synthetic review audit failure/);
    const [[rolledBack]] = await connection.execute("SELECT state FROM marketplace_submissions WHERE id=?", [submissionId]);
    assert.equal(rolledBack.state, "submitted");
    const [emptyJobs] = await connection.execute("SELECT id FROM marketplace_publication_jobs");
    assert.equal(emptyJobs.length, 0);
    const [emptyCounter] = await connection.execute("SELECT id FROM platform_publication_sequence");
    assert.equal(emptyCounter.length, 0);
    assert.equal((await decideLocalSubmission(authentication(), "fixture", approve)).state, "approved_pending_signature");
    const [[job]] = await connection.execute("SELECT state,publication_sequence FROM marketplace_publication_jobs");
    assert.equal(job.state, "queued");
    assert.equal(String(job.publication_sequence), "1");
    await assert.rejects(decideLocalSubmission(authentication(), "fixture", { submissionId, decision: "in_review" }),
        { code: "staff.approval_withdrawal_required" });
    assert.equal((await decideLocalSubmission(authentication(), "fixture",
        { submissionId, decision: "rejected", note: "Withdraw fixture approval" })).state, "rejected");
    const [[cancelled]] = await connection.execute("SELECT state,lease_owner,lease_expires_at FROM marketplace_publication_jobs");
    assert.equal(cancelled.state, "cancelled");
    assert.equal(cancelled.lease_owner, null);
    assert.equal(cancelled.lease_expires_at, null);
    return 13;
}
