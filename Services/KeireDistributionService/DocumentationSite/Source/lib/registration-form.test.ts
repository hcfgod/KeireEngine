import { readFileSync } from "node:fs";
import { stripTypeScriptTypes } from "node:module";
import { runInNewContext } from "node:vm";
import { describe, expect, it, vi } from "vitest";

function registrationForm(fetch: ReturnType<typeof vi.fn>) {
    const source = readFileSync(new URL("../pages/account/create/index.astro", import.meta.url), "utf8");
    const script = source.match(/<script>([\s\S]*?)<\/script>/)?.[1];
    if (!script) {
        throw new Error("Registration form script is missing");
    }
    const message = { dataset: {}, textContent: "" };
    const button = { disabled: false };
    let submit: (event: { preventDefault: () => void }) => Promise<void>;
    class TestForm {
        addEventListener(_name: string, listener: typeof submit) {
            submit = listener;
        }

        querySelector(selector: string) {
            return selector.includes("button") ? button : message;
        }
    }
    const form = new TestForm();
    runInNewContext(stripTypeScriptTypes(script), {
        document: { querySelector: () => form },
        HTMLFormElement: TestForm,
        FormData: class {
            get(name: string) {
                return name === "accepted" ? "on" : "synthetic-test-value";
            }
        },
        AbortSignal,
        fetch,
    });
    return { message, button, submit: () => submit({ preventDefault() {} }) };
}

describe("registration form recovery", () => {
    it("restores the submit button and explains uncertain outcomes after network failure", async () => {
        const form = registrationForm(vi.fn().mockRejectedValue(new Error("connection lost")));
        await form.submit();
        expect(form.button.disabled).toBe(false);
        expect(form.message.textContent).toContain("Check your inbox before trying again");
    });

    it("handles non-JSON proxy errors without leaving the form busy", async () => {
        const form = registrationForm(vi.fn().mockResolvedValue(new Response("<html>Unavailable</html>", { status: 503 })));
        await form.submit();
        expect(form.button.disabled).toBe(false);
        expect(form.message.textContent).toContain("could not confirm account creation");
    });

    it("prevents duplicate requests while awaiting the first response and displays server feedback", async () => {
        let resolveResponse: (response: Response) => void = () => {};
        const fetch = vi.fn().mockImplementation(() => new Promise<Response>((resolve) => {
            resolveResponse = resolve;
        }));
        const form = registrationForm(fetch);
        const pending = form.submit();
        await form.submit();
        expect(fetch).toHaveBeenCalledTimes(1);
        expect(form.button.disabled).toBe(true);
        resolveResponse(Response.json({ error: { message: "Please try again later." } }, { status: 503 }));
        await pending;
        expect(form.button.disabled).toBe(false);
        expect(form.message.textContent).toBe("Please try again later.");
    });
});
