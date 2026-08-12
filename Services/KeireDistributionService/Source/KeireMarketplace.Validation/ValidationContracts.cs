using System.Text.Json.Serialization;

namespace Keire.Marketplace.Validation;

public static class MarketplaceValidationContract
{
    public const int ReportSchemaVersion = 1;
    public const string ValidatorVersion = "0.3.1";
    public const string PolicyVersion = "marketplace-2026-08-12";
    public const long MaximumPackageBytes = 64L * 1024 * 1024 * 1024;
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

public sealed class MarketplaceValidationReport
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

    [JsonPropertyName("diagnostics")]
    public required IReadOnlyList<ValidationDiagnostic> Diagnostics { get; init; }

    [JsonPropertyName("completedAt")]
    public required DateTimeOffset CompletedAt { get; init; }
}

public sealed record PackageValidationRequest(
    string PackagePath,
    string WorkRoot,
    string AssetToolPath,
    string DotnetPath,
    string? ManagedApiPath,
    string MalwareScannerPath,
    long ExpectedPackageBytes,
    string ExpectedPackageSha256,
    string ValidatorFingerprintSha256);

public sealed record MalwareScanResult(string Status, string? EngineVersion, IReadOnlyList<ValidationDiagnostic> Diagnostics);

public interface IMalwareScanner
{
    Task<MalwareScanResult> ScanFileAsync(string path, CancellationToken cancellationToken);

    Task<MalwareScanResult> ScanDirectoryAsync(string path, CancellationToken cancellationToken);
}
