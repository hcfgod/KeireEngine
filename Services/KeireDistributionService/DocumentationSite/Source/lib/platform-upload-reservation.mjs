import { randomUUID } from "node:crypto";
import { localUploadInput } from "./platform-upload-input.mjs";
import { LocalUploadError, selectUploadProduct } from "./platform-upload-product.mjs";

export async function reserveLocalUpload(authentication, sessionToken, rawInput) {
    const input = localUploadInput(rawInput);
    const result = await authentication.withSessionTransaction(sessionToken, async (connection, session) => {
        const productId = await selectUploadProduct(connection, session, input);
        const [versions] = await connection.execute(
            "SELECT id,state FROM marketplace_product_versions WHERE product_id=? AND version=? FOR UPDATE",
            [productId, input.version]);
        const version = versions[0];
        if (version && !["draft", "validation_failed", "changes_requested"].includes(version.state)) {
            throw new LocalUploadError(409, "publisher.version_not_editable", "This release can no longer receive a replacement upload.");
        }
        const versionId = version?.id ?? randomUUID();
        const metadata = [input.installKind, input.minimumEngineVersion, input.maximumEngineVersion,
            JSON.stringify(input.platforms), JSON.stringify(input.architectures), JSON.stringify(input.rendererCapabilities),
            input.managedApiVersion, input.releaseNotes];
        if (version) {
            await connection.execute(`UPDATE marketplace_uploads SET state='expired',lease_owner=NULL,lease_expires_at=NULL
                WHERE version_id=? AND state IN ('created','uploading') AND expires_at<=UTC_TIMESTAMP(6)`, [versionId]);
            const [active] = await connection.execute(`SELECT id FROM marketplace_uploads WHERE version_id=?
                AND state IN ('created','uploading','uploaded','leased') FOR UPDATE`, [versionId]);
            if (active.length) {
                throw new LocalUploadError(409, "publisher.upload_already_active", "This release already has an active upload.");
            }
            await connection.execute(`UPDATE marketplace_product_versions SET state='draft',install_kind=?,
                minimum_engine_version=?,maximum_engine_version=?,platforms=?,architectures=?,renderer_capabilities=?,
                managed_api_version=?,release_notes_markdown=?,archive_storage_path=NULL,archive_storage_bucket=NULL,
                archive_sha256=NULL,archive_size_bytes=NULL,manifest_sha256=NULL,signature_key_id=NULL,
                validator_version=NULL,executable_code_fingerprint=NULL,updated_at=UTC_TIMESTAMP(6) WHERE id=?`,
            [...metadata, versionId]);
        } else {
            await connection.execute(`INSERT INTO marketplace_product_versions
                (install_kind,minimum_engine_version,maximum_engine_version,platforms,architectures,renderer_capabilities,
                 managed_api_version,release_notes_markdown,id,product_id,version,state,created_at,updated_at)
                VALUES (?,?,?,?,?,?,?,?,?,?,?,'draft',UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
            [...metadata, versionId, productId, input.version]);
        }
        const uploadId = randomUUID();
        const storagePath = `${productId}/${versionId}/${input.expectedSha256}.keireassetpackage`;
        const [previous] = await connection.execute(
            "SELECT id FROM marketplace_uploads WHERE storage_path_digest=UNHEX(SHA2(?,256)) FOR UPDATE", [storagePath]);
        if (previous.length) {
            throw new LocalUploadError(409, "publisher.upload_content_already_reserved",
                "These exact release bytes already have an upload record. Check its status before uploading again.");
        }
        await connection.execute(`INSERT INTO marketplace_uploads
            (id,version_id,created_by,storage_bucket,storage_path,expected_size_bytes,expected_sha256,
             state,created_at,expires_at,validation_attempts)
            VALUES (?,?,?,'marketplace-packages',?,?,?,'created',UTC_TIMESTAMP(6),
                DATE_ADD(UTC_TIMESTAMP(6),INTERVAL 2 HOUR),0)`,
        [uploadId, versionId, session.userId, storagePath, input.expectedSizeBytes, input.expectedSha256]);
        await connection.execute(`INSERT INTO platform_audit_events
            (occurred_at,actor_user_id,actor_session_id,action,target_type,target_id,correlation_id,metadata)
            VALUES (UTC_TIMESTAMP(6),?,NULL,'marketplace.upload_reserved','marketplace_upload',?,?,?)`,
        [session.userId, uploadId, randomUUID(), JSON.stringify({ productId, versionId,
            storageBucket: "marketplace-packages", expectedSizeBytes: input.expectedSizeBytes,
            expectedSha256: input.expectedSha256 })]);
        const [uploads] = await connection.execute("SELECT expires_at FROM marketplace_uploads WHERE id=?", [uploadId]);
        return { uploadId, versionId, productId, bucket: "marketplace-packages", storagePath,
            expiresAt: uploads[0].expires_at.replace(" ", "T") + "Z" };
    });
    if (!result) {
        throw new LocalUploadError(401, "account.authentication_required", "Sign in again to upload a package.");
    }
    return result;
}
