import assert from "node:assert/strict";
import { createHash, randomBytes, randomUUID } from "node:crypto";
import bcrypt from "bcryptjs";
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
    assert.equal((await auth.completeTotp(challenge.challengeToken, randomUUID(), "005924")).state, "invalid_credentials");
    const authenticated = await auth.completeTotp(challenge.challengeToken, factorId, "005924");
    assert.equal(authenticated.state, "authenticated");
    assert.equal(authenticated.assurance, 2);
    assert.equal((await auth.session(authenticated.token)).userId, userId);
    checks += 7;

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
    checks += 7;

    stage = "expiration-and-revocation";
    const expiring = await auth.signIn(email, password, clientIdentity);
    now += 5 * 60 * 1000;
    assert.equal((await auth.completeTotp(expiring.challengeToken, factorId, "005924")).state, "invalid_credentials");
    await auth.signOut(authenticated.token);
    assert.equal(await auth.session(authenticated.token), null);
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
    console.log(JSON.stringify({ status: "local-authentication-acceptance-passed", checks,
        realAccountUsed: false, liveWebsiteChanged: false }));
} catch {
    console.error(JSON.stringify({ status: "failed", stage, checks, sensitiveDiagnosticsSuppressed: true }));
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
