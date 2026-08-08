import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const source = fs.readFileSync(
    path.join(root, "Services", "KeireDistributionService", "Website", "assets", "contact.js"),
    "utf8",
);

class FakeElement {
    constructor() {
        this.dataset = {};
        this.textContent = "";
    }
}

class FakeForm extends FakeElement {
    constructor() {
        super();
        this.fields = new Map([
            ["company", ""],
            ["name", "Website Test"],
            ["email", "website@example.invalid"],
            ["category", "feedback"],
            ["subject", "Contact frontend test"],
            ["message", "This verifies the contact form request payload."],
        ]);
        this.resetCount = 0;
    }

    addEventListener(name, listener) {
        assert.equal(name, "submit");
        this.listener = listener;
    }

    reportValidity() {
        return true;
    }

    reset() {
        ++this.resetCount;
    }
}

class FakeButton extends FakeElement {
    constructor() {
        super();
        this.attributes = new Map();
        this.disabled = false;
    }

    setAttribute(name, value) {
        this.attributes.set(name, value);
    }

    removeAttribute(name) {
        this.attributes.delete(name);
    }
}

const form = new FakeForm();
const status = new FakeElement();
const submit = new FakeButton();
const requests = [];
let response = { ok: true, json: async () => ({ message: "Thanks. Your message was received." }) };
const context = vm.createContext({
    console,
    Error,
    HTMLElement: FakeElement,
    HTMLFormElement: FakeForm,
    HTMLButtonElement: FakeButton,
    FormData: class FormData {
        constructor(target) {
            this.fields = target.fields;
        }

        get(name) {
            return this.fields.get(name);
        }
    },
    document: {
        querySelector(selector) {
            if (selector === "[data-contact-form]") return form;
            if (selector === "[data-contact-status]") return status;
            if (selector === "[data-contact-submit]") return submit;
            return null;
        },
    },
    fetch: async (url, options) => {
        requests.push({ url, options });
        return response;
    },
});
vm.runInContext(source, context, { filename: "contact.js" });

let prevented = false;
await form.listener({ preventDefault() { prevented = true; } });
assert.equal(prevented, true);
assert.equal(requests.length, 1);
assert.equal(requests[0].url, "https://khjduyjamzwumhducmou.supabase.co/functions/v1/website-contact");
assert.equal(requests[0].options.method, "POST");
assert.equal(JSON.parse(requests[0].options.body).category, "feedback");
assert.equal(status.dataset.state, "success");
assert.equal(form.resetCount, 1);
assert.equal(submit.disabled, false);
assert.equal(submit.attributes.has("aria-busy"), false);

response = { ok: false, json: async () => ({ message: "Please wait before sending another message." }) };
await form.listener({ preventDefault() {} });
assert.equal(status.dataset.state, "error");
assert.equal(status.textContent, "Please wait before sending another message.");
assert.equal(form.resetCount, 1);

console.log("Website contact form behavior passed.");
