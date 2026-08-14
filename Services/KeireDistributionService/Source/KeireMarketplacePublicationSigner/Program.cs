using System.Text;
using System.Text.Json.Serialization;
using Keire.Distribution;
using Keire.Marketplace.Security;
using Keire.Marketplace.Validation;
using Keire.Marketplace.PublicationSigner;

return await PublicationSignerProgram.RunAsync();

internal static class PublicationSignerProgram
{
    public static async Task<int> RunAsync()
    {
        using CancellationTokenSource cancellation = new();
        Console.CancelKeyPress += (_, eventArguments) =>
        {
            eventArguments.Cancel = true;
            cancellation.Cancel();
        };
        try
        {
            SignerOptions options = SignerOptions.FromEnvironment();
            using MarketplaceSigningKey signingKey =
                MarketplaceSigningKey.FromEnvironment("KEIRE_MARKETPLACE_PUBLICATION_PRIVATE_KEY");
            if (!string.Equals(
                    signingKey.PublicDocument.KeyId,
                    options.PublicationVerificationKey.Document.KeyId,
                    StringComparison.Ordinal))
            {
                throw new InvalidDataException("The Marketplace signing key does not match its pinned public key.");
            }

            using PublicationQueueApi api = new(options);
            Console.WriteLine($"Marketplace publication signer '{options.WorkerId}' is ready.");
            while (!cancellation.IsCancellationRequested)
            {
                PublicationLease? lease;
                try
                {
                    lease = await api.LeaseAsync(signingKey.PublicDocument.KeyId, cancellation.Token);
                }
                catch (Exception exception) when (exception is not OperationCanceledException)
                {
                    Console.Error.WriteLine($"Publication queue polling failed safely: {exception.Message}");
                    await Task.Delay(options.IdlePollInterval, cancellation.Token);
                    continue;
                }

                if (lease is null)
                {
                    await Task.Delay(options.IdlePollInterval, cancellation.Token);
                    continue;
                }

                try
                {
                    VerifyValidatorAttestation(lease, options.ValidatorVerificationKey);
                    string signedManifest = SignPublication(lease, signingKey);
                    await api.RenewAsync(lease.JobId, cancellation.Token);
                    await api.CompleteAsync(lease.JobId, signedManifest, cancellation.Token);
                    Console.WriteLine($"Published Marketplace version {lease.VersionId:D} from job {lease.JobId:D}.");
                }
                catch (Exception exception) when (exception is not OperationCanceledException)
                {
                    bool retryable = exception is HttpRequestException or IOException or TimeoutException;
                    string errorCode = retryable ? "publication.transient_failure" : "publication.evidence_invalid";
                    Console.Error.WriteLine($"Publication job {lease.JobId:D} failed safely: {exception.Message}");
                    try
                    {
                        await api.FailAsync(lease.JobId, errorCode, retryable, cancellation.Token);
                    }
                    catch (Exception failureException) when (failureException is not OperationCanceledException)
                    {
                        Console.Error.WriteLine($"Publication failure reporting was rejected: {failureException.Message}");
                    }
                }
            }

            return 0;
        }
        catch (OperationCanceledException)
        {
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"Marketplace publication signer stopped: {exception.Message}");
            return 2;
        }
    }

    internal static void VerifyValidatorAttestation(
        PublicationLease lease,
        MarketplaceVerificationKey verificationKey)
    {
        SignedValidatorAttestation attestation = DistributionJson.DeserializeStrict<SignedValidatorAttestation>(
            Encoding.UTF8.GetBytes(lease.ValidationAttestation));
        if (attestation.SchemaVersion != MarketplaceValidationContract.AttestationSchemaVersion ||
            !string.Equals(attestation.Signature.Algorithm, "ed25519", StringComparison.Ordinal) ||
            !string.Equals(attestation.Signature.KeyId, lease.AttestationKeyId, StringComparison.Ordinal) ||
            !string.Equals(attestation.Signature.KeyId, verificationKey.Document.KeyId, StringComparison.Ordinal) ||
            !verificationKey.VerifyBase64(Encoding.UTF8.GetBytes(attestation.Document), attestation.Signature.Value))
        {
            throw new InvalidDataException("The approved validator attestation signature is invalid.");
        }

        ValidatorAttestationDocument document = DistributionJson.DeserializeStrict<ValidatorAttestationDocument>(
            Encoding.UTF8.GetBytes(attestation.Document));
        if (document.SchemaVersion != MarketplaceValidationContract.AttestationSchemaVersion ||
            document.VersionId != lease.VersionId.ToString("D") || document.StorageBucket != lease.StorageBucket ||
            document.StoragePath != lease.StoragePath || document.PackageSha256 != lease.ArtifactSha256 ||
            document.PackageSizeBytes != lease.ArtifactSizeBytes || document.ManifestSha256 != lease.ManifestSha256 ||
            document.EvidenceStoragePath != lease.EvidenceStoragePath ||
            document.EvidenceSha256 != lease.EvidenceSha256 || document.EvidenceSizeBytes != lease.EvidenceSizeBytes ||
            !document.Passed || document.MalwareScanResult != ValidationStatuses.Clean ||
            document.SecretScanResult != ValidationStatuses.Clean ||
            document.ManagedValidationResult is not (ValidationStatuses.Passed or ValidationStatuses.NotApplicable))
        {
            throw new InvalidDataException("The approved validator attestation does not match the publication lease.");
        }
    }

    internal static string SignPublication(PublicationLease lease, MarketplaceSigningKey signingKey)
    {
        string expiresAt = lease.PublicationExpiresAt.UtcDateTime.ToString(
            "yyyy-MM-dd'T'HH:mm:ss.fffffff'Z'",
            System.Globalization.CultureInfo.InvariantCulture);
        PublicationDocument document = new()
        {
            KeyId = signingKey.PublicDocument.KeyId,
            Sequence = lease.PublicationSequence,
            ExpiresAt = expiresAt,
            ProductId = lease.ProductId.ToString("D"),
            VersionId = lease.VersionId.ToString("D"),
            ArtifactSha256 = lease.ArtifactSha256,
            ArtifactSizeBytes = lease.ArtifactSizeBytes,
            ManifestSha256 = lease.ManifestSha256,
            ReleaseStoragePath = lease.StoragePath,
        };
        string documentText = Encoding.UTF8.GetString(DistributionJson.Serialize(document));
        SignedPublicationEnvelope envelope = new()
        {
            Document = documentText,
            Signature = new PublicationSignature
            {
                KeyId = signingKey.PublicDocument.KeyId,
                Value = signingKey.SignBase64(Encoding.UTF8.GetBytes(documentText)),
                Sequence = lease.PublicationSequence,
                ExpiresAt = expiresAt,
            },
        };
        return Encoding.UTF8.GetString(DistributionJson.Serialize(envelope));
    }
}

internal sealed class PublicationDocument
{
    [JsonPropertyName("schemaVersion")]
    public int SchemaVersion { get; init; } = 1;

    [JsonPropertyName("keyId")]
    public required string KeyId { get; init; }

    [JsonPropertyName("sequence")]
    public required long Sequence { get; init; }

    [JsonPropertyName("expiresAt")]
    public required string ExpiresAt { get; init; }

    [JsonPropertyName("productId")]
    public required string ProductId { get; init; }

    [JsonPropertyName("versionId")]
    public required string VersionId { get; init; }

    [JsonPropertyName("artifactSha256")]
    public required string ArtifactSha256 { get; init; }

    [JsonPropertyName("artifactSizeBytes")]
    public required long ArtifactSizeBytes { get; init; }

    [JsonPropertyName("manifestSha256")]
    public required string ManifestSha256 { get; init; }

    [JsonPropertyName("releaseStoragePath")]
    public required string ReleaseStoragePath { get; init; }
}

internal sealed class SignedPublicationEnvelope
{
    [JsonPropertyName("schemaVersion")]
    public int SchemaVersion { get; init; } = 1;

    [JsonPropertyName("document")]
    public required string Document { get; init; }

    [JsonPropertyName("signature")]
    public required PublicationSignature Signature { get; init; }
}

internal sealed class PublicationSignature
{
    [JsonPropertyName("algorithm")]
    public string Algorithm { get; init; } = "ed25519";

    [JsonPropertyName("keyId")]
    public required string KeyId { get; init; }

    [JsonPropertyName("value")]
    public required string Value { get; init; }

    [JsonPropertyName("sequence")]
    public required long Sequence { get; init; }

    [JsonPropertyName("expiresAt")]
    public required string ExpiresAt { get; init; }
}
