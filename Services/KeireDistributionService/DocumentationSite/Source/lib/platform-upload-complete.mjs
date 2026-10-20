import { LocalUploadError } from "./platform-upload-product.mjs";
import { authorizeLocalUpload, transferLocalUpload } from "./platform-upload-transfer.mjs";

/** A resumable offset is not integrity evidence: verify the complete bytes before queuing validation. */
export async function completeResumableUpload(authentication, sessionToken, uploadId, fileId, datastore, blobStore) {
    if (typeof fileId !== "string" || !/^[a-f0-9]{32}$/.test(fileId)) {
        throw new LocalUploadError(400, "publisher.transfer_invalid", "The resumable transfer identifier is invalid.");
    }
    const reservation = await authorizeLocalUpload(authentication, sessionToken, uploadId);
    let upload;
    try {
        upload = await datastore.getUpload(fileId);
    } catch {
        throw new LocalUploadError(404, "publisher.transfer_unavailable", "The resumable transfer is unavailable.");
    }
    if (upload.metadata?.reservationId !== reservation.uploadId) {
        throw new LocalUploadError(403, "publisher.transfer_mismatch", "This transfer belongs to a different upload.");
    }
    if (upload.size !== reservation.expectedSizeBytes || upload.offset !== reservation.expectedSizeBytes) {
        throw new LocalUploadError(409, "publisher.transfer_incomplete", "Finish uploading the package before requesting validation.");
    }
    async function* chunks() {
        // Do not open a file until the transfer's second authorization check has succeeded.
        yield* datastore.read(fileId);
    }
    return transferLocalUpload(authentication, sessionToken, uploadId, blobStore, chunks());
}
