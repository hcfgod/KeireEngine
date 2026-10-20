import path from "node:path";
import { LocalBlobStore } from "./local-blob-store.ts";
import { PrivateUploadFileStore } from "./platform-upload-file-store.mjs";
import { verifyPrivateUploadDirectory } from "./platform-upload-directory.mjs";
import { browserUploadAuthorization } from "./platform-upload-browser-authorization.mjs";
import { createUploadTransport } from "./platform-upload-transport.mjs";
import { reserveLocalUpload } from "./platform-upload-reservation.mjs";
import { prepareResumableUpload } from "./platform-upload-prepare.mjs";
import { completeResumableUpload } from "./platform-upload-complete.mjs";

/** Owned by PlatformBackend; installation must provision both private directories first. */
export async function createLocalUploadService({ authentication, spoolDirectory, blobDirectory, siteOrigin,
    permissionOptions, verifyDirectory = verifyPrivateUploadDirectory }) {
    const spoolRoot = await verifyDirectory(spoolDirectory, permissionOptions);
    const blobRoot = await verifyDirectory(blobDirectory, permissionOptions);
    const relative = path.relative(spoolRoot, blobRoot);
    const reverse = path.relative(blobRoot, spoolRoot);
    const isNested = (value) => value === "" || (!value.startsWith(`..${path.sep}`)
        && value !== ".." && !path.isAbsolute(value));
    if (isNested(relative) || isNested(reverse)) {
        throw new Error("Resumable and immutable upload storage must be separate directories.");
    }
    const datastore = new PrivateUploadFileStore({ directory: spoolRoot });
    const blobStore = new LocalBlobStore(blobRoot);
    const transport = createUploadTransport({
        datastore, siteOrigin, allowCreation: false, authorize: browserUploadAuthorization(authentication),
    });
    let closing = false;
    let active = 0;
    let drained;
    let shutdown;
    async function run(action) {
        if (closing) {
            throw new Error("Upload service is shutting down.");
        }
        active++;
        try {
            return await action();
        } finally {
            active--;
            if (active === 0) {
                drained?.();
            }
        }
    }
    return Object.freeze({
        reserve: (token, input) => run(async () => {
            const reservation = await reserveLocalUpload(authentication, token, input);
            const transfer = await prepareResumableUpload(authentication, token, reservation.uploadId, datastore);
            return { ...reservation, ...transfer };
        }),
        prepare: (token, uploadId) => run(() => prepareResumableUpload(authentication, token, uploadId, datastore)),
        complete: (token, uploadId) => run(() => completeResumableUpload(authentication, token, uploadId,
            typeof uploadId === "string" ? uploadId.replaceAll("-", "").toLowerCase() : "",
            datastore, blobStore)),
        handle: (request) => run(() => transport.handle(request)),
        close() {
            closing = true;
            shutdown ??= active === 0 ? Promise.resolve() : new Promise((resolve) => { drained = resolve; });
            return shutdown;
        },
    });
}
