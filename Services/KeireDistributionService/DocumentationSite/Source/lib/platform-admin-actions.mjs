import { randomUUID } from "node:crypto";
import { LocalUploadError } from "./platform-upload-product.mjs";

const uuidPattern = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
export const localAdminOperations = Object.freeze(["application.decide", "report.decide", "staff.set", "feature.set"]);

function reject(status, code, message) {
    throw new LocalUploadError(status, code, message);
}

function text(value, minimum, maximum) {
    return typeof value === "string" && [...value.trim()].length >= minimum && [...value].length <= maximum;
}

function identifier(value) {
    return typeof value === "string" && uuidPattern.test(value);
}

export function validateAdminAction(input) {
    let valid = false;
    switch (input?.operation) {
        case "application.decide":
            valid = identifier(input.applicationId) && ["changes_requested", "approved", "rejected"].includes(input.decision)
                && text(input.decisionNote, 3, 5000)
                && (input.decision !== "approved" || (typeof input.publisherSlug === "string"
                    && /^[a-z0-9][a-z0-9-]{2,62}$/.test(input.publisherSlug)));
            break;
        case "report.decide":
            valid = identifier(input.reportId) && ["triaged", "resolved", "dismissed"].includes(input.state)
                && text(input.resolutionNote ?? "", input.state === "triaged" ? 0 : 3, 5000);
            break;
        case "staff.set":
            valid = identifier(input.targetUserId) && ["moderator", "administrator"].includes(input.role)
                && typeof input.active === "boolean" && text(input.reason, 3, 1000);
            break;
        case "feature.set":
            valid = typeof input.key === "string" && /^[a-z][a-z0-9_]{2,63}$/.test(input.key)
                && typeof input.enabled === "boolean" && text(input.reason, 3, 1000)
                && !(input.key === "paid_checkout_enabled" && input.enabled);
            break;
    }
    if (!valid) {
        reject(400, "staff.operation_invalid", "Check the operation, identifiers, decision and required reason.");
    }
}

async function audit(connection, session, action, targetType, targetId, metadata) {
    await connection.execute(`INSERT INTO platform_audit_events
        (occurred_at,actor_user_id,action,target_type,target_id,correlation_id,metadata)
        VALUES (UTC_TIMESTAMP(6),?,?,?,?,?,?)`,
    [session.userId, action, targetType, targetId, randomUUID(), JSON.stringify(metadata)]);
}

async function decideApplication(connection, session, input) {
    const [[application]] = await connection.execute(`SELECT id,state,organization_id,public_name,statement,website_url
        FROM publisher_applications WHERE id=? FOR UPDATE`, [input.applicationId]);
    if (!application || application.state !== "submitted") {
        reject(409, "publisher.application_not_reviewable", "This application is no longer waiting for review.");
    }
    let publisherId = null;
    if (input.decision === "approved") {
        // The organization lock serializes approval of different applications for the same publisher.
        const [[organization]] = await connection.execute(
            "SELECT id,deleted_at FROM organizations WHERE id=? FOR UPDATE", [application.organization_id]);
        if (!organization || organization.deleted_at !== null) {
            reject(409, "publisher.organization_unavailable", "The organization is no longer available for approval.");
        }
        const [[publisher]] = await connection.execute(
            "SELECT id FROM publishers WHERE organization_id=? FOR UPDATE", [application.organization_id]);
        publisherId = publisher?.id ?? randomUUID();
        if (publisher) {
            await connection.execute(`UPDATE publishers SET display_name=?,summary=?,website_url=?,verified=TRUE,
                suspended_at=NULL,updated_at=UTC_TIMESTAMP(6) WHERE id=?`,
            [application.public_name, [...application.statement].slice(0, 1000).join(""), application.website_url, publisherId]);
        } else {
            await connection.execute(`INSERT INTO publishers
                (id,organization_id,slug,display_name,summary,website_url,verified,created_at,updated_at)
                VALUES (?,?,?,?,?,?,TRUE,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
            [publisherId, application.organization_id, input.publisherSlug, application.public_name,
                [...application.statement].slice(0, 1000).join(""), application.website_url]);
        }
    }
    await connection.execute(`UPDATE publisher_applications SET state=?,reviewed_at=UTC_TIMESTAMP(6),
        reviewed_by=?,decision_note=?,updated_at=UTC_TIMESTAMP(6) WHERE id=?`,
    [input.decision, session.userId, input.decisionNote.trim(), application.id]);
    await audit(connection, session, "publisher.application_decided", "publisher_application", application.id,
        { decision: input.decision, publisherId, organizationId: application.organization_id });
    return { application_id: application.id, application_state: input.decision, publisher_id: publisherId };
}

async function decideReport(connection, session, input) {
    const [[report]] = await connection.execute(
        "SELECT id,state FROM marketplace_reports WHERE id=? FOR UPDATE", [input.reportId]);
    if (!report || !["open", "triaged"].includes(report.state)) {
        reject(409, "staff.report_not_reviewable", "This report has already been closed or is unavailable.");
    }
    await connection.execute(`UPDATE marketplace_reports SET state=?,assigned_to=?,resolution_note=?,
        resolved_at=CASE WHEN ? THEN UTC_TIMESTAMP(6) ELSE NULL END WHERE id=?`,
    [input.state, session.userId, (input.resolutionNote ?? "").trim() || null, input.state !== "triaged", report.id]);
    await audit(connection, session, "marketplace.report_decided", "marketplace_report", report.id, { state: input.state });
    return { report_id: report.id, report_state: input.state };
}

async function setStaff(connection, session, input, members) {
    const [[account]] = await connection.execute(
        "SELECT id FROM platform_accounts WHERE id=? AND deleted_at IS NULL AND (banned_until IS NULL OR banned_until<=UTC_TIMESTAMP(6)) FOR SHARE", [input.targetUserId]);
    if (!account) {
        reject(400, "staff.account_unavailable", "Choose an active account for this staff assignment.");
    }
    const activeAdministrators = members.filter((member) => [true, 1].includes(member.active)
        && member.revoked_at === null && member.role === "administrator");
    if (activeAdministrators.some((member) => member.user_id === input.targetUserId)
        && activeAdministrators.length === 1 && (!input.active || input.role !== "administrator")) {
        reject(409, "staff.last_administrator_required", "Keep at least one active administrator.");
    }
    await connection.execute(`INSERT INTO platform_staff_members
        (user_id,role,active,appointed_by,appointed_at,updated_at,revoked_at)
        VALUES (?,?,?,?,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6),CASE WHEN ? THEN NULL ELSE UTC_TIMESTAMP(6) END)
        ON DUPLICATE KEY UPDATE role=?,active=?,appointed_by=?,updated_at=UTC_TIMESTAMP(6),
            revoked_at=CASE WHEN ? THEN NULL ELSE UTC_TIMESTAMP(6) END`,
    [input.targetUserId, input.role, input.active, session.userId, input.active,
        input.role, input.active, session.userId, input.active]);
    await audit(connection, session, "staff.assignment_changed", "platform_staff_member", input.targetUserId,
        { role: input.role, active: input.active, reason: input.reason.trim() });
    return { user_id: input.targetUserId, role: input.role, active: input.active };
}

async function setFeature(connection, session, input) {
    const [[flag]] = await connection.execute(
        "SELECT `key` FROM platform_feature_flags WHERE `key`=? FOR UPDATE", [input.key]);
    if (!flag) {
        reject(400, "staff.feature_unavailable", "Choose an existing feature gate.");
    }
    await connection.execute(`UPDATE platform_feature_flags SET enabled=?,updated_at=UTC_TIMESTAMP(6),updated_by=?
        WHERE \`key\`=?`, [input.enabled, session.userId, input.key]);
    await audit(connection, session, "platform.feature_flag_changed", "platform_feature_flag", input.key,
        { enabled: input.enabled, reason: input.reason.trim() });
    return { flag_key: input.key, enabled: input.enabled };
}

export async function applyLocalAdminAction(authentication, token, input) {
    validateAdminAction(input);
    try {
        const result = await authentication.withSessionTransaction(token, async (connection, session) => {
            if (session.assurance !== 2) {
                reject(403, "account.mfa_required", "Verify your authenticator before making staff changes.");
            }
            // All membership mutations lock the complete ordered set before checking the last administrator.
            // This prevents two concurrent administrators from both removing the final remaining access.
            const [members] = input.operation === "staff.set"
                ? await connection.execute("SELECT user_id,role,active,revoked_at FROM platform_staff_members ORDER BY user_id FOR UPDATE")
                : await connection.execute("SELECT user_id,role,active,revoked_at FROM platform_staff_members WHERE user_id=? FOR SHARE", [session.userId]);
            const staff = members.find((member) => member.user_id === session.userId);
            if (!staff || ![true, 1].includes(staff.active) || staff.revoked_at !== null
                || !["moderator", "administrator"].includes(staff.role)) {
                reject(403, "staff.authorization_required", "An active staff role is required.");
            }
            if (["staff.set", "feature.set"].includes(input.operation) && staff.role !== "administrator") {
                reject(403, "staff.administrator_required", "An active administrator must make this change.");
            }
            switch (input.operation) {
                case "application.decide": return decideApplication(connection, session, input);
                case "report.decide": return decideReport(connection, session, input);
                case "staff.set": return setStaff(connection, session, input, members);
                case "feature.set": return setFeature(connection, session, input);
            }
        });
        if (!result) {
            reject(401, "account.authentication_required", "Sign in again before making staff changes.");
        }
        return result;
    } catch (error) {
        if (error?.code === "ER_DUP_ENTRY") {
            reject(409, "staff.record_conflict", "That identifier is already in use. Refresh and choose another value.");
        }
        throw error;
    }
}
