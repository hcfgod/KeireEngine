import assert from "node:assert/strict";
import { test } from "vitest";
import { isolatedEnvironment, validateWorkerProcessConfiguration } from "./qa-marketplace-worker-processes.mjs";

test("removes inherited provider configuration regardless of Windows name casing", () => {
    const environment = {
        Path: "C:/tools",
        TEMP: "C:/temp",
        KEIRE_PLATFORM_URL: "https://production.example",
        keire_validator_broker_secret: "private-placeholder",
        Supabase_Service_Role_Key: "private-placeholder",
    };
    assert.deepEqual(isolatedEnvironment(environment), { Path: "C:/tools", TEMP: "C:/temp" });
    assert.equal(Object.keys(environment).length, 5);
});

function configuration() {
    return {
        origin: "https://localhost:34567/",
        dotnet: "C:/qa/dotnet.exe",
        brokerAssembly: "C:/qa/broker.dll",
        signerAssembly: "C:/qa/signer.dll",
        exchangeRoot: "C:/qa/exchange",
        validatorPublicKey: "C:/qa/validator.json",
        publicationPublicKey: "C:/qa/publication.json",
        validatorFingerprint: "a".repeat(64),
        brokerSecret: "b".repeat(64),
        signerSecret: "s".repeat(64),
        publicationPrivateKey: "test-placeholder-not-a-key",
        isolatedFixture: true,
    };
}

test("accepts isolated localhost process configuration", () => {
    assert.doesNotThrow(() => validateWorkerProcessConfiguration(configuration()));
});

for (const origin of ["https://example.com/", "http://localhost/", "https://localhost/path",
    "https://user:password@localhost/", "https://localhost/?secret=value", "https://localhost/#x"]) {
    test(`rejects non-isolated origin ${origin}`, () => {
        assert.throws(() => validateWorkerProcessConfiguration({ ...configuration(), origin }));
    });
}

for (const change of [{ publicationOrigin: "https://example.com/" }, { isolatedFixture: false }, { brokerSecret: "short" },
    { signerSecret: "contains whitespace ".repeat(4) }, { validatorFingerprint: "bad" },
    { exchangeRoot: "relative" }, { publicationPrivateKey: "" }]) {
    test(`rejects invalid ${Object.keys(change)[0]}`, () => {
        assert.throws(() => validateWorkerProcessConfiguration({ ...configuration(), ...change }));
    });
}
