using System.Text.Json.Serialization;

namespace Keire.Marketplace.Validation;

public static class MarketplaceValidationContract
{
    public const int ReportSchemaVersion = 2;
    public const int AttestationSchemaVersion = 1;
    public const int EvidenceSchemaVersion = 1;
    public const string ValidatorVersion = "0.4.0";
    public const string PolicyVersion = "marketplace-2026-08-13";
    public const long MaximumPackageBytes = 64L * 1024 * 1024 * 1024;
    public const int MaximumEvidenceBytes = 8 * 1024 * 1024;
    public const long MaximumManagedDefinitionBytes = 1024 * 1024;
    public const int MaximumExtractedEntries = 1_000_000;
}

public static class ValidationStatuses
{
    public const string Clean = "clean";
    public const string Failed = "error";
    public const string Infected = "infected";
    public const string NotApplicable = "not_applicable";
    public const string Passed = "passed";
}

public static class ValidationSeverities
{
    public const string Error = "error";
    public const string Warning = "warning";
}

public sealed class ValidationDiagnostic
{
    [JsonPropertyName("code")]
    public required string Code { get; init; }

    [JsonPropertyName("severity")]
    public required string Severity { get; init; }

    [JsonPropertyName("message")]
    public required string Message { get; init; }

    [JsonPropertyName("path")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? Path { get; init; }
}

public sealed record MarketplaceValidationReport
{
    [JsonPropertyName("schemaVersion")]
    public int SchemaVersion { get; init; } = MarketplaceValidationContract.ReportSchemaVersion;

    [JsonPropertyName("validatorVersion")]
    public string ValidatorVersion { get; init; } = MarketplaceValidationContract.ValidatorVersion;

    [JsonPropertyName("validatorFingerprintSha256")]
    public required string ValidatorFingerprintSha256 { get; init; }

    [JsonPropertyName("policyVersion")]
    public string PolicyVersion { get; init; } = MarketplaceValidationContract.PolicyVersion;

    [JsonPropertyName("packageSha256")]
    public required string PackageSha256 { get; init; }

    [JsonPropertyName("manifestSha256")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? ManifestSha256 { get; init; }

    [JsonPropertyName("passed")]
    public required bool Passed { get; init; }

    [JsonPropertyName("malwareScanResult")]
    public required string MalwareScanResult { get; init; }

    [JsonPropertyName("secretScanResult")]
    public required string SecretScanResult { get; init; }

    [JsonPropertyName("managedValidationResult")]
    public required string ManagedValidationResult { get; init; }

    [JsonPropertyName("codeFingerprintSha256")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public string? CodeFingerprintSha256 { get; init; }

    [JsonPropertyName("evidenceStoragePath")]
    public required string EvidenceStoragePath { get; init; }

    [JsonPropertyName("evidenceSha256")]
    public required string EvidenceSha256 { get; init; }

    [JsonPropertyName("evidenceSizeBytes")]
    public required long EvidenceSizeBytes { get; init; }

    [JsonPropertyName("attestation")]
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public SignedValidatorAttestation? Attestation { get; init; }

    [JsonPropertyName("diagnostics")]
    public required IReadOnlyList<ValidationDiagnostic> Diagnostics { get; init; }

    [JsonPropertyName("completedAt")]
    public required DateTimeOffset CompletedAt { get; init; }
}

public sealed record PackageValidationRequest(
    Guid UploadId,
    Guid VersionId,
    string StorageBucket,
    string StoragePath,
    string PackagePath,
    string WorkRoot,
    string AssetToolPath,
    string DotnetPath,
    string? ManagedApiPath,
    string MalwareScannerPath,
    long ExpectedPackageBytes,
    string ExpectedPackageSha256,
    string ValidatorFingerprintSha256);

public sealed record MarketplaceValidationOutput(
    MarketplaceValidationReport Report,
    byte[] ReviewEvidence);

public sealed class SignedValidatorAttestation
{
    [JsonPropertyName("schemaVersion")]
    public int SchemaVersion { get; init; } = MarketplaceValidationContract.AttestationSchemaVersion;

    [JsonPropertyName("document")]
    public required string Document { get; init; }

    [JsonPropertyName("signature")]
    public required ValidatorAttestationSignature Signature { get; init; }
}

public sealed class ValidatorAttestationSignature
{
    [JsonPropertyName("algorithm")]
    public string Algorithm { get; init; } = "ed25519";

    [JsonPropertyName("keyId")]
    public required string KeyId { get; init; }

    [JsonPropertyName("value")]
    public required string Value { get; init; }
}

public sealed class ValidatorAttestationDocument
{
    [JsonPropertyName("schemaVersion")]
    public int SchemaVersion { get; init; } = MarketplaceValidationContract.AttestationSchemaVersion;

    [JsonPropertyName("keyId")]
    public required string KeyId { get; init; }

    [JsonPropertyName("uploadId")]
    public required string UploadId { get; init; }

    [JsonPropertyName("versionId")]
    public required string VersionId { get; init; }

    [JsonPropertyName("storageBucket")]
    public required string StorageBucket { get; init; }

    [JsonPropertyName("storagePath")]
    public required string StoragePath { get; init; }

    [JsonPropertyName("packageSha256")]
    public required string PackageSha256 { get; init; }

    [JsonPropertyName("packageSizeBytes")]
    public required long PackageSizeBytes { get; init; }

    [JsonPropertyName("manifestSha256")]
    public string? ManifestSha256 { get; init; }

    [JsonPropertyName("evidenceStoragePath")]
    public required string EvidenceStoragePath { get; init; }

    [JsonPropertyName("evidenceSha256")]
    public required string EvidenceSha256 { get; init; }

    [JsonPropertyName("evidenceSizeBytes")]
    public required long EvidenceSizeBytes { get; init; }

    [JsonPropertyName("validatorVersion")]
    public required string ValidatorVersion { get; init; }

    [JsonPropertyName("validatorFingerprintSha256")]
    public required string ValidatorFingerprintSha256 { get; init; }

    [JsonPropertyName("policyVersion")]
    public required string PolicyVersion { get; init; }

    [JsonPropertyName("malwareScanResult")]
    public required string MalwareScanResult { get; init; }

    [JsonPropertyName("secretScanResult")]
    public required string SecretScanResult { get; init; }

    [JsonPropertyName("managedValidationResult")]
    public required string ManagedValidationResult { get; init; }

    [JsonPropertyName("codeFingerprintSha256")]
    public string? CodeFingerprintSha256 { get; init; }

    [JsonPropertyName("passed")]
    public required bool Passed { get; init; }

    [JsonPropertyName("completedAt")]
    public required DateTimeOffset CompletedAt { get; init; }
}

public sealed record MalwareScanResult(string Status, string? EngineVersion, IReadOnlyList<ValidationDiagnostic> Diagnostics);

public interface IMalwareScanner
{
    Task<MalwareScanResult> ScanFileAsync(string path, CancellationToken cancellationToken);

    Task<MalwareScanResult> ScanDirectoryAsync(string path, CancellationToken cancellationToken);
}
