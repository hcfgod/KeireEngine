import { open, readFile, unlink } from "node:fs/promises";
import path from "node:path";
import { randomUUID } from "node:crypto";

/** No stale-lock guessing: an interrupted host requires operator inspection before removing this marker. */
export async function acquireProductionWriterLease(spoolDirectory) {
    const lockPath = path.join(spoolDirectory, ".keire-platform-writer.lock");
    const contents = JSON.stringify({ schemaVersion: 1, processId: process.pid, owner: randomUUID() });
    let handle;
    try {
        handle = await open(lockPath, "wx", 0o600);
        await handle.writeFile(contents, "utf8");
        await handle.sync();
    } catch {
        await handle?.close().catch(() => {});
        throw new Error("The upload spool is already owned or its writer marker is unavailable. Inspect the existing host before retrying.");
    }
    let released;
    return {
        release() {
            released ??= (async () => {
                try {
                    if (await readFile(lockPath, "utf8") !== contents) {
                        throw new Error("The production writer marker changed; refusing to remove it.");
                    }
                    // Windows cannot unlink an open marker reliably. Its private parent is writable only by the service identity.
                    await handle.close();
                    handle = null;
                    await unlink(lockPath);
                } finally {
                    await handle?.close();
                    handle = null;
                }
            })();
            return released;
        },
    };
}
