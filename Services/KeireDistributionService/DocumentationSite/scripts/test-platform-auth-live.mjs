import assert from "node:assert/strict";
import { createEmailChallengeSchema } from "../Source/lib/platform-email-store.mjs";
import { verifyEmailChallengeStore } from "./platform-email-store-acceptance.mjs";
import { verifyEmailSendLimits } from "./platform-email-limits-acceptance.mjs";
import { verifyEmailSessionAssurance } from "./platform-email-session-acceptance.mjs";
import { verifyEmailEnrollment } from "./platform-email-enrollment-acceptance.mjs";
import { createHash, randomBytes, randomUUID } from "node:crypto";
import bcrypt from "bcryptjs";
import { createLocalAuthenticationHttp } from "../Source/lib/platform-auth-http.mjs";
import { PlatformBackend } from "../Source/lib/platform-backend.mjs";
import { verifyAccountVisibility } from "./platform-account-view-acceptance.mjs";
import { verifyLocalClaims } from "./platform-claims-acceptance.mjs";
import { verifyLocalOrganizations } from "./platform-organizations-acceptance.mjs";
import { verifyPublisherApplications } from "./platform-publisher-applications-acceptance.mjs";
import { verifyUploadConcurrency } from "./platform-upload-concurrency-acceptance.mjs";
import { verifyPublicationSequence } from "./platform-publication-sequence-acceptance.mjs";
import { verifyLocalDataRequests } from "./platform-data-requests-acceptance.mjs";
import { verifyAuthenticatorEnrollment } from "./platform-mfa-management-acceptance.mjs";
import { createRecoverySchema } from "../Source/lib/platform-recovery-store.mjs";
import { verifyRecoveryStore } from "./platform-recovery-store-acceptance.mjs";
import { verifyPasswordReplacement } from "./platform-password-recovery-acceptance.mjs";
import { createRecoveryDeliverySchema } from "../Source/lib/platform-recovery-delivery.mjs";
import { verifyRecoveryDelivery } from "./platform-recovery-delivery-acceptance.mjs";
import { createRegistrationSchema } from "../Source/lib/platform-registration.mjs";
import { verifyRegistrationStaging } from "./platform-registration-acceptance.mjs";
import { verifyRegistrationConfirmation } from "./platform-registration-confirmation-acceptance.mjs";
import { verifyLocalCatalog } from "./platform-catalog-acceptance.mjs";
import { createPlatformPool } from "../Source/lib/platform-mysql.mjs";
import { sealAccountRecord } from "../Source/lib/platform-account-import.mjs";
import { createAuthenticationSchema, LocalPasswordAuthentication } from "../Source/lib/platform-local-auth.mjs";

process.stdin.setEncoding("utf8");
let input = "";
for await (const chunk of process.stdin) {
    input += chunk;
    if (input.length > 65536) {
        throw new Error("Authentication acceptance input exceeded its limit.");
    }
}
const settings = JSON.parse(input);
input = "";
const migration = await createPlatformPool(settings, "migration");
const runtime = await createPlatformPool(settings);
const userId = randomUUID();
const factorId = randomUUID();
const email = `qa-${userId}@invalid.example`;
const clientIdentity = `qa-${userId}`;
const encryptionKey = randomBytes(32);
const password = randomBytes(24).toString("base64url");
let now = 1234567890000;
let auth;
let stage = "schema";
let fixtureCreated = false;
let checks = 0;
try {
    if (process.argv.includes("--create-schema")) {
        await createAuthenticationSchema(migration);
    }
    if (process.argv.includes("--create-email-schema")) {
        await createEmailChallengeSchema(migration);
    }
    if (process.argv.includes("--create-recovery-schema")) {
        await createRecoverySchema(migration);
    }
    if (process.argv.includes("--create-recovery-delivery-schema")) {
        await createRecoveryDeliverySchema(migration);
    }
    if (process.argv.includes("--create-registration-schema")) {
        await createRegistrationSchema(migration);
    }
    stage = "fixture";
    const passwordHash = await bcrypt.hash(password, 4);
    await migration.execute(`INSERT INTO platform_accounts
        (id,email,email_key,password_hash,email_confirmed_at,app_metadata,user_metadata,source_sealed)
        VALUES (?,?,?,?,'2000-01-01 00:00:00.000000','{}','{}',?)`,
    [userId, email, email, passwordHash, sealAccountRecord({ fixture: true }, encryptionKey, `account:${userId}`)]);
    fixtureCreated = true;
    await migration.execute(`INSERT INTO platform_mfa_factors
        (id,user_id,factor_type,status,friendly_name,source_sealed) VALUES (?,?,'totp','verified','QA fixture',?)`,
    [factorId, userId, sealAccountRecord({ id: factorId, user_id: userId, secret: "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ" },
        encryptionKey, `mfa:${factorId}`)]);
    auth = await LocalPasswordAuthentication.create(runtime, encryptionKey, () => now);
    stage = "password-and-mfa";
    assert.equal((await auth.signIn(email, "incorrect", clientIdentity)).state, "invalid_credentials");
    const challenge = await auth.signIn(email, password, clientIdentity);
    assert.equal(challenge.state, "mfa_required");
    assert.equal(await auth.session(challenge.challengeToken), null);
    const pending = await auth.pendingChallenge(challenge.challengeToken);
    assert.equal(pending.factors[0].id, factorId);
    assert.equal(Object.hasOwn(pending, "userId"), false);
    checks += 2;
    assert.equal((await auth.completeTotp(challenge.challengeToken, randomUUID(), "005924")).state, "invalid_credentials");
    const authenticated = await auth.completeTotp(challenge.challengeToken, factorId, "005924");
    assert.equal(authenticated.state, "authenticated");
    assert.equal(authenticated.assurance, 2);
    assert.equal((await auth.session(authenticated.token)).userId, userId);
    assert.equal(await auth.pendingChallenge(challenge.challengeToken), null);
    checks++;
    checks += 7;

    stage = "transactional-session-authorization";
    assert.equal(await auth.withSessionTransaction(authenticated.token, async (connection, session) => {
        assert.equal(session.userId, userId);
        const [rows] = await connection.execute("SELECT id FROM platform_accounts WHERE id = ?", [session.userId]);
        return rows[0].id;
    }), userId);
    assert.equal(await auth.withSessionTransaction("invalid", () => {
        throw new Error("An invalid session reached a protected mutation.");
    }), null);
    checks += 2;

    stage = "replay-protection";
    assert.equal((await auth.completeTotp(challenge.challengeToken, factorId, "005924")).state, "invalid_credentials");
    const replay = await auth.signIn(email, password, clientIdentity);
    assert.equal((await auth.completeTotp(replay.challengeToken, factorId, "005924")).state, "invalid_credentials");
    checks += 2;

    stage = "challenge-attempt-limit";
    const exhausted = await auth.signIn(email, password, clientIdentity);
    for (let attempt = 0; attempt < 5; attempt++) {
        assert.equal((await auth.completeTotp(exhausted.challengeToken, factorId, "invalid")).state, "invalid_credentials");
    }
    const [attemptRows] = await migration.execute("SELECT attempts FROM platform_auth_challenges WHERE token_hash = ?",
        [createHash("sha256").update(exhausted.challengeToken).digest("hex")]);
    assert.equal(attemptRows[0].attempts, 5);
    assert.equal((await auth.completeTotp(exhausted.challengeToken, factorId, "005924")).state, "invalid_credentials");
    assert.equal(await auth.pendingChallenge(exhausted.challengeToken), null);
    checks++;
    checks += 7;

    stage = "expiration-and-revocation";
    const expiring = await auth.signIn(email, password, clientIdentity);
    now += 5 * 60 * 1000;
    assert.equal((await auth.completeTotp(expiring.challengeToken, factorId, "005924")).state, "invalid_credentials");
    assert.equal(await auth.pendingChallenge(expiring.challengeToken), null);
    checks++;
    await auth.signOut(authenticated.token);
    assert.equal(await auth.session(authenticated.token), null);
    assert.equal(await auth.withSessionTransaction(authenticated.token, () => {
        throw new Error("A revoked session reached a protected mutation.");
    }), null);
    checks++;
    checks += 2;

    stage = "ban-enforcement";
    await migration.execute("UPDATE platform_accounts SET banned_until = '2099-01-01' WHERE id = ?", [userId]);
    assert.equal((await auth.signIn(email, password, clientIdentity)).state, "invalid_credentials");
    await migration.execute("UPDATE platform_accounts SET banned_until = NULL WHERE id = ?", [userId]);
    checks++;

    stage = "concurrent-rate-limit";
    now += 15 * 60 * 1000;
    const results = await Promise.all(Array.from({ length: 12 }, () => auth.signIn(email, "incorrect", clientIdentity)));
    assert.equal(results.filter((result) => result.state === "rate_limited").length, 2);
    assert.equal(results.filter((result) => result.state === "invalid_credentials").length, 10);
    checks += 2;

    stage = "concurrent-mfa-replay";
    now = 2000000000000;
    const firstChallenge = await auth.signIn(email, password, clientIdentity);
    const secondChallenge = await auth.signIn(email, password, clientIdentity);
    const simultaneous = await Promise.all([
        auth.completeTotp(firstChallenge.challengeToken, factorId, "279037"),
        auth.completeTotp(secondChallenge.challengeToken, factorId, "279037"),
    ]);
    assert.equal(simultaneous.filter((result) => result.state === "authenticated").length, 1);
    assert.equal(simultaneous.filter((result) => result.state === "invalid_credentials").length, 1);
    const accepted = simultaneous.find((result) => result.state === "authenticated");
    assert.equal((await auth.session(accepted.token)).assurance, 2);
    checks += 3;

    stage = "password-change-and-session-expiry";
    await migration.execute("UPDATE platform_accounts SET password_hash = ? WHERE id = ?",
        [await bcrypt.hash("new synthetic password", 4), userId]);
    assert.equal(await auth.session(accepted.token), null);
    await migration.execute("UPDATE platform_accounts SET password_hash = ? WHERE id = ?", [passwordHash, userId]);
    now += 8 * 60 * 60 * 1000;
    assert.equal(await auth.session(accepted.token), null);
    checks += 2;

    stage = "new-mfa-requirement";
    await migration.execute("UPDATE platform_mfa_factors SET status = 'unverified' WHERE id = ?", [factorId]);
    const passwordOnly = await auth.signIn(email, password, clientIdentity);
    assert.equal(passwordOnly.assurance, 1);
    await migration.execute("UPDATE platform_mfa_factors SET status = 'verified' WHERE id = ?", [factorId]);
    assert.equal(await auth.session(passwordOnly.token), null);
    checks += 2;
    stage = "http-browser-authentication";
    now = 20000000000000;
    const origin = "https://qa.invalid.example";
    const handle = createLocalAuthenticationHttp(auth, origin);
    const post = (route, body, cookies = "", requestOrigin = origin) => new Request(origin + route, {
        method: "POST",
        headers: { origin: requestOrigin, "content-type": "application/json", cookie: cookies },
        body: JSON.stringify(body),
    });
    const cookieValue = (response, name) => {
        const values = response.headers.getSetCookie().filter((value) => value.startsWith(`${name}=`));
        assert.equal(values.length, 1);
        assert.ok(values[0].includes("HttpOnly; Secure; SameSite=Lax"));
        return values[0].split(";")[0];
    };
    const login = await handle(post("/account/session", { email, password }), clientIdentity, "qa");
    assert.equal(login.status, 200);
    const loginBody = await login.json();
    assert.equal(loginBody.data.mfaRequired, true);
    assert.equal(loginBody.data.factors[0].id, factorId);
    const challengeCookie = cookieValue(login, "__Host-keire-challenge");
    const statusResponse = await handle(new Request(origin + "/account/mfa/challenge/status", {
        headers: { cookie: challengeCookie },
    }), clientIdentity, "qa");
    assert.equal(statusResponse.status, 200);
    assert.equal((await statusResponse.json()).data.factors[0].id, factorId);
    checks += 2;
    const beforeMfa = await handle(new Request(origin + "/account/session", {
        headers: { cookie: challengeCookie },
    }), clientIdentity, "qa");
    assert.equal((await beforeMfa.json()).data.signedIn, false);
    checks += 4;

    stage = "http-mfa-and-session";
    const verified = await handle(post("/account/mfa/challenge/verify",
        { factorId, code: "353130" }, challengeCookie), clientIdentity, "qa");
    assert.equal(verified.status, 200);
    assert.equal((await verified.json()).data.userId, userId);
    const sessionCookie = cookieValue(verified, "__Host-keire-session");
    const sessionResponse = await handle(new Request(origin + "/account/session", {
        headers: { cookie: sessionCookie },
    }), clientIdentity, "qa");
    assert.deepEqual((await sessionResponse.json()).data, { signedIn: true, assuranceLevel: "aal2" });
    const replayResponse = await handle(post("/account/mfa/challenge/verify",
        { factorId, code: "353130" }, challengeCookie), clientIdentity, "qa");
    assert.equal(replayResponse.status, 401);
    checks += 4;

    stage = "http-csrf-and-logout";
    const rejected = await handle(post("/account/session/delete", {}, sessionCookie, "https://other.invalid"), clientIdentity, "qa");
    assert.equal(rejected.status, 403);
    const stillValid = await handle(new Request(origin + "/account/session", {
        headers: { cookie: sessionCookie },
    }), clientIdentity, "qa");
    assert.equal((await stillValid.json()).data.signedIn, true);
    const logout = await handle(post("/account/session/delete", {}, sessionCookie), clientIdentity, "qa");
    assert.equal(logout.status, 303);
    const afterLogout = await handle(new Request(origin + "/account/session", {
        headers: { cookie: sessionCookie },
    }), clientIdentity, "qa");
    assert.equal((await afterLogout.json()).data.signedIn, false);
    checks += 4;
    stage = "email-send-limits";
    checks += await verifyEmailEnrollment(runtime, auth, userId, password, factorId, now);
    checks += await verifyEmailSessionAssurance(runtime, auth, userId, encryptionKey, now);
    checks += await verifyEmailSendLimits(runtime);
    stage = "email-challenge-store";
    checks += await verifyEmailChallengeStore(runtime, userId, encryptionKey);
    stage = "account-visibility";
    checks += await verifyAccountVisibility(migration, userId, encryptionKey);
    stage = "marketplace-claims";
    checks += await verifyLocalClaims(migration, userId, auth, factorId, now);
    checks += await verifyLocalOrganizations(migration, userId);
    checks += await verifyPublisherApplications(migration, userId);
    stage = "upload-and-validator-leases";
    checks += await verifyUploadConcurrency(migration, userId);
    stage = "publication-sequence";
    checks += await verifyPublicationSequence(migration);
    stage = "account-data-requests";
    checks += await verifyLocalDataRequests(migration, userId);
    stage = "authenticator-enrollment";
    checks += await verifyAuthenticatorEnrollment(migration, auth, userId, password);
    stage = "password-recovery-store";
    checks += await verifyRecoveryStore(runtime, userId, now);
    stage = "password-recovery-replacement";
    checks += await verifyPasswordReplacement(runtime, auth, userId, factorId, now);
    stage = "recovery-delivery";
    checks += await verifyRecoveryDelivery(runtime, auth, userId, now);
    stage = "registration-staging";
    checks += await verifyRegistrationStaging(runtime, auth, now, email);
    stage = "registration-confirmation";
    checks += await verifyRegistrationConfirmation(runtime, auth, now);
    stage = "public-catalog";
    checks += await verifyLocalCatalog(migration);

    stage = "backend-lifecycle";
    const transferredKey = Buffer.from(encryptionKey);
    const backend = new PlatformBackend({ provider: "mysql",
        loadLocalSecrets: async () => ({ settings, accountEncryptionKey: transferredKey }) });
    try {
        const services = await Promise.all(Array.from({ length: 4 }, () => backend.withBackend(async (service) => {
            assert.equal(await service.authentication.session("invalid"), null);
            return service;
        })));
        assert.ok(services.every((service) => service === services[0]));
        assert.ok(transferredKey.equals(Buffer.alloc(32)));
        checks += 2;
    } finally {
        await backend.close();
    }
    await assert.rejects(backend.withBackend(() => undefined), /shutting down/);
    checks++;
    console.log(JSON.stringify({ status: "local-authentication-acceptance-passed", checks,
        realAccountUsed: false, liveWebsiteChanged: false }));
} catch (error) {
    if (/^(csharp|submission)-[a-z-]{1,60}$/.test(error?.fixtureStage ?? "")) {
        stage = error.fixtureStage;
    }
    const databaseCode = /^ER_[A-Z0-9_]{1,80}$/.test(error?.code ?? "") ? error.code : undefined;
    console.error(JSON.stringify({ status: "failed", stage, checks, databaseCode, sensitiveDiagnosticsSuppressed: true }));
    process.exitCode = 1;
} finally {
    auth?.close();
    try {
        if (fixtureCreated) {
            await migration.execute("DELETE FROM platform_auth_sessions WHERE user_id = ?", [userId]);
            await migration.execute("DELETE FROM platform_auth_challenges WHERE user_id = ?", [userId]);
            await migration.execute("DELETE FROM platform_mfa_counters WHERE factor_id = ?", [factorId]);
            await migration.execute("DELETE FROM platform_mfa_factors WHERE user_id = ?", [userId]);
            await migration.execute("DELETE FROM platform_accounts WHERE id = ?", [userId]);
            for (const subject of [`email:${email}`, `client:${clientIdentity}`, `pair:${clientIdentity}\0${email}`]) {
                await migration.execute("DELETE FROM platform_auth_limits WHERE subject_hash = ?",
                    [createHash("sha256").update(subject).digest("hex")]);
            }
        }
    } catch {
        console.error("Synthetic authentication fixture cleanup failed; inspect isolated QA data before continuing.");
        process.exitCode = 1;
    }
    encryptionKey.fill(0);
    await Promise.allSettled([migration.end(), runtime.end()]);
}
