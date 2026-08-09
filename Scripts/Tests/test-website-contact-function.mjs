import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

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

const functionSource = await readFile(
    new URL("../../supabase/functions/website-contact/index.ts", import.meta.url),
    "utf8",
);
assert.match(functionSource, /Deno\.env\.get\("CONTACT_RATE_LIMIT_SECRET"\)/u);
assert.doesNotMatch(functionSource, /CONTACT_RATE_LIMIT_SECRET[^\n]*\?\?\s*adminKey/u);
assert.match(functionSource, /\.rpc\("submit_website_contact"/u);
assert.doesNotMatch(functionSource, /reserve_website_contact_submission/u);
assert.doesNotMatch(functionSource, /\.from\("website_contact_submissions"\)/u);

const migrationSource = await readFile(
    new URL(
        "../../supabase/migrations/20260809082811_submit_website_contact_atomically_and_schedule_cleanup.sql",
        import.meta.url,
    ),
    "utf8",
);
assert.match(migrationSource, /create function public\.submit_website_contact\(/u);
assert.match(migrationSource, /pg_advisory_xact_lock/u);
assert.match(migrationSource, /insert into public\.website_contact_rate_limits/u);
assert.match(migrationSource, /insert into public\.website_contact_submissions/u);
assert.match(migrationSource, /cron\.schedule\(/u);
const submitFunctionSource = migrationSource.slice(
    migrationSource.indexOf("create function public.submit_website_contact"),
    migrationSource.indexOf("revoke all on function public.submit_website_contact"),
);
assert.doesNotMatch(submitFunctionSource, /delete from public\.website_contact_rate_limits/u);

console.log("Website contact Edge Function request guards passed.");
