/** session is produced by LocalPasswordAuthentication, never deserialized from a request body. */
export async function localAccountOverview(pool, session) {
    if (!session || !/^[0-9a-f-]{36}$/.test(session.userId) || ![1, 2].includes(session.assurance)) {
        throw new Error("An authenticated local session is required.");
    }
    const userId = session.userId;
    const [accounts] = await pool.execute(`SELECT id,email FROM platform_accounts
        WHERE id=? AND deleted_at IS NULL AND email_confirmed_at IS NOT NULL
            AND (banned_until IS NULL OR banned_until <= UTC_TIMESTAMP(6))`, [userId]);
    if (accounts.length !== 1) {
        throw new Error("The local account is unavailable.");
    }
    const [[profiles], [entitlements], [organizations], [sessions], [identities], [staff], [factors], [flags]] = await Promise.all([
        pool.execute("SELECT display_name,handle,avatar_url,biography,locale,export_requested_at,deletion_requested_at FROM profiles WHERE user_id=?", [userId]),
        pool.execute(`SELECT e.id,e.product_id,e.organization_id,e.granted_at,p.display_name,p.slug,publisher.slug AS publisher_slug
            FROM marketplace_entitlements e JOIN marketplace_products p ON p.id=e.product_id
            JOIN publishers publisher ON publisher.id=p.publisher_id
            WHERE e.revoked_at IS NULL AND (e.user_id=? OR EXISTS (
                SELECT 1 FROM organization_memberships m JOIN organizations o ON o.id=m.organization_id
                WHERE m.organization_id=e.organization_id AND m.user_id=? AND o.deleted_at IS NULL))
            ORDER BY e.granted_at DESC,e.id LIMIT 12`, [userId, userId]),
        pool.execute(`SELECT m.role,o.id,o.slug,o.display_name FROM organization_memberships m
            JOIN organizations o ON o.id=m.organization_id WHERE m.user_id=? AND o.deleted_at IS NULL
            ORDER BY o.display_name,o.id`, [userId]),
        pool.execute(`SELECT id,client_type,device_name,created_at,last_used_at,revoked_at
            FROM oauth_device_sessions WHERE user_id=? ORDER BY last_used_at DESC,id LIMIT 20`, [userId]),
        pool.execute(`SELECT provider,JSON_UNQUOTE(JSON_EXTRACT(identity_data,'$.email')) AS email
            FROM platform_identities WHERE user_id=? ORDER BY provider,id`, [userId]),
        pool.execute("SELECT role FROM platform_staff_members WHERE user_id=? AND active=TRUE AND revoked_at IS NULL", [userId]),
        pool.execute("SELECT id,factor_type,status,friendly_name FROM platform_mfa_factors WHERE user_id=? ORDER BY id", [userId]),
        pool.execute("SELECT enabled FROM platform_feature_flags WHERE `key` IN ('marketplace_enabled','publisher_portal_enabled')"),
    ]);
    const utc = (value) => value === null ? null : value.replace(" ", "T") + "Z";
    return {
        user: { ...accounts[0], identities: identities.map((identity) => ({
            provider: identity.provider, identity_data: { email: identity.email },
        })) },
        profile: profiles[0] ?? null,
        entitlements: entitlements.map((item) => ({ ...item, granted_at: utc(item.granted_at),
            marketplace_products: { display_name: item.display_name, slug: item.slug,
                publishers: { slug: item.publisher_slug } } })),
        organizations: organizations.map((item) => ({ role: item.role,
            organizations: { id: item.id, slug: item.slug, display_name: item.display_name } })),
        sessions: sessions.map((item) => ({ ...item, created_at: utc(item.created_at),
            last_used_at: utc(item.last_used_at), revoked_at: utc(item.revoked_at) })),
        staffRole: staff[0]?.role ?? null,
        authenticatorFactors: factors.filter((factor) => factor.factor_type === "totp"),
        hasVerifiedFactors: factors.some((factor) => factor.status === "verified"),
        emailVerificationEnabled: factors.some((factor) => factor.factor_type === "email" && factor.status === "verified"),
        organizationCreationEnabled: session.assurance === 2 && flags.some((flag) => Boolean(flag.enabled)),
    };
}
