import { platformTransaction } from "./platform-mysql.mjs";

/** Operator-only recovery. Original metadata is authoritative; never replace a publication's identity. */
export async function recoverStorageObject(pool, stores, objectId, chunks) {
    if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(objectId ?? "")) {
        throw new Error("Invalid recovery object identifier.");
    }
    return platformTransaction(pool, async (connection) => {
        const [[object]] = await connection.execute(
            "SELECT bucket_id,size_bytes,expected_sha256 FROM platform_storage_objects WHERE id=? FOR UPDATE",
            [objectId]);
        const store = object && stores.get(object.bucket_id);
        const sizeBytes = Number(object?.size_bytes);
        if (!store || !/^[0-9a-f]{64}$/.test(object.expected_sha256 ?? "") ||
            !Number.isSafeInteger(sizeBytes) || sizeBytes < 0) {
            throw new Error("Recovery requires original integrity metadata and an explicitly mapped private store.");
        }
        const identity = { sha256: object.expected_sha256, sizeBytes };
        // put rejects mismatched bytes and never replaces an existing immutable object.
        await store.put(identity, chunks);
        await store.verify(identity);
        await connection.execute(`UPDATE platform_storage_objects
            SET recovered_sha256=expected_sha256,recovered_at=COALESCE(recovered_at,UTC_TIMESTAMP(6))
            WHERE id=?`, [objectId]);
        return { objectId, sha256: identity.sha256, sizeBytes };
    });
}
