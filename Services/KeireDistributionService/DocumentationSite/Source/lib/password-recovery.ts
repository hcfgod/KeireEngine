import type { SupabaseClient } from "@supabase/supabase-js";
import { MarketplaceApiError } from "./api";
import { isPasswordRecoverySession, requiresMfaChallenge, type AssuranceState } from "./auth";

function passwordUpdateError(error: unknown): MarketplaceApiError {
    const code = error && typeof error === "object" && "code" in error
        ? (error as { code?: unknown }).code
        : null;
    switch (code) {
    case "weak_password":
        return new MarketplaceApiError(400, "account.password_too_weak",
            "Choose a stronger password that does not appear in compromised-password lists.");
    case "same_password":
        return new MarketplaceApiError(409, "account.password_unchanged",
            "Choose a password that is different from your current password.");
    case "reauthentication_needed":
    case "reauthentication_not_valid":
        return new MarketplaceApiError(403, "account.reauthentication_required",
            "Start a new password-recovery request before trying again.");
    default:
        return new MarketplaceApiError(400, "account.password_update_failed",
            "The password could not be updated. Start a new recovery request and try again.");
    }
}

export async function replaceRecoveredPassword(
    supabase: SupabaseClient,
    assurance: AssuranceState,
    password: string,
): Promise<void> {
    if (!assurance.available) {
        throw new MarketplaceApiError(503, "account.assurance_unavailable",
            "Password recovery could not be verified. Try again before choosing a new password.");
    }
    if (!isPasswordRecoverySession(assurance)) {
        throw new MarketplaceApiError(403, "account.password_recovery_required",
            "Start a password-recovery request before choosing a new password.");
    }
    if (requiresMfaChallenge(assurance)) {
        throw new MarketplaceApiError(403, "account.mfa_required",
            "Verify a second factor before choosing a new password.");
    }

    const update = await supabase.auth.updateUser({ password });
    if (update.error) throw passwordUpdateError(update.error);

    const signOut = await supabase.auth.signOut({ scope: "local" });
    if (signOut.error) {
        throw new MarketplaceApiError(503, "account.password_session_close_failed",
            "Your password was updated, but the recovery session could not be closed. Sign out before continuing.");
    }
}
