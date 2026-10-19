import { readFile } from "node:fs/promises";
import mysql from "mysql2/promise";

export function platformPoolOptions(settings, role, certificateAuthority) {
    if (!settings || settings.host !== "127.0.0.1" || settings.port !== 3306 ||
        settings.database !== "keire_platform" || !["runtime", "migration"].includes(role)) {
        throw new Error("Invalid local platform database configuration.");
    }
    const account = settings[role];
    const expectedUser = role === "runtime" ? "keire_platform_web" : "keire_platform_migrate";
    if (!account || account.user !== expectedUser || typeof account.password !== "string" ||
        account.password.length < 32 || !Buffer.isBuffer(certificateAuthority) || certificateAuthority.length === 0) {
        throw new Error("The platform database requires a dedicated account and trusted CA.");
    }
    return {
        host: settings.host,
        port: settings.port,
        database: settings.database,
        user: account.user,
        password: account.password,
        ssl: { ca: certificateAuthority, rejectUnauthorized: true, verifyIdentity: true },
        charset: "utf8mb4_bin",
        timezone: "Z",
        dateStrings: true,
        supportBigNumbers: true,
        bigNumberStrings: true,
        decimalNumbers: false,
        multipleStatements: false,
        connectionLimit: 4,
        maxIdle: 2,
        idleTimeout: 30000,
        waitForConnections: true,
        queueLimit: 32,
        connectTimeout: 10000,
        enableKeepAlive: true,
    };
}

/** The application owns this pool and must await pool.end() during shutdown. Never log settings. */
export async function createPlatformPool(settings, role = "runtime") {
    if (typeof settings?.certificateAuthorityPath !== "string") {
        throw new Error("The platform database CA path is missing.");
    }
    const certificateAuthority = await readFile(settings.certificateAuthorityPath);
    return mysql.createPool(platformPoolOptions(settings, role, certificateAuthority));
}

/** A callback runs once: retrying uncertain commits could duplicate acquisitions or publication transitions. */
export async function platformTransaction(pool, action) {
    const connection = await pool.getConnection();
    let reusable = false;
    let began = false;
    try {
        await connection.beginTransaction();
        began = true;
        const result = await action(connection);
        await connection.commit();
        reusable = true;
        return result;
    } catch (error) {
        if (began) {
            try {
                await connection.rollback();
                reusable = true;
            } catch {
                // Preserve the original failure and discard a connection with uncertain transaction state.
            }
        }
        throw error;
    } finally {
        if (reusable) {
            connection.release();
        } else {
            connection.destroy();
        }
    }
}
