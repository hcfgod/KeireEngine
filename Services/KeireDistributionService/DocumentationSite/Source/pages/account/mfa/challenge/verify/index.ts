import type { APIRoute } from "astro";
import { POST as verify } from "../../verify/index";

export const prerender = false;
export const POST: APIRoute = verify;
