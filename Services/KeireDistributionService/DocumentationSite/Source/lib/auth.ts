import type { SupabaseClient } from "@supabase/supabase-js";
import { runtimeEnvironment } from "./runtime-env";

export type AssuranceLevel = "aal1" | "aal2" | null;

export interface AssuranceState {
    available: boolean;
    currentLevel: AssuranceLevel;
    nextLevel: AssuranceLevel;
    currentAuthenticationMethods: string[];
}

export type OAuthFailureReason = "cancelled" | "code_missing" | "exchange_failed" | "unavailable";
export const passwordRecoveryPath = "/account/update-password/";

const recoverySessionPaths = new Set([
    "/account/update-password",
    "/account/password",
    "/account/mfa/challenge",
    "/account/mfa/challenge/verify",
    "/account/session/delete",
]);

function authenticationMethods(value: unknown): string[] {
    if (!Array.isArray(value)) return [];
    return [...new Set(value.flatMap((entry) => {
        const method = typeof entry === "string"
            ? entry
            : entry && typeof entry === "object" && "method" in entry
                ? (entry as { method?: unknown }).method
                : null;
        return typeof method === "string" && /^[a-z0-9_-]{1,64}$/i.test(method) ? [method.toLowerCase()] : [];
    }))];
}

export function safeLocalPath(value: string | null | undefined, fallback = "/account/"): string {
    if (!value?.startsWith("/") || value.startsWith("//") || value.includes("\\") || /[\u0000-\u001f\u007f]/.test(value)) {
        return fallback;
    }
    try {
        const expectedOrigin = "https://keire.invalid";
        const resolved = new URL(value, expectedOrigin);
        return resolved.origin === expectedOrigin
            ? `${resolved.pathname}${resolved.search}${resolved.hash}`
            : fallback;
    } catch {
        return fallback;
    }
}

export async function getAssuranceState(supabase: SupabaseClient, jwt?: string): Promise<AssuranceState> {
    const { data, error } = await supabase.auth.mfa.getAuthenticatorAssuranceLevel(jwt);
    if (error || !data) {
        return { available: false, currentLevel: null, nextLevel: null, currentAuthenticationMethods: [] };
    }
    return {
        available: true,
        currentLevel: data.currentLevel ?? null,
        nextLevel: data.nextLevel ?? null,
        currentAuthenticationMethods: authenticationMethods(data.currentAuthenticationMethods),
    };
}

export function requiresMfaChallenge(state: AssuranceState): boolean {
    return state.nextLevel === "aal2" && state.currentLevel !== "aal2";
}

export function isPasswordRecoverySession(state: AssuranceState): boolean {
    return state.currentAuthenticationMethods.includes("recovery");
}

export function isPasswordRecoveryRequestAllowed(state: AssuranceState, pathname: string): boolean {
    const normalized = pathname.length > 1 ? pathname.replace(/\/+$/, "") : pathname;
    return state.available && (!isPasswordRecoverySession(state) || recoverySessionPaths.has(normalized));
}

export function accountCallbackDestination(state: AssuranceState, requestedPath: string): string {
    return isPasswordRecoverySession(state) ? passwordRecoveryPath : safeLocalPath(requestedPath);
}

export function mfaChallengePath(next: string): string {
    return `/account/mfa/challenge/?next=${encodeURIComponent(safeLocalPath(next))}`;
}

export function oauthFailurePath(reason: OAuthFailureReason, next: string, correlationId: string): string {
    const parameters = new URLSearchParams({
        oauth_error: reason,
        next: safeLocalPath(next),
    });
    if (/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(correlationId)) {
        parameters.set("reference", correlationId);
    }
    return `/account/sign-in/?${parameters.toString()}`;
}

export function externalUrl(path: string): URL {
    const configured = runtimeEnvironment("PUBLIC_SITE_URL");
    if (!configured) {
        throw new Error("PUBLIC_SITE_URL is not configured.");
    }
    const origin = new URL(configured);
    if (origin.protocol !== "https:" || origin.username || origin.password || origin.pathname !== "/" ||
        origin.search || origin.hash) {
        throw new Error("PUBLIC_SITE_URL must be an HTTPS origin.");
    }
    return new URL(path, origin);
}
