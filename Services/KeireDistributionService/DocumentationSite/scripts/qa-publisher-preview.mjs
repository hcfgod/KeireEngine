import { randomUUID } from "node:crypto";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";

export async function createPublisherPreview(pool, userId) {
    const organizationId = randomUUID();
    const publisherId = randomUUID();
    const directory = await mkdtemp(path.join(tmpdir(), "keire-publisher-preview-"));
    const packagePath = path.join(directory, "synthetic-upload-only.keireassetpackage");
    async function close() {
        await platformTransaction(pool, async (connection) => {
            await connection.execute(`DELETE audit FROM platform_audit_events audit
                JOIN marketplace_uploads upload ON upload.id=audit.target_id
                JOIN marketplace_product_versions version ON version.id=upload.version_id
                JOIN marketplace_products product ON product.id=version.product_id
                WHERE product.publisher_id=? AND audit.actor_user_id=?`, [publisherId, userId]);
            await connection.execute(`DELETE audit FROM platform_audit_events audit
                JOIN marketplace_products product ON product.id=audit.target_id
                WHERE product.publisher_id=? AND audit.actor_user_id=?`, [publisherId, userId]);
            await connection.execute(`DELETE version FROM marketplace_product_versions version
                JOIN marketplace_products product ON product.id=version.product_id WHERE product.publisher_id=?`, [publisherId]);
            await connection.execute("DELETE FROM marketplace_products WHERE publisher_id=?", [publisherId]);
            await connection.execute("DELETE FROM publishers WHERE id=?", [publisherId]);
            await connection.execute("DELETE FROM organizations WHERE id=?", [organizationId]);
        });
        if (path.dirname(path.resolve(directory)) !== path.resolve(tmpdir())
            || !path.basename(directory).startsWith("keire-publisher-preview-")) {
            throw new Error("Refusing cleanup outside the publisher preview fixture.");
        }
        await rm(directory, { recursive: true, force: true });
    }
    try {
        await platformTransaction(pool, async (connection) => {
            await connection.execute(`INSERT INTO organizations
                (id,slug,display_name,created_by,created_at,updated_at)
                VALUES (?,?,'Browser upload QA',?,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
            [organizationId, `qa-${organizationId}`, userId]);
            await connection.execute(`INSERT INTO organization_memberships
                (organization_id,user_id,role,invited_by,created_at) VALUES (?,?,'owner',?,UTC_TIMESTAMP(6))`,
            [organizationId, userId, userId]);
            await connection.execute(`INSERT INTO publishers
                (id,organization_id,slug,display_name,summary,verified,created_at,updated_at)
                VALUES (?,?,?,'Browser upload QA','Disposable upload acceptance fixture',FALSE,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
            [publisherId, organizationId, `qa-${publisherId}`]);
        });
        await writeFile(packagePath, Buffer.alloc(8 * 1024 * 1024, 0x51), { flag: "wx", mode: 0o600 });
        return { packagePath, close };
    } catch (error) {
        await close();
        throw error;
    }
}
