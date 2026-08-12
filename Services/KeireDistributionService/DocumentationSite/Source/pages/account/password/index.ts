import type { APIRoute } from "astro";
import { apiError, apiResponse, boundedString, parseJsonObject, requireSupabase, requireUser } from "../../../lib/api";
export const prerender = false;
export const PATCH: APIRoute = async (context) => { try { const supabase = requireSupabase(context); requireUser(context); const input = await parseJsonObject(context); const password = boundedString(input.password, "password", 10, 256); const { error } = await supabase.auth.updateUser({ password }); if (error) throw error; return apiResponse(context, { data: { updated: true } }); } catch (error) { return apiError(context, error); } };
