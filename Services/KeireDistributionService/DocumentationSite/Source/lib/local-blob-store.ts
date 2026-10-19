import { createHash, randomUUID } from "node:crypto";
import { constants } from "node:fs";
import type { FileHandle } from "node:fs/promises";
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

    private async openVerified(identity: BlobIdentity): Promise<FileHandle> {
        const location = this.blobPath(identity);
        const information = await lstat(location);
        if (!information.isFile() || information.isSymbolicLink()) {
            throw new Error("Stored blob does not match its expected size or file type.");
        }
        // O_NOFOLLOW is unavailable on Windows; private directory ACLs remain mandatory there.
        const file = await open(location, constants.O_RDONLY | (constants.O_NOFOLLOW ?? 0));
        try {
            const opened = await file.stat();
            if (!opened.isFile() || opened.size !== identity.sizeBytes ||
                opened.dev !== information.dev || opened.ino !== information.ino) {
                throw new Error("Stored blob does not match its expected size or file type.");
            }
            const digest = createHash("sha256");
            const buffer = Buffer.alloc(64 * 1024);
            let position = 0;
            while (position < identity.sizeBytes) {
                const result = await file.read(buffer, 0,
                    Math.min(buffer.length, identity.sizeBytes - position), position);
                if (result.bytesRead === 0) {
                    throw new Error("Stored blob ended before its expected size.");
                }
                digest.update(buffer.subarray(0, result.bytesRead));
                position += result.bytesRead;
            }
            if ((await file.stat()).size !== identity.sizeBytes || digest.digest("hex") !== identity.sha256) {
                throw new Error("Stored blob failed SHA-256 verification.");
            }
            return file;
        } catch (error) {
            await file.close();
            throw error;
        }
    }

    async verify(identity: BlobIdentity): Promise<void> {
        const file = await this.openVerified(identity);
        await file.close();
    }

    /** Authorize first. The directory must prohibit external writers for the lifetime of a read. */
    async *read(identity: BlobIdentity): AsyncGenerator<Uint8Array> {
        const file = await this.openVerified(identity);
        try {
            let position = 0;
            while (position < identity.sizeBytes) {
                const buffer = Buffer.alloc(Math.min(64 * 1024, identity.sizeBytes - position));
                const result = await file.read(buffer, 0, buffer.length, position);
                if (result.bytesRead === 0) {
                    throw new Error("Stored blob ended before its expected size.");
                }
                position += result.bytesRead;
                yield buffer.subarray(0, result.bytesRead);
            }
        } finally {
            await file.close();
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
