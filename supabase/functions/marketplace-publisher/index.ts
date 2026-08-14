import {
    authenticate, databaseFailure, handleError, json, optionalUuid, preflight, readJson, RequestError, requiredUuid,
    stringField, validateOrigin,
} from "../_shared/marketplace.ts";

const maximumPackageBytes = 64 * 1024 * 1024 * 1024;
const semanticVersion = /^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)([-+][0-9A-Za-z.-]+)?$/;
const safeToken = /^[A-Za-z0-9][A-Za-z0-9._+-]{0,63}$/;

function optionalString(input: Record<string, unknown>, name: string, maximum: number): string | null {
    const value = input[name];
    if (value == null || value === "") return null;
    return stringField(input, name, 1, maximum);
}

function tokenArray(input: Record<string, unknown>, name: string, maximumItems: number): string[] {
    const value = input[name];
    if (!Array.isArray(value) || value.length > maximumItems ||
        value.some((item) => typeof item !== "string" || !safeToken.test(item)) ||
        new Set(value).size !== value.length) {
        throw new RequestError(400, "request.invalid_field", `${name} must contain unique portable tokens.`);
    }
    return value as string[];
}

function packageSize(input: Record<string, unknown>): number {
    const value = input.expectedSizeBytes;
    if (!Number.isSafeInteger(value) || (value as number) < 1 || (value as number) > maximumPackageBytes) {
        throw new RequestError(400, "request.invalid_field", "expectedSizeBytes is outside the package limit.");
    }
    return value as number;
}

function resumableEndpoint(): string {
    try {
        const projectUrl = new URL(Deno.env.get("SUPABASE_URL") ?? "");
        const match = projectUrl.hostname.match(/^([a-z0-9]{20})\.supabase\.co$/);
        if (match) return `https://${match[1]}.storage.supabase.co/storage/v1/upload/resumable/sign`;
    } catch {
        // A missing or malformed hosted URL is reported through the bounded public error below.
    }
    throw new RequestError(503, "marketplace.not_configured", "Storage uploads are not configured.");
}

Deno.serve(async (request: Request) => {
    let origin = "";
    try {
        origin = validateOrigin(request);
        if (request.method === "OPTIONS") return preflight(origin);
        if (request.method !== "POST") throw new RequestError(405, "request.method_not_allowed", "POST is required.");
        const caller = await authenticate(request);
        if (caller.assuranceLevel !== "aal2") {
            throw new RequestError(403, "account.mfa_required", "A verified second factor is required.");
        }
        const input = await readJson(request);
        const operation = stringField(input, "operation", 3, 64);
        if (operation === "application.submit") {
            const { data, error } = await caller.admin.rpc("service_submit_publisher_application", {
                p_actor_user_id: caller.user.id,
                p_application_id: requiredUuid(input, "applicationId"),
            });
            if (error) throw databaseFailure(error);
            return json(origin, 200, { data: { applicationId: data, state: "submitted" } });
        }
        if (operation === "version.submit") {
            const { data, error } = await caller.admin.rpc("service_submit_marketplace_version", {
                p_actor_user_id: caller.user.id,
                p_version_id: requiredUuid(input, "versionId"),
            });
            if (error) throw databaseFailure(error);
            const submission = Array.isArray(data) ? data[0] : data;
            if (!submission || typeof submission.submission_id !== "string" ||
                submission.submission_state !== "submitted") {
                throw new RequestError(503, "publisher.submission_failed",
                    "The validated package could not be submitted for staff review.");
            }
            return json(origin, 201, {
                data: { submissionId: submission.submission_id, state: submission.submission_state },
            });
        }
        if (operation === "upload.reserve") {
            const productName = stringField(input, "productName", 2, 128).trim();
            const productSummary = stringField(input, "productSummary", 20, 240).trim();
            const version = stringField(input, "version", 5, 128);
            const minimumEngineVersion = stringField(input, "minimumEngineVersion", 5, 128);
            const maximumEngineVersion = optionalString(input, "maximumEngineVersion", 128);
            const installKind = stringField(input, "installKind", 8, 32);
            const expectedSha256 = stringField(input, "expectedSha256", 64, 64);
            if (!semanticVersion.test(version) || !semanticVersion.test(minimumEngineVersion) ||
                (maximumEngineVersion && !semanticVersion.test(maximumEngineVersion)) ||
                !["registry", "asset_import", "complete_project"].includes(installKind) ||
                !/^[0-9a-f]{64}$/.test(expectedSha256)) {
                throw new RequestError(400, "publisher.upload_invalid", "The package release metadata is invalid.");
            }
            const { data, error } = await caller.admin.rpc("service_reserve_marketplace_named_upload", {
                p_actor_user_id: caller.user.id,
                p_product_id: optionalUuid(input, "productId"),
                p_publisher_id: requiredUuid(input, "publisherId"),
                p_category_id: requiredUuid(input, "categoryId"),
                p_product_name: productName,
                p_product_summary: productSummary,
                p_license_spdx: stringField(input, "licenseSpdx", 2, 64),
                p_license_revision: stringField(input, "licenseRevision", 1, 64),
                p_version: version,
                p_install_kind: installKind,
                p_minimum_engine_version: minimumEngineVersion,
                p_maximum_engine_version: maximumEngineVersion,
                p_platforms: tokenArray(input, "platforms", 16),
                p_architectures: tokenArray(input, "architectures", 16),
                p_renderer_capabilities: tokenArray(input, "rendererCapabilities", 32),
                p_managed_api_version: optionalString(input, "managedApiVersion", 128),
                p_release_notes_markdown: optionalString(input, "releaseNotes", 50_000) ?? "",
                p_expected_size_bytes: packageSize(input),
                p_expected_sha256: expectedSha256,
            });
            if (error) throw databaseFailure(error);
            const reservation = Array.isArray(data) ? data[0] : data;
            if (!reservation || typeof reservation.upload_id !== "string" ||
                typeof reservation.version_id !== "string" || typeof reservation.product_id !== "string" ||
                typeof reservation.storage_path !== "string" ||
                typeof reservation.expires_at !== "string") {
                throw new RequestError(503, "publisher.upload_reservation_failed",
                    "The upload reservation could not be created.");
            }
            const signed = await caller.admin.storage.from("marketplace-packages")
                .createSignedUploadUrl(reservation.storage_path, { upsert: false });
            if (signed.error || !signed.data?.token) {
                await caller.admin.rpc("service_cancel_marketplace_upload", {
                    p_actor_user_id: caller.user.id,
                    p_upload_id: reservation.upload_id,
                });
                throw new RequestError(503, "publisher.upload_grant_failed",
                    "The path-scoped upload grant could not be created.");
            }
            return json(origin, 201, {
                data: {
                    uploadId: reservation.upload_id,
                    versionId: reservation.version_id,
                    productId: reservation.product_id,
                    bucket: "marketplace-packages",
                    storagePath: reservation.storage_path,
                    uploadToken: signed.data.token,
                    resumableEndpoint: resumableEndpoint(),
                    expiresAt: reservation.expires_at,
                },
            });
        }
        if (operation === "upload.complete") {
            const { data, error } = await caller.admin.rpc("service_complete_marketplace_upload", {
                p_actor_user_id: caller.user.id,
                p_upload_id: requiredUuid(input, "uploadId"),
            });
            if (error) throw databaseFailure(error);
            const completed = Array.isArray(data) ? data[0] : data;
            if (!completed || typeof completed.upload_id !== "string" ||
                typeof completed.version_id !== "string" || completed.state !== "uploaded") {
                throw new RequestError(503, "publisher.upload_completion_failed",
                    "The completed upload could not be queued for validation.");
            }
            return json(origin, 200, {
                data: { uploadId: completed.upload_id, versionId: completed.version_id, state: completed.state },
            });
        }
        if (operation === "upload.cancel") {
            const uploadId = requiredUuid(input, "uploadId");
            const { data: cancelled, error } = await caller.admin.rpc("service_cancel_marketplace_upload_v2", {
                p_actor_user_id: caller.user.id,
                p_upload_id: uploadId,
            }).single();
            if (error) throw databaseFailure(error);
            const cancelledUpload = cancelled as Record<string, unknown> | null;
            if (cancelledUpload && typeof cancelledUpload.storage_bucket === "string" &&
                typeof cancelledUpload.storage_path === "string") {
                const removal = await caller.admin.storage.from(cancelledUpload.storage_bucket)
                    .remove([cancelledUpload.storage_path]);
                if (removal.error) console.error("cancelled marketplace upload cleanup failed", uploadId);
            }
            return json(origin, 200, { data: { uploadId, state: "expired" } });
        }
        throw new RequestError(400, "request.operation_invalid", "The publisher operation is invalid.");
    } catch (error) {
        return handleError(origin, error);
    }
});
