import { describe, expect, it, vi } from "vitest";
import { createVerificationMailer } from "./platform-mail.mjs";

const settings = {
    schemaVersion: 1, provider: "gmail", host: "smtp.gmail.com", port: 465, secure: true,
    username: "sender@example.com", fromAddress: "sender@example.com", password: "abcdefghijklmnop",
};

function fixture() {
    const transport = {
        verify: vi.fn().mockResolvedValue(true),
        sendMail: vi.fn().mockResolvedValue({ accepted: ["user@example.com"], rejected: [] }),
        close: vi.fn(),
    };
    const factory = vi.fn().mockReturnValue(transport);
    return { mailer: createVerificationMailer(settings, factory), transport, factory };
}

describe("verification mail delivery", () => {
    it("sends registration links only to the configured origin and keeps tokens out of the query", async () => {
        const { transport } = fixture();
        const mailer = createVerificationMailer(settings, () => transport, "https://example.test");
        const token = "a".repeat(43);
        await mailer.sendRegistration({ recipient: "user@example.com", token });
        const message = transport.sendMail.mock.calls[0][0];
        expect(message.text).toContain(`https://example.test/account/confirm/#registration=${token}`);
        expect(message.text).toContain("password you chose");
        expect(message.text).toContain("No account has been created");
        expect(message.envelope.to).toEqual(["user@example.com"]);
        transport.sendMail.mockRejectedValue(new Error("private SMTP response"));
        await expect(mailer.sendRegistration({ recipient: "user@example.com", token }))
            .rejects.toThrow("The registration email could not be sent.");
        expect(transport.sendMail).toHaveBeenCalledTimes(2);
    });

    it("rejects registration messages without an origin or with malformed recipients or tokens", async () => {
        const { mailer, transport } = fixture();
        await expect(mailer.sendRegistration({ recipient: "user@example.com", token: "a".repeat(43) }))
            .rejects.toThrow("Invalid registration");
        const configured = createVerificationMailer(settings, () => transport, "https://example.test");
        for (const request of [
            { recipient: "user@example.com\r\nBcc: other@example.com", token: "a".repeat(43) },
            { recipient: "user@example.com", token: "invalid" },
        ]) {
            await expect(configured.sendRegistration(request)).rejects.toThrow("Invalid registration");
        }
        expect(transport.sendMail).not.toHaveBeenCalled();
    });

    it("sends recovery tokens only in the configured HTTPS page fragment", async () => {
        const { transport } = fixture();
        const mailer = createVerificationMailer(settings, () => transport, "https://example.test");
        const token = "a".repeat(43);
        await mailer.sendRecovery({ recipient: "user@example.com", token });
        const message = transport.sendMail.mock.calls[0][0];
        expect(message.text).toContain(`https://example.test/account/update-password/#recovery=${token}`);
        expect(message.text).toContain("authenticator");
        expect(message.subject).toBe("Reset your Kéire password");
        expect(message.envelope.to).toEqual(["user@example.com"]);
    });

    it("rejects unconfigured recovery, redirected origins, and malformed recovery tokens", async () => {
        const { mailer, transport } = fixture();
        await expect(mailer.sendRecovery({ recipient: "user@example.com", token: "a".repeat(43) })).rejects.toThrow("Invalid recovery");
        expect(transport.sendMail).not.toHaveBeenCalled();
        for (const origin of ["http://example.test", "https://example.test/path", "https://user@example.test", "https://example.test#fragment"]) {
            expect(() => createVerificationMailer(settings, () => transport, origin)).toThrow();
        }
        const configured = createVerificationMailer(settings, () => transport, "https://example.test");
        await expect(configured.sendRecovery({ recipient: "user@example.com", token: "bad\r\ntoken" })).rejects.toThrow("Invalid recovery");
        transport.sendMail.mockRejectedValue(new Error("private SMTP details"));
        await expect(configured.sendRecovery({ recipient: "user@example.com", token: "a".repeat(43) }))
            .rejects.toThrow("The recovery email could not be sent.");
        expect(transport.sendMail).toHaveBeenCalledTimes(1);
    });

    it("requires TLS and disables logging and external message content", async () => {
        const { mailer, transport, factory } = fixture();
        expect(factory).toHaveBeenCalledWith(expect.objectContaining({
            secure: true, logger: false, debug: false, disableFileAccess: true, disableUrlAccess: true,
            tls: expect.objectContaining({ rejectUnauthorized: true, minVersion: "TLSv1.2" }),
        }));
        await mailer.verify();
        expect(transport.sendMail).not.toHaveBeenCalled();
        await mailer.sendVerification({ recipient: "user@example.com", code: "00123456", purpose: "enroll" });
        expect(transport.sendMail).toHaveBeenCalledWith(expect.objectContaining({
            envelope: { from: settings.fromAddress, to: ["user@example.com"] },
            text: expect.stringContaining("enable email verification"),
        }));
        expect(transport.sendMail.mock.calls[0][0].text).toContain("00123456");
    });

    it.each([
        { recipient: "user@example.com\r\nBcc: attacker@example.com", code: "12345678", purpose: "sign_in" },
        { recipient: "user@example.com,attacker@example.com", code: "12345678", purpose: "sign_in" },
        { recipient: "user@example.com", code: "123456", purpose: "sign_in" },
        { recipient: "user@example.com", code: "12345678", purpose: "toString" },
    ])("rejects malformed message input before contacting SMTP", async (request) => {
        const { mailer, transport } = fixture();
        await expect(mailer.sendVerification(request)).rejects.toThrow("Invalid verification email request.");
        expect(transport.sendMail).not.toHaveBeenCalled();
    });

    it("suppresses sensitive SMTP errors and does not automatically retry", async () => {
        const { mailer, transport } = fixture();
        transport.sendMail.mockRejectedValue(new Error("private password or recipient"));
        await expect(mailer.sendVerification({ recipient: "user@example.com", code: "12345678", purpose: "sign_in" }))
            .rejects.toThrow("The verification email could not be sent. Please try again later.");
        expect(transport.sendMail).toHaveBeenCalledTimes(1);
        transport.verify.mockRejectedValue(new Error("private credentials"));
        await expect(mailer.verify()).rejects.toThrow("Email delivery credentials or connection could not be verified.");
    });

    it("treats recipient rejection as failure", async () => {
        const { mailer, transport } = fixture();
        transport.sendMail.mockResolvedValue({ accepted: [], rejected: ["user@example.com"] });
        await expect(mailer.sendVerification({ recipient: "user@example.com", code: "12345678", purpose: "sign_in" }))
            .rejects.toThrow("could not be sent");
    });

    it("closes idempotently and prevents reuse", async () => {
        const { mailer, transport } = fixture();
        mailer.close();
        mailer.close();
        expect(transport.close).toHaveBeenCalledTimes(1);
        await expect(mailer.verify()).rejects.toThrow("closed");
        await expect(mailer.sendVerification({ recipient: "user@example.com", code: "12345678", purpose: "sign_in" }))
            .rejects.toThrow("closed");
    });

    it("rejects insecure or redirected credentials before transport construction", () => {
        const factory = vi.fn();
        for (const replacement of [{ host: "evil.example" }, { secure: false }, { fromAddress: "other@example.com" }]) {
            expect(() => createVerificationMailer({ ...settings, ...replacement }, factory)).toThrow("invalid");
        }
        expect(factory).not.toHaveBeenCalled();
    });
});
