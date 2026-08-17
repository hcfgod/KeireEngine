//#region Source/lib/marketplace.ts
async function featureEnabled(supabase, key) {
	if (!supabase) return false;
	const { data, error } = await supabase.from("platform_feature_flags").select("enabled").eq("key", key).maybeSingle();
	return !error && data?.enabled === true;
}
async function marketplaceEnabled(supabase) {
	return featureEnabled(supabase, "marketplace_enabled");
}
//#endregion
export { marketplaceEnabled as n, featureEnabled as t };
