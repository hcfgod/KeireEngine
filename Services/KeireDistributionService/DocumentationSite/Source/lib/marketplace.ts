import type { SupabaseClient } from "@supabase/supabase-js";

export interface MarketplaceCard {
    id: string;
    slug: string;
    display_name: string;
    short_description: string;
    license_spdx: string;
    featured: boolean;
    publisher_slug: string;
    publisher_name: string;
    publisher_verified: boolean;
    category_slug: string;
    category_name: string;
    rating_average: number;
    rating_count: number;
}

export async function featureEnabled(supabase: SupabaseClient | null, key: string): Promise<boolean> {
    if (!supabase) {
        return false;
    }
    const { data, error } = await supabase
        .from("platform_feature_flags")
        .select("enabled")
        .eq("key", key)
        .maybeSingle();
    return !error && data?.enabled === true;
}

export async function marketplaceEnabled(supabase: SupabaseClient | null): Promise<boolean> {
    return featureEnabled(supabase, "marketplace_enabled");
}

export async function loadMarketplaceCards(supabase: SupabaseClient | null, limit = 6): Promise<MarketplaceCard[]> {
    if (!supabase || !await marketplaceEnabled(supabase)) {
        return [];
    }
    const { data, error } = await supabase
        .from("marketplace_catalog")
        .select("id,slug,display_name,short_description,license_spdx,featured,publisher_slug,publisher_name,publisher_verified,category_slug,category_name,rating_average,rating_count")
        .order("featured", { ascending: false })
        .order("published_at", { ascending: false })
        .limit(limit);
    if (error) {
        console.error(JSON.stringify({ level: "error", event: "marketplace.catalog_load_failed", error: error.message }));
        return [];
    }
    return (data ?? []) as MarketplaceCard[];
}
