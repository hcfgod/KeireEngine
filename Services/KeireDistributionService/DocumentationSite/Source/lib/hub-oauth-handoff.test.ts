import { readFileSync } from "node:fs";
import { runInNewContext } from "node:vm";
import { describe, expect, it } from "vitest";

const page = readFileSync(new URL("../pages/oauth/hub/callback.astro", import.meta.url), "utf8");
const script = /<script>([\s\S]*?)<\/script>/.exec(page)?.[1];

describe("Hub browser handoff", () => {
    it("reports only an attempt, including retries, without focus or timeout-based outcome claims", () => {
        expect(script).toBeDefined();
        class Element {
            textContent = "Your browser may ask for permission to open Kéire Hub.";
            dataset: Record<string, string> = {};
        }
        const status = new Element();
        const clickHandlers: Array<() => void> = [];
        const links = [0, 1].map(() => ({
            addEventListener(event: string, callback: () => void) {
                expect(event).toBe("click");
                clickHandlers.push(callback);
            },
        }));
        const unexpectedBrowserApi = () => {
            throw new Error("Browser focus and elapsed time cannot prove a native app handoff outcome.");
        };
        runInNewContext(script!, {
            HTMLElement: Element,
            document: {
                querySelectorAll: () => links,
                querySelector: () => status,
                addEventListener: unexpectedBrowserApi,
            },
            window: {
                addEventListener: unexpectedBrowserApi,
                setTimeout: unexpectedBrowserApi,
            },
        });
        expect(clickHandlers).toHaveLength(2);
        for (const click of clickHandlers) {
            click();
            expect(status.textContent).toContain("App link requested.");
            expect(status.textContent).toContain("This page cannot confirm the result.");
            expect(status.dataset.kind).toBeUndefined();
        }
    });

    it("keeps recovery optional and identifies the Hub as the source of sign-in status", () => {
        expect(page).toContain('<details class="hub-handoff__recovery"');
        expect(page).toContain("Need help opening Kéire Hub?");
        expect(page).toContain("Check your account status in the Hub");
        expect(page).toContain("if it shows you are signed in, you can close this tab");
        expect(page).not.toContain("No app handoff was detected");
        expect(page).not.toContain("navigator.clipboard");
    });
});
