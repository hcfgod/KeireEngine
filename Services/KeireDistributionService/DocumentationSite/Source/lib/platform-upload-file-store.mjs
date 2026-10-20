import { lstatSync } from "node:fs";
import { lstat, open, unlink } from "node:fs/promises";
import path from "node:path";
import { FileStore } from "@tus/file-store";

/** Requires an existing, application-private directory (including Windows ACL provisioning). */
export class PrivateUploadFileStore extends FileStore {
    checkOrCreateDirectory() {
        if (!path.isAbsolute(this.directory)) {
            throw new Error("Upload storage requires an absolute private directory.");
        }
        const root = lstatSync(this.directory);
        if (!root.isDirectory() || root.isSymbolicLink()) {
            throw new Error("Upload storage must be an ordinary private directory.");
        }
        if (process.platform !== "win32" && (root.mode & 0o077) !== 0) {
            throw new Error("Upload storage must not be accessible by other users.");
        }
    }

    resolve(id) {
        if (typeof id !== "string" || !/^[a-f0-9]{32}$/.test(id)) {
            throw new Error("Invalid resumable upload file identifier.");
        }
        return super.resolve(id);
    }

    async create(upload) {
        const location = this.resolve(upload.id);
        // Duplicate creation must never truncate a partial or completed upload.
        const file = await open(location, "wx", 0o600);
        try {
            await file.sync();
            await this.configstore.set(upload.id, upload);
        } catch (error) {
            await file.close();
            await unlink(location);
            throw error;
        }
        await file.close();
        upload.storage = { type: "file", path: location };
        return upload;
    }

    async getUpload(id) {
        const information = await lstat(this.resolve(id));
        if (!information.isFile() || information.isSymbolicLink()) {
            throw new Error("Upload data must be an ordinary file.");
        }
        return super.getUpload(id);
    }
}
