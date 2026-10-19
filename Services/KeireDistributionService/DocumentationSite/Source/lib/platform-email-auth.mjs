import { createHash, randomUUID } from "node:crypto";
import { accountAvailable } from "./platform-local-auth.mjs";
import { tokenDigest, verifyImportedPassword } from "./platform-auth-crypto.mjs";
import { sealAccountRecord } from "./platform-account-import.mjs";
import { createEmailCode } from "./platform-email-code.mjs";
import { consumeEmailChallenge, finishEmailDelivery, insertEmailChallenge } from "./platform-email-store.mjs";
import { reserveEmailSend } from "./platform-email-limits.mjs";
import { platformTransaction } from "./platform-mysql.mjs";

const digest = (value) => createHash("sha256").update(value).digest("hex");
const invalid = Object.freeze({ state: "invalid_credentials" });

/** Borrows authentication and mailer; the application owns and closes both after draining requests. */
export class LocalEmailAuthentication {
    constructor(authentication, mailer) {
        this.authentication = authentication;
        this.mailer = mailer;
    }

    async enrollmentAuthorization(connection, sessionToken, now) {
        const sessionHash = tokenDigest(sessionToken);
        if (!sessionHash) {
            return null;
        }
        const [sessions] = await connection.execute(
            "SELECT * FROM platform_auth_sessions WHERE token_hash = ? FOR UPDATE", [sessionHash]);
        const session = sessions[0];
        if (!session || Number(session.expires_ms) <= now) {
            return null;
        }
        const [accounts] = await connection.execute("SELECT * FROM platform_accounts WHERE id = ? FOR UPDATE", [session.user_id]);
        const account = accounts[0];
        if (!accountAvailable(account, now) || digest(account.password_hash ?? "") !== session.password_digest) {
            return null;
        }
        const [factors] = await connection.execute(
            "SELECT id,factor_type FROM platform_mfa_factors WHERE user_id = ? AND status = 'verified' FOR UPDATE", [account.id]);
        if (factors.length > 0 && (session.assurance_level !== 2 ||
            !factors.some((factor) => factor.id === session.factor_id && factor.factor_type === "totp"))) {
            return null;
        }
        return {
            account,
            context: { purpose: "enroll", userId: account.id, email: account.email,
                passwordDigest: session.password_digest, authorizationDigest: sessionHash },
        };
    }

    async deliver(issued) {
        if (issued.state !== "pending_delivery") {
            return issued;
        }
        const { challenge, context } = issued;
        try {
            await this.mailer.sendVerification({ recipient: context.email, code: challenge.code, purpose: context.purpose });
        } catch {
            await finishEmailDelivery(this.authentication.pool, challenge.token, false);
            return { state: "delivery_failed" };
        } finally {
            challenge.code = null;
        }
        if (!await finishEmailDelivery(this.authentication.pool, challenge.token, true)) {
            return { state: "delivery_failed" };
        }
        return { state: "code_sent", token: challenge.token, expiresAt: challenge.record.expiresAt };
    }

    async beginEnrollment(sessionToken, password, clientIdentity) {
        const auth = this.authentication;
        const now = auth.now();
        if (typeof password !== "string" || password.length > 1024 || typeof clientIdentity !== "string" ||
            clientIdentity.length === 0 || clientIdentity.length > 256) {
            return invalid;
        }
        const session = await auth.session(sessionToken);
        if (!session) {
            return invalid;
        }
        const [accounts] = await auth.pool.execute("SELECT * FROM platform_accounts WHERE id = ?", [session.userId]);
        const candidate = accounts[0];
        if (!accountAvailable(candidate, now)) {
            return invalid;
        }
        if (!await auth.reserveAttempt(candidate.email_key, `enroll:${clientIdentity}`, now)) {
            return { state: "rate_limited", retryAfterSeconds: 900 };
        }
        if (!await verifyImportedPassword(password, candidate.password_hash)) {
            return invalid;
        }
        const issued = await platformTransaction(auth.pool, async (connection) => {
            const authorization = await this.enrollmentAuthorization(connection, sessionToken, now);
            if (!authorization || authorization.account.password_hash !== candidate.password_hash) {
                return invalid;
            }
            const limits = await reserveEmailSend(connection, candidate.id, clientIdentity, now);
            if (!limits.allowed) {
                return { state: "rate_limited", retryAfterSeconds: limits.retryAfterSeconds };
            }
            const challenge = createEmailCode(auth.encryptionKey, authorization.context, now);
            await insertEmailChallenge(connection, candidate.id, challenge.record);
            return { state: "pending_delivery", challenge, context: authorization.context };
        });
        return this.deliver(issued);
    }

    async completeEnrollment(sessionToken, emailToken, code) {
        const auth = this.authentication;
        const now = auth.now();
        const authorization = await platformTransaction(auth.pool,
            (connection) => this.enrollmentAuthorization(connection, sessionToken, now));
        if (!authorization) {
            return invalid;
        }
        const result = await consumeEmailChallenge(auth.pool, auth.encryptionKey, authorization.context,
            emailToken, code, now, async (connection) => {
                const current = await this.enrollmentAuthorization(connection, sessionToken, now);
                if (!current || current.context.email !== authorization.context.email ||
                    current.context.passwordDigest !== authorization.context.passwordDigest) {
                    return null;
                }
                const [existing] = await connection.execute(`SELECT id FROM platform_mfa_factors
                    WHERE user_id = ? AND factor_type = 'email' AND status = 'verified' FOR UPDATE`, [current.account.id]);
                if (existing.length > 0) {
                    return { state: "already_enrolled" };
                }
                const factorId = randomUUID();
                await connection.execute(`INSERT INTO platform_mfa_factors
                    (id,user_id,factor_type,status,friendly_name,source_sealed) VALUES (?,?,'email','verified','Email verification',?)`,
                [factorId, current.account.id, sealAccountRecord({ email: current.account.email }, auth.encryptionKey, `mfa:${factorId}`)]);
                return { state: "enrolled", factorId };
            });
        return result ?? invalid;
    }
}
