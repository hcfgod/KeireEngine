import type { APIRoute } from "astro";
import { apiError, apiResponse, boundedString, MarketplaceApiError, requireAal2,
    requireSupabase, requireUser, throwEdgeFunctionError } from "../../../../../lib/api";
import { featureEnabled } from "../../../../../lib/marketplace";

export const prerender = false;

export const GET: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        requireUser(context);
        await requireAal2(context);
        if (!await featureEnabled(supabase, "publisher_portal_enabled")) {
            throw new MarketplaceApiError(503, "publisher.disabled", "Publisher uploads are not enabled.");
        }

        const uploadId = boundedString(context.params.id, "uploadId", 36, 36);
        const uploadResult = await supabase.from("marketplace_uploads")
            .select("id,version_id,state,validation_attempts,uploaded_at,created_at")
            .eq("id", uploadId).maybeSingle();
        if (uploadResult.error) {
            throw new Error(`Publisher upload status lookup failed (${uploadResult.error.code}).`);
        }
        if (!uploadResult.data) {
            throw new MarketplaceApiError(404, "publisher.upload_not_found", "The package upload was not found.");
        }

        const [versionResult, reportResult] = await Promise.all([
            supabase.from("marketplace_product_versions")
                .select("id,state").eq("id", uploadResult.data.version_id).maybeSingle(),
            supabase.from("marketplace_validation_reports")
                .select("passed,diagnostics,validator_version,completed_at")
                .eq("upload_id", uploadId).order("completed_at", { ascending: false }).limit(1).maybeSingle(),
        ]);
        if (versionResult.error) {
            throw new Error(`Publisher version status lookup failed (${versionResult.error.code}).`);
        }
        if (reportResult.error) {
            throw new Error(`Publisher validation status lookup failed (${reportResult.error.code}).`);
        }

        const diagnostics = Array.isArray(reportResult.data?.diagnostics) ? reportResult.data.diagnostics : [];
        return apiResponse(context, {
            data: {
                uploadId: uploadResult.data.id,
                state: uploadResult.data.state,
                validationAttempts: uploadResult.data.validation_attempts,
                versionId: uploadResult.data.version_id,
                versionState: versionResult.data?.state ?? null,
                validation: reportResult.data ? {
                    passed: reportResult.data.passed,
                    diagnosticCount: diagnostics.length,
                    validatorVersion: reportResult.data.validator_version,
                    completedAt: reportResult.data.completed_at,
                } : null,
            },
            meta: { apiVersion: "publisher/v1", correlationId: context.locals.correlationId },
        });
    } catch (error) {
        return apiError(context, error);
    }
};

export const DELETE: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        requireUser(context);
        await requireAal2(context);
        if (!await featureEnabled(supabase, "publisher_portal_enabled")) {
            throw new MarketplaceApiError(503, "publisher.disabled", "Publisher uploads are not enabled.");
        }
        const uploadId = boundedString(context.params.id, "uploadId", 36, 36);
        const transition = await supabase.functions.invoke("marketplace-publisher", {
            body: { operation: "upload.cancel", uploadId },
        });
        if (transition.error) await throwEdgeFunctionError(transition.error);
        return apiResponse(context, {
            data: { uploadId, state: "expired" },
            meta: { apiVersion: "publisher/v1", correlationId: context.locals.correlationId },
        });
    } catch (error) {
        return apiError(context, error);
    }
};
