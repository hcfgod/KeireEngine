import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { randomUUID } from "node:crypto";
import { access } from "node:fs/promises";
import path from "node:path";

export function isolatedEnvironment(environment = process.env) {
    // Never inherit production queue credentials or worker configuration.
    // Windows environment names are case-insensitive.
    return Object.fromEntries(Object.entries(environment).filter(([name]) => {
        const normalizedName = name.toUpperCase();
        return !normalizedName.startsWith("KEIRE_") && !normalizedName.startsWith("SUPABASE_");
    }));
}

export function validateWorkerProcessConfiguration(configuration) {
    for (const rawOrigin of [configuration.origin, configuration.publicationOrigin ?? configuration.origin]) {
        const origin = new URL(rawOrigin);
        assert.equal(origin.protocol, "https:");
        assert.equal(origin.hostname, "localhost");
        assert.equal(origin.pathname, "/");
        assert.equal(origin.username + origin.password + origin.search + origin.hash, "");
    }
    for (const name of ["dotnet", "brokerAssembly", "signerAssembly", "exchangeRoot",
        "validatorPublicKey", "publicationPublicKey"]) {
        assert.ok(path.isAbsolute(configuration[name] ?? ""), `${name} must be absolute.`);
    }
    assert.match(configuration.validatorFingerprint, /^[0-9a-f]{64}$/);
    for (const name of ["brokerSecret", "signerSecret"]) {
        assert.match(configuration[name], /^\S{32,256}$/);
    }
    assert.ok(typeof configuration.publicationPrivateKey === "string" &&
        configuration.publicationPrivateKey.length > 0, "An isolated signing identity is required.");
    assert.equal(configuration.isolatedFixture, true,
        "The caller must supply an isolated database fixture and localhost server.");
}

async function withinDeadline(operation, signal) {
    signal.throwIfAborted();
    let rejectOnAbort;
    const aborted = new Promise((resolve, reject) => {
        rejectOnAbort = () => reject(signal.reason);
        signal.addEventListener("abort", rejectOnAbort, { once: true });
    });
    try {
        return await Promise.race([operation(), aborted]);
    } finally {
        signal.removeEventListener("abort", rejectOnAbort);
    }
}

function startProcess(executable, assembly, environment) {
    const child = spawn(executable, [assembly], {
        windowsHide: true,
        stdio: ["ignore", "ignore", "ignore"],
        env: environment,
    });
    let failure;
    let finished = false;
    const completion = new Promise((resolve) => {
        child.once("error", (error) => {
            failure = error;
            finished = true;
            resolve();
        });
        child.once("exit", () => {
            finished = true;
            resolve();
        });
    });
    return {
        assertRunning() {
            assert.ok(!finished && !failure, "The isolated worker process exited before acceptance completed.");
        },
        async stop() {
            if (!finished) {
                child.kill();
            }
            await completion;
        },
    };
}

// Caller owns synthetic MySQL rows, storage and the guarded offline worker. This helper
// cannot seed reports: the broker's production loop must collect a fresh signed report.
export async function runMarketplaceWorkerProcesses(configuration, acceptance) {
    validateWorkerProcessConfiguration(configuration);
    for (const name of ["dotnet", "brokerAssembly", "signerAssembly", "exchangeRoot",
        "validatorPublicKey", "publicationPublicKey"]) {
        await access(configuration[name]);
    }
    const common = {
        ...isolatedEnvironment(),
        KEIRE_PLATFORM_BACKEND: "mysql",
        KEIRE_PLATFORM_URL: configuration.origin,
        KEIRE_VALIDATOR_ATTESTATION_PUBLIC_KEY: configuration.validatorPublicKey,
    };
    const broker = startProcess(configuration.dotnet, configuration.brokerAssembly, {
        ...common,
        KEIRE_VALIDATOR_BROKER_SECRET: configuration.brokerSecret,
        KEIRE_VALIDATOR_EXPECTED_FINGERPRINT_SHA256: configuration.validatorFingerprint,
        KEIRE_VALIDATOR_EXCHANGE_ROOT: configuration.exchangeRoot,
        KEIRE_VALIDATOR_WORKER_ID: `qa-broker-${randomUUID()}`,
        KEIRE_VALIDATOR_LEASE_SECONDS: "900",
        KEIRE_VALIDATOR_IDLE_POLL_MS: "250",
        KEIRE_VALIDATOR_REPORT_POLL_MS: "100",
        KEIRE_VALIDATOR_JOB_TIMEOUT_SECONDS: "600",
    });
    let signer;
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(new Error("Isolated worker acceptance timed out.")), 660000);
    const signal = controller.signal;
    try {
        const validation = await withinDeadline(() => acceptance.waitForValidation({ signal, assertRunning: broker.assertRunning }), signal);
        broker.assertRunning();
        await withinDeadline(() => acceptance.approveValidatedVersion(validation, { signal }), signal);
        signer = startProcess(configuration.dotnet, configuration.signerAssembly, {
            ...common,
            KEIRE_PLATFORM_URL: configuration.publicationOrigin ?? configuration.origin,
            KEIRE_MARKETPLACE_PUBLICATION_SIGNER_SECRET: configuration.signerSecret,
            KEIRE_MARKETPLACE_PUBLICATION_WORKER_ID: `qa-signer-${randomUUID()}`,
            KEIRE_MARKETPLACE_PUBLICATION_LEASE_SECONDS: "300",
            KEIRE_MARKETPLACE_PUBLICATION_IDLE_POLL_MS: "250",
            KEIRE_MARKETPLACE_PUBLICATION_PUBLIC_KEY: configuration.publicationPublicKey,
            KEIRE_MARKETPLACE_PUBLICATION_PRIVATE_KEY: configuration.publicationPrivateKey,
        });
        const publication = await withinDeadline(() => acceptance.waitForPublication({ signal, assertRunning: signer.assertRunning }), signal);
        signer.assertRunning();
        await withinDeadline(() => acceptance.verifyPublication(publication, { signal }), signal);
        return { status: "actual-broker-worker-signer-passed" };
    } finally {
        clearTimeout(timeout);
        controller.abort();
        await Promise.all([broker.stop(), signer?.stop()]);
    }
}
