import assert from "node:assert/strict";

/** Grant staff access only to this run's disposable account and remove it before returning. */
export async function verifyAdminHttp({ pool, userId, request, setStage }) {
    const [[account]] = await pool.execute("SELECT email_key FROM platform_accounts WHERE id=?", [userId]);
    assert.equal(account?.email_key, `http-qa-${userId}@invalid.example`);
    const [[existing]] = await pool.execute("SELECT COUNT(*) AS total FROM platform_staff_members WHERE user_id=?", [userId]);
    assert.equal(Number(existing.total), 0);
    setStage("admin-nonstaff-denied");
    const denied = await request("/admin/marketplace/");
    assert.equal(denied.status, 404);
    await denied.arrayBuffer();
    try {
        await pool.execute(`INSERT INTO platform_staff_members
            (user_id,role,active,appointed_by,appointed_at,updated_at)
            VALUES (?,'moderator',TRUE,?,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`, [userId, userId]);
        setStage("admin-rendered");
        const page = await request("/admin/marketplace/");
        assert.equal(page.status, 200);
        assert.match(page.headers.get("cache-control"), /no-store/);
        assert.equal(page.headers.get("referrer-policy"), "no-referrer");
        assert.match(await page.text(), /Operations center/);
        setStage("admin-revoked");
        await pool.execute("UPDATE platform_staff_members SET active=FALSE,revoked_at=UTC_TIMESTAMP(6) WHERE user_id=?", [userId]);
        const revoked = await request("/admin/marketplace/");
        assert.equal(revoked.status, 404);
        await revoked.arrayBuffer();
        return 6;
    } finally {
        await pool.execute("DELETE FROM platform_staff_members WHERE user_id=?", [userId]);
    }
}
