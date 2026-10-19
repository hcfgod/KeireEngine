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
const origin = process.env.KEIRE_QA_TLS_THUMBPRINT ? `https://localhost:${process.env.KEIRE_QA_PORT}` : "https://qa.invalid.example";
let secrets;
let pool;
let child;
let childExited;
let fixtureCreated = false;
let stage = "settings";
let checks = 0;
const capturedMail = [];
const capturedRecovery = [];
const interactive = process.argv.includes("--interactive");
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
        if (message?.type === "qa-recovery-email" && message.message?.recipient === email) {
            capturedRecovery.push(message.message);
            if (interactive) {
                console.log(JSON.stringify({ syntheticRecoveryToken: message.message.token }));
            }
        }
        if (message?.type === "qa-verification-email" && message.message?.recipient === email) {
            capturedMail.push(message.message);
            if (interactive) {
                console.log(JSON.stringify({
                    syntheticEmailCode: message.message.code,
                    purpose: message.message.purpose,
                }));
            }
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
    const base = process.env.KEIRE_QA_TLS_THUMBPRINT ? origin : `http://127.0.0.1:${process.env.KEIRE_QA_PORT}`;
    let cookies = new Map();
    async function request(route, body, extraHeaders = {}, method = body === undefined ? "GET" : "POST") {
        const response = await fetch(base + route, {
            method, redirect: "manual", signal: AbortSignal.timeout(10000),
            headers: { origin, "content-type": "application/json", cookie: [...cookies].map(([key, value]) => `${key}=${value}`).join("; "), ...extraHeaders },
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
    if (interactive) {
        stage = "interactive-browser";
        console.log(JSON.stringify({ syntheticBrowserAccount: true, url: `${base}/account/sign-in/`, email, password,
            commands: "totp prints the synthetic authenticator code; stop cleans up and exits" }));
        await new Promise((resolve) => {
            const interactiveLifetimeMs = 45 * 60 * 1000;
            const timer = setTimeout(finish, interactiveLifetimeMs);

            function finish() {
                clearTimeout(timer);
                process.stdin.off("data", handleCommand);
                process.stdin.off("end", finish);
                process.stdin.pause();
                resolve();
            }

            function handleCommand(input) {
                const command = input.trim();
                if (command === "stop") {
                    finish();
                } else if (command === "totp") {
                    const counter = Buffer.alloc(8);
                    counter.writeBigUInt64BE(BigInt(Math.floor(Date.now() / 30000)));
                    const digest = createHmac("sha1", Buffer.from("12345678901234567890")).update(counter).digest();
                    const offset = digest[digest.length - 1] & 15;
                    const code = String((digest.readUInt32BE(offset) & 0x7fffffff) % 1000000).padStart(6, "0");
                    console.log(JSON.stringify({ syntheticTotp: code }));
                }
            }

            process.stdin.setEncoding("utf8");
            process.stdin.on("data", handleCommand);
            process.stdin.once("end", finish);
        });
    } else {
        stage = "public-catalog";
        const catalogResponse = await request("/marketplace/v1/catalog/?limit=1");
        assert.equal(catalogResponse.status, 200);
        const catalog = await catalogResponse.json();
        assert.equal(catalog.page.limit, 1);
        assert.equal(catalog.data.length, 1);
        assert.equal(typeof catalog.data[0].publisher_slug, "string");
        checks += 4;
        stage = "public-product-detail";
        const detailResponse = await request(`/marketplace/v1/products/${catalog.data[0].id}/`);
        assert.equal(detailResponse.status, 200);
        const detail = (await detailResponse.json()).data;
        assert.equal(detail.id, catalog.data[0].id);
        assert.ok(Array.isArray(detail.versions) && Array.isArray(detail.media) && Array.isArray(detail.reviews));
        assert.ok(detail.versions.every((version) => !Object.hasOwn(version, "archive_storage_path") &&
            ["published", "withdrawn", "security_revoked"].includes(version.state)));
        assert.equal((await request(`/marketplace/v1/products/${randomUUID()}/`)).status, 404);
        assert.equal((await request("/marketplace/v1/products/not-a-uuid/")).status, 400);
        checks += 6;
        stage = "rendered-product-detail";
        const productPage = await request(`/marketplace/${detail.publisher_slug}/${detail.slug}/`);
        assert.equal(productPage.status, 200);
        const productHtml = await productPage.text();
        assert.ok(productHtml.includes(detail.display_name));
        assert.match(productPage.headers.get("cache-control"), /private, no-store/);
        assert.equal((await request(`/marketplace/${detail.publisher_slug}/missing-${randomUUID()}/`)).status, 404);
        checks += 4;
        const [nonPublicProducts] = await pool.execute(`SELECT p.id FROM marketplace_products p
            JOIN publishers publisher ON publisher.id=p.publisher_id
            JOIN marketplace_categories category ON category.id=p.category_id
            WHERE p.state<>'published' OR publisher.suspended_at IS NOT NULL OR category.active=FALSE`);
        for (const product of nonPublicProducts) {
            assert.equal((await request(`/marketplace/v1/products/${product.id}/`)).status, 404);
            checks += 1;
        }
        stage = "rendered-publisher";
        const publisherPage = await request(`/marketplace/publishers/${detail.publisher_slug}/`);
        assert.equal(publisherPage.status, 200);
        assert.ok((await publisherPage.text()).includes(detail.publisher_name));
        assert.equal((await request(`/marketplace/publishers/missing-${randomUUID()}/`)).status, 404);
        checks += 3;
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
        assert.equal((await request("/marketplace/v1/claims/")).status, 405);
        assert.equal((await request("/marketplace/v1/claims/", {})).status, 401);
        checks += 2;
        assert.equal((await request("/marketplace/v1/library/")).status, 401);
        checks++;
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
        assert.match(accountHtml, /Send verification email/);
        assert.match(accountHtml, /Create an organization/);
        stage = "organization-creation";
        const organizationInput = { slug: `http-${userId}`, displayName: "HTTP QA organization" };
        const organizationResponse = await request("/marketplace/v1/organizations/", organizationInput);
        assert.equal(organizationResponse.status, 201);
        const organization = (await organizationResponse.json()).data;
        assert.equal(organization.slug, organizationInput.slug);
        assert.equal((await request("/marketplace/v1/organizations/", organizationInput)).status, 409);
        assert.equal((await request("/marketplace/v1/organizations/", organizationInput,
            { origin: "https://other.invalid" })).status, 403);
        assert.match(await (await request("/account/")).text(), /HTTP QA organization/);
        checks += 6;
        const library = await request("/marketplace/v1/library/");
        const invalidClaim = await request("/marketplace/v1/claims/", { ownership: "organization" });
        assert.equal(invalidClaim.status, 400);
        assert.match(invalidClaim.headers.get("cache-control"), /private, no-store/);
        checks += 2;
        assert.equal(library.status, 200);
        assert.deepEqual((await library.json()).data, []);
        assert.match(library.headers.get("cache-control"), /private, no-store/);
        const organizationLibrary = await request(`/marketplace/v1/library/?organizationId=${randomUUID()}`);
        assert.equal(organizationLibrary.status, 200);
        assert.deepEqual((await organizationLibrary.json()).data, []);
        checks += 5;
        assert.match(accountHtml, /data-session-server-signed-in="true"/);
        const signedInProduct = await request(`/marketplace/${detail.publisher_slug}/${detail.slug}/`);
        assert.equal(signedInProduct.status, 200);
        const signedInProductHtml = await signedInProduct.text();
        assert.match(signedInProductHtml, /data-session-server-signed-in="true"/);
        assert.match(signedInProductHtml, /Read full .* license terms/);
        assert.ok(signedInProductHtml.includes(detail.license_canonical_url));
        checks += 2;
        stage = "browser-acquisition";
        const claimBody = { productId: detail.id, ownership: "personal", licenseSpdx: detail.license_spdx,
            licenseRevision: detail.license_revision, acceptedLicenseSnapshot: detail.license_acceptance_snapshot };
        const claimHeaders = { "idempotency-key": randomUUID() };
        const claimResponse = await request("/marketplace/v1/claims/", claimBody, claimHeaders);
        assert.equal(claimResponse.status, 201);
        const claimData = (await claimResponse.json()).data;
        const retryResponse = await request("/marketplace/v1/claims/", claimBody, claimHeaders);
        assert.equal(retryResponse.status, 201);
        assert.equal((await retryResponse.json()).data.entitlementId, claimData.entitlementId);
        const acquiredLibrary = (await (await request("/marketplace/v1/library/")).json()).data;
        assert.equal(acquiredLibrary.length, 1);
        assert.equal(acquiredLibrary[0].id, claimData.entitlementId);
        const ownedAccountHtml = await (await request("/account/")).text();
        assert.ok(ownedAccountHtml.includes(`href="/marketplace/${detail.publisher_slug}/${detail.slug}/"`));
        checks++;
        checks += 5;
        checks += 3;
        checks++;
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
        const emailChallengePage = await request("/account/mfa/challenge/");
        assert.equal(emailChallengePage.status, 200);
        const emailChallengeHtml = await emailChallengePage.text();
        assert.match(emailChallengeHtml, /Send email code/);
        assert.match(emailChallengeHtml, /data-method="email"/);
        checks += 3;
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

        stage = "organization-email-assurance";
        assert.equal((await request("/marketplace/v1/organizations/", {
            slug: `email-${userId}`, displayName: "Must not be created",
        })).status, 403);
        assert.doesNotMatch(await (await request("/account/")).text(), /data-organization-form/);
        checks += 2;

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

        stage = "password-recovery-http";
        const recoveryPage = await request("/account/recover/");
        assert.equal(recoveryPage.status, 200);
        assert.equal(recoveryPage.headers.get("referrer-policy"), "no-referrer");
        const passwordPage = await request("/account/update-password/");
        assert.equal(passwordPage.status, 200);
        assert.match(await passwordPage.text(), /Confirm new password/);
        stage = "password-recovery-enqueue";
        for (const limit of emailSendLimits(userId, clientIdentity)) {
            if (limit.windowMs === 60000) {
                await pool.execute("UPDATE platform_auth_limits SET window_start=? WHERE subject_hash=?", [Date.now() - 60000, limit.hash]);
            }
        }
        assert.equal((await request("/account/recovery/", { email })).status, 202);
        const deliveryDeadline = Date.now() + 10000;
        let recovery;
        while (Date.now() < deliveryDeadline) {
            recovery = capturedRecovery.at(-1);
            if (recovery) {
                const [rows] = await pool.execute("SELECT delivery_state FROM platform_recovery_challenges WHERE token_hash=?",
                    [createHash("sha256").update(recovery.token).digest("hex")]);
                if (rows[0]?.delivery_state === "sent") break;
            }
            await new Promise((resolve) => setTimeout(resolve, 100));
        }
        assert.ok(recovery);
        stage = "password-recovery-exchange";
        const exchange = await request("/account/recovery/session/", { token: recovery.token });
        stage = `password-recovery-exchange-status-${exchange.status}`;
        assert.equal(exchange.status, 200);
        stage = "password-recovery-exchange-factor";
        assert.equal((await exchange.json()).data.factors[0].id, factorId);
        stage = "password-recovery-exchange-session";
        assert.equal((await (await request("/account/session/")).json()).data.signedIn, false);
        const nextPassword = `Recovered-${randomUUID()}`;
        stage = "password-recovery-replace";
        assert.equal((await request("/account/password/", { password: nextPassword }, {}, "PATCH")).status, 400);
        const resetCounter = Buffer.alloc(8);
        resetCounter.writeBigUInt64BE(BigInt(Math.floor(Date.now() / 30000) + 1));
        const resetDigest = createHmac("sha1", Buffer.from("12345678901234567890")).update(resetCounter).digest();
        const resetOffset = resetDigest[resetDigest.length - 1] & 15;
        const resetCode = String((resetDigest.readUInt32BE(resetOffset) & 0x7fffffff) % 1000000).padStart(6, "0");
        const reset = await request("/account/password/", { password: nextPassword, factorId, code: resetCode }, {}, "PATCH");
        assert.equal(reset.status, 200);
        assert.deepEqual((await reset.json()).data, { updated: true, signedOut: true });
        stage = "password-recovery-new-login";
        assert.equal((await request("/account/recovery/session/", { token: recovery.token })).status, 400);
        assert.equal((await request("/account/session/", { email, password })).status, 401);
        const newLogin = await request("/account/session/", { email, password: nextPassword });
        assert.equal(newLogin.status, 200);
        assert.equal((await newLogin.json()).data.mfaRequired, true);
        checks += 15;
    }
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
            await pool.execute("DELETE FROM platform_audit_events WHERE actor_user_id=? AND action='organization.created'", [userId]);
            await pool.execute(`DELETE membership FROM organization_memberships membership
                JOIN organizations organization ON organization.id=membership.organization_id WHERE organization.created_by=?`, [userId]);
            await pool.execute("DELETE FROM organizations WHERE created_by=?", [userId]);
            await pool.execute("DELETE FROM platform_audit_events WHERE actor_user_id=? AND action='account.password_recovered'", [userId]);
            await pool.execute("DELETE FROM platform_recovery_challenges WHERE user_id=?", [userId]);
            for (const recovery of capturedRecovery) {
                await pool.execute("DELETE FROM platform_recovery_delivery WHERE id=?", [recovery.deliveryId]);
                const hash = createHash("sha256").update(recovery.token).digest("hex");
                for (const subject of [`email:recovery:${hash}`, `pair:recovery-complete:${clientIdentity}\0recovery:${hash}`]) {
                    await pool.execute("DELETE FROM platform_auth_limits WHERE subject_hash=?", [createHash("sha256").update(subject).digest("hex")]);
                }
            }
            for (const subject of [`email:recovery-request:${email}`, `client:recovery-request:${clientIdentity}`,
                `pair:recovery-request:${clientIdentity}\0recovery-request:${email}`, `client:recovery-complete:${clientIdentity}`]) {
                await pool.execute("DELETE FROM platform_auth_limits WHERE subject_hash=?", [createHash("sha256").update(subject).digest("hex")]);
            }
            await pool.execute("DELETE FROM platform_audit_events WHERE actor_user_id=? AND action='marketplace.product_claimed'", [userId]);
            await pool.execute("DELETE FROM marketplace_entitlements WHERE user_id=?", [userId]);
            await pool.execute(`DELETE item FROM marketplace_order_items item
                JOIN marketplace_orders orders ON orders.id=item.order_id WHERE orders.user_id=?`, [userId]);
            await pool.execute("DELETE FROM marketplace_orders WHERE user_id=?", [userId]);
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
    console.log(JSON.stringify({ status: interactive ? "interactive-browser-fixture-closed" : "rendered-http-authentication-passed", checks,
        fixturesRemoved: true, realAccountUsed: false, liveWebsiteChanged: false }));
}
