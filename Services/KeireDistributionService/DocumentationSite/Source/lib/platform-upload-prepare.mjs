import { LocalUploadError } from "./platform-upload-product.mjs";
import { authorizeLocalUpload } from "./platform-upload-transfer.mjs";

/** One stable file per immutable reservation; cancellation/retry creates a different reservation ID. */
export async function prepareResumableUpload(authentication, sessionToken, uploadId, datastore) {
    const reservation = await authorizeLocalUpload(authentication, sessionToken, uploadId);
    const fileId = reservation.uploadId.replaceAll("-", "").toLowerCase();
    try {
        await datastore.create({
            id: fileId,
            size: reservation.expectedSizeBytes,
            offset: 0,
            creation_date: new Date().toISOString(),
            metadata: { reservationId: reservation.uploadId },
        });
    } catch (error) {
        if (error?.code !== "EEXIST") {
            throw error;
        }
        // Exclusive creation preserves existing bytes. Concurrent initialization can be retried after metadata lands.
    }
    let upload;
    try {
        upload = await datastore.getUpload(fileId);
    } catch {
        throw new LocalUploadError(503, "publisher.transfer_initializing",
            "The transfer could not be opened. Retry setup for this reservation.");
    }
    if (upload.metadata?.reservationId !== reservation.uploadId || upload.size !== reservation.expectedSizeBytes
        || !Number.isSafeInteger(upload.offset) || upload.offset < 0 || upload.offset > upload.size) {
        throw new LocalUploadError(409, "publisher.transfer_mismatch",
            "The saved transfer does not match this reservation. Cancel it and start a new upload.");
    }
    return { uploadId: reservation.uploadId, fileId, offset: upload.offset,
        uploadUrl: `/publisher/v1/transfer/${fileId}` };
}
