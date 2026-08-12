import type { APIRoute } from "astro";
import { apiError, apiResponse, boundedString, parseJsonObject, requireSupabase, requireUser } from "../../../../lib/api";

export const prerender = false;
export const POST: APIRoute = async (context) => {
    try {
        requireUser(context);
        const input = await parseJsonObject(context, 4096);
        const friendlyName = boundedString(input.friendlyName, "friendlyName", 3, 64).trim();
        const { data, error } = await requireSupabase(context).auth.mfa.enroll({ factorType: "totp", friendlyName });
        if (error || !data?.totp) throw error ?? new Error("TOTP enrollment returned no setup data.");
        return apiResponse(context, { data: { factorId: data.id, qrCode: data.totp.qr_code, secret: data.totp.secret } });
    } catch (error) {
        return apiError(context, error);
    }
};
