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

export interface MarketplaceCatalog {
    status: "ready" | "disabled" | "unavailable";
    products: MarketplaceCard[];
    categories: { slug: string; display_name: string }[];
}

// Public browsing uses the anonymous client's existing RLS policies; claims still require authentication.
export async function loadMarketplaceCatalog(
    supabase: SupabaseClient | null, search = "", category = "",
): Promise<MarketplaceCatalog> {
    const empty = { products: [], categories: [] };
    if (!supabase) return { ...empty, status: "unavailable" };
    try {
        const flag = await supabase.from("platform_feature_flags").select("enabled")
            .eq("key", "marketplace_enabled").maybeSingle();
        if (flag.error || !flag.data) return { ...empty, status: "unavailable" };
        if (!flag.data.enabled) return { ...empty, status: "disabled" };
        let query = supabase.from("marketplace_catalog")
            .select("id,slug,display_name,short_description,license_spdx,featured,publisher_slug,publisher_name,publisher_verified,category_slug,category_name,rating_average,rating_count,published_at")
            .order("featured", { ascending: false }).order("published_at", { ascending: false }).limit(30);
        if (search) query = query.ilike("display_name", `%${search.replaceAll("%", "\\%").replaceAll("_", "\\_")}%`);
        if (category) query = query.eq("category_slug", category);
        const [products, categories] = await Promise.all([
            query,
            supabase.from("marketplace_categories").select("slug,display_name").eq("active", true).order("sort_order"),
        ]);
        if (products.error || categories.error) return { ...empty, status: "unavailable" };
        return { status: "ready", products: products.data ?? [], categories: categories.data ?? [] };
    } catch {
        return { ...empty, status: "unavailable" };
    }
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
