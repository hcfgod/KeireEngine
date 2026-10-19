import { createHash, randomUUID } from "node:crypto";
import { createReadStream } from "node:fs";
import { link, lstat, mkdir, open, unlink } from "node:fs/promises";
import path from "node:path";

export interface BlobIdentity {
    sha256: string;
    sizeBytes: number;
}

function validateIdentity(identity: BlobIdentity): void {
    if (!/^[a-f0-9]{64}$/.test(identity.sha256) ||
        !Number.isSafeInteger(identity.sizeBytes) || identity.sizeBytes < 0) {
        throw new Error("Invalid blob identity.");
    }
}

/** Private immutable bytes only. Routes must authorize bucket/object access before obtaining a blob identity. */
export class LocalBlobStore {
    private readonly root: string;

    constructor(root: string) {
        if (!path.isAbsolute(root)) {
            throw new Error("Blob storage requires an absolute private directory.");
        }
        this.root = path.resolve(root);
    }

    private async prepareRoot(): Promise<void> {
        await mkdir(this.root, { recursive: true, mode: 0o700 });
        const information = await lstat(this.root);
        if (!information.isDirectory() || information.isSymbolicLink()) {
            throw new Error("Blob storage root must be an ordinary private directory.");
        }
    }

    private blobPath(identity: BlobIdentity): string {
        validateIdentity(identity);
        return path.join(this.root, identity.sha256);
    }

    async verify(identity: BlobIdentity): Promise<void> {
        const location = this.blobPath(identity);
        const information = await lstat(location);
        if (!information.isFile() || information.isSymbolicLink() || information.size !== identity.sizeBytes) {
            throw new Error("Stored blob does not match its expected size or file type.");
        }
        const digest = createHash("sha256");
        for await (const chunk of createReadStream(location)) {
            digest.update(chunk);
        }
        if (digest.digest("hex") !== identity.sha256) {
            throw new Error("Stored blob failed SHA-256 verification.");
        }
    }

    async put(identity: BlobIdentity, chunks: AsyncIterable<Uint8Array>): Promise<void> {
        const destination = this.blobPath(identity);
        await this.prepareRoot();
        const temporary = path.join(this.root, `.upload-${randomUUID()}`);
        const file = await open(temporary, "wx", 0o600);
        let closed = false;
        try {
            const digest = createHash("sha256");
            let size = 0;
            for await (const chunk of chunks) {
                if (!(chunk instanceof Uint8Array) || chunk.byteLength > identity.sizeBytes - size) {
                    throw new Error("Blob upload exceeded its declared size or supplied invalid bytes.");
                }
                size += chunk.byteLength;
                digest.update(chunk);
                let offset = 0;
                while (offset < chunk.byteLength) {
                    const result = await file.write(chunk, offset, chunk.byteLength - offset);
                    if (result.bytesWritten === 0) {
                        throw new Error("Blob upload made no write progress.");
                    }
                    offset += result.bytesWritten;
                }
            }
            if (size !== identity.sizeBytes || digest.digest("hex") !== identity.sha256) {
                throw new Error("Blob upload failed size or SHA-256 verification.");
            }
            await file.sync();
            await file.close();
            closed = true;
            try {
                // Exclusive hard-link creation publishes complete bytes without replacing an existing object.
                await link(temporary, destination);
            } catch (error) {
                if ((error as NodeJS.ErrnoException).code !== "EEXIST") {
                    throw error;
                }
                await this.verify(identity);
            }
        } finally {
            try {
                if (!closed) {
                    await file.close();
                }
            } finally {
                await unlink(temporary);
            }
        }
    }
}
