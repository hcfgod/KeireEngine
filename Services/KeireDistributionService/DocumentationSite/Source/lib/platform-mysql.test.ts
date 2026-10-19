import { describe, expect, it, vi } from "vitest";
import { platformPoolOptions, platformTransaction } from "./platform-mysql.mjs";

function fixture() {
    const connection = {
        beginTransaction: vi.fn().mockResolvedValue(undefined),
        commit: vi.fn().mockResolvedValue(undefined),
        rollback: vi.fn().mockResolvedValue(undefined),
        release: vi.fn(),
        destroy: vi.fn(),
    };
    return { connection, pool: { getConnection: vi.fn().mockResolvedValue(connection) } };
}

describe("platform transactions", () => {
    it("commits a successful operation and releases its connection", async () => {
        const { connection, pool } = fixture();
        await expect(platformTransaction(pool, async () => "result")).resolves.toBe("result");
        expect(connection.commit).toHaveBeenCalledOnce();
        expect(connection.rollback).not.toHaveBeenCalled();
        expect(connection.release).toHaveBeenCalledOnce();
    });

    it("rolls back callback failures without replacing the original error", async () => {
        const { connection, pool } = fixture();
        const failure = new Error("operation failed");
        await expect(platformTransaction(pool, async () => { throw failure; })).rejects.toBe(failure);
        expect(connection.commit).not.toHaveBeenCalled();
        expect(connection.rollback).toHaveBeenCalledOnce();
        expect(connection.release).toHaveBeenCalledOnce();
    });

    it("discards connections after rollback failure", async () => {
        const { connection, pool } = fixture();
        const failure = new Error("original");
        connection.rollback.mockRejectedValue(new Error("connection lost"));
        await expect(platformTransaction(pool, async () => { throw failure; })).rejects.toBe(failure);
        expect(connection.destroy).toHaveBeenCalledOnce();
        expect(connection.release).not.toHaveBeenCalled();
    });

    it("discards a connection when transaction startup fails", async () => {
        const { connection, pool } = fixture();
        connection.beginTransaction.mockRejectedValue(new Error("begin failed"));
        const action = vi.fn();
        await expect(platformTransaction(pool, action)).rejects.toThrow("begin failed");
        expect(action).not.toHaveBeenCalled();
        expect(connection.destroy).toHaveBeenCalledOnce();
    });

    it("never repeats an operation after an uncertain commit", async () => {
        const { connection, pool } = fixture();
        connection.commit.mockRejectedValue(new Error("commit connection lost"));
        const action = vi.fn().mockResolvedValue(undefined);
        await expect(platformTransaction(pool, action)).rejects.toThrow("commit connection lost");
        expect(action).toHaveBeenCalledOnce();
        expect(connection.rollback).toHaveBeenCalledOnce();
    });
});

describe("platform connection configuration", () => {
    const settings = {
        host: "127.0.0.1", port: 3306, database: "keire_platform",
        runtime: { user: "keire_platform_web", password: "x".repeat(96) },
    };

    it("enforces verified TLS and retains precise database values", () => {
        const options = platformPoolOptions(settings, "runtime", Buffer.from("test CA"));
        expect(options.ssl).toMatchObject({ rejectUnauthorized: true, verifyIdentity: true });
        expect(options.multipleStatements).toBe(false);
        expect(options.bigNumberStrings).toBe(true);
        expect(options.decimalNumbers).toBe(false);
        expect(options.dateStrings).toBe(true);
        expect(options.queueLimit).toBeGreaterThan(0);
    });

    it("rejects another application's database or administrator credentials", () => {
        expect(() => platformPoolOptions({ ...settings, database: "aether_platform" }, "runtime", Buffer.from("ca")))
            .toThrow("configuration");
        expect(() => platformPoolOptions({ ...settings, runtime: { ...settings.runtime, user: "root" } }, "runtime", Buffer.from("ca")))
            .toThrow("dedicated account");
    });
});
