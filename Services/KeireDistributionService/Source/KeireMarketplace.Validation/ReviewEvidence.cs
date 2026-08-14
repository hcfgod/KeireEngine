using System.Text.Json.Serialization;

namespace Keire.Marketplace.Validation;

internal sealed class MarketplaceReviewEvidence
{
    [JsonPropertyName("schemaVersion")]
    public int SchemaVersion { get; init; } = MarketplaceValidationContract.EvidenceSchemaVersion;

    [JsonPropertyName("packageSha256")]
    public required string PackageSha256 { get; init; }

    [JsonPropertyName("manifestSha256")]
    public string? ManifestSha256 { get; init; }

    [JsonPropertyName("validatorVersion")]
    public string ValidatorVersion { get; init; } = MarketplaceValidationContract.ValidatorVersion;

    [JsonPropertyName("validatorFingerprintSha256")]
    public required string ValidatorFingerprintSha256 { get; init; }

    [JsonPropertyName("policyVersion")]
    public string PolicyVersion { get; init; } = MarketplaceValidationContract.PolicyVersion;

    [JsonPropertyName("malwareScanResult")]
    public required string MalwareScanResult { get; init; }

    [JsonPropertyName("malwareEngineVersion")]
    public string? MalwareEngineVersion { get; init; }

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

    [JsonPropertyName("diagnostics")]
    public required IReadOnlyList<ValidationDiagnostic> Diagnostics { get; init; }

    [JsonPropertyName("inventory")]
    public MarketplaceReviewInventory? Inventory { get; init; }

    [JsonPropertyName("counts")]
    public required MarketplaceReviewCounts Counts { get; init; }
}

internal sealed class MarketplaceReviewInventory
{
    [JsonPropertyName("packageId")]
    public required string PackageId { get; init; }

    [JsonPropertyName("version")]
    public required string Version { get; init; }

    [JsonPropertyName("publisherId")]
    public required string PublisherId { get; init; }

    [JsonPropertyName("displayName")]
    public required string DisplayName { get; init; }

    [JsonPropertyName("summary")]
    public required string Summary { get; init; }

    [JsonPropertyName("channel")]
    public required string Channel { get; init; }

    [JsonPropertyName("installKind")]
    public required string InstallKind { get; init; }

    [JsonPropertyName("compatibility")]
    public required PackageCompatibility Compatibility { get; init; }

    [JsonPropertyName("dependencies")]
    public required IReadOnlyList<PackageDependency> Dependencies { get; init; }

    [JsonPropertyName("conflicts")]
    public required IReadOnlyList<PackageDependency> Conflicts { get; init; }

    [JsonPropertyName("files")]
    public required IReadOnlyList<PackageFile> Files { get; init; }

    [JsonPropertyName("assets")]
    public required IReadOnlyList<PackageAsset> Assets { get; init; }

    [JsonPropertyName("samples")]
    public required IReadOnlyList<PackageSample> Samples { get; init; }

    [JsonPropertyName("managedAssemblies")]
    public required IReadOnlyList<PackageManagedAssembly> ManagedAssemblies { get; init; }

    [JsonPropertyName("licenses")]
    public required IReadOnlyList<PackageLicense> Licenses { get; init; }

    [JsonPropertyName("entryPoints")]
    public required IReadOnlyList<string> EntryPoints { get; init; }

    [JsonPropertyName("installedSizeBytes")]
    public required ulong InstalledSizeBytes { get; init; }
}

internal sealed record MarketplaceReviewCounts(
    [property: JsonPropertyName("files")] int Files,
    [property: JsonPropertyName("assets")] int Assets,
    [property: JsonPropertyName("samples")] int Samples,
    [property: JsonPropertyName("managedAssemblies")] int ManagedAssemblies,
    [property: JsonPropertyName("licenses")] int Licenses,
    [property: JsonPropertyName("entryPoints")] int EntryPoints);
