import assert from "node:assert/strict";
import { randomBytes } from "node:crypto";
import { createLocalPublicationService } from "../Source/lib/platform-publication-service.mjs";
import { createQaPlatformHttp } from "./qa-validator-http.mjs";
import { verifyCSharpQueueHttp } from "./qa-validator-csharp.mjs";

/** Call only with the validator fixture's connection-local tables and temporary immutable blob store. */
export async function verifyCSharpPublication(pool, connection, store) {
    const [[key]] = await connection.execute(`SELECT signing.key_id,signing.public_key_base64,signing.fingerprint
        FROM marketplace_validator_attestation_keys signing
        JOIN marketplace_validation_reports report ON report.attestation_key_id=signing.key_id`);
    assert.ok(key, "Publication acceptance requires a validated C# report.");
    const validatorKey = {
        schemaVersion: 1, algorithm: "Ed25519", keyId: key.key_id,
        publicKey: key.public_key_base64, fingerprint: key.fingerprint,
    };
    await connection.execute("DELETE FROM marketplace_publications");
    await connection.execute("DELETE FROM platform_audit_events WHERE action='marketplace.version_published'");
    await connection.execute("UPDATE marketplace_product_versions SET state='approved_pending_signature'");
    await connection.execute("UPDATE marketplace_submissions SET state='approved_pending_signature'");
    await connection.execute(`UPDATE marketplace_publication_jobs SET state='queued',attempt_count=0,
        publication_sequence=9007199254740993,published_at=NULL,signing_key_id=NULL,
        cancelled_at=NULL,cancelled_by=NULL,last_error_code=NULL,retry_after=NULL`);
    const queueSecret = randomBytes(32).toString("hex");
    const http = await createQaPlatformHttp("publication", (siteOrigin) => createLocalPublicationService({
        pool, packageStore: store, evidenceStore: store, siteOrigin, queueSecret,
    }));
    try {
        await verifyCSharpQueueHttp("publication", { origin: http.origin, queueSecret, validatorKey }, async (document) => {
            assert.equal(document.algorithm, "Ed25519");
            await connection.execute(`INSERT INTO marketplace_signature_keys
                (key_id,algorithm,public_key_base64,fingerprint,active,valid_from,created_at)
                VALUES (?,'ed25519',?,?,TRUE,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
            [document.keyId, document.publicKey, document.fingerprint]);
        });
        const [[job]] = await connection.execute("SELECT state,CAST(publication_sequence AS CHAR) AS sequence FROM marketplace_publication_jobs");
        assert.equal(job.state, "published");
        assert.equal(job.sequence, "9007199254740993");
        const [[publications]] = await connection.execute("SELECT COUNT(*) AS total FROM marketplace_publications");
        assert.equal(Number(publications.total), 1);
        const [[receipts]] = await connection.execute("SELECT COUNT(*) AS total FROM platform_audit_events WHERE action='marketplace.version_published'");
        assert.equal(Number(receipts.total), 1);
        return 5;
    } finally {
        await http.close();
    }
}
