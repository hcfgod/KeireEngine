import { readFileSync } from "node:fs";
import { stripTypeScriptTypes } from "node:module";
import { runInNewContext } from "node:vm";
import { describe, expect, it, vi } from "vitest";

function signInForm(fetch: ReturnType<typeof vi.fn>) {
    const source = readFileSync(new URL("../pages/account/sign-in.astro", import.meta.url), "utf8");
    const script = source.match(/<script>([\s\S]*?)<\/script>/)?.[1];
    if (!script) {
        throw new Error("Sign-in script is missing");
    }
    const message = { dataset: {}, textContent: "" };
    const button = { disabled: false };
    const location = { href: "" };
    let submit: (event: { preventDefault: () => void }) => Promise<void>;
    class TestForm {
        dataset = { next: "/account/data/" };

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
            get() {
                return "synthetic-test-value";
            }
        },
        AbortSignal,
        fetch,
        location,
    });
    return { message, button, location, submit: () => submit({ preventDefault() {} }) };
}

describe("sign-in form recovery", () => {
    it.each(["network", "non-json"])("allows retry after %s failure without redirecting", async (failure) => {
        const fetch = failure === "network"
            ? vi.fn().mockRejectedValue(new Error("connection lost"))
            : vi.fn().mockResolvedValue(new Response("<html>Unavailable</html>", { status: 503 }));
        const form = signInForm(fetch);
        await form.submit();
        expect(form.button.disabled).toBe(false);
        expect(form.message.textContent).toContain("could not confirm sign-in");
        expect(form.location.href).toBe("");
        await form.submit();
        expect(fetch).toHaveBeenCalledTimes(2);
    });

    it("prevents duplicate submissions and restores controls after server rejection", async () => {
        let resolveResponse: (response: Response) => void = () => {};
        const fetch = vi.fn().mockImplementation(() => new Promise<Response>((resolve) => {
            resolveResponse = resolve;
        }));
        const form = signInForm(fetch);
        const pending = form.submit();
        await form.submit();
        expect(fetch).toHaveBeenCalledTimes(1);
        expect(form.button.disabled).toBe(true);
        expect(fetch.mock.calls[0][1].signal).toBeInstanceOf(AbortSignal);
        resolveResponse(Response.json({ error: { message: "Try again later." } }, { status: 429 }));
        await pending;
        expect(form.button.disabled).toBe(false);
        expect(form.message.textContent).toBe("Try again later.");
        expect(form.location.href).toBe("");
    });

    it.each([false, true])("preserves the destination with MFA required=%s", async (mfaRequired) => {
        const form = signInForm(vi.fn().mockResolvedValue(Response.json({ data: { mfaRequired } })));
        await form.submit();
        expect(form.location.href).toBe(mfaRequired
            ? "/account/mfa/challenge/?next=%2Faccount%2Fdata%2F"
            : "/account/data/");
    });
});
