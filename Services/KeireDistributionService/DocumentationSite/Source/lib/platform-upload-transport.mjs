import { Server } from "@tus/server";
import { LocalUploadError } from "./platform-upload-product.mjs";

/** Application-owned transport. Authorization must resolve the current reservation on every request. */
export function createUploadTransport({ datastore, authorize, siteOrigin, path = "/publisher/v1/transfer" }) {
    const origin = new URL(siteOrigin);
    if (origin.protocol !== "https:" || origin.origin !== siteOrigin) {
        throw new Error("Uploads require a canonical HTTPS origin.");
    }
    const reservations = new WeakMap();
    const reject = (status_code, body) => {
        throw { status_code, body };
    };
    const server = new Server({
        path,
        datastore,
        relativeLocation: true,
        respectForwardedHeaders: false,
        allowedOrigins: [siteOrigin],
        allowedCredentials: true,
        allowedHeaders: ["x-signature"],
        maxSize: (request) => reservations.get(request)?.expectedSizeBytes ?? 0,
        onIncomingRequest: async (request, fileId) => {
            if (!["POST", "PATCH", "HEAD", "OPTIONS"].includes(request.method)) {
                reject(405, "Use the publisher cancellation endpoint to cancel an upload.");
            }
            if (request.method === "OPTIONS") {
                return;
            }
            const requestOrigin = request.headers.get("origin");
            // Same-origin browser HEAD probes may omit Origin; they still require reservation authorization.
            if (requestOrigin !== siteOrigin && !(request.method === "HEAD" && requestOrigin === null)) {
                reject(403, "The upload request origin could not be verified.");
            }
            let reservation;
            try {
                reservation = await authorize(request);
            } catch (error) {
                if (error instanceof LocalUploadError) {
                    reject(error.status, error.message);
                }
                reject(503, "Upload authorization is temporarily unavailable.");
            }
            if (!reservation || !Number.isSafeInteger(reservation.expectedSizeBytes)
                || reservation.expectedSizeBytes < 1 || typeof reservation.uploadId !== "string") {
                reject(403, "The upload reservation is unavailable.");
            }
            reservations.set(request, reservation);
            if (request.method !== "POST" && fileId) {
                const upload = await datastore.getUpload(fileId);
                if (upload.metadata?.reservationId !== reservation.uploadId
                    || upload.size !== reservation.expectedSizeBytes) {
                    reject(403, "This transfer does not belong to the upload reservation.");
                }
            }
        },
        onUploadCreate: async (request, upload) => {
            const reservation = reservations.get(request);
            if (!reservation || upload.size !== reservation.expectedSizeBytes) {
                reject(400, "The upload length must match the package reservation.");
            }
            return { metadata: { reservationId: reservation.uploadId } };
        },
    });
    return {
        async handle(request) {
            const response = await server.handleWeb(request);
            response.headers.set("cache-control", "private, no-store");
            response.headers.set("x-content-type-options", "nosniff");
            return response;
        },
    };
}
