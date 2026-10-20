import { randomUUID } from "node:crypto";
import { localUploadProductSlugs } from "./platform-upload-input.mjs";

export class LocalUploadError extends Error {
    static [Symbol.hasInstance](value) {
        return value?.[Symbol.for("Keire.LocalUploadError")] === true;
    }

    constructor(status, code, message) {
        super(message);
        // The application host and bundled routes may load distinct copies of this module.
        Object.defineProperty(this, Symbol.for("Keire.LocalUploadError"), { value: true });
        this.status = status;
        this.code = code;
    }
}

function reject(code, message, status = 409) {
    throw new LocalUploadError(status, code, message);
}

/** Call only within the authenticated reservation transaction with validated metadata. */
export async function selectUploadProduct(connection, session, input) {
    if (session.assurance !== 2) {
        reject("account.mfa_required", "Verify an authenticator before uploading.", 403);
    }
    const [flags] = await connection.execute(
        "SELECT enabled FROM platform_feature_flags WHERE `key`='publisher_portal_enabled' FOR SHARE");
    if (!flags.some((flag) => Boolean(flag.enabled))) {
        reject("publisher.disabled", "Publisher uploads are not enabled.", 503);
    }
    // The publisher lock serializes slug creation and release reservations across all its members.
    const [publishers] = await connection.execute(
        "SELECT organization_id FROM publishers WHERE id=? AND suspended_at IS NULL FOR UPDATE", [input.publisherId]);
    if (publishers.length !== 1) {
        reject("publisher.not_manageable", "This publisher is not available to your account.", 403);
    }
    const organizationId = publishers[0].organization_id;
    const [organizations] = await connection.execute(
        "SELECT id FROM organizations WHERE id=? AND deleted_at IS NULL FOR SHARE", [organizationId]);
    const [members] = await connection.execute(
        "SELECT role FROM organization_memberships WHERE organization_id=? AND user_id=? FOR SHARE",
        [organizationId, session.userId]);
    if (organizations.length !== 1 || !members.some((member) => ["owner", "admin"].includes(member.role))) {
        reject("publisher.not_manageable", "This publisher is not available to your account.", 403);
    }
    const [categories] = await connection.execute(
        "SELECT id FROM marketplace_categories WHERE id=? AND active=TRUE FOR SHARE", [input.categoryId]);
    const [licenses] = await connection.execute(
        "SELECT license_id FROM marketplace_license_revisions WHERE license_id=? AND revision=? FOR SHARE",
        [input.licenseSpdx, input.licenseRevision]);
    if (categories.length !== 1 || licenses.length !== 1) {
        reject("publisher.product_metadata_invalid", "Choose an active category and an approved license revision.", 400);
    }
    const columns = "id,state,display_name,category_id,short_description,license_spdx,license_revision";
    let product;
    let slug;
    if (input.productId) {
        const [rows] = await connection.execute(
            `SELECT ${columns} FROM marketplace_products WHERE id=? AND publisher_id=? FOR UPDATE`,
            [input.productId, input.publisherId]);
        product = rows[0];
        if (!product) {
            reject("publisher.product_not_editable", "This product cannot be edited by this publisher.", 403);
        }
    } else {
        for (const candidate of localUploadProductSlugs(input.productName)) {
            slug = candidate;
            const [rows] = await connection.execute(
                `SELECT ${columns} FROM marketplace_products WHERE publisher_id=? AND slug=? FOR UPDATE`,
                [input.publisherId, slug]);
            product = rows[0];
            if (!product || product.display_name.toLowerCase() === input.productName.toLowerCase()) {
                break;
            }
        }
        if (product && product.display_name.toLowerCase() !== input.productName.toLowerCase()) {
            reject("publisher.product_name_conflict", "Another product uses this name's URL. Choose a different name.");
        }
    }
    if (!product) {
        const id = randomUUID();
        await connection.execute(`INSERT INTO marketplace_products
            (id,publisher_id,category_id,slug,display_name,short_description,description_markdown,state,
             license_spdx,license_revision,featured,created_at,updated_at)
            VALUES (?,?,?,?,?,?,'','draft',?,?,FALSE,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
        [id, input.publisherId, input.categoryId, slug, input.productName, input.productSummary,
            input.licenseSpdx, input.licenseRevision]);
        await connection.execute(`INSERT INTO marketplace_offers
            (id,product_id,currency,amount_minor,active,created_at) VALUES (?,?,'USD',0,TRUE,UTC_TIMESTAMP(6))`,
        [randomUUID(), id]);
        await connection.execute(`INSERT INTO platform_audit_events
            (occurred_at,actor_user_id,actor_session_id,action,target_type,target_id,correlation_id,metadata)
            VALUES (UTC_TIMESTAMP(6),?,NULL,'marketplace.product_created','marketplace_product',?,?,?)`,
        [session.userId, id, randomUUID(), JSON.stringify({ publisherId: input.publisherId,
            categoryId: input.categoryId, displayName: input.productName, slug })]);
        return id;
    }
    if (!["draft", "changes_requested", "published"].includes(product.state)) {
        reject("publisher.product_not_editable", "This product is not in an editable state.", 403);
    }
    if (product.state === "published") {
        if (product.display_name.toLowerCase() !== input.productName.toLowerCase() ||
            product.category_id !== input.categoryId || product.short_description !== input.productSummary ||
            product.license_spdx !== input.licenseSpdx || product.license_revision !== input.licenseRevision) {
            reject("publisher.published_product_metadata_immutable",
                "A new release must preserve the published product's name, category, summary and license.", 400);
        }
        return product.id;
    }
    await connection.execute(`UPDATE marketplace_products SET category_id=?,display_name=?,short_description=?,
        license_spdx=?,license_revision=?,updated_at=UTC_TIMESTAMP(6) WHERE id=?`,
    [input.categoryId, input.productName, input.productSummary, input.licenseSpdx, input.licenseRevision, product.id]);
    return product.id;
}
