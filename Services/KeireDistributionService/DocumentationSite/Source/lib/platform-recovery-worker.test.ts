import { afterEach, describe, expect, it, vi } from "vitest";
import { RecoveryDeliveryWorker } from "./platform-recovery-worker.mjs";

afterEach(() => vi.useRealTimers());

describe("recovery delivery worker ownership", () => {
    it("does not overlap delivery and drains it before shutdown", async () => {
        vi.useFakeTimers();
        let finish;
        const processNext = vi.fn(() => new Promise((resolve) => { finish = resolve; }));
        const worker = new RecoveryDeliveryWorker(processNext);
        worker.start();
        worker.start();
        await vi.advanceTimersByTimeAsync(0);
        await vi.advanceTimersByTimeAsync(10000);
        expect(processNext).toHaveBeenCalledTimes(1);
        let closed = false;
        const shutdown = worker.close().then(() => { closed = true; });
        await Promise.resolve();
        expect(closed).toBe(false);
        finish(true);
        await shutdown;
        await vi.advanceTimersByTimeAsync(10000);
        expect(processNext).toHaveBeenCalledTimes(1);
        await worker.close();
        expect(() => worker.start()).toThrow("closed");
    });

    it("backs off failures and cancels pending polls on close", async () => {
        vi.useFakeTimers();
        const processNext = vi.fn().mockRejectedValueOnce(new Error("private diagnostics")).mockResolvedValue(false);
        const worker = new RecoveryDeliveryWorker(processNext);
        worker.start();
        await vi.advanceTimersByTimeAsync(0);
        expect(worker.failed).toBe(true);
        await vi.advanceTimersByTimeAsync(4999);
        expect(processNext).toHaveBeenCalledTimes(1);
        await vi.advanceTimersByTimeAsync(1);
        expect(worker.failed).toBe(false);
        await worker.close();
        await vi.advanceTimersByTimeAsync(2000);
        expect(processNext).toHaveBeenCalledTimes(2);
    });
});
