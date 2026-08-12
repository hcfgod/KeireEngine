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
