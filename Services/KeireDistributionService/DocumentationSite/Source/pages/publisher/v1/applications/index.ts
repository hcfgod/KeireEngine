import type { APIRoute } from "astro";
import { apiError, apiResponse, boundedString, MarketplaceApiError, parseJsonObject, requireAal2,
    requireSupabase, requireUser, throwEdgeFunctionError } from "../../../../lib/api";
import { featureEnabled } from "../../../../lib/marketplace";

export const prerender = false;

function optionalHttpsUrl(value: unknown, name: string): string | null {
    if (value == null || value === "") return null;
    const text = boundedString(value, name, 8, 2048).trim();
    try {
        const url = new URL(text);
        if (url.protocol !== "https:" || url.username || url.password) throw new Error("unsafe URL");
        return url.toString();
    } catch {
        throw new MarketplaceApiError(400, "publisher.url_invalid", `${name} must be a public HTTPS URL.`);
    }
}

export const POST: APIRoute = async (context) => {
    try {
        const supabase = requireSupabase(context);
        const user = requireUser(context);
        await requireAal2(context);
        if (!await featureEnabled(supabase, "publisher_portal_enabled")) {
            throw new MarketplaceApiError(503, "publisher.disabled", "Publisher applications are not enabled.");
        }
        const input = await parseJsonObject(context);
        const action = input.action === "submit" ? "submit" : "save";
        const applicationId = input.applicationId == null || input.applicationId === ""
            ? null
            : boundedString(input.applicationId, "applicationId", 36, 36);
        const organizationId = boundedString(input.organizationId, "organizationId", 36, 36);
        const values = {
            applicant_user_id: user.id,
            organization_id: organizationId,
            public_name: boundedString(input.publicName, "publicName", 2, 96).trim(),
            website_url: optionalHttpsUrl(input.websiteUrl, "websiteUrl"),
            portfolio_url: optionalHttpsUrl(input.portfolioUrl, "portfolioUrl"),
            statement: boundedString(input.statement, "statement", 20, 5000).trim(),
            state: "draft" as const,
            updated_at: new Date().toISOString(),
        };
        const result = applicationId
            ? await supabase.from("publisher_applications").update(values).eq("id", applicationId)
                .eq("applicant_user_id", user.id).in("state", ["draft", "changes_requested"])
                .select("id,state").single()
            : await supabase.from("publisher_applications").insert(values).select("id,state").single();
        if (result.error || !result.data) {
            throw new MarketplaceApiError(409, "publisher.application_not_editable",
                "The publisher application could not be saved in its current state.");
        }
        if (action === "submit") {
            const transition = await supabase.functions.invoke("marketplace-publisher", {
                body: { operation: "application.submit", applicationId: result.data.id },
            });
            if (transition.error) await throwEdgeFunctionError(transition.error);
            return apiResponse(context, {
                data: { applicationId: result.data.id, state: "submitted" },
                meta: { apiVersion: "publisher/v1", correlationId: context.locals.correlationId },
            });
        }
        return apiResponse(context, {
            data: { applicationId: result.data.id, state: "draft" },
            meta: { apiVersion: "publisher/v1", correlationId: context.locals.correlationId },
        });
    } catch (error) {
        return apiError(context, error);
    }
};
