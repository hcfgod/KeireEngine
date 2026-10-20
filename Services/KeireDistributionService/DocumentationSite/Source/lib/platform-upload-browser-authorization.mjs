import { cookieToken } from "./platform-auth-http.mjs";
import { LocalUploadError } from "./platform-upload-product.mjs";
import { authorizeLocalUpload } from "./platform-upload-transfer.mjs";

/** The reservation header identifies a resource; only the authenticated cookie authorizes access. */
export function browserUploadAuthorization(authentication) {
    return async (request) => {
        if (request.headers.has("authorization")) {
            throw new LocalUploadError(401, "account.invalid_authorization", "Use browser sign-in to upload a package.");
        }
        const sessionToken = cookieToken(request, "__Host-keire-session");
        if (!sessionToken) {
            throw new LocalUploadError(401, "account.authentication_required", "Sign in again to upload a package.");
        }
        return authorizeLocalUpload(authentication, sessionToken, request.headers.get("x-signature"));
    };
}
