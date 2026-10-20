import assert from "node:assert/strict";
import { createHash, generateKeyPairSync, sign } from "node:crypto";
import { completeLocalPublication } from "../Source/lib/platform-publication-complete.mjs";
import { leaseLocalPublication } from "../Source/lib/platform-publication-leases.mjs";

/** Database transition acceptance only; blob verification is separately covered by the immutable-store tests. */
export async function verifyPublicationCompletion(pool, connection) {
    let stage = "submission-publication-setup";
    try {
        return await verifyTransitions(pool, connection, (next) => { stage = next; });
    } catch (error) {
        const actualCode = error.actual?.code;
        error.fixtureStage = typeof actualCode === "string" && /^[a-z._]{1,64}$/.test(actualCode)
            ? `${stage}-${actualCode.replaceAll(".", "-").replaceAll("_", "-")}` : stage;
        throw error;
    }
}

async function verifyTransitions(pool, connection, setStage) {
    const keys = generateKeyPairSync("ed25519");
    const publicBytes = Buffer.from(keys.publicKey.export({ format: "jwk" }).x, "base64url");
    await connection.execute(`UPDATE marketplace_signature_keys SET public_key_base64=?,fingerprint=?
        WHERE key_id='publication-fixture'`,
    [publicBytes.toString("base64"), `sha256:${createHash("sha256").update(publicBytes).digest("hex")}`]);
    await connection.execute("UPDATE marketplace_submissions SET state='approved_pending_signature'");
    await connection.execute("UPDATE marketplace_product_versions SET state='approved_pending_signature'");
    await connection.execute(`UPDATE marketplace_publication_jobs SET state='queued',attempt_count=0,
        cancelled_at=NULL,cancelled_by=NULL,last_error_code=NULL`);
    const lease = await leaseLocalPublication(pool, "completion-fixture", "publication-fixture");
    const document = JSON.stringify({
        schemaVersion: 1, keyId: "publication-fixture", sequence: Number(lease.publication_sequence),
        expiresAt: lease.publication_expires_at, productId: lease.product_id, versionId: lease.version_id,
        artifactSha256: lease.artifact_sha256, artifactSizeBytes: Number(lease.artifact_size_bytes),
        manifestSha256: lease.manifest_sha256, releaseStoragePath: lease.storage_path,
    });
    assert.equal(lease.publication_sequence, "1");
    const signedManifest = JSON.stringify({
        schemaVersion: 1, document,
        signature: {
            algorithm: "ed25519", keyId: "publication-fixture",
            value: sign(null, Buffer.from(document), keys.privateKey).toString("base64"),
            sequence: 1, expiresAt: lease.publication_expires_at,
        },
    });
    let verifications = 0;
    const stores = {
        packageStore: {
            async verify(identity) {
                assert.deepEqual(identity, { sha256: lease.artifact_sha256, sizeBytes: Number(lease.artifact_size_bytes) });
                verifications++;
            },
        },
        evidenceStore: {
            async verify(identity) {
                assert.deepEqual(identity, { sha256: lease.evidence_sha256, sizeBytes: Number(lease.evidence_size_bytes) });
                verifications++;
            },
        },
    };
    const complete = (selectedPool = pool, selectedStores = stores, worker = "completion-fixture", text = signedManifest) =>
        completeLocalPublication(selectedPool, selectedStores, lease.job_id, worker, text);
    await assert.rejects(complete(pool, stores, "other-worker"), { code: "publication.lease_lost" });
    setStage("submission-publication-missing-evidence");
    await assert.rejects(complete(pool, {
        ...stores, evidenceStore: { async verify() { throw new Error("Synthetic missing evidence"); } },
    }), { code: "publication.files_unavailable" });
    setStage("submission-publication-expired-during-verification");
    await assert.rejects(complete(pool, {
        ...stores,
        evidenceStore: {
            async verify() {
                await connection.execute(`UPDATE marketplace_publication_jobs
                    SET lease_expires_at=DATE_SUB(UTC_TIMESTAMP(6),INTERVAL 1 SECOND) WHERE id=?`, [lease.job_id]);
            },
        },
    }), { code: "publication.lease_lost" });
    setStage("submission-publication-revoked-during-verification");
    await assert.rejects(complete(pool, {
        ...stores,
        evidenceStore: {
            async verify() {
                await connection.execute("UPDATE marketplace_signature_keys SET active=FALSE WHERE key_id='publication-fixture'");
            },
        },
    }), { code: "publication.lease_lost" });
    setStage("submission-publication-audit-rollback");
    const failingPool = {
        async getConnection() {
            const transaction = await pool.getConnection();
            return {
                ...transaction,
                execute(sql, parameters) {
                    if (sql.includes("INSERT INTO platform_audit_events")) {
                        throw new Error("Synthetic completion audit failure");
                    }
                    return transaction.execute(sql, parameters);
                },
            };
        },
    };
    await assert.rejects(complete(failingPool), /Synthetic completion audit failure/);
    const [[before]] = await connection.execute(`SELECT state FROM marketplace_publication_jobs WHERE id=?`, [lease.job_id]);
    assert.equal(before.state, "leased");
    const [publicationsBefore] = await connection.execute("SELECT id FROM marketplace_publications");
    assert.equal(publicationsBefore.length, 0);
    setStage("submission-publication-success-retry");
    const published = await complete();
    assert.equal(published.version_id, lease.version_id);
    assert.equal(published.product_id, lease.product_id);
    assert.ok(Number.isFinite(Date.parse(published.published_at)));
    const verificationCount = verifications;
    assert.deepEqual(await complete(), published);
    assert.equal(verifications, verificationCount);
    await assert.rejects(complete(pool, stores, "other-worker"), { code: "publication.lease_lost" });
    await assert.rejects(complete(pool, stores, "completion-fixture", `${signedManifest} `),
        { code: "publication.lease_lost" });
    const [[after]] = await connection.execute(`SELECT state,lease_owner,lease_expires_at
        FROM marketplace_publication_jobs WHERE id=?`, [lease.job_id]);
    assert.equal(after.state, "published");
    assert.equal(after.lease_owner, null);
    assert.equal(after.lease_expires_at, null);
    const [[version]] = await connection.execute("SELECT state FROM marketplace_product_versions WHERE id=?", [lease.version_id]);
    const [[submission]] = await connection.execute("SELECT state FROM marketplace_submissions");
    const [[product]] = await connection.execute("SELECT state FROM marketplace_products WHERE id=?", [lease.product_id]);
    assert.equal(version.state, "published");
    assert.equal(submission.state, "approved");
    assert.equal(product.state, "published");
    const [publications] = await connection.execute("SELECT id FROM marketplace_publications");
    assert.equal(publications.length, 1);
    return 21;
}
