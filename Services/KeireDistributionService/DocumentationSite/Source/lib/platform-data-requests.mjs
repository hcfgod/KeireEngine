import { randomUUID } from "node:crypto";

export class LocalDataRequestError extends Error {
    constructor(status, code, message) {
        super(message);
        this.status = status;
        this.code = code;
    }
}

/** Records a support request only; fulfillment must not be confused with deleting an account. */
export async function requestLocalAccountData(authentication, sessionToken, kind) {
    if (kind !== "export" && kind !== "deletion") {
        throw new LocalDataRequestError(400, "account.invalid_data_request", "Choose an export or deletion request.");
    }
    const field = kind === "export" ? "export_requested_at" : "deletion_requested_at";
    const result = await authentication.withSessionTransaction(sessionToken, async (connection, session) => {
        const [profiles] = await connection.execute(
            `SELECT ${field} AS requested_at FROM profiles WHERE user_id=? FOR UPDATE`, [session.userId]);
        if (profiles.length !== 1) {
            throw new LocalDataRequestError(409, "account.profile_unavailable", "Your profile is unavailable. Contact support to request your account data.");
        }
        if (profiles[0].requested_at === null) {
            await connection.execute(`UPDATE profiles SET ${field}=UTC_TIMESTAMP(6),updated_at=UTC_TIMESTAMP(6) WHERE user_id=?`,
                [session.userId]);
            await connection.execute(`INSERT INTO platform_audit_events
                (occurred_at,actor_user_id,actor_session_id,action,target_type,target_id,correlation_id,metadata)
                VALUES (UTC_TIMESTAMP(6),?,NULL,?,'account',?,?,'{}')`,
            [session.userId, `account.${kind}_requested`, session.userId, randomUUID()]);
        }
        return { kind, requested: true };
    });
    if (!result) {
        throw new LocalDataRequestError(401, "account.authentication_required", "Sign in again to request your account data.");
    }
    return result;
}
