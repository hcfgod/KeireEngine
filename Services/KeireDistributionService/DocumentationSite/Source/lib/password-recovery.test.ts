import type { SupabaseClient } from "@supabase/supabase-js";
import { describe, expect, it, vi } from "vitest";
import {
    accountCallbackDestination,
    isPasswordRecoveryRequestAllowed,
    passwordRecoveryPath,
    type AssuranceState,
} from "./auth";
import { MarketplaceApiError } from "./api";
import { replaceRecoveredPassword } from "./password-recovery";

function assurance(methods: string[], currentLevel: AssuranceState["currentLevel"] = "aal1",
                   nextLevel: AssuranceState["nextLevel"] = "aal1"): AssuranceState {
    return { available: true, currentLevel, nextLevel, currentAuthenticationMethods: methods };
}

function authClient(updateError: unknown = null, signOutError: unknown = null) {
    const updateUser = vi.fn().mockResolvedValue({ data: {}, error: updateError });
    const signOut = vi.fn().mockResolvedValue({ error: signOutError });
    return {
        client: { auth: { updateUser, signOut } } as unknown as SupabaseClient,
        signOut,
        updateUser,
    };
}

describe("password recovery policy", () => {
    it("rejects password recovery and navigation when assurance cannot be verified", async () => {
        const client = authClient();
        const unavailable = { ...assurance([]), available: false };
        await expect(replaceRecoveredPassword(client.client, unavailable, "new-password-value"))
            .rejects.toMatchObject<Partial<MarketplaceApiError>>({
                code: "account.assurance_unavailable",
                status: 503,
            });
        expect(isPasswordRecoveryRequestAllowed(unavailable, "/account/")).toBe(false);
        expect(client.updateUser).not.toHaveBeenCalled();
    });

    it("rejects ordinary AAL1 and AAL2 sessions before changing the password", async () => {
        for (const state of [assurance([], "aal1", "aal1"), assurance(["password"], "aal2", "aal2")]) {
            const client = authClient();
            await expect(replaceRecoveredPassword(client.client, state, "new-password-value"))
                .rejects.toMatchObject<Partial<MarketplaceApiError>>({ code: "account.password_recovery_required" });
            expect(client.updateUser).not.toHaveBeenCalled();
        }
    });

    it("allows recovery AAL1 without an enrolled factor and closes the recovery session", async () => {
        const client = authClient();
        await replaceRecoveredPassword(client.client, assurance(["recovery"]), "new-password-value");
        expect(client.updateUser).toHaveBeenCalledWith({ password: "new-password-value" });
        expect(client.signOut).toHaveBeenCalledWith({ scope: "local" });
    });

    it("requires the enrolled second factor before a recovery password change", async () => {
        const client = authClient();
        await expect(replaceRecoveredPassword(
            client.client,
            assurance(["recovery"], "aal1", "aal2"),
            "new-password-value",
        )).rejects.toMatchObject<Partial<MarketplaceApiError>>({ code: "account.mfa_required" });
        expect(client.updateUser).not.toHaveBeenCalled();
    });

    it("allows recovery AAL2 and does not close the session when the update fails", async () => {
        const client = authClient({ code: "same_password" });
        await expect(replaceRecoveredPassword(
            client.client,
            assurance(["recovery", "totp"], "aal2", "aal2"),
            "new-password-value",
        )).rejects.toMatchObject<Partial<MarketplaceApiError>>({ code: "account.password_unchanged", status: 409 });
        expect(client.signOut).not.toHaveBeenCalled();
    });

    it("reports a changed password separately when the recovery session cannot be closed", async () => {
        const client = authClient(null, { code: "session_not_found" });
        await expect(replaceRecoveredPassword(client.client, assurance(["recovery"]), "new-password-value"))
            .rejects.toMatchObject<Partial<MarketplaceApiError>>({
                code: "account.password_session_close_failed",
                status: 503,
            });
        expect(client.updateUser).toHaveBeenCalledOnce();
    });

    it("keeps recovery callbacks and sessions inside the reset, MFA, and sign-out boundary", () => {
        const recovery = assurance(["recovery"]);
        expect(accountCallbackDestination(recovery, "/publisher/")).toBe(passwordRecoveryPath);
        expect(isPasswordRecoveryRequestAllowed(recovery, "/account/update-password/")).toBe(true);
        expect(isPasswordRecoveryRequestAllowed(recovery, "/account/mfa/challenge/verify/")).toBe(true);
        expect(isPasswordRecoveryRequestAllowed(recovery, "/account/session/delete/")).toBe(true);
        expect(isPasswordRecoveryRequestAllowed(recovery, "/account/")).toBe(false);
        expect(isPasswordRecoveryRequestAllowed(recovery, "/publisher/")).toBe(false);
    });
});
