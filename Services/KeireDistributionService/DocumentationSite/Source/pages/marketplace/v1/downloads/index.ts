import type { APIRoute } from "astro";
import { apiError, apiResponse, boundedString, MarketplaceApiError, parseJsonObject, requireSupabase, requireUser,
    throwEdgeFunctionError } from "../../../../lib/api";
import { featureEnabled } from "../../../../lib/marketplace";

export const prerender = false;

export const POST: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        requireUser(context);
        if (!await featureEnabled(supabase, "marketplace_enabled") || !await featureEnabled(supabase, "asset_packages_enabled")) {
            throw new MarketplaceApiError(503, "marketplace.asset_packages_disabled", "Marketplace package downloads are not enabled.");
        }
        const input = await parseJsonObject(context, 8 * 1024);
        const versionId = boundedString(input.versionId, "versionId", 36, 36);
        const deviceSessionId = boundedString(input.deviceSessionId, "deviceSessionId", 36, 36);
        const organizationId = input.organizationId == null ? null : boundedString(input.organizationId, "organizationId", 36, 36);
        const { data: edgeData, error } = await supabase.functions.invoke("marketplace-hub", { body: {
            operation: "download.issue", versionId, deviceSessionId, organizationId,
        } });
        const data = edgeData?.data;
        if (error) {
            await throwEdgeFunctionError(error);
        }
        if (!data) throw new MarketplaceApiError(500, "marketplace.download_grant_failed", "A download grant was not created.");
        const signed = await supabase.storage.from("marketplace-releases").createSignedUrl(data.storage_path, 600, {
            download: `keire-${versionId}.keireassetpackage`,
        });
        if (signed.error || !signed.data?.signedUrl) throw new MarketplaceApiError(503, "marketplace.download_unavailable", "The verified package is temporarily unavailable.");
        return apiResponse(context, {
            data: {
                grantId: data.grant_id,
                url: signed.data.signedUrl,
                expiresAt: data.expires_at,
                archiveSha256: data.archive_sha256,
                archiveSizeBytes: data.archive_size_bytes,
                signedPublication: data.signed_publication,
            },
            meta: { apiVersion: "marketplace/v1", correlationId: context.locals.correlationId },
        }, 201);
    } catch (error) { return apiError(context, error); }
};
