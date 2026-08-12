using System.Text.Json.Serialization;

namespace Keire.Marketplace.Validation;

internal sealed class ExtractedPackageDocument
{
    [JsonPropertyName("schemaVersion")]
    public required uint SchemaVersion { get; init; }

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
    public required List<PackageDependency> Dependencies { get; init; }

    [JsonPropertyName("conflicts")]
    public required List<PackageDependency> Conflicts { get; init; }

    [JsonPropertyName("files")]
    public required List<PackageFile> Files { get; init; }

    [JsonPropertyName("assets")]
    public required List<PackageAsset> Assets { get; init; }

    [JsonPropertyName("samples")]
    public required List<PackageSample> Samples { get; init; }

    [JsonPropertyName("managedAssemblies")]
    public required List<PackageManagedAssembly> ManagedAssemblies { get; init; }

    [JsonPropertyName("licenses")]
    public required List<PackageLicense> Licenses { get; init; }

    [JsonPropertyName("entryPoints")]
    public required List<string> EntryPoints { get; init; }

    [JsonPropertyName("installedSizeBytes")]
    public required ulong InstalledSizeBytes { get; init; }

    [JsonPropertyName("signatureKeyId")]
    public required string SignatureKeyId { get; init; }

    [JsonPropertyName("archive")]
    public required PackageArchive Archive { get; init; }

    [JsonPropertyName("stagingRoot")]
    public required string StagingRoot { get; init; }
}

internal sealed class PackageCompatibility
{
    [JsonPropertyName("minimumEngineVersion")]
    public required string MinimumEngineVersion { get; init; }

    [JsonPropertyName("maximumEngineVersion")]
    public string? MaximumEngineVersion { get; init; }

    [JsonPropertyName("platforms")]
    public required List<string> Platforms { get; init; }

    [JsonPropertyName("architectures")]
    public required List<string> Architectures { get; init; }

    [JsonPropertyName("rendererCapabilities")]
    public required List<string> RendererCapabilities { get; init; }

    [JsonPropertyName("managedApiVersion")]
    public required string ManagedApiVersion { get; init; }
}

internal sealed class PackageDependency
{
    [JsonPropertyName("packageId")]
    public required string PackageId { get; init; }

    [JsonPropertyName("version")]
    public required string Version { get; init; }
}

internal sealed class PackageFile
{
    [JsonPropertyName("path")]
    public required string Path { get; init; }

    [JsonPropertyName("sizeBytes")]
    public required ulong SizeBytes { get; init; }

    [JsonPropertyName("sha256")]
    public required string Sha256 { get; init; }

    [JsonPropertyName("mode")]
    public required uint Mode { get; init; }
}

internal sealed class PackageAsset
{
    [JsonPropertyName("id")]
    public required string Id { get; init; }

    [JsonPropertyName("type")]
    public required string Type { get; init; }

    [JsonPropertyName("source")]
    public required string Source { get; init; }

    [JsonPropertyName("metadata")]
    public required string Metadata { get; init; }

    [JsonPropertyName("dependencies")]
    public required List<string> Dependencies { get; init; }
}

internal sealed class PackageSample
{
    [JsonPropertyName("id")]
    public required string Id { get; init; }

    [JsonPropertyName("displayName")]
    public required string DisplayName { get; init; }

    [JsonPropertyName("description")]
    public required string Description { get; init; }

    [JsonPropertyName("root")]
    public required string Root { get; init; }
}

internal sealed class PackageManagedAssembly
{
    [JsonPropertyName("name")]
    public required string Name { get; init; }

    [JsonPropertyName("definition")]
    public required string Definition { get; init; }

    [JsonPropertyName("scope")]
    public required string Scope { get; init; }
}

internal sealed class PackageLicense
{
    [JsonPropertyName("id")]
    public required string Id { get; init; }

    [JsonPropertyName("path")]
    public required string Path { get; init; }
}

internal sealed class PackageArchive
{
    [JsonPropertyName("sizeBytes")]
    public required ulong SizeBytes { get; init; }

    [JsonPropertyName("sha256")]
    public required string Sha256 { get; init; }

    [JsonPropertyName("manifestSha256")]
    public required string ManifestSha256 { get; init; }
}

internal sealed class ManagedAssemblyDefinitionDocument
{
    [JsonPropertyName("schemaVersion")]
    public required uint SchemaVersion { get; init; }

    [JsonPropertyName("name")]
    public required string Name { get; init; }

    [JsonPropertyName("rootNamespace")]
    public string? RootNamespace { get; init; }

    [JsonPropertyName("classification")]
    public string? Classification { get; init; }

    [JsonPropertyName("sourceRoots")]
    public required List<string> SourceRoots { get; init; }

    [JsonPropertyName("references")]
    public List<string>? References { get; init; }

    [JsonPropertyName("packages")]
    public List<ManagedPackageReference>? Packages { get; init; }

    [JsonPropertyName("defineSymbols")]
    public List<string>? DefineSymbols { get; init; }

    [JsonPropertyName("allowUnsafe")]
    public bool? AllowUnsafe { get; init; }
}

internal sealed class ManagedPackageReference
{
    [JsonPropertyName("name")]
    public required string Name { get; init; }

    [JsonPropertyName("version")]
    public required string Version { get; init; }
}
