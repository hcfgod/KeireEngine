import assert from "node:assert/strict";
import { createHash, randomUUID } from "node:crypto";
import { enqueueRegistration, registrationLifetimeMs } from "../Source/lib/platform-registration.mjs";
import { openAccountRecord } from "../Source/lib/platform-account-import.mjs";
import { tokenDigest, verifyImportedPassword } from "../Source/lib/platform-auth-crypto.mjs";
import { processRegistrationDelivery } from "../Source/lib/platform-registration-delivery.mjs";

export async function verifyRegistrationStaging(pool, auth, now, existingEmail) {
    const email = `registration-qa-${randomUUID()}@invalid.example`;
    const clientIdentity = `qa:${randomUUID()}`;
    const ids = [];
    const authentication = {
        now: () => now,
        encryptionKey: auth.encryptionKey,
        reserveAttempt: (...args) => auth.reserveAttempt(...args),
        pool: {
            execute: async (sql, parameters) => {
                ids.push(parameters[0]);
                return pool.execute(sql, parameters);
            },
        },
    };
    const input = { email, displayName: "Registration QA", password: "synthetic-registration-password", accepted: true };
    let checks = 0;
    try {
        assert.deepEqual(await enqueueRegistration(authentication, { ...input, accepted: false }, clientIdentity),
            { state: "invalid_request" });
        assert.equal(ids.length, 0);
        checks += 2;
        const tokens = [];
        for (let index = 0; index < 2; index++) {
            assert.deepEqual(await enqueueRegistration(authentication, input, clientIdentity), { state: "accepted" });
            const [rows] = await pool.execute("SELECT * FROM platform_registration_requests WHERE id=?", [ids[index]]);
            assert.equal(rows.length, 1);
            const row = rows[0];
            const payload = openAccountRecord(row.payload_sealed, auth.encryptionKey, `registration:${row.id}`);
            assert.equal(payload.email, email);
            assert.equal(payload.displayName, input.displayName);
            assert.equal(await verifyImportedPassword(input.password, payload.passwordHash), true);
            assert.equal(tokenDigest(payload.token), row.token_hash);
            assert.equal(row.delivery_state, "queued");
            assert.equal(Number(row.used), 0);
            assert.equal(Number(row.expires_ms) - Number(row.created_ms), registrationLifetimeMs);
            assert.equal(row.payload_sealed.includes(Buffer.from(input.password)), false);
            tokens.push(payload.token);
            checks += 10;
        }
        assert.notEqual(tokens[0], tokens[1]);
        const [accounts] = await pool.execute("SELECT id FROM platform_accounts WHERE email_key=?", [email]);
        assert.equal(accounts.length, 0);
        checks += 2;
        const delivered = [];
        const deliveryAuthentication = { pool, encryptionKey: auth.encryptionKey, now: () => now };
        const mailer = { sendRegistration: async (message) => { delivered.push(message); } };
        const results = await Promise.all([
            processRegistrationDelivery(deliveryAuthentication, mailer, ids[0]),
            processRegistrationDelivery(deliveryAuthentication, mailer, ids[0]),
        ]);
        assert.equal(results.filter(Boolean).length, 1);
        assert.equal(delivered.length, 1);
        assert.equal(delivered[0].token, tokens[0]);
        assert.equal(delivered[0].recipient, email);
        const [sentRows] = await pool.execute("SELECT * FROM platform_registration_requests WHERE id=?", [ids[0]]);
        assert.equal(sentRows[0].delivery_state, "sent");
        const confirmation = openAccountRecord(sentRows[0].payload_sealed, auth.encryptionKey, `registration:${ids[0]}`);
        assert.equal(Object.hasOwn(confirmation, "token"), false);
        assert.equal(await verifyImportedPassword(input.password, confirmation.passwordHash), true);
        assert.equal(await processRegistrationDelivery(deliveryAuthentication, mailer, ids[0]), false);
        checks += 8;

        let uncertainSends = 0;
        const failingMailer = { sendRegistration: async () => {
            uncertainSends++;
            throw new Error("Synthetic uncertain delivery");
        } };
        assert.equal(await processRegistrationDelivery(deliveryAuthentication, failingMailer, ids[1]), true);
        assert.equal(await processRegistrationDelivery(deliveryAuthentication, failingMailer, ids[1]), false);
        assert.equal(uncertainSends, 1);
        const [failedRows] = await pool.execute("SELECT * FROM platform_registration_requests WHERE id=?", [ids[1]]);
        assert.equal(failedRows[0].delivery_state, "failed");
        assert.equal(failedRows[0].payload_sealed, null);
        assert.equal(Number(failedRows[0].used), 1);
        checks += 6;

        await enqueueRegistration(authentication, input, clientIdentity);
        const expiredAuthentication = { ...deliveryAuthentication, now: () => now + registrationLifetimeMs };
        assert.equal(await processRegistrationDelivery(expiredAuthentication, mailer, ids[2]), false);
        const [expiredRows] = await pool.execute("SELECT payload_sealed,used FROM platform_registration_requests WHERE id=?", [ids[2]]);
        assert.equal(expiredRows[0].payload_sealed, null);
        assert.equal(Number(expiredRows[0].used), 1);
        assert.equal(delivered.length, 1);
        checks += 4;

        await enqueueRegistration(authentication, { ...input, email: existingEmail }, clientIdentity);
        assert.equal(await processRegistrationDelivery(deliveryAuthentication, mailer, ids[3]), true);
        const [existingRows] = await pool.execute("SELECT payload_sealed,used FROM platform_registration_requests WHERE id=?", [ids[3]]);
        assert.equal(existingRows[0].payload_sealed, null);
        assert.equal(Number(existingRows[0].used), 1);
        assert.equal(delivered.length, 1);
        checks += 4;
        return checks;
    } finally {
        for (const id of ids) {
            await pool.execute("DELETE FROM platform_registration_requests WHERE id=?", [id]);
        }
        for (const subject of [
            `email:registration:${email}`,
            `email:registration:${existingEmail}`,
            `client:registration:${clientIdentity}`,
            `pair:registration:${clientIdentity}\0registration:${email}`,
            `pair:registration:${clientIdentity}\0registration:${existingEmail}`,
        ]) {
            const hash = createHash("sha256").update(subject).digest("hex");
            await pool.execute("DELETE FROM platform_auth_limits WHERE subject_hash=?", [hash]);
        }
    }
}
