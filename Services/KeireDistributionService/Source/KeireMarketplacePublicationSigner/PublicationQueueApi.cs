using System.Text.Json.Serialization;
using Keire.Distribution;

namespace Keire.Marketplace.PublicationSigner;

internal sealed class PublicationQueueApi : IDisposable
{
    private const int MaximumResponseBytes = 1024 * 1024;
    private readonly HttpClient m_Http;
    private readonly SignerOptions m_Options;

    public PublicationQueueApi(SignerOptions options, HttpMessageHandler? handler = null)
    {
        m_Options = options;
        m_Http = handler is null ? new HttpClient() : new HttpClient(handler, disposeHandler: true);
        m_Http.BaseAddress = options.SupabaseUrl;
        m_Http.Timeout = Timeout.InfiniteTimeSpan;
        m_Http.DefaultRequestHeaders.UserAgent.ParseAdd("KeireMarketplacePublicationSigner/0.4.2");
    }

    public async Task<PublicationLease?> LeaseAsync(string signingKeyId, CancellationToken cancellationToken)
    {
        byte[] response = await PostAsync(new PublicationQueueRequest
        {
            Action = "lease",
            WorkerId = m_Options.WorkerId,
            LeaseSeconds = m_Options.LeaseSeconds,
            SigningKeyId = signingKeyId,
        }, cancellationToken);
        PublicationLeaseResponse lease = DistributionJson.DeserializeStrict<PublicationLeaseResponse>(response);
        lease.Data.Lease?.Validate();
        return lease.Data.Lease;
    }

    public async Task RenewAsync(Guid jobId, CancellationToken cancellationToken)
    {
        _ = await PostAsync(new PublicationQueueRequest
        {
            Action = "renew",
            WorkerId = m_Options.WorkerId,
            LeaseSeconds = m_Options.LeaseSeconds,
            JobId = jobId,
        }, cancellationToken);
    }

    public async Task CompleteAsync(Guid jobId, string signedManifest, CancellationToken cancellationToken)
    {
        _ = await PostAsync(new PublicationQueueRequest
        {
            Action = "complete",
            WorkerId = m_Options.WorkerId,
            LeaseSeconds = m_Options.LeaseSeconds,
            JobId = jobId,
            SignedManifest = signedManifest,
        }, cancellationToken);
    }

    public async Task FailAsync(
        Guid jobId,
        string errorCode,
        bool retryable,
        CancellationToken cancellationToken)
    {
        _ = await PostAsync(new PublicationQueueRequest
        {
            Action = "fail",
            WorkerId = m_Options.WorkerId,
            LeaseSeconds = m_Options.LeaseSeconds,
            JobId = jobId,
            ErrorCode = errorCode,
            Retryable = retryable,
        }, cancellationToken);
    }

    public void Dispose()
    {
        m_Http.Dispose();
    }

    private async Task<byte[]> PostAsync(PublicationQueueRequest body, CancellationToken cancellationToken)
    {
        using HttpRequestMessage request = new(HttpMethod.Post, "functions/v1/marketplace-publication-queue")
        {
            Content = new ByteArrayContent(DistributionJson.Serialize(body)),
        };
        request.Content.Headers.ContentType = new System.Net.Http.Headers.MediaTypeHeaderValue("application/json");
        request.Headers.Add("x-keire-publication-secret", m_Options.QueueSecret);
        using HttpResponseMessage response = await m_Http.SendAsync(
            request, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
        if (!response.IsSuccessStatusCode)
        {
            throw new HttpRequestException("The Marketplace publication queue rejected a transition.", null,
                response.StatusCode);
        }

        if (response.Content.Headers.ContentLength is long length && length > MaximumResponseBytes)
        {
            throw new InvalidDataException("A publication queue response exceeds its size limit.");
        }

        await using Stream stream = await response.Content.ReadAsStreamAsync(cancellationToken);
        using MemoryStream result = new();
        byte[] buffer = new byte[16 * 1024];
        while (true)
        {
            int count = await stream.ReadAsync(buffer, cancellationToken);
            if (count == 0)
            {
                return result.ToArray();
            }

            if (result.Length > MaximumResponseBytes - count)
            {
                throw new InvalidDataException("A publication queue response exceeds its size limit.");
            }

            result.Write(buffer, 0, count);
        }
    }
}

internal sealed class PublicationQueueRequest
{
    [JsonPropertyName("action")]
    public required string Action { get; init; }

    [JsonPropertyName("workerId")]
    public required string WorkerId { get; init; }

    [JsonPropertyName("leaseSeconds")]
    public required int LeaseSeconds { get; init; }

    [JsonPropertyName("signingKeyId")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? SigningKeyId { get; init; }

    [JsonPropertyName("jobId")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
    public Guid JobId { get; init; }

    [JsonPropertyName("signedManifest")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? SignedManifest { get; init; }

    [JsonPropertyName("errorCode")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? ErrorCode { get; init; }

    [JsonPropertyName("retryable")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public bool? Retryable { get; init; }
}

internal sealed class PublicationLeaseResponse
{
    [JsonPropertyName("data")]
    public required PublicationLeaseData Data { get; init; }
}

internal sealed class PublicationLeaseData
{
    [JsonPropertyName("lease")]
    public PublicationLease? Lease { get; init; }
}

internal sealed class PublicationLease
{
    [JsonPropertyName("job_id")]
    public required Guid JobId { get; init; }

    [JsonPropertyName("version_id")]
    public required Guid VersionId { get; init; }

    [JsonPropertyName("product_id")]
    public required Guid ProductId { get; init; }

    [JsonPropertyName("storage_bucket")]
    public required string StorageBucket { get; init; }

    [JsonPropertyName("storage_path")]
    public required string StoragePath { get; init; }

    [JsonPropertyName("artifact_sha256")]
    public required string ArtifactSha256 { get; init; }

    [JsonPropertyName("artifact_size_bytes")]
    public required long ArtifactSizeBytes { get; init; }

    [JsonPropertyName("manifest_sha256")]
    public required string ManifestSha256 { get; init; }

    [JsonPropertyName("publication_sequence")]
    public required long PublicationSequence { get; init; }

    [JsonPropertyName("publication_expires_at")]
    public required DateTimeOffset PublicationExpiresAt { get; init; }

    [JsonPropertyName("validation_attestation")]
    public required string ValidationAttestation { get; init; }

    [JsonPropertyName("attestation_key_id")]
    public required string AttestationKeyId { get; init; }

    [JsonPropertyName("evidence_storage_path")]
    public required string EvidenceStoragePath { get; init; }

    [JsonPropertyName("evidence_sha256")]
    public required string EvidenceSha256 { get; init; }

    [JsonPropertyName("evidence_size_bytes")]
    public required long EvidenceSizeBytes { get; init; }

    public void Validate()
    {
        if (JobId == Guid.Empty || VersionId == Guid.Empty || ProductId == Guid.Empty ||
            StorageBucket is not ("marketplace-packages" or "marketplace-quarantine") ||
            !string.Equals(StoragePath, DistributionPaths.NormalizeRelativePath(StoragePath), StringComparison.Ordinal) ||
            !DistributionPaths.IsSha256(ArtifactSha256) || ArtifactSizeBytes is <= 0 or > 68719476736L ||
            !DistributionPaths.IsSha256(ManifestSha256) || PublicationSequence <= 0 ||
            PublicationExpiresAt <= DateTimeOffset.UtcNow || !DistributionPaths.IsKeyId(AttestationKeyId) ||
            !DistributionPaths.IsSha256(EvidenceSha256) || EvidenceSizeBytes is < 2 or > 8 * 1024 * 1024 ||
            string.IsNullOrEmpty(ValidationAttestation))
        {
            throw new InvalidDataException("The publication lease contains invalid immutable metadata.");
        }
    }
}
