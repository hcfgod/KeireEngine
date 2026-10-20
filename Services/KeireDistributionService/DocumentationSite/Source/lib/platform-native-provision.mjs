import { nativeAuthorizationSchema } from "./platform-native-authorization.mjs";
import { nativeSessionSchema } from "./platform-native-session-schema.mjs";

/** Initial provisioning only. Existing or partially provisioned schemas require explicit inspection. */
export async function createNativeSchema(pool) {
    const schemas = [nativeAuthorizationSchema, ...nativeSessionSchema];
    const names = schemas.map((sql) => /^CREATE TABLE (\w+)/.exec(sql)[1]);
    const [existing] = await pool.execute(`SELECT TABLE_NAME FROM information_schema.TABLES
        WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN (?,?,?,?)`, names);
    if (existing.length !== 0) {
        throw new Error("Native authentication tables already exist; inspect the schema before provisioning.");
    }
    // MySQL DDL commits implicitly. Never drop existing tables or disguise partial creation as success.
    for (const sql of schemas) {
        await pool.execute(sql);
    }
    return names;
}
