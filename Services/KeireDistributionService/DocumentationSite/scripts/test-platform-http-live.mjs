import assert from "node:assert/strict";
import { fork } from "node:child_process";
import { createHash, createHmac, randomBytes, randomUUID } from "node:crypto";
import { fileURLToPath } from "node:url";
import bcrypt from "bcryptjs";
import { createPlatformPool } from "../Source/lib/platform-mysql.mjs";
import { sealAccountRecord } from "../Source/lib/platform-account-import.mjs";
import { loadWindowsPlatformSecrets } from "../Source/lib/platform-secrets.mjs";
import { emailSendLimits } from "../Source/lib/platform-email-limits.mjs";

const userId = randomUUID();
const factorId = randomUUID();
const email = `http-qa-${userId}@invalid.example`;
const password = randomBytes(24).toString("base64url");
const clientIdentity = `qa:${userId}:127.0.0.1`;
const origin = "https://qa.invalid.example";
let secrets;
let pool;
let child;
let childExited;
let fixtureCreated = false;
let stage = "settings";
let checks = 0;
const capturedMail = [];
try {
    secrets = await loadWindowsPlatformSecrets({
        powerShellExecutable: process.env.KEIRE_QA_POWERSHELL,
        settingsDirectory: process.env.KEIRE_QA_SETTINGS_DIRECTORY,
        decryptScriptPath: fileURLToPath(new URL("../../scripts/read-local-platform-runtime.ps1", import.meta.url)),
    });
    pool = await createPlatformPool(secrets.settings);
    stage = "fixture";
    await pool.execute(`INSERT INTO platform_accounts
        (id,email,email_key,password_hash,email_confirmed_at,app_metadata,user_metadata,source_sealed)
        VALUES (?,?,?,?,'2000-01-01 00:00:00.000000','{}','{}',?)`,
    [userId, email, email, await bcrypt.hash(password, 4),
        sealAccountRecord({ fixture: true }, secrets.accountEncryptionKey, `account:${userId}`)]);
    fixtureCreated = true;
    await pool.execute(`INSERT INTO platform_mfa_factors
        (id,user_id,factor_type,status,friendly_name,source_sealed) VALUES (?,?,'totp','verified','HTTP QA authenticator',?)`,
    [factorId, userId, sealAccountRecord({ secret: "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ" },
        secrets.accountEncryptionKey, `mfa:${factorId}`)]);
    secrets.accountEncryptionKey.fill(0);

    stage = "host-startup";
    child = fork(fileURLToPath(new URL("./start-local-platform-qa.mjs", import.meta.url)), [], {
        env: { ...process.env, KEIRE_QA_CLIENT_NAMESPACE: userId, KEIRE_QA_MAIL_MODE: "capture" }, silent: true, windowsHide: true,
    });
    child.stdout.resume();
    child.stderr.resume();
    child.on("message", (message) => {
        if (message?.type === "qa-verification-email" && message.message?.recipient === email) {
            capturedMail.push(message.message);
        }
    });
    childExited = new Promise((resolve) => child.once("close", resolve));
    await new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error("QA host startup timed out")), 30000);
        child.once("error", () => { clearTimeout(timer); reject(new Error("QA host failed")); });
        child.once("exit", () => { clearTimeout(timer); reject(new Error("QA host exited")); });
        child.on("message", (message) => {
            if (message?.type === "qa-host-ready") {
                clearTimeout(timer);
                resolve();
            }
        });
    });
    const base = `http://127.0.0.1:${process.env.KEIRE_QA_PORT}`;
    let cookies = new Map();
    async function request(route, body) {
        const response = await fetch(base + route, {
            method: body === undefined ? "GET" : "POST", redirect: "manual", signal: AbortSignal.timeout(10000),
            headers: { origin, "content-type": "application/json", cookie: [...cookies].map(([key, value]) => `${key}=${value}`).join("; ") },
            body: body === undefined ? undefined : JSON.stringify(body),
        });
        for (const value of response.headers.getSetCookie()) {
            assert.ok(value.includes("HttpOnly; Secure; SameSite=Lax"));
            const first = value.split(";")[0];
            const separator = first.indexOf("=");
            cookies.set(first.slice(0, separator), first.slice(separator + 1));
        }
        return response;
    }
    stage = "public-catalog";
    const catalogResponse = await request("/marketplace/v1/catalog/?limit=1");
    assert.equal(catalogResponse.status, 200);
    const catalog = await catalogResponse.json();
    assert.equal(catalog.page.limit, 1);
    assert.equal(catalog.data.length, 1);
    assert.equal(typeof catalog.data[0].publisher_slug, "string");
    checks += 4;
    stage = "rendered-marketplace";
    const marketplace = await request("/marketplace/");
    assert.equal(marketplace.status, 200);
    assert.ok((await marketplace.text()).includes(catalog.data[0].slug));
    const filteredMarketplace = await request(`/marketplace/?q=${randomUUID()}`);
    assert.equal(filteredMarketplace.status, 200);
    assert.match(await filteredMarketplace.text(), /No products matched/);
    checks += 4;
    if (catalog.page.nextCursor) {
        const nextPage = await request(`/marketplace/v1/catalog/?limit=1&cursor=${encodeURIComponent(catalog.page.nextCursor)}`);
        assert.equal(nextPage.status, 200);
        const nextCatalog = await nextPage.json();
        assert.ok(nextCatalog.data.every((entry) => entry.id !== catalog.data[0].id));
        checks += 2;
    }
    stage = "login";
    const login = await request("/account/session/", { email, password });
    assert.equal(login.status, 200);
    assert.equal((await login.json()).data.mfaRequired, true);
    assert.equal((await (await request("/account/session/")).json()).data.signedIn, false);
    checks += 3;

    stage = "rendered-mfa";
    const page = await request("/account/mfa/challenge/");
    assert.equal(page.status, 200);
    assert.match(await page.text(), /HTTP QA authenticator/);
    checks += 2;
    const counter = Buffer.alloc(8);
    counter.writeBigUInt64BE(BigInt(Math.floor(Date.now() / 30000)));
    const digest = createHmac("sha1", Buffer.from("12345678901234567890")).update(counter).digest();
    const offset = digest[digest.length - 1] & 15;
    const code = String((digest.readUInt32BE(offset) & 0x7fffffff) % 1000000).padStart(6, "0");
    stage = "mfa-verification";
    const verification = await request("/account/mfa/challenge/verify/", { factorId, code });
    assert.equal(verification.status, 200);
    assert.equal((await verification.json()).data.userId, userId);
    assert.deepEqual((await (await request("/account/session/")).json()).data,
        { signedIn: true, assuranceLevel: "aal2" });
    checks += 3;

    stage = "account-overview";
    const accountPage = await request("/account/");
    assert.equal(accountPage.status, 200);
    assert.equal(accountPage.headers.get("cache-control"), "private, no-store");
    const accountHtml = await accountPage.text();
    assert.ok(accountHtml.includes(email));
    assert.match(accountHtml, /No claimed assets yet/);
    checks += 4;

    stage = "email-enrollment";
    const enrollment = await request("/account/email/enroll/", { password });
    assert.equal(enrollment.status, 200);
    assert.equal((await enrollment.json()).data.codeSent, true);
    assert.equal(capturedMail.at(-1)?.purpose, "enroll");
    const enrollmentVerification = await request("/account/email/enroll/verify/", { code: capturedMail.at(-1).code });
    assert.equal(enrollmentVerification.status, 200);
    assert.equal((await enrollmentVerification.json()).data.emailEnabled, true);
    checks += 5;
    await (await request("/account/session/delete/", {})).arrayBuffer();
    const emailLogin = await request("/account/session/", { email, password });
    const emailLoginBody = await emailLogin.json();
    assert.equal(emailLogin.status, 200);
    const emailFactor = emailLoginBody.data.factors.find((factor) => factor.factor_type === "email");
    assert.ok(emailFactor);
    const cooldown = await request("/account/email/challenge/", { factorId: emailFactor.id });
    assert.equal(cooldown.status, 429);
    await cooldown.arrayBuffer();
    // Age only this synthetic account's send windows instead of waiting a minute in HTTP acceptance.
    for (const limit of emailSendLimits(userId, clientIdentity)) {
        if (limit.windowMs === 60000) {
            await pool.execute("UPDATE platform_auth_limits SET window_start = ? WHERE subject_hash = ?", [Date.now() - 60000, limit.hash]);
        }
    }
    const sendCode = await request("/account/email/challenge/", { factorId: emailFactor.id });
    assert.equal(sendCode.status, 200);
    await sendCode.arrayBuffer();
    assert.equal(capturedMail.at(-1)?.purpose, "sign_in");
    const emailVerification = await request("/account/email/challenge/verify/",
        { factorId: emailFactor.id, code: capturedMail.at(-1).code });
    assert.equal(emailVerification.status, 200);
    await emailVerification.arrayBuffer();
    assert.deepEqual((await (await request("/account/session/")).json()).data,
        { signedIn: true, assuranceLevel: "aal1" });
    checks += 7;

    stage = "logout";
    const savedCookies = new Map(cookies);
    const logout = await request("/account/session/delete/", {});
    assert.equal(logout.status, 303);
    await logout.arrayBuffer();
    assert.equal((await (await request("/account/session/")).json()).data.signedIn, false);
    const anonymousAccount = await request("/account/");
    assert.equal(anonymousAccount.status, 303);
    assert.ok(anonymousAccount.headers.get("location").startsWith("/account/sign-in/"));
    await anonymousAccount.arrayBuffer();
    checks += 2;
    cookies = savedCookies;
    assert.equal((await (await request("/account/session/")).json()).data.signedIn, false);
    checks += 3;
} catch {
    console.error(JSON.stringify({ status: "failed", stage, checks }));
    process.exitCode = 1;
} finally {
    if (child && child.exitCode === null) {
        if (child.connected) {
            child.send({ type: "close-qa-host" }, (error) => {
                if (error) {
                    process.exitCode = 1;
                }
            });
        }
        const timer = setTimeout(() => {
            process.exitCode = 1;
            console.error("QA host did not stop gracefully within its deadline.");
            child.kill();
        }, 15000);
        const exitCode = await childExited;
        clearTimeout(timer);
        if (exitCode !== 0) {
            process.exitCode = 1;
        }
    }
    try {
        if (fixtureCreated) {
            await pool.execute("DELETE FROM platform_auth_sessions WHERE user_id=?", [userId]);
            await pool.execute("DELETE FROM platform_auth_challenges WHERE user_id=?", [userId]);
            await pool.execute("DELETE FROM platform_email_challenges WHERE user_id=?", [userId]);
            await pool.execute("DELETE FROM platform_mfa_counters WHERE factor_id=?", [factorId]);
            await pool.execute("DELETE FROM platform_mfa_factors WHERE user_id=?", [userId]);
            await pool.execute("DELETE FROM platform_accounts WHERE id=?", [userId]);
            for (const subject of [`email:${email}`, `client:${clientIdentity}`, `pair:${clientIdentity}\0${email}`,
                `client:enroll:${clientIdentity}`, `pair:enroll:${clientIdentity}\0${email}`]) {
                await pool.execute("DELETE FROM platform_auth_limits WHERE subject_hash=?",
                    [createHash("sha256").update(subject).digest("hex")]);
            }
            for (const limit of emailSendLimits(userId, clientIdentity)) {
                await pool.execute("DELETE FROM platform_auth_limits WHERE subject_hash=?", [limit.hash]);
            }
        }
    } catch {
        console.error("Synthetic HTTP fixture cleanup failed.");
        process.exitCode = 1;
    }
    secrets?.accountEncryptionKey.fill(0);
    capturedMail.length = 0;
    await pool?.end();
}
if (!process.exitCode) {
    console.log(JSON.stringify({ status: "rendered-http-authentication-passed", checks,
        fixturesRemoved: true, realAccountUsed: false, liveWebsiteChanged: false }));
}
