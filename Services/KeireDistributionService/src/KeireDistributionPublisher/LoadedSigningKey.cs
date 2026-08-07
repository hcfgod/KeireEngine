using System.Security.Cryptography;
using Keire.Distribution;
using NSec.Cryptography;

namespace Keire.Distribution.Publisher;

internal sealed class LoadedSigningKey : IDisposable
{
    private readonly Key m_Key;

    private LoadedSigningKey(Key key, DistributionPublicKeyDocument publicDocument)
    {
        m_Key = key;
        PublicDocument = publicDocument;
    }

    public DistributionPublicKeyDocument PublicDocument { get; }

    public static LoadedSigningKey Create()
    {
        KeyCreationParameters parameters = new()
        {
            ExportPolicy = KeyExportPolicies.AllowPlaintextArchiving,
        };
        Key key = Key.Create(SignatureAlgorithm.Ed25519, parameters);
        try
        {
            return new LoadedSigningKey(key, DistributionPublicKeys.Create(key.PublicKey));
        }
        catch
        {
            key.Dispose();
            throw;
        }
    }

    public static LoadedSigningKey Import(ReadOnlySpan<byte> privateKey)
    {
        Key key = Key.Import(SignatureAlgorithm.Ed25519, privateKey, KeyBlobFormat.PkixPrivateKey);
        try
        {
            return new LoadedSigningKey(key, DistributionPublicKeys.Create(key.PublicKey));
        }
        catch
        {
            key.Dispose();
            throw;
        }
    }

    public byte[] ExportPrivateKey()
    {
        return m_Key.Export(KeyBlobFormat.PkixPrivateKey);
    }

    public byte[] Sign(ReadOnlySpan<byte> data)
    {
        return SignatureAlgorithm.Ed25519.Sign(m_Key, data);
    }

    public void Dispose()
    {
        m_Key.Dispose();
    }
}

internal sealed class LoadedPublicKey
{
    private readonly PublicKey m_Key;

    public LoadedPublicKey(DistributionPublicKeyDocument document, PublicKey key)
    {
        Document = document;
        m_Key = key;
    }

    public DistributionPublicKeyDocument Document { get; }

    public bool Verify(ReadOnlySpan<byte> data, ReadOnlySpan<byte> signature)
    {
        return SignatureAlgorithm.Ed25519.Verify(m_Key, data, signature);
    }
}

internal static class DistributionPublicKeys
{
    public static DistributionPublicKeyDocument Create(PublicKey key)
    {
        byte[] raw = key.Export(KeyBlobFormat.RawPublicKey);
        try
        {
            byte[] fingerprint = SHA256.HashData(raw);
            string fingerprintHex = Convert.ToHexStringLower(fingerprint);
            return new DistributionPublicKeyDocument
            {
                SchemaVersion = DistributionContract.PublicKeySchemaVersion,
                Algorithm = DistributionContract.SignatureAlgorithm,
                KeyId = $"ed25519-{fingerprintHex[..32]}",
                PublicKey = Convert.ToBase64String(raw),
                Fingerprint = $"sha256:{fingerprintHex}",
            };
        }
        finally
        {
            CryptographicOperations.ZeroMemory(raw);
        }
    }

    public static LoadedPublicKey Load(string path)
    {
        FileInfo file = new(Path.GetFullPath(path));
        if (!file.Exists || file.Length is <= 0 or > 32 * 1024)
        {
            throw new InvalidDataException("The trusted public-key file is missing or outside its size limit.");
        }

        FileSystemSafety.RejectLink(file);
        DistributionPublicKeyDocument document;
        try
        {
            document = DistributionJson.DeserializeStrict<DistributionPublicKeyDocument>(File.ReadAllBytes(file.FullName));
        }
        catch (System.Text.Json.JsonException exception)
        {
            throw new InvalidDataException("The trusted public-key file is malformed.", exception);
        }

        if (document.SchemaVersion != DistributionContract.PublicKeySchemaVersion ||
            !string.Equals(document.Algorithm, DistributionContract.SignatureAlgorithm, StringComparison.Ordinal) ||
            !DistributionPaths.IsKeyId(document.KeyId) ||
            string.IsNullOrEmpty(document.PublicKey) || string.IsNullOrEmpty(document.Fingerprint))
        {
            throw new InvalidDataException("The trusted public-key document metadata is invalid.");
        }

        byte[] raw;
        try
        {
            raw = Convert.FromBase64String(document.PublicKey);
        }
        catch (FormatException exception)
        {
            throw new InvalidDataException("The trusted public key is not valid base64.", exception);
        }

        try
        {
            if (raw.Length != 32 ||
                !string.Equals(Convert.ToBase64String(raw), document.PublicKey, StringComparison.Ordinal))
            {
                throw new InvalidDataException("The trusted Ed25519 public key must be canonical 32-byte base64.");
            }

            byte[] fingerprint = SHA256.HashData(raw);
            string fingerprintHex = Convert.ToHexStringLower(fingerprint);
            string expectedKeyId = $"ed25519-{fingerprintHex[..32]}";
            string expectedFingerprint = $"sha256:{fingerprintHex}";
            if (!string.Equals(document.KeyId, expectedKeyId, StringComparison.Ordinal) ||
                !string.Equals(document.Fingerprint, expectedFingerprint, StringComparison.Ordinal))
            {
                throw new InvalidDataException("The trusted public-key ID or fingerprint is invalid.");
            }

            PublicKey key = PublicKey.Import(SignatureAlgorithm.Ed25519, raw, KeyBlobFormat.RawPublicKey);
            return new LoadedPublicKey(document, key);
        }
        catch (Exception exception) when (exception is CryptographicException or ArgumentException or FormatException)
        {
            throw new InvalidDataException("The trusted Ed25519 public key is invalid.", exception);
        }
        finally
        {
            CryptographicOperations.ZeroMemory(raw);
        }
    }
}
