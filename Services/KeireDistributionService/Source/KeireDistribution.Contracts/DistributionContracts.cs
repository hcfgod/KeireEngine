using System.Text.Json.Serialization;

namespace Keire.Distribution;

public static class DistributionContract
{
    public const int SnapshotSchemaVersion = 1;
    public const int SignaturesSchemaVersion = 1;
    public const int PublicKeySchemaVersion = 1;
    public const string SignatureAlgorithm = "Ed25519";
    public const string SignaturesFileName = "signatures.json";
    public const string SnapshotManifestFileName = "snapshot.json";
    public const string CurrentPointerFileName = "current";
}

public sealed class DistributionPublicKeyDocument
{
    [JsonPropertyName("schemaVersion")]
    public int SchemaVersion { get; init; }

    [JsonPropertyName("algorithm")]
    public required string Algorithm { get; init; }

    [JsonPropertyName("keyId")]
    public required string KeyId { get; init; }

    [JsonPropertyName("publicKey")]
    public required string PublicKey { get; init; }

    [JsonPropertyName("fingerprint")]
    public required string Fingerprint { get; init; }
}

public static class DistributionFileKinds
{
    public const string Catalog = "catalog";
    public const string Content = "content";
    public const string Package = "package";
}

public sealed class DistributionSnapshotManifest
{
    [JsonPropertyName("schemaVersion")]
    public int SchemaVersion { get; init; }

    [JsonPropertyName("snapshotId")]
    public required string SnapshotId { get; init; }

    [JsonPropertyName("createdAt")]
    public DateTimeOffset CreatedAt { get; init; }

    [JsonPropertyName("files")]
    public required List<DistributionFileManifest> Files { get; init; }
}

public sealed class DistributionFileManifest
{
    [JsonPropertyName("path")]
    public required string Path { get; init; }

    [JsonPropertyName("kind")]
    public required string Kind { get; init; }

    [JsonPropertyName("size")]
    public long Size { get; init; }

    [JsonPropertyName("sha256")]
    public required string Sha256 { get; init; }

    [JsonPropertyName("signature")]
    public SignedDocumentMetadata? Signature { get; init; }
}

public sealed class SignedDocumentMetadata
{
    [JsonPropertyName("algorithm")]
    public required string Algorithm { get; init; }

    [JsonPropertyName("keyId")]
    public required string KeyId { get; init; }

    [JsonPropertyName("value")]
    public required string Value { get; init; }

    [JsonPropertyName("sequence")]
    public long Sequence { get; init; }

    [JsonPropertyName("expiresAt")]
    public DateTimeOffset ExpiresAt { get; init; }
}

public sealed class DistributionSignaturesManifest
{
    [JsonPropertyName("schemaVersion")]
    public int SchemaVersion { get; init; }

    [JsonPropertyName("documents")]
    public required List<SignedDocumentEntry> Documents { get; init; }
}

public sealed class SignedDocumentEntry
{
    [JsonPropertyName("path")]
    public required string Path { get; init; }

    [JsonPropertyName("algorithm")]
    public required string Algorithm { get; init; }

    [JsonPropertyName("keyId")]
    public required string KeyId { get; init; }

    [JsonPropertyName("signature")]
    public required string Signature { get; init; }

    [JsonPropertyName("sequence")]
    public long Sequence { get; init; }

    [JsonPropertyName("expiresAt")]
    public DateTimeOffset ExpiresAt { get; init; }

    public SignedDocumentMetadata ToMetadata()
    {
        return new SignedDocumentMetadata
        {
            Algorithm = Algorithm,
            KeyId = KeyId,
            Value = Signature,
            Sequence = Sequence,
            ExpiresAt = ExpiresAt,
        };
    }
}
