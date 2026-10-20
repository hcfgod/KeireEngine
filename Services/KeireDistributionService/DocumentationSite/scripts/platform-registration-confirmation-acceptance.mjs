import assert from "node:assert/strict";
import { createHash, randomUUID } from "node:crypto";
import { enqueueRegistration, registrationLifetimeMs } from "../Source/lib/platform-registration.mjs";
import { processRegistrationDelivery } from "../Source/lib/platform-registration-delivery.mjs";
import { confirmRegistration } from "../Source/lib/platform-registration-confirmation.mjs";
import { openAccountRecord } from "../Source/lib/platform-account-import.mjs";
import { verifyImportedPassword } from "../Source/lib/platform-auth-crypto.mjs";

export async function verifyRegistrationConfirmation(pool, auth, now) {
    const email = `confirmation-qa-${randomUUID()}@invalid.example`;
    const client = `qa:${randomUUID()}`;
    const ids = [];
    const limitSubjects = new Set();
    const authentication = {
        encryptionKey: auth.encryptionKey,
        now: () => now,
        pool,
        reserveAttempt: async (subject, identity, time) => {
            limitSubjects.add(`email:${subject}`);
            limitSubjects.add(`client:${identity}`);
            limitSubjects.add(`pair:${identity}\0${subject}`);
            return auth.reserveAttempt(subject, identity, time);
        },
    };
    const input = { email, displayName: "Confirmation QA", password: "synthetic-original-password", accepted: true };
    const signup = { ...authentication, pool: { execute: async (sql, values) => {
        ids.push(values[0]);
        return pool.execute(sql, values);
    } } };
    const tokens = [];
    let checks = 0;
    try {
        for (let index = 0; index < 4; index++) {
            await enqueueRegistration(signup, input, client);
            const [rows] = await pool.execute("SELECT payload_sealed FROM platform_registration_requests WHERE id=?", [ids[index]]);
            const pending = openAccountRecord(rows[0].payload_sealed, auth.encryptionKey, `registration:${ids[index]}`);
            tokens.push(pending.token);
            assert.equal((await confirmRegistration(authentication, pending.token, input.password, client)).state, "invalid_confirmation");
            await processRegistrationDelivery(authentication, { sendRegistration: async () => {} }, ids[index]);
            checks++;
        }
        assert.equal((await confirmRegistration(authentication, tokens[0], "incorrect-password", client)).state, "invalid_confirmation");
        checks++;
        const failingPool = { getConnection: async () => {
            const connection = await pool.getConnection();
            return {
                beginTransaction: () => connection.beginTransaction(),
                commit: () => connection.commit(),
                rollback: () => connection.rollback(),
                release: () => connection.release(),
                destroy: () => connection.destroy(),
                execute: (sql, values) => {
                    if (sql.includes("INSERT INTO platform_profiles")) {
                        throw new Error("Synthetic profile failure");
                    }
                    return connection.execute(sql, values);
                },
            };
        } };
        await assert.rejects(confirmRegistration({ ...authentication, pool: failingPool }, tokens[0], input.password, client),
            /Synthetic profile failure/);
        const [rolledBack] = await pool.execute("SELECT id FROM platform_accounts WHERE email_key=?", [email]);
        assert.equal(rolledBack.length, 0);
        checks += 2;
        const results = await Promise.all([
            confirmRegistration(authentication, tokens[0], input.password, client),
            confirmRegistration(authentication, tokens[0], input.password, client),
        ]);
        assert.equal(results.filter((result) => result.state === "confirmed").length, 1);
        assert.equal((await confirmRegistration(authentication, tokens[0], input.password, client)).state, "invalid_confirmation");
        assert.equal((await confirmRegistration(authentication, tokens[1], input.password, client)).state, "invalid_confirmation");
        const [accounts] = await pool.execute("SELECT * FROM platform_accounts WHERE email_key=?", [email]);
        assert.equal(accounts.length, 1);
        assert.ok(accounts[0].email_confirmed_at);
        assert.equal(await verifyImportedPassword(input.password, accounts[0].password_hash), true);
        const userId = accounts[0].id;
        const [profiles] = await pool.execute("SELECT display_name FROM platform_profiles WHERE user_id=?", [userId]);
        assert.equal(profiles[0].display_name, input.displayName);
        const [identities] = await pool.execute("SELECT provider FROM platform_identities WHERE user_id=?", [userId]);
        assert.deepEqual(identities.map((row) => row.provider), ["email"]);
        const [sessions] = await pool.execute("SELECT token_hash FROM platform_auth_sessions WHERE user_id=?", [userId]);
        assert.equal(sessions.length, 0);
        const [consumed] = await pool.execute("SELECT payload_sealed,used FROM platform_registration_requests WHERE id=?", [ids[0]]);
        assert.equal(consumed[0].payload_sealed, null);
        assert.equal(Number(consumed[0].used), 1);
        checks += 11;
        const expired = { ...authentication, now: () => now + registrationLifetimeMs };
        assert.equal((await confirmRegistration(expired, tokens[2], input.password, client)).state, "invalid_confirmation");
        for (let index = 0; index < 5; index++) {
            assert.equal((await confirmRegistration(authentication, tokens[3], "incorrect-password", client)).state, "invalid_confirmation");
        }
        assert.equal((await confirmRegistration(authentication, tokens[3], input.password, client)).state, "invalid_confirmation");
        const [exhausted] = await pool.execute("SELECT attempts,payload_sealed FROM platform_registration_requests WHERE id=?", [ids[3]]);
        assert.equal(Number(exhausted[0].attempts), 5);
        assert.equal(exhausted[0].payload_sealed, null);
        checks += 9;
        for (const subject of [`email:${email}`, `client:${client}`, `pair:${client}\0${email}`]) {
            limitSubjects.add(subject);
        }
        const login = await auth.signIn(email, input.password, client);
        assert.equal(login.state, "authenticated");
        assert.equal((await auth.session(login.token)).userId, userId);
        checks += 2;
        return checks;
    } finally {
        const [accounts] = await pool.execute("SELECT id FROM platform_accounts WHERE email_key=?", [email]);
        for (const { id } of accounts) {
            await pool.execute("DELETE FROM platform_auth_sessions WHERE user_id=?", [id]);
            await pool.execute("DELETE FROM platform_auth_challenges WHERE user_id=?", [id]);
            await pool.execute("DELETE FROM platform_audit_events WHERE actor_user_id=? AND action='account.registered'", [id]);
            await pool.execute("DELETE FROM platform_identities WHERE user_id=?", [id]);
            await pool.execute("DELETE FROM platform_profiles WHERE user_id=?", [id]);
            await pool.execute("DELETE FROM platform_accounts WHERE id=?", [id]);
        }
        for (const id of ids) {
            await pool.execute("DELETE FROM platform_registration_requests WHERE id=?", [id]);
        }
        for (const subject of limitSubjects) {
            await pool.execute("DELETE FROM platform_auth_limits WHERE subject_hash=?",
                [createHash("sha256").update(subject).digest("hex")]);
        }
    }
}
