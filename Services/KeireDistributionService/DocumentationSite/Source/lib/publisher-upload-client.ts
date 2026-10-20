export function publisherUploadTransport(reservation: Record<string, unknown>, local: boolean, origin: string) {
    if (!local) {
        if (typeof reservation.resumableEndpoint !== "string" || typeof reservation.uploadToken !== "string") {
            throw new Error("The upload service returned an incomplete reservation.");
        }
        return { endpoint: reservation.resumableEndpoint, headers: { "x-signature": reservation.uploadToken },
            uploadDataDuringCreation: true };
    }
    if (typeof reservation.uploadId !== "string"
        || !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(reservation.uploadId)
        || typeof reservation.uploadUrl !== "string") {
        throw new Error("The upload service returned an incomplete reservation.");
    }
    const url = new URL(reservation.uploadUrl, origin);
    const expectedPath = `/publisher/v1/transfer/${reservation.uploadId.replaceAll("-", "").toLowerCase()}/`;
    if (url.origin !== origin || url.pathname !== expectedPath || url.search || url.hash || url.username || url.password) {
        throw new Error("The upload service returned an invalid transfer address.");
    }
    return { uploadUrl: url.href, headers: { "x-signature": reservation.uploadId },
        withCredentials: true, uploadDataDuringCreation: false };
}

/** Confirmation is idempotent; an uncertain response must never trigger cancellation or a new reservation. */
export async function confirmLocalUpload(uploadId: string, request: typeof fetch = fetch): Promise<void> {
    if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(uploadId)) {
        throw new Error("The upload reference is invalid.");
    }
    const response = await request(`/publisher/v1/uploads/${uploadId}/complete/`, {
        method: "POST",
        credentials: "same-origin",
        headers: { "Content-Type": "application/json" },
        body: "{}",
    });
    const payload = await response.json().catch(() => null);
    if (!response.ok) {
        throw new Error(payload?.error?.message ?? "Upload confirmation is temporarily unavailable.");
    }
    if (payload?.data?.uploadId !== uploadId
        || !["uploaded", "leased", "validated", "failed"].includes(payload?.data?.state)) {
        throw new Error("The server did not confirm this upload.");
    }
}
