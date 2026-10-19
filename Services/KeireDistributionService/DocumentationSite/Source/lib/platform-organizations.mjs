import { randomUUID } from "node:crypto";

export class LocalOrganizationError extends Error {
    constructor(status, code, message) {
        super(message);
        this.status = status;
        this.code = code;
    }
}

function reject(status, code, message) {
    throw new LocalOrganizationError(status, code, message);
}

/** Authorization and all ownership writes share the locked session transaction. */
export async function createLocalOrganization(authentication, sessionToken, input) {
    const slug = typeof input?.slug === "string" ? input.slug.trim().toLowerCase() : "";
    const displayName = typeof input?.displayName === "string" ? input.displayName.trim() : "";
    if (!/^[a-z0-9][a-z0-9-]{2,62}$/.test(slug) || !displayName || displayName.length > 96) {
        reject(400, "organization.invalid_input", "Enter a name and a URL slug using 3–63 lowercase letters, numbers, or hyphens.");
    }
    const result = await authentication.withSessionTransaction(sessionToken, async (connection, session) => {
        if (session.assurance !== 2) {
            reject(403, "account.mfa_required", "Verify an authenticator before creating an organization.");
        }
        const [flags] = await connection.execute(`SELECT enabled FROM platform_feature_flags
            WHERE \`key\` IN ('marketplace_enabled','publisher_portal_enabled') FOR SHARE`);
        if (!flags.some((flag) => Boolean(flag.enabled))) {
            reject(503, "organization.unavailable", "Organization creation is temporarily unavailable.");
        }
        const organizationId = randomUUID();
        try {
            await connection.execute(`INSERT INTO organizations
                (id,slug,display_name,created_by,created_at,updated_at)
                VALUES (?,?,?,?,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`,
            [organizationId, slug, displayName, session.userId]);
        } catch (error) {
            if (error?.code === "ER_DUP_ENTRY") {
                reject(409, "organization.slug_unavailable", "That organization URL is already in use. Choose another slug.");
            }
            throw error;
        }
        await connection.execute(`INSERT INTO organization_memberships
            (organization_id,user_id,role,invited_by,created_at)
            VALUES (?,?,'owner',?,UTC_TIMESTAMP(6))`, [organizationId, session.userId, session.userId]);
        await connection.execute(`INSERT INTO platform_audit_events
            (occurred_at,actor_user_id,actor_session_id,action,target_type,target_id,correlation_id,metadata)
            VALUES (UTC_TIMESTAMP(6),?,NULL,'organization.created','organization',?,?,'{}')`,
        [session.userId, organizationId, randomUUID()]);
        return { organizationId, slug, displayName };
    });
    if (!result) {
        reject(401, "account.authentication_required", "Sign in again to create an organization.");
    }
    return result;
}
