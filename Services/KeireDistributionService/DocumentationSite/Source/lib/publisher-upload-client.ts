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
