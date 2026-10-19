import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import { platformTransaction } from "../Source/lib/platform-mysql.mjs";
import { verifyMarketplaceRecords } from "../Source/lib/platform-marketplace-import.mjs";
import { marketplaceCoreTables } from "../Source/lib/platform-marketplace-schema.mjs";

export async function verifySupportConstraints(pool, records) {
    const rollback = new Error("intentional support acceptance rollback");
    let checks = 0;
    await assert.rejects(platformTransaction(pool, async (connection) => {
        const reject = async (sql, values = [], code = "ER_CHECK_CONSTRAINT_VIOLATED") => {
            await assert.rejects(connection.execute(sql, values), (error) => error.code === code);
            checks++;
        };
        const [users] = await connection.query("SELECT user_id FROM profiles LIMIT 1");
        assert.equal(users.length, 1);
        const userId = users[0].user_id;
        await reject("UPDATE profiles SET locale = 'invalid_locale' WHERE user_id = ?", [userId]);
        await reject("UPDATE profiles SET notification_preferences = JSON_ARRAY() WHERE user_id = ?", [userId]);
        await reject("UPDATE profiles SET handle = 'INVALID!' WHERE user_id = ?", [userId]);
        await connection.execute(`INSERT INTO platform_staff_members
            (user_id,role,active,appointed_by,appointed_at,updated_at,revoked_at)
            VALUES (?,'administrator',1,NULL,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6),NULL)
            ON DUPLICATE KEY UPDATE role = 'administrator',active = 1,revoked_at = NULL`, [userId]);
        await reject("UPDATE platform_staff_members SET role = 'unknown' WHERE user_id = ?", [userId]);
        await reject("UPDATE platform_staff_members SET active = 0 WHERE user_id = ?", [userId]);

        const productId = randomUUID();
        const productTable = marketplaceCoreTables.find((table) => table.name === "marketplace_products");
        const names = Object.keys(productTable.columns);
        const select = names.map((name) => ["id", "slug"].includes(name) ? "?" : `\`${name}\``);
        const [inserted] = await connection.execute(`INSERT INTO marketplace_products (${names.map((name) => `\`${name}\``).join(",")})
            SELECT ${select.join(",")} FROM marketplace_products LIMIT 1`, [productId, `qa-${productId}`]);
        assert.equal(inserted.affectedRows, 1);
        const reviewId = randomUUID();
        const reviewSql = `INSERT INTO marketplace_reviews
            (id,product_id,author_user_id,owner_organization_id,rating,title,body,publisher_reply,hidden_at,created_at,updated_at)
            VALUES (?,?,?,NULL,4,'Migration fixture','This is an isolated migration fixture.',NULL,NULL,UTC_TIMESTAMP(6),UTC_TIMESTAMP(6))`;
        await connection.execute(reviewSql, [reviewId, productId, userId]);
        await reject(reviewSql, [randomUUID(), productId, userId], "ER_DUP_ENTRY");
        await reject("UPDATE marketplace_reviews SET rating = 6 WHERE id = ?", [reviewId]);
        const reportId = randomUUID();
        await connection.execute(`INSERT INTO marketplace_reports
            (id,reporter_user_id,product_id,review_id,reason,details,state,assigned_to,resolution_note,created_at,resolved_at)
            VALUES (?,?,NULL,?,'other','Isolated migration fixture','open',NULL,NULL,UTC_TIMESTAMP(6),NULL)`,
        [reportId, userId, reviewId]);
        await reject("UPDATE marketplace_reports SET review_id = NULL WHERE id = ?", [reportId]);
        await reject("UPDATE marketplace_reports SET product_id = ? WHERE id = ?", [productId, reportId]);
        await connection.execute("DELETE FROM marketplace_reviews WHERE id = ?", [reviewId]);
        const [reports] = await connection.execute("SELECT COUNT(*) AS count FROM marketplace_reports WHERE id = ?", [reportId]);
        assert.equal(Number(reports[0].count), 0);
        checks++;

        const [audit] = await connection.execute(`INSERT INTO platform_audit_events
            (occurred_at,actor_user_id,actor_session_id,action,target_type,target_id,correlation_id,metadata)
            VALUES (UTC_TIMESTAMP(6),?,NULL,'qa.migration','fixture','isolated',?,JSON_OBJECT('fixture',true))`, [userId, randomUUID()]);
        await reject("UPDATE platform_audit_events SET metadata = JSON_ARRAY() WHERE id = ?", [audit.insertId]);
        const contactId = randomUUID();
        await connection.execute(`INSERT INTO website_contact_submissions
            (id,submitted_at,name,email,category,subject,message,status)
            VALUES (?,UTC_TIMESTAMP(6),'Migration QA','qa@example.invalid','support','Migration fixture','Isolated migration fixture only.','new')`, [contactId]);
        await reject("UPDATE website_contact_submissions SET status = 'sent' WHERE id = ?", [contactId]);
        await reject(`INSERT INTO marketplace_rate_limits (bucket,subject,window_started_at,request_count)
            VALUES ('qa.migration',?,UTC_TIMESTAMP(6),0)`, [randomUUID()]);
        await connection.execute("UPDATE profiles SET biography = 'Isolated migration rollback check' WHERE user_id = ?", [userId]);
        throw rollback;
    }), (error) => error === rollback);
    await verifyMarketplaceRecords(pool, records);
    return checks + 1;
}
