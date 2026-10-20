import assert from "node:assert/strict";
import { fork } from "node:child_process";
import { createHash, createHmac, randomBytes, randomUUID } from "node:crypto";
import { fileURLToPath } from "node:url";
import { connect as connectTls } from "node:tls";
import bcrypt from "bcryptjs";
import jsQR from "jsqr";
import sharp from "sharp";
import { createPlatformPool } from "../Source/lib/platform-mysql.mjs";
import { sealAccountRecord } from "../Source/lib/platform-account-import.mjs";
import { loadWindowsPlatformSecrets } from "../Source/lib/platform-secrets.mjs";
import { emailSendLimits } from "../Source/lib/platform-email-limits.mjs";

const userId = randomUUID();
const factorId = randomUUID();
const email = `http-qa-${userId}@invalid.example`;
const registrationEmail = `registration-qa-${userId}@invalid.example`;
const registrationPassword = randomBytes(24).toString("base64url");
const password = randomBytes(24).toString("base64url");
const clientIdentity = `qa:${userId}:127.0.0.1`;
const origin = process.env.KEIRE_QA_TLS_THUMBPRINT ? `https://localhost:${process.env.KEIRE_QA_PORT}` : "https://qa.invalid.example";
let secrets;
let pool;
let child;
let childExited;
let shutdownPhase = "not-started";
let spareConnection;
let fixtureCreated = false;
let stage = "settings";
let checks = 0;
const capturedMail = [];
const capturedRecovery = [];
const capturedRegistration = [];
const registrationRequestIds = new Set();
const interactive = process.argv.includes("--interactive");

function syntheticAuthenticatorCode(secret, offset = 0) {
    const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    const bits = [...secret].map((character) => alphabet.indexOf(character).toString(2).padStart(5, "0")).join("");
    const bytes = [];
    for (let index = 0; index + 8 <= bits.length; index += 8) {
        bytes.push(parseInt(bits.slice(index, index + 8), 2));
    }
    const counter = Buffer.alloc(8);
    counter.writeBigUInt64BE(BigInt(Math.floor(Date.now() / 30000) + offset));
    const digest = createHmac("sha1", Buffer.from(bytes)).update(counter).digest();
    const position = digest[digest.length - 1] & 15;
    return String((digest.readUInt32BE(position) & 0x7fffffff) % 1000000).padStart(6, "0");
}

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
    await pool.execute(`INSERT INTO profiles (user_id,created_at,updated_at,locale,notification_preferences)
        VALUES (?,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6),'en','{}')`, [userId]);
    await pool.execute(`INSERT INTO platform_mfa_factors
        (id,user_id,factor_type,status,friendly_name,source_sealed) VALUES (?,?,'totp','verified','HTTP QA authenticator',?)`,
    [factorId, userId, sealAccountRecord({ secret: "GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ" },
        secrets.accountEncryptionKey, `mfa:${factorId}`)]);
    secrets.accountEncryptionKey.fill(0);

    stage = "host-startup";
    child = fork(fileURLToPath(new URL("./start-local-platform-qa.mjs", import.meta.url)), [], {
        env: { ...process.env, KEIRE_QA_CLIENT_NAMESPACE: userId, KEIRE_QA_MAIL_MODE: "capture",
            KEIRE_QA_REGISTRATION_EMAIL: registrationEmail }, silent: true, windowsHide: true,
    });
    child.stdout.resume();
    child.stderr.resume();
    child.on("message", (message) => {
        if (message?.type === "qa-registration-request" && /^[a-f0-9-]{36}$/.test(message.id)) {
            registrationRequestIds.add(message.id);
        }
        if (message?.type === "qa-registration-email" && message.message?.recipient === registrationEmail) {
            capturedRegistration.push(message.message);
        }
        if (message?.type === "qa-shutdown-phase" && ["connections", "services", "complete"].includes(message.phase)) {
            shutdownPhase = message.phase;
        }
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
    if (process.argv.includes("--idle-connection")) {
        if (!process.env.KEIRE_QA_TLS_THUMBPRINT) {
            throw new Error("The idle connection check requires the trusted HTTPS QA listener.");
        }
        await new Promise((resolve, reject) => {
            spareConnection = connectTls({
                host: "127.0.0.1", port: Number(process.env.KEIRE_QA_PORT), servername: "localhost",
                rejectUnauthorized: true,
            }, resolve);
            spareConnection.once("error", reject);
        });
    }
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
        if (process.argv.includes("--registration-preview")) {
            const registration = await request("/account/registration/", {
                email: registrationEmail, password: registrationPassword, displayName: "Browser Registration QA", accepted: true,
            });
            assert.equal(registration.status, 202);
            let previewToken;
            let previewReady = false;
            for (let attempt = 0; attempt < 50; attempt++) {
                previewToken = capturedRegistration.at(-1)?.token;
                if (previewToken) {
                    const [rows] = await pool.execute("SELECT delivery_state FROM platform_registration_requests WHERE token_hash=?",
                        [createHash("sha256").update(previewToken).digest("hex")]);
                    if (rows[0]?.delivery_state === "sent") {
                        previewReady = true;
                        break;
                    }
                }
                await new Promise((resolve) => setTimeout(resolve, 100));
            }
            assert.ok(previewToken);
            assert.ok(previewReady, "Registration preview delivery did not finish.");
            console.log(JSON.stringify({ syntheticRegistrationPreview: true,
                url: `${base}/account/confirm/#registration=${previewToken}` }));
        }
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
        stage = "registration-pages";
        const registrationPage = await request("/account/create/");
        assert.equal(registrationPage.status, 200);
        assert.match(await registrationPage.text(), /data-create-account/);
        const confirmationPage = await request("/account/confirm/");
        assert.equal(confirmationPage.status, 200);
        assert.match(await confirmationPage.text(), /Registration password/);
        assert.equal(confirmationPage.headers.get("referrer-policy"), "no-referrer");
        assert.equal((await request("/account/registration/", { accepted: false })).status, 400);
        assert.equal((await request("/account/registration/session/", { token: "invalid" })).status, 400);
        assert.equal((await request("/account/registration/status/")).status, 400);
        assert.equal((await request("/account/registration/confirm/", { password: "synthetic" })).status, 400);
        checks += 9;
        stage = "registration-request-http";
        const registration = await request("/account/registration/", {
            email: registrationEmail, password: registrationPassword, displayName: "HTTPS Registration QA", accepted: true,
        });
        assert.equal(registration.status, 202);
        assert.deepEqual((await registration.json()).data, { verificationRequired: true });
        assert.equal(registration.headers.has("set-cookie"), false);
        const [beforeConfirmation] = await pool.execute("SELECT id FROM platform_accounts WHERE email_key=?", [registrationEmail]);
        assert.equal(beforeConfirmation.length, 0);
        checks += 4;
        stage = "registration-delivery-http";
        let registrationToken;
        for (let attempt = 0; attempt < 50; attempt++) {
            registrationToken = capturedRegistration.at(-1)?.token;
            if (registrationToken) {
                const [rows] = await pool.execute("SELECT delivery_state FROM platform_registration_requests WHERE token_hash=?",
                    [createHash("sha256").update(registrationToken).digest("hex")]);
                if (rows[0]?.delivery_state === "sent") {
                    break;
                }
            }
            await new Promise((resolve) => setTimeout(resolve, 100));
        }
        assert.ok(registrationToken);
        assert.equal(registrationRequestIds.size, 1);
        checks += 2;
        stage = "registration-confirmation-http";
        const registrationExchange = await request("/account/registration/session/", { token: registrationToken });
        assert.equal(registrationExchange.status, 200);
        assert.ok(cookies.get("__Host-keire-registration"));
        assert.equal((await request("/account/registration/status/")).status, 200);
        assert.equal((await request("/account/registration/confirm/", { password: "incorrect" })).status, 400);
        const confirmed = await request("/account/registration/confirm/", { password: registrationPassword });
        assert.equal(confirmed.status, 200);
        assert.deepEqual((await confirmed.json()).data, { confirmed: true });
        assert.equal(cookies.get("__Host-keire-registration"), "");
        assert.equal((await request("/account/registration/session/", { token: registrationToken })).status, 400);
        assert.equal((await (await request("/account/session/")).json()).data.signedIn, false);
        checks += 9;
        stage = "registration-login-http";
        assert.equal((await request("/account/session/", { email: registrationEmail, password: registrationPassword })).status, 200);
        const registeredAccount = await request("/account/");
        assert.equal(registeredAccount.status, 200);
        assert.match(await registeredAccount.text(), /HTTPS Registration QA/);
        assert.equal((await request("/account/data/")).status, 200);
        checks += 4;
        stage = "registration-existing-account-http";
        const originalRequestIds = new Set(registrationRequestIds);
        const replacementPassword = randomBytes(24).toString("base64url");
        const duplicateRegistration = await request("/account/registration/", {
            email: registrationEmail, password: replacementPassword, displayName: "Unwanted replacement", accepted: true,
        });
        assert.equal(duplicateRegistration.status, 202);
        assert.deepEqual((await duplicateRegistration.json()).data, { verificationRequired: true });
        const duplicateId = [...registrationRequestIds].find((id) => !originalRequestIds.has(id));
        assert.ok(duplicateId);
        let duplicateState;
        for (let attempt = 0; attempt < 50; attempt++) {
            const [rows] = await pool.execute("SELECT delivery_state,payload_sealed FROM platform_registration_requests WHERE id=?", [duplicateId]);
            duplicateState = rows[0];
            if (duplicateState?.delivery_state === "failed") {
                break;
            }
            await new Promise((resolve) => setTimeout(resolve, 100));
        }
        assert.equal(duplicateState.delivery_state, "failed");
        assert.equal(duplicateState.payload_sealed, null);
        assert.equal(capturedRegistration.length, 1);
        assert.equal((await request("/account/session/", { email: registrationEmail, password: replacementPassword })).status, 401);
        assert.equal((await request("/account/session/", { email: registrationEmail, password: registrationPassword })).status, 200);
        checks += 8;
        cookies = new Map();
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
        const securityPage = await request("/account/mfa/");
        assert.equal(securityPage.status, 200);
        const securityHtml = await securityPage.text();
        assert.match(securityHtml, /Current password/);
        assert.match(securityHtml, /HTTP QA authenticator/);
        checks += 3;
        assert.equal((await request("/account/mfa/enroll/", { password: "incorrect", friendlyName: "QA setup" })).status, 400);
        const setupResponse = await request("/account/mfa/enroll/", { password, friendlyName: "QA setup" });
        assert.equal(setupResponse.status, 200);
        const setup = (await setupResponse.json()).data;
        assert.match(await (await request("/account/mfa/")).text(), /Cancel unfinished setup/);
        checks++;
        assert.match(setup.secret, /^[A-Z2-7]{32}$/);
        assert.match(setup.qrCode, /^data:image\/png;base64,/);
        assert.equal(Buffer.from(setup.qrCode.split(",")[1], "base64").subarray(0, 8).toString("hex"), "89504e470d0a1a0a");
        checks += 2;
        const qrPixels = await sharp(Buffer.from(setup.qrCode.split(",")[1], "base64"))
            .ensureAlpha()
            .raw()
            .toBuffer({ resolveWithObject: true });
        const decodedQr = jsQR(new Uint8ClampedArray(qrPixels.data), qrPixels.info.width, qrPixels.info.height);
        assert.ok(decodedQr, "The authenticator QR image must be readable");
        const authenticatorUri = new URL(decodedQr.data);
        assert.equal(authenticatorUri.protocol, "otpauth:");
        assert.equal(authenticatorUri.hostname, "totp");
        assert.equal(decodeURIComponent(authenticatorUri.pathname.slice(1)), `Kéire Engine:${email}`);
        assert.equal(authenticatorUri.searchParams.get("secret"), setup.secret);
        assert.equal(authenticatorUri.searchParams.get("issuer"), "Kéire Engine");
        assert.equal(authenticatorUri.searchParams.get("algorithm"), "SHA1");
        assert.equal(authenticatorUri.searchParams.get("digits"), "6");
        assert.equal(authenticatorUri.searchParams.get("period"), "30");
        checks += 9;
        assert.equal((await request("/account/mfa/verify/", { factorId: setup.factorId, code: "bad" })).status, 400);
        const cancellation = await request("/account/mfa/remove/", { factorId: setup.factorId });
        assert.equal(cancellation.status, 200);
        assert.deepEqual((await cancellation.json()).data, { removed: true, signedOut: false });
        checks += 6;
        stage = "authenticator-confirmation-http";
        const secondSetup = await request("/account/mfa/enroll/", { password, friendlyName: "QA confirmed" });
        assert.equal(secondSetup.status, 200);
        const confirmedSetup = (await secondSetup.json()).data;
        const previousSessionCookie = `__Host-keire-session=${cookies.get("__Host-keire-session")}`;
        const confirmSetup = await request("/account/mfa/verify/", {
            factorId: confirmedSetup.factorId, code: syntheticAuthenticatorCode(confirmedSetup.secret),
        });
        assert.equal(confirmSetup.status, 200);
        assert.equal((await confirmSetup.json()).data.currentLevel, "aal2");
        assert.equal((await (await request("/account/session/", undefined, { cookie: previousSessionCookie })).json()).data.signedIn, false);
        assert.equal((await (await request("/account/session/")).json()).data.assuranceLevel, "aal2");
        checks += 5;
        const accountPage = await request("/account/");
        assert.equal(accountPage.status, 200);
        assert.equal(accountPage.headers.get("cache-control"), "private, no-store");
        const accountHtml = await accountPage.text();
        assert.ok(accountHtml.includes(email));
        assert.match(accountHtml, /No claimed assets yet/);
        assert.match(accountHtml, /Send verification email/);
        assert.match(accountHtml, /Create an organization/);
        stage = "organization-creation";
        assert.equal((await request("/account/data/")).status, 200);
        assert.equal((await request("/account/data/request/", { kind: "invalid" })).status, 400);
        for (const kind of ["export", "deletion"]) {
            assert.equal((await request("/account/data/request/", { kind })).status, 202);
            assert.equal((await request("/account/data/request/", { kind })).status, 202);
        }
        checks += 6;
        const privacyStatus = await (await request("/account/data/")).text();
        assert.match(privacyStatus, /Your export request is recorded/);
        assert.match(privacyStatus, /Your deletion request is recorded/);
        assert.equal((await request("/account/data/request/", { kind: "export" }, { origin: "https://other.invalid" })).status, 403);
        assert.equal((await request("/account/data/request/", { kind: "export" }, { authorization: "Bearer unsupported" })).status, 401);
        checks += 4;
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
        assert.ok((await exchange.json()).data.factors.some((factor) => factor.id === factorId));
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
        stage = "authenticator-removal-http";
        const finalVerification = await request("/account/mfa/challenge/verify/", {
            factorId: confirmedSetup.factorId, code: syntheticAuthenticatorCode(confirmedSetup.secret, 1),
        });
        assert.equal(finalVerification.status, 200);
        const removedSessionCookie = `__Host-keire-session=${cookies.get("__Host-keire-session")}`;
        const removal = await request("/account/mfa/remove/", { factorId: confirmedSetup.factorId });
        assert.equal(removal.status, 200);
        assert.deepEqual((await removal.json()).data, { removed: true, signedOut: true });
        assert.equal((await (await request("/account/session/", undefined, { cookie: removedSessionCookie })).json()).data.signedIn, false);
        assert.equal((await request("/account/mfa/enroll/", { password: nextPassword, friendlyName: "Cannot enroll" })).status, 401);
        checks += 5;
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
            console.error(`QA host did not stop gracefully within its deadline (${shutdownPhase}).`);
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
            const [registeredAccounts] = await pool.execute("SELECT id FROM platform_accounts WHERE email_key=?", [registrationEmail]);
            for (const { id } of registeredAccounts) {
                await pool.execute("DELETE FROM platform_auth_sessions WHERE user_id=?", [id]);
                await pool.execute("DELETE FROM platform_auth_challenges WHERE user_id=?", [id]);
                await pool.execute("DELETE FROM platform_audit_events WHERE actor_user_id=? AND action='account.registered'", [id]);
                await pool.execute("DELETE FROM platform_profiles WHERE user_id=?", [id]);
                await pool.execute("DELETE FROM platform_identities WHERE user_id=?", [id]);
                await pool.execute("DELETE FROM platform_accounts WHERE id=?", [id]);
            }
            for (const id of registrationRequestIds) {
                await pool.execute("DELETE FROM platform_registration_requests WHERE id=?", [id]);
            }
            const registrationLimits = [
                `email:registration:${registrationEmail}`, `client:registration:${clientIdentity}`,
                `pair:registration:${clientIdentity}\0registration:${registrationEmail}`,
                `client:registration-confirm:${clientIdentity}`,
                `email:${registrationEmail}`, `client:${clientIdentity}`, `pair:${clientIdentity}\0${registrationEmail}`,
            ];
            for (const registration of capturedRegistration) {
                const hash = createHash("sha256").update(registration.token).digest("hex");
                registrationLimits.push(`email:registration-confirm:${hash}`,
                    `pair:registration-confirm:${clientIdentity}\0registration-confirm:${hash}`);
            }
            for (const subject of registrationLimits) {
                await pool.execute("DELETE FROM platform_auth_limits WHERE subject_hash=?",
                    [createHash("sha256").update(subject).digest("hex")]);
            }
            const [remainingRegistrationAccounts] = await pool.execute("SELECT id FROM platform_accounts WHERE email_key=?", [registrationEmail]);
            assert.equal(remainingRegistrationAccounts.length, 0);
            await pool.execute("DELETE FROM platform_audit_events WHERE actor_user_id=? AND action IN ('account.authenticator_added','account.authenticator_removed','account.authenticator_setup_cancelled')", [userId]);
            for (const subject of [`email:mfa-enroll:${userId}`, `client:mfa-enroll:${clientIdentity}`,
                `pair:mfa-enroll:${clientIdentity}\0mfa-enroll:${userId}`]) {
                await pool.execute("DELETE FROM platform_auth_limits WHERE subject_hash=?", [createHash("sha256").update(subject).digest("hex")]);
            }
            await pool.execute("DELETE FROM platform_audit_events WHERE actor_user_id=? AND action IN ('account.export_requested','account.deletion_requested')", [userId]);
            await pool.execute("DELETE FROM profiles WHERE user_id=?", [userId]);
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
            await pool.execute(`DELETE counter FROM platform_mfa_counters counter
                JOIN platform_mfa_factors factor ON factor.id=counter.factor_id WHERE factor.user_id=?`, [userId]);
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
    spareConnection?.destroy();
    secrets?.accountEncryptionKey.fill(0);
    capturedMail.length = 0;
    await pool?.end();
}
if (!process.exitCode) {
    console.log(JSON.stringify({ status: interactive ? "interactive-browser-fixture-closed" : "rendered-http-authentication-passed", checks,
        fixturesRemoved: true, realAccountUsed: false, liveWebsiteChanged: false }));
}
