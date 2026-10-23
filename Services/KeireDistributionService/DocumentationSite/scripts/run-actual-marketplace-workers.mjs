import assert from "node:assert/strict";
import { createHash, generateKeyPairSync, randomUUID } from "node:crypto";
import { readFile, writeFile, mkdir, lstat } from "node:fs/promises";
import { Readable } from "node:stream";
import path from "node:path";
import { createPlatformPool } from "../Source/lib/platform-mysql.mjs";
import { LocalBlobStore } from "../Source/lib/local-blob-store.ts";
import { verifyActualMarketplaceWorkers } from "./platform-actual-worker-acceptance.mjs";

const options = {};
for (let index = 2; index < process.argv.length; index += 2) {
    const name = process.argv[index];
    assert.ok(["--worker-config", "--result", "--package", "--dotnet", "--broker", "--signer"].includes(name));
    assert.ok(!Object.hasOwn(options, name));
    const value = process.argv[index + 1];
    assert.ok(path.isAbsolute(value ?? ""));
    options[name] = value;
}
for (const name of ["--worker-config", "--result", "--package", "--dotnet", "--broker", "--signer"]) {
    assert.ok(options[name], `Missing ${name}.`);
}
let pool;
let privateBytes;
let stage = "configuration";
try {
    const worker = JSON.parse(await readFile(options["--worker-config"], "utf8"));
    assert.equal(worker.schemaVersion, 1);
    assert.equal(worker.isolatedFixture, true);
    const resultDirectory = path.dirname(options["--result"]);
    const fixtureRoot = path.join(resultDirectory, `pipeline-fixture-${randomUUID()}`);
    await mkdir(fixtureRoot, { mode: 0o700 });
    // The enclosing PowerShell runner applies private Windows ACLs before this process starts.
    const information = await lstat(options["--package"]);
    assert.ok(information.isFile() && !information.isSymbolicLink() && information.size > 0 && information.size < 64 * 1024 * 1024);
    const packageBytes = await readFile(options["--package"]);
    const packageIdentity = { sha256: createHash("sha256").update(packageBytes).digest("hex"), sizeBytes: packageBytes.length };
    const store = new LocalBlobStore(path.join(fixtureRoot, "blobs"));
    await store.put(packageIdentity, Readable.from([packageBytes]));
    const signing = generateKeyPairSync("ed25519");
    privateBytes = signing.privateKey.export({ type: "pkcs8", format: "der" });
    const publicBytes = signing.publicKey.export({ type: "spki", format: "der" }).subarray(-32);
    const fingerprint = createHash("sha256").update(publicBytes).digest("hex");
    const publicDocument = { schemaVersion: 1, algorithm: "Ed25519", keyId: `ed25519-${fingerprint.slice(0, 32)}`,
        publicKey: publicBytes.toString("base64"), fingerprint: `sha256:${fingerprint}` };
    const publicationPublicKey = path.join(fixtureRoot, "publication-public.json");
    await writeFile(publicationPublicKey, JSON.stringify(publicDocument), { flag: "wx", mode: 0o600 });
    process.stdin.setEncoding("utf8");
    let input = "";
    for await (const chunk of process.stdin) {
        input += chunk;
        assert.ok(input.length <= 65536, "Protected settings input exceeded its limit.");
    }
    const settings = JSON.parse(input);
    input = "";
    stage = "database";
    // Acceptance creates connection-private tables; the runtime account deliberately cannot.
    pool = await createPlatformPool(settings, "migration");
    stage = "worker-pipeline";
    const result = await verifyActualMarketplaceWorkers(pool, randomUUID(), randomUUID(), store, {
        ...worker,
        dotnet: options["--dotnet"], brokerAssembly: options["--broker"], signerAssembly: options["--signer"],
        publicationPublicKey, publicationPrivateKey: privateBytes.toString("base64"), packageIdentity,
    });
    await writeFile(options["--result"], JSON.stringify({ status: "passed", acceptance: result.status,
        packageIdentity, managedCompilation: true, syntheticTemporaryTables: true }), { flag: "wx", mode: 0o600 });
    console.log(JSON.stringify({ status: "passed" }));
} catch (error) {
    const failure = { status: "failed", stage: /^[a-z-]{1,80}$/.test(error.fixtureStage ?? "") ? error.fixtureStage : stage };
    if (/^ER_[A-Z0-9_]{1,80}$/.test(error.code ?? "")) {
        failure.databaseCode = error.code;
    }
    await writeFile(options["--result"], JSON.stringify(failure), { flag: "wx", mode: 0o600 }).catch(() => {});
    console.error(JSON.stringify(failure));
    process.exitCode = 1;
} finally {
    privateBytes?.fill(0);
    await pool?.end();
}
