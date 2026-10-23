import { randomBytes } from "node:crypto";

// Private setup subprocess: stdout must remain in the parent's protected-storage pipe.
try {
    const [siteOrigin, ...extra] = process.argv.slice(2);
    const origin = new URL(siteOrigin);
    if (extra.length || origin.protocol !== "https:" || origin.origin !== siteOrigin) {
        throw new Error("Invalid origin.");
    }
    process.stdout.write(JSON.stringify({
        schemaVersion: 1,
        siteOrigin,
        brokerSecret: randomBytes(32).toString("base64url"),
        publicationSecret: randomBytes(32).toString("base64url"),
        transferKey: randomBytes(32).toString("base64"),
    }));
} catch {
    process.stderr.write("Worker credential setup failed.\n");
    process.exitCode = 1;
}
