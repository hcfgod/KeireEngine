import type { SupabaseClient } from "@supabase/supabase-js";

export interface AssetOffer { amount_minor: number; currency: string; }

export function purchaseState(offer: AssetOffer | null, owned = false, available = true) {
    if (owned) return { state: "owned", label: "In My Assets", message: "This asset is already in your personal library.", canClaim: false };
    if (!available || !offer) return { state: "unavailable", label: "Currently unavailable", message: "This asset is not available to acquire right now. Please check back later.", canClaim: false };
    if (!Number.isSafeInteger(offer.amount_minor) || offer.amount_minor < 0 || !/^[A-Z]{3}$/.test(offer.currency))
        return { state: "unavailable", label: "Currently unavailable", message: "Pricing is being updated. Please check back later.", canClaim: false };
    if (offer.amount_minor > 0) return { state: "purchase_unavailable", label: "Purchasing coming soon", message: "This is a paid asset. Purchasing is not available yet. You have not been charged.", canClaim: false };
    return { state: "free", label: "Get asset", message: "Add this free asset to your personal library, then open it in the Editor.", canClaim: true };
}

export function offerPrice(offer: AssetOffer | null): string {
    if (!offer || purchaseState(offer).state === "unavailable") return "Currently unavailable";
    if (offer.amount_minor === 0) return "Free";
    try {
        const formatter = new Intl.NumberFormat("en", { style: "currency", currency: offer.currency });
        const digits = formatter.resolvedOptions().maximumFractionDigits ?? 2;
        return formatter.format(offer.amount_minor / 10 ** digits);
    } catch { return "Paid asset"; }
}

export async function loadOffer(supabase: SupabaseClient, productId: string): Promise<AssetOffer | null> {
    const { data, error } = await supabase.from("marketplace_offers")
        .select("amount_minor,currency").eq("product_id", productId).eq("active", true).maybeSingle();
    if (error) throw error;
    return data;
}
