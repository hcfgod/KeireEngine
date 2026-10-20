import { LocalPublisherApplicationError } from "./platform-publisher-applications.mjs";

// Every private resource query rechecks the organization boundary; child IDs are never trusted from a request.
const publisherAccess = `JOIN organizations organization ON organization.id=publisher.organization_id
    JOIN organization_memberships member ON member.organization_id=organization.id
    WHERE member.user_id=? AND member.role IN ('owner','admin')
    AND organization.deleted_at IS NULL AND publisher.suspended_at IS NULL`;

function timestamps(rows, fields) {
    return rows.map((row) => {
        const value = { ...row };
        for (const field of fields) {
            if (typeof value[field] === "string") {
                value[field] = value[field].replace(" ", "T") + "Z";
            }
        }
        return value;
    });
}

/** Reads run inside the authenticated session transaction; no anonymous or email-only private data. */
export async function localPublisherPortal(authentication, sessionToken) {
    const result = await authentication.withSessionTransaction(sessionToken, async (connection, session) => {
        if (session.assurance !== 2) {
            throw new LocalPublisherApplicationError(403, "account.mfa_required",
                "Verify an authenticator to open the publisher portal.");
        }
        const [flags] = await connection.execute(
            "SELECT enabled FROM platform_feature_flags WHERE `key`='publisher_portal_enabled'");
        const data = { enabled: flags.some((flag) => Boolean(flag.enabled)), applications: [], organizations: [],
            publishers: [], products: [], categories: [], licenses: [], versions: [], uploads: [], validationReports: [] };
        if (!data.enabled) {
            return data;
        }
        const [applications] = await connection.execute(`SELECT id,organization_id,public_name,website_url,portfolio_url,
            statement,state,submitted_at,decision_note,updated_at
            FROM publisher_applications WHERE applicant_user_id=? ORDER BY updated_at DESC,id LIMIT 100`, [session.userId]);
        data.applications = timestamps(applications, ["submitted_at", "updated_at"]);
        const [organizations] = await connection.execute(`SELECT member.role,organization.id,organization.display_name,organization.slug
            FROM organization_memberships member JOIN organizations organization ON organization.id=member.organization_id
            WHERE member.user_id=? AND member.role IN ('owner','admin') AND organization.deleted_at IS NULL
            ORDER BY organization.display_name,organization.id`, [session.userId]);
        data.organizations = organizations.map(({ role, ...organization }) => ({ role, organizations: organization }));
        const [publishers] = await connection.execute(`SELECT publisher.id,publisher.organization_id,
            publisher.display_name,publisher.slug,publisher.verified,publisher.suspended_at
            FROM publishers publisher ${publisherAccess} ORDER BY publisher.id`, [session.userId]);
        data.publishers = publishers.map((publisher) => ({ ...publisher, verified: Boolean(publisher.verified) }));
        const [products] = await connection.execute(`SELECT product.id,product.publisher_id,product.category_id,
            product.slug,product.display_name,product.short_description,product.state,product.license_spdx,
            product.license_revision,product.updated_at FROM marketplace_products product
            JOIN publishers publisher ON publisher.id=product.publisher_id ${publisherAccess}
            ORDER BY product.display_name,product.id`, [session.userId]);
        data.products = timestamps(products, ["updated_at"]);
        [data.categories] = await connection.execute(`SELECT id,slug,display_name,description,sort_order
            FROM marketplace_categories WHERE active=TRUE ORDER BY sort_order,id`);
        [data.licenses] = await connection.execute(`SELECT license_id,revision,display_name,license_kind
            FROM marketplace_license_revisions ORDER BY display_name,license_id,revision`);
        const [versions] = await connection.execute(`SELECT version.id,version.product_id,version.version,version.state,
            version.install_kind,version.minimum_engine_version,version.maximum_engine_version,version.platforms,
            version.architectures,version.renderer_capabilities,version.managed_api_version,version.updated_at
            FROM marketplace_product_versions version JOIN marketplace_products product ON product.id=version.product_id
            JOIN publishers publisher ON publisher.id=product.publisher_id ${publisherAccess}
            ORDER BY version.updated_at DESC,version.id LIMIT 100`, [session.userId]);
        data.versions = timestamps(versions, ["updated_at"]);
        const [uploads] = await connection.execute(`SELECT upload.id,upload.version_id,upload.state,
            upload.expected_size_bytes,upload.expected_sha256,upload.uploaded_at,upload.expires_at,upload.created_at
            FROM marketplace_uploads upload JOIN marketplace_product_versions version ON version.id=upload.version_id
            JOIN marketplace_products product ON product.id=version.product_id
            JOIN publishers publisher ON publisher.id=product.publisher_id ${publisherAccess}
            ORDER BY upload.created_at DESC,upload.id LIMIT 100`, [session.userId]);
        data.uploads = timestamps(uploads, ["uploaded_at", "expires_at", "created_at"]);
        const [reports] = await connection.execute(`SELECT report.id,report.upload_id,report.passed,report.diagnostics,
            report.malware_scan_result,report.secret_scan_result,report.managed_validation_result,
            report.validator_version,report.completed_at FROM marketplace_validation_reports report
            JOIN marketplace_uploads upload ON upload.id=report.upload_id
            JOIN marketplace_product_versions version ON version.id=upload.version_id
            JOIN marketplace_products product ON product.id=version.product_id
            JOIN publishers publisher ON publisher.id=product.publisher_id ${publisherAccess}
            ORDER BY report.completed_at DESC,report.id LIMIT 100`, [session.userId]);
        data.validationReports = timestamps(reports, ["completed_at"])
            .map((report) => ({ ...report, passed: Boolean(report.passed) }));
        return data;
    });
    if (!result) {
        throw new LocalPublisherApplicationError(401, "account.authentication_required",
            "Sign in again to open the publisher portal.");
    }
    return result;
}
