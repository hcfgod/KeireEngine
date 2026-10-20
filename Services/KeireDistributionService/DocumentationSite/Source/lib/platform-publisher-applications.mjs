import { randomUUID } from "node:crypto";

export class LocalPublisherApplicationError extends Error {
    constructor(status, code, message) {
        super(message);
        this.status = status;
        this.code = code;
    }
}

function reject(status, code, message) {
    throw new LocalPublisherApplicationError(status, code, message);
}

const uuidPattern = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

function optionalHttpsUrl(value) {
    if (value == null || value === "") {
        return null;
    }
    try {
        if (typeof value !== "string" || value.length > 2048) {
            throw new Error("Invalid URL");
        }
        const url = new URL(value.trim());
        if (url.protocol !== "https:" || url.username || url.password || url.href.length > 2048) {
            throw new Error("Invalid URL");
        }
        return url.href;
    } catch {
        reject(400, "publisher.url_invalid", "Website and portfolio links must use HTTPS without embedded credentials.");
    }
}

function applicationInput(input) {
    const applicationId = input?.applicationId == null || input.applicationId === "" ? null : input.applicationId;
    const organizationId = input?.organizationId;
    const publicName = typeof input?.publicName === "string" ? input.publicName.trim() : "";
    const statement = typeof input?.statement === "string" ? input.statement.trim() : "";
    if ((applicationId !== null && (typeof applicationId !== "string" || !uuidPattern.test(applicationId))) ||
        typeof organizationId !== "string" || !uuidPattern.test(organizationId) ||
        publicName.length < 2 || publicName.length > 96 || statement.length < 20 || statement.length > 5000 ||
        (input.action !== undefined && !["save", "submit"].includes(input.action))) {
        reject(400, "publisher.application_invalid", "Choose an organization, a 2–96 character name, and a 20–5000 character statement.");
    }
    return { applicationId, organizationId, publicName, statement,
        websiteUrl: optionalHttpsUrl(input.websiteUrl), portfolioUrl: optionalHttpsUrl(input.portfolioUrl),
        state: input.action === "submit" ? "submitted" : "draft" };
}

/** Locks the current session, application and membership until content, state and audit commit together. */
export async function saveLocalPublisherApplication(authentication, sessionToken, input) {
    const values = applicationInput(input);
    const result = await authentication.withSessionTransaction(sessionToken, async (connection, session) => {
        if (session.assurance !== 2) {
            reject(403, "account.mfa_required", "Verify an authenticator before editing a publisher application.");
        }
        const [flags] = await connection.execute(
            "SELECT enabled FROM platform_feature_flags WHERE `key`='publisher_portal_enabled' FOR SHARE");
        if (!flags.some((flag) => Boolean(flag.enabled))) {
            reject(503, "publisher.disabled", "Publisher applications are not enabled.");
        }
        if (values.applicationId) {
            const [applications] = await connection.execute(
                "SELECT applicant_user_id,state FROM publisher_applications WHERE id=? FOR UPDATE",
                [values.applicationId]);
            if (applications.length !== 1 || applications[0].applicant_user_id !== session.userId ||
                !["draft", "changes_requested"].includes(applications[0].state)) {
                reject(409, "publisher.application_not_editable", "This publisher application cannot be edited.");
            }
        }
        const [organizations] = await connection.execute(
            "SELECT id FROM organizations WHERE id=? AND deleted_at IS NULL FOR SHARE", [values.organizationId]);
        const [memberships] = await connection.execute(
            "SELECT role FROM organization_memberships WHERE organization_id=? AND user_id=? FOR SHARE",
            [values.organizationId, session.userId]);
        if (organizations.length !== 1 || !memberships.some((member) => ["owner", "admin"].includes(member.role))) {
            reject(403, "organization.authorization_required", "An organization owner or administrator must save this application.");
        }
        const applicationId = values.applicationId ?? randomUUID();
        const parameters = [values.organizationId, values.publicName, values.websiteUrl,
            values.portfolioUrl, values.statement, values.state, values.state === "submitted"];
        if (values.applicationId) {
            await connection.execute(`UPDATE publisher_applications SET organization_id=?,public_name=?,
                website_url=?,portfolio_url=?,statement=?,state=?,
                submitted_at=IF(?,UTC_TIMESTAMP(6),submitted_at),
                reviewed_at=IF(state='submitted',NULL,reviewed_at),
                reviewed_by=IF(state='submitted',NULL,reviewed_by),
                decision_note=IF(state='submitted',NULL,decision_note),updated_at=UTC_TIMESTAMP(6)
                WHERE id=?`, [...parameters, applicationId]);
        } else {
            await connection.execute(`INSERT INTO publisher_applications
                (organization_id,public_name,website_url,portfolio_url,statement,state,submitted_at,
                 id,applicant_user_id,created_at,updated_at)
                VALUES (?,?,?,?,?,?,IF(?,UTC_TIMESTAMP(6),NULL),?,?,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
            [...parameters, applicationId, session.userId]);
        }
        const action = values.state === "submitted" ? "publisher.application_submitted" : "publisher.application_saved";
        await connection.execute(`INSERT INTO platform_audit_events
            (occurred_at,actor_user_id,actor_session_id,action,target_type,target_id,correlation_id,metadata)
            VALUES (UTC_TIMESTAMP(6),?,NULL,?,'publisher_application',?,?,'{}')`,
        [session.userId, action, applicationId, randomUUID()]);
        return { applicationId, state: values.state };
    });
    if (!result) {
        reject(401, "account.authentication_required", "Sign in again to edit your publisher application.");
    }
    return result;
}
