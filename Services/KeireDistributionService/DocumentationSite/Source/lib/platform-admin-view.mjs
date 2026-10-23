import { localAdminOperations } from "./platform-admin-actions.mjs";
import { LocalUploadError } from "./platform-upload-product.mjs";

const queueLimit = 100;
const submissionFields = "id,version_id,validation_report_id,submitted_by,state,assigned_to,decision_note,submitted_at,decided_at";
const versionFields = "id,product_id,version,state,install_kind,platforms,architectures,archive_sha256,archive_size_bytes,manifest_sha256,validator_version,updated_at";

function rowsForDisplay(rows) {
    return rows.map((row) => Object.fromEntries(Object.entries(row).map(([name, value]) => {
        if (value instanceof Date) {
            return [name, value.toISOString()];
        }
        if (typeof value === "string" && /_at$/.test(name) && /^\d{4}-\d{2}-\d{2} /.test(value)) {
            return [name, value.replace(" ", "T") + "Z"];
        }
        return [name, value];
    })));
}

async function relatedRows(connection, table, fields, column, values, suffix = "") {
    const identifiers = [...new Set(values.filter((value) => typeof value === "string" && value.length > 0))];
    if (!identifiers.length) {
        return [];
    }
    // SQL identifiers are internal constants. Only bound record identifiers originate in data.
    const [rows] = await connection.execute(`SELECT ${fields} FROM ${table}
        WHERE ${column} IN (${identifiers.map(() => "?").join(",")}) ${suffix}`, identifiers);
    return rowsForDisplay(rows);
}

/** Private reads share the session transaction and lock the current staff membership before accessing queues. */
export async function localAdminOverview(authentication, sessionToken) {
    const result = await authentication.withSessionTransaction(sessionToken, async (connection, session) => {
        if (session.assurance !== 2) {
            throw new LocalUploadError(403, "account.mfa_required", "Verify your authenticator to open staff operations.");
        }
        const [[staffIdentity]] = await connection.execute(
            "SELECT role,active,revoked_at FROM platform_staff_members WHERE user_id=? FOR SHARE", [session.userId]);
        if (!staffIdentity || ![true, 1].includes(staffIdentity.active) || staffIdentity.revoked_at !== null
            || !["moderator", "administrator"].includes(staffIdentity.role)) {
            throw new LocalUploadError(404, "staff.not_found", "This page is not available.");
        }
        const view = { role: staffIdentity.role, queueLimit, limitedQueues: [],
            availableOperations: ["submission.decide", ...localAdminOperations], evidenceAvailable: false };
        const queries = [
            ["applications", `SELECT id,applicant_user_id,organization_id,public_name,website_url,portfolio_url,statement,state,submitted_at,updated_at
                FROM publisher_applications WHERE state='submitted' ORDER BY submitted_at,id LIMIT 101`],
            ["activeSubmissions", `SELECT ${submissionFields} FROM marketplace_submissions
                WHERE state IN ('submitted','in_review') ORDER BY submitted_at,id LIMIT 101`],
            ["signingSubmissions", `SELECT ${submissionFields} FROM marketplace_submissions
                WHERE state='approved_pending_signature' ORDER BY decided_at,id LIMIT 101`],
            ["submissionHistory", `SELECT ${submissionFields} FROM marketplace_submissions
                WHERE state IN ('changes_requested','approved','rejected','withdrawn') ORDER BY decided_at DESC,id LIMIT 20`],
            ["reports", `SELECT id,reporter_user_id,product_id,review_id,reason,details,state,assigned_to,resolution_note,created_at
                FROM marketplace_reports WHERE state IN ('open','triaged') ORDER BY created_at,id LIMIT 101`],
            ["staff", `SELECT user_id,role,active,appointed_by,appointed_at,updated_at,revoked_at
                FROM platform_staff_members ORDER BY appointed_at,user_id LIMIT 101`],
            ["flags", "SELECT `key`,enabled,description,updated_at,updated_by FROM platform_feature_flags ORDER BY `key` LIMIT 101"],
            ["audits", `SELECT id,actor_user_id,action,target_type,target_id,occurred_at,metadata
                FROM platform_audit_events ORDER BY occurred_at DESC,id DESC LIMIT 30`],
            ["officialProducts", `SELECT product.id,product.slug,product.display_name,product.state,product.updated_at
                FROM marketplace_products product JOIN publishers publisher ON publisher.id=product.publisher_id
                WHERE publisher.slug='keire-engine' AND publisher.verified=TRUE ORDER BY product.display_name,product.id LIMIT 101`],
        ];
        for (const [name, query] of queries) {
            const [rows] = await connection.execute(query);
            if (rows.length > queueLimit) {
                view.limitedQueues.push(name);
            }
            view[name] = rowsForDisplay(rows.slice(0, queueLimit));
        }
        const submissions = [...view.activeSubmissions, ...view.signingSubmissions, ...view.submissionHistory];
        const versionIds = submissions.map((submission) => submission.version_id);
        view.versions = await relatedRows(connection, "marketplace_product_versions", versionFields, "id", versionIds);
        view.publicationJobs = await relatedRows(connection, "marketplace_publication_jobs",
            "id,version_id,state,attempt_count,last_error_code,created_at,updated_at,published_at", "version_id", versionIds);
        view.products = await relatedRows(connection, "marketplace_products",
            "id,publisher_id,slug,display_name,state,license_spdx,license_revision,updated_at", "id",
            view.versions.map((version) => version.product_id));
        view.validationReports = await relatedRows(connection, "marketplace_validation_reports",
            "id,upload_id,passed,diagnostics,malware_scan_result,secret_scan_result,managed_validation_result,validator_version,validator_fingerprint_sha256,policy_version,package_sha256,manifest_sha256,evidence_sha256,evidence_size_bytes,attestation_key_id,completed_at",
            "id", submissions.map((submission) => submission.validation_report_id));
        // Only the latest official version is shown; do not load an unbounded release history.
        const [officialVersions] = await connection.execute(`SELECT id,product_id,version,state,updated_at FROM (
            SELECT version.id,version.product_id,version.version,version.state,version.updated_at,
                ROW_NUMBER() OVER (PARTITION BY version.product_id ORDER BY version.updated_at DESC,version.id) AS release_rank
            FROM marketplace_product_versions version JOIN marketplace_products product ON product.id=version.product_id
            JOIN publishers publisher ON publisher.id=product.publisher_id
            WHERE publisher.slug='keire-engine' AND publisher.verified=TRUE
        ) latest WHERE release_rank=1 ORDER BY updated_at DESC,id LIMIT 100`);
        view.officialVersions = rowsForDisplay(officialVersions);
        return view;
    });
    if (!result) {
        throw new LocalUploadError(401, "account.authentication_required", "Sign in to open staff operations.");
    }
    return result;
}
