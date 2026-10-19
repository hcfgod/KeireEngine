/** Owns one delivery operation at a time. Close before disposing its borrowed mailer, authentication, or pool. */
export class RecoveryDeliveryWorker {
    constructor(processNext) {
        if (typeof processNext !== "function") {
            throw new Error("Recovery delivery requires a bounded worker operation.");
        }
        this.processNext = processNext;
        this.started = false;
        this.closed = false;
        this.timer = null;
        this.active = null;
        this.failed = false;
    }

    start() {
        if (this.closed) {
            throw new Error("Recovery delivery worker is closed.");
        }
        if (!this.started) {
            this.started = true;
            this.schedule(0);
        }
    }

    schedule(delay) {
        if (!this.closed) {
            this.timer = setTimeout(() => {
                this.timer = null;
                this.active = this.poll();
            }, delay);
            this.timer.unref?.();
        }
    }

    async poll() {
        let delay = 1000;
        try {
            const handled = await this.processNext();
            this.failed = false;
            delay = handled ? 0 : 1000;
        } catch {
            // Never log credential-bearing SMTP/database diagnostics or retry the claimed job here.
            this.failed = true;
            delay = 5000;
        } finally {
            this.active = null;
            this.schedule(delay);
        }
    }

    async close() {
        this.closed = true;
        if (this.timer !== null) {
            clearTimeout(this.timer);
            this.timer = null;
        }
        await this.active;
    }
}
