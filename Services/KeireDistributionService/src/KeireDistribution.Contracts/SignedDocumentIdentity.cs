using System.Globalization;
using System.Text.Json;

namespace Keire.Distribution;

public sealed record SignedDocumentIdentity(
    int SchemaVersion,
    string KeyId,
    long Sequence,
    DateTimeOffset ExpiresAt)
{
    public static SignedDocumentIdentity Parse(byte[] bytes, string documentName)
    {
        ArgumentNullException.ThrowIfNull(bytes);
        if (string.IsNullOrWhiteSpace(documentName))
        {
            throw new ArgumentException("A document name is required.", nameof(documentName));
        }

        try
        {
            using JsonDocument document = DistributionJson.ParseStrict(bytes, 128);
            if (document.RootElement.ValueKind != JsonValueKind.Object)
            {
                throw InvalidIdentity(documentName);
            }

            JsonElement root = document.RootElement;
            if (!root.TryGetProperty("schemaVersion", out JsonElement schemaVersion) ||
                !schemaVersion.TryGetInt32(out int schema) || schema <= 0 ||
                !root.TryGetProperty("keyId", out JsonElement keyId) || keyId.ValueKind != JsonValueKind.String ||
                !DistributionPaths.IsKeyId(keyId.GetString()) ||
                !root.TryGetProperty("sequence", out JsonElement sequence) ||
                !sequence.TryGetInt64(out long sequenceValue) || sequenceValue < 0 ||
                !root.TryGetProperty("expiresAt", out JsonElement expiresAt) ||
                expiresAt.ValueKind != JsonValueKind.String ||
                !HasExplicitUtcOffset(expiresAt.GetString()) ||
                !DateTimeOffset.TryParse(
                    expiresAt.GetString(),
                    CultureInfo.InvariantCulture,
                    DateTimeStyles.RoundtripKind,
                    out DateTimeOffset expiry) ||
                expiry == default || expiry.Offset != TimeSpan.Zero)
            {
                throw InvalidIdentity(documentName);
            }

            return new SignedDocumentIdentity(schema, keyId.GetString()!, sequenceValue, expiry);
        }
        catch (JsonException exception)
        {
            throw new InvalidDataException($"Distribution document is malformed: '{documentName}'.", exception);
        }
    }

    public void ValidateMatches(SignedDocumentMetadata signature, string documentName)
    {
        ArgumentNullException.ThrowIfNull(signature);
        if (!string.Equals(KeyId, signature.KeyId, StringComparison.Ordinal) ||
            Sequence != signature.Sequence || ExpiresAt != signature.ExpiresAt)
        {
            throw new InvalidDataException(
                $"Distribution document signed identity does not match its detached signature metadata: '{documentName}'.");
        }
    }

    private static InvalidDataException InvalidIdentity(string documentName)
    {
        return new InvalidDataException($"Distribution document signed identity is invalid: '{documentName}'.");
    }

    private static bool HasExplicitUtcOffset(string? value)
    {
        return value is not null &&
            (value.EndsWith('Z') || value.EndsWith("+00:00", StringComparison.Ordinal));
    }
}
