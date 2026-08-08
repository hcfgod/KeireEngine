import assert from "node:assert/strict";

import {
    ContactRequestError,
    maximumRequestBytes,
    readBoundedJson,
    requestAddress,
} from "../../supabase/functions/website-contact/request.mjs";

const validPayload = JSON.stringify({ message: "bounded" });
const validRequest = new Request("https://example.invalid", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: validPayload,
});
assert.deepEqual(await readBoundedJson(validRequest), { message: "bounded" });

const oversizedHeader = new Request("https://example.invalid", {
    method: "POST",
    headers: { "content-length": String(maximumRequestBytes + 1) },
    body: "{}",
});
await assert.rejects(() => readBoundedJson(oversizedHeader), (error) => {
    assert.ok(error instanceof ContactRequestError);
    assert.equal(error.status, 413);
    return true;
});

const chunk = new Uint8Array(maximumRequestBytes + 1);
const chunkedRequest = new Request("https://example.invalid", {
    method: "POST",
    body: new ReadableStream({
        start(controller) {
            controller.enqueue(chunk);
            controller.close();
        },
    }),
    duplex: "half",
});
await assert.rejects(() => readBoundedJson(chunkedRequest), (error) => {
    assert.ok(error instanceof ContactRequestError);
    assert.equal(error.status, 413);
    return true;
});

const spoofedForwarding = new Request("https://example.invalid", {
    headers: {
        "cf-connecting-ip": "203.0.113.42",
        "x-forwarded-for": "198.51.100.10",
        "x-real-ip": "192.0.2.5",
    },
});
assert.equal(requestAddress(spoofedForwarding), "203.0.113.42");
assert.equal(requestAddress(new Request("https://example.invalid")), "unknown");

console.log("Website contact Edge Function request guards passed.");
