using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using Keire.Distribution;
using Keire.Marketplace.Validation;

namespace Keire.Marketplace.Validator.Broker;

internal sealed class SupabaseValidatorApi : IDisposable
{
    private const int MaximumApiResponseBytes = 1024 * 1024;
    private readonly HttpClient m_Http;
    private readonly BrokerOptions m_Options;

    public SupabaseValidatorApi(BrokerOptions options, HttpMessageHandler? handler = null)
    {
        m_Options = options;
        m_Http = handler is null ? new HttpClient() : new HttpClient(handler, disposeHandler: true);
        m_Http.BaseAddress = options.SupabaseUrl;
        m_Http.Timeout = Timeout.InfiniteTimeSpan;
        m_Http.DefaultRequestHeaders.UserAgent.ParseAdd("KeireMarketplaceValidatorBroker/0.3.1");
    }

    public async Task<ValidationLease?> LeaseAsync(CancellationToken cancellationToken)
    {
        byte[] request = DistributionJson.Serialize(new QueueRequest
        {
            Action = "lease",
            WorkerId = m_Options.WorkerId,
            LeaseSeconds = m_Options.LeaseSeconds,
        });
        byte[] response = await PostQueueAsync(request, cancellationToken);
        LeaseResponse leaseResponse = DistributionJson.DeserializeStrict<LeaseResponse>(response);
        ValidationLease? lease = leaseResponse.Data.Lease;
        lease?.Validate(m_Options.SupabaseUrl);
        return lease;
    }

    public async Task RenewAsync(Guid uploadId, CancellationToken cancellationToken)
    {
        byte[] request = DistributionJson.Serialize(new QueueRequest
        {
            Action = "renew",
            UploadId = uploadId,
            WorkerId = m_Options.WorkerId,
            LeaseSeconds = m_Options.LeaseSeconds,
        });
        _ = await PostQueueAsync(request, cancellationToken);
    }

    public async Task DownloadAsync(
        ValidationLease lease,
        string destination,
        CancellationToken cancellationToken)
    {
        using HttpRequestMessage request = new(HttpMethod.Get, lease.DownloadUrl);
        using HttpResponseMessage response = await m_Http.SendAsync(
            request,
            HttpCompletionOption.ResponseHeadersRead,
            cancellationToken);
        if (!response.IsSuccessStatusCode)
        {
            throw new HttpRequestException("Supabase Storage did not return the leased quarantine object.", null, response.StatusCode);
        }

        if (response.Content.Headers.ContentLength is long contentLength && contentLength != lease.ExpectedSizeBytes)
        {
            throw new InvalidDataException("The quarantine object content length does not match its upload declaration.");
        }

        await using Stream source = await response.Content.ReadAsStreamAsync(cancellationToken);
        await using FileStream output = new(
            destination,
            FileMode.CreateNew,
            FileAccess.Write,
            FileShare.None,
            128 * 1024,
            FileOptions.Asynchronous | FileOptions.SequentialScan | FileOptions.WriteThrough);
        using IncrementalHash hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        byte[] buffer = new byte[128 * 1024];
        long total = 0;
        while (true)
        {
            int count = await source.ReadAsync(buffer, cancellationToken);
            if (count == 0)
            {
                break;
            }

            if (total > lease.ExpectedSizeBytes - count)
            {
                throw new InvalidDataException("The quarantine object exceeds its upload declaration.");
            }

            await output.WriteAsync(buffer.AsMemory(0, count), cancellationToken);
            hash.AppendData(buffer.AsSpan(0, count));
            total += count;
        }

        await output.FlushAsync(cancellationToken);
        output.Flush(flushToDisk: true);
        string digest = Convert.ToHexStringLower(hash.GetHashAndReset());
        if (total != lease.ExpectedSizeBytes || !string.Equals(digest, lease.ExpectedSha256, StringComparison.Ordinal))
        {
            throw new InvalidDataException("The quarantine object does not match its immutable upload declaration.");
        }
    }

    public async Task CompleteAsync(
        ValidationLease lease,
        ReadOnlyMemory<byte> reportBytes,
        CancellationToken cancellationToken)
    {
        using JsonDocument reportDocument = DistributionJson.ParseStrict(reportBytes.ToArray(), 24);
        MarketplaceValidationReport report =
            JsonSerializer.Deserialize<MarketplaceValidationReport>(reportDocument.RootElement, DistributionJson.Options)
            ?? throw new InvalidDataException("The offline validator report is empty.");
        if (!string.Equals(report.PackageSha256, lease.ExpectedSha256, StringComparison.Ordinal) ||
            !string.Equals(
                report.ValidatorFingerprintSha256,
                m_Options.ExpectedValidatorFingerprintSha256,
                StringComparison.Ordinal) ||
            !string.Equals(report.ValidatorVersion, MarketplaceValidationContract.ValidatorVersion, StringComparison.Ordinal) ||
            !string.Equals(report.PolicyVersion, MarketplaceValidationContract.PolicyVersion, StringComparison.Ordinal))
        {
            throw new InvalidDataException("The offline validator report does not match its leased package or pinned worker.");
        }
        _ = ValidatorAttestation.Verify(
            lease.UploadId,
            lease.VersionId,
            lease.StorageBucket,
            lease.StoragePath,
            lease.ExpectedSizeBytes,
            lease.ExpectedSha256,
            report,
            m_Options.AttestationVerificationKey);

        byte[] request = DistributionJson.Serialize(new QueueRequest
        {
            Action = "complete",
            UploadId = lease.UploadId,
            WorkerId = m_Options.WorkerId,
            LeaseSeconds = m_Options.LeaseSeconds,
            Report = reportDocument.RootElement.Clone(),
        });
        _ = await PostQueueAsync(request, cancellationToken);
    }

    public async Task UploadEvidenceAsync(
        ValidationLease lease,
        ReadOnlyMemory<byte> evidence,
        CancellationToken cancellationToken)
    {
        if (evidence.Length is < 2 or > MarketplaceValidationContract.MaximumEvidenceBytes)
        {
            throw new InvalidDataException("The review evidence is outside its size limit.");
        }

        using HttpRequestMessage request = new(HttpMethod.Put, lease.EvidenceUploadUrl)
        {
            Content = new ReadOnlyMemoryContent(evidence),
        };
        request.Content.Headers.ContentType = new System.Net.Http.Headers.MediaTypeHeaderValue("application/json");
        request.Headers.Add("x-upsert", "false");
        using HttpResponseMessage response = await m_Http.SendAsync(request, cancellationToken);
        if (!response.IsSuccessStatusCode)
        {
            throw new HttpRequestException(
                "Supabase Storage rejected the signed review-evidence upload.",
                null,
                response.StatusCode);
        }
    }

    public void Dispose()
    {
        m_Http.Dispose();
    }

    private async Task<byte[]> PostQueueAsync(byte[] body, CancellationToken cancellationToken)
    {
        using HttpRequestMessage request = new(HttpMethod.Post, "functions/v1/marketplace-validator-queue")
        {
            Content = new ByteArrayContent(body),
        };
        request.Content.Headers.ContentType = new System.Net.Http.Headers.MediaTypeHeaderValue("application/json");
        request.Headers.Add("x-keire-validator-secret", m_Options.BrokerSecret);
        using HttpResponseMessage response = await m_Http.SendAsync(
            request,
            HttpCompletionOption.ResponseHeadersRead,
            cancellationToken);
        if (!response.IsSuccessStatusCode)
        {
            throw new HttpRequestException("The validator queue boundary rejected a transition.", null, response.StatusCode);
        }

        return await ReadBoundedAsync(response.Content, MaximumApiResponseBytes, cancellationToken);
    }

    private static async Task<byte[]> ReadBoundedAsync(
        HttpContent content,
        int maximumBytes,
        CancellationToken cancellationToken)
    {
        if (content.Headers.ContentLength is long contentLength && contentLength > maximumBytes)
        {
            throw new InvalidDataException("A Supabase response exceeds its size limit.");
        }

        await using Stream stream = await content.ReadAsStreamAsync(cancellationToken);
        using MemoryStream result = new();
        byte[] buffer = new byte[16 * 1024];
        while (true)
        {
            int count = await stream.ReadAsync(buffer, cancellationToken);
            if (count == 0)
            {
                return result.ToArray();
            }

            if (result.Length > maximumBytes - count)
            {
                throw new InvalidDataException("A Supabase response exceeds its size limit.");
            }

            result.Write(buffer, 0, count);
        }
    }
}

internal sealed class ValidationLease
{
    [JsonPropertyName("uploadId")]
    public required Guid UploadId { get; init; }

    [JsonPropertyName("versionId")]
    public required Guid VersionId { get; init; }

    [JsonPropertyName("storagePath")]
    public required string StoragePath { get; init; }

    [JsonPropertyName("storageBucket")]
    public required string StorageBucket { get; init; }

    [JsonPropertyName("evidenceStoragePath")]
    public required string EvidenceStoragePath { get; init; }

    [JsonPropertyName("evidenceUploadUrl")]
    public required string EvidenceUploadUrl { get; init; }

    [JsonPropertyName("expectedSizeBytes")]
    public required long ExpectedSizeBytes { get; init; }

    [JsonPropertyName("expectedSha256")]
    public required string ExpectedSha256 { get; init; }

    [JsonPropertyName("leaseExpiresAt")]
    public required DateTimeOffset LeaseExpiresAt { get; init; }

    [JsonPropertyName("downloadUrl")]
    public required string DownloadUrl { get; init; }

    public void Validate(Uri supabaseUrl)
    {
        if (!Uri.TryCreate(DownloadUrl, UriKind.Absolute, out Uri? download) ||
            !Uri.TryCreate(EvidenceUploadUrl, UriKind.Absolute, out Uri? evidenceUpload) ||
            download.Scheme != Uri.UriSchemeHttps || evidenceUpload.Scheme != Uri.UriSchemeHttps ||
            !SameOrigin(download, supabaseUrl) || !SameOrigin(evidenceUpload, supabaseUrl))
        {
            throw new InvalidDataException("The validator lease contains an untrusted download origin.");
        }

        if (UploadId == Guid.Empty || VersionId == Guid.Empty ||
            ExpectedSizeBytes is <= 0 or > MarketplaceValidationContract.MaximumPackageBytes ||
            !DistributionPaths.IsSha256(ExpectedSha256) ||
            StorageBucket is not ("marketplace-packages" or "marketplace-quarantine") ||
            !string.Equals(StoragePath, DistributionPaths.NormalizeRelativePath(StoragePath), StringComparison.Ordinal) ||
            !string.Equals(EvidenceStoragePath, DistributionPaths.NormalizeRelativePath(EvidenceStoragePath),
                StringComparison.Ordinal) ||
            LeaseExpiresAt <= DateTimeOffset.UtcNow)
        {
            throw new InvalidDataException("The validator lease response is invalid.");
        }
    }

    private static bool SameOrigin(Uri first, Uri second)
    {
        return string.Equals(first.Scheme, second.Scheme, StringComparison.OrdinalIgnoreCase) &&
            string.Equals(first.Host, second.Host, StringComparison.OrdinalIgnoreCase) && first.Port == second.Port;
    }
}

internal sealed class QueueRequest
{
    [JsonPropertyName("action")]
    public required string Action { get; init; }

    [JsonPropertyName("workerId")]
    public required string WorkerId { get; init; }

    [JsonPropertyName("leaseSeconds")]
    public required int LeaseSeconds { get; init; }

    [JsonPropertyName("uploadId")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingDefault)]
    public Guid UploadId { get; init; }

    [JsonPropertyName("report")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public JsonElement? Report { get; init; }
}

internal sealed class LeaseResponse
{
    [JsonPropertyName("data")]
    public required LeaseResponseData Data { get; init; }
}

internal sealed class LeaseResponseData
{
    [JsonPropertyName("lease")]
    public ValidationLease? Lease { get; init; }
}
