import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { submitLocalVersion } from "../Source/lib/platform-version-submit.mjs";
import { handleLocalVersionSubmit } from "../Source/lib/platform-version-submit-http.mjs";
import { verifyReviewDecision } from "./platform-review-decision-acceptance.mjs";

/** Caller supplies connection-local product/version/report/submission/audit copies. */
export async function verifyVersionSubmission(pool, versionId) {
    const connection = await pool.getConnection();
    try {
        const [[upload]] = await connection.execute(
            "SELECT created_by FROM marketplace_uploads WHERE version_id=?", [versionId]);
        const actor = { userId: upload.created_by, assurance: 2 };
        return await runSubmissionChecks(pool, connection, actor, versionId);
    } catch (error) {
        error.fixtureStage ??= "submission-transaction";
        throw error;
    } finally {
        connection.release();
    }
}

async function runSubmissionChecks(pool, connection, actor, versionId) {
    let stage = "submission-assurance";
    let transactionError;
    try {
        const authentication = (session, failAudit = false) => ({
            withSessionTransaction: (_token, action) => platformTransaction(pool, (transaction) => action({
                execute(sql, parameters) {
                    if (failAudit && sql.includes("INSERT INTO platform_audit_events")) {
                        throw new Error("Synthetic submission audit failure");
                    }
                    return transaction.execute(sql, parameters);
                },
            }, session)).catch((error) => {
                transactionError = error;
                throw error;
            }),
        });
        await assert.rejects(submitLocalVersion(authentication({ ...actor, assurance: 1 }), "fixture", versionId),
            { code: "auth.mfa_required" });
        stage = "submission-ownership";
        await assert.rejects(submitLocalVersion(authentication({ ...actor, userId: randomUUID() }), "fixture", versionId),
            { code: "publisher.version_not_submittable" });
        stage = "submission-audit-rollback";
        await assert.rejects(submitLocalVersion(authentication(actor, true), "fixture", versionId),
            /Synthetic submission audit failure/);
        const [[rolledBack]] = await connection.execute("SELECT state FROM marketplace_product_versions WHERE id=?", [versionId]);
        assert.equal(rolledBack.state, "validated");
        const [empty] = await connection.execute("SELECT id FROM marketplace_submissions");
        assert.equal(empty.length, 0);
        stage = "submission-http";
        transactionError = null;
        const request = () => new Request(`https://qa.invalid/publisher/v1/versions/${versionId}/submit/`, {
            method: "POST", headers: { origin: "https://qa.invalid", cookie: `__Host-keire-session=${"a".repeat(43)}` },
        });
        const response = await handleLocalVersionSubmit(authentication(actor), request(), "https://qa.invalid", "fixture");
        if (transactionError) {
            throw transactionError;
        }
        stage = response.status === 201 ? "submission-http-body" : "submission-http-rejected";
        if (response.status !== 201) {
            const rejected = await response.clone().json();
            if (/^[a-z._]{1,45}$/.test(rejected.error?.code ?? "")) {
                stage = `submission-${rejected.error.code.replaceAll(".", "-").replaceAll("_", "-")}`;
            }
        }
        assert.equal(response.status, 201);
        const { data } = await response.json();
        const [[submission]] = await connection.execute("SELECT * FROM marketplace_submissions WHERE id=?", [data.submissionId]);
        stage = "submission-persisted-state";
        assert.equal(submission.state, "submitted");
        assert.equal(submission.submitted_by, actor.userId);
        stage = "submission-duplicate";
        assert.equal((await handleLocalVersionSubmit(authentication(actor), request(), "https://qa.invalid", "fixture")).status, 409);
        await connection.execute("UPDATE marketplace_submissions SET state='changes_requested',decision_note='Fixture changes'");
        await connection.execute("UPDATE marketplace_product_versions SET state='validated' WHERE id=?", [versionId]);
        stage = "submission-resubmit";
        const resubmitted = await submitLocalVersion(authentication(actor), "fixture", versionId);
        assert.equal(resubmitted.submissionId, data.submissionId);
        const [[updated]] = await connection.execute("SELECT decision_note,assigned_to FROM marketplace_submissions");
        assert.equal(updated.decision_note, null);
        assert.equal(updated.assigned_to, null);
        stage = "submission-review-decision";
        return 12 + await verifyReviewDecision(pool, connection, actor, data.submissionId);
    } catch (error) {
        error.fixtureStage = stage;
        throw error;
    }
}
