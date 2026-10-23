/// <reference path="../.astro/types.d.ts" />
/// <reference types="astro/client" />

import type { SupabaseClient, User } from "@supabase/supabase-js";
import type { AssuranceState } from "./lib/auth";

declare namespace App {
    interface Locals {
        correlationId: string;
        assurance: AssuranceState;
        supabase: SupabaseClient | null;
        user: User | null;
        platformBackend?: import("./lib/platform-backend.mjs").PlatformBackend;
        platformClientIdentity?: string;
        platformVerifiedOrigin?: string;
        localEmailAvailable?: boolean;
        localNativeConsent?: {
            authorization_id: string;
            client: { name: string };
            redirect_uri: string;
            scope: string;
        };
        localMarketplaceContext?: {
            pool: Awaited<ReturnType<typeof import("./lib/platform-mysql.mjs").createPlatformPool>>;
            session: { userId: string; assurance: number } | null;
        };
        localProductView?: Awaited<ReturnType<typeof import("./lib/platform-product.mjs").localProductPage>>;
        localCatalogPool?: Awaited<ReturnType<typeof import("./lib/platform-mysql.mjs").createPlatformPool>>;
        localAccountView?: Awaited<ReturnType<typeof import("./lib/platform-account-view.mjs").localAccountOverview>> | null;
        localRecovery?: boolean;
        localRegistration?: boolean;
        localPublisherView?: Awaited<ReturnType<typeof import("./lib/platform-publisher-portal.mjs").localPublisherPortal>>;
        localAdminView?: Awaited<ReturnType<typeof import("./lib/platform-admin-view.mjs").localAdminOverview>>;
        pendingMfaChallenge: {
            factors: Array<{ id: string; friendly_name: string | null; factor_type?: string }>;
            expiresAt: number;
        } | null;
    }
}

interface ImportMetaEnv {
    readonly PUBLIC_SUPABASE_URL?: string;
    readonly PUBLIC_SUPABASE_PUBLISHABLE_KEY?: string;
    readonly PUBLIC_SITE_URL?: string;
    readonly KEIRE_DISTRIBUTION_HEALTH_URL?: string;
    readonly KEIRE_VALIDATOR_HEALTH_URL?: string;
}

interface ImportMeta {
    readonly env: ImportMetaEnv;
}
