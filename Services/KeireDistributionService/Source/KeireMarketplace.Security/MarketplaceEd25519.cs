using System.Security.Cryptography;
using Keire.Distribution;
using NSec.Cryptography;

namespace Keire.Marketplace.Security;

public sealed class MarketplaceSigningKey : IDisposable
{
    private const int MaximumEnvironmentValueCharacters = 32 * 1024;
    private readonly Key m_Key;

    private MarketplaceSigningKey(Key key, DistributionPublicKeyDocument publicDocument)
    {
        m_Key = key;
        PublicDocument = publicDocument;
    }

    public DistributionPublicKeyDocument PublicDocument { get; }

    public static MarketplaceSigningKey Create()
    {
        Key key = Key.Create(SignatureAlgorithm.Ed25519);
        try
        {
            return new MarketplaceSigningKey(key, CreatePublicDocument(key.PublicKey));
        }
        catch
        {
            key.Dispose();
            throw;
        }
    }

    public static MarketplaceSigningKey FromEnvironment(string variableName)
    {
        string? encoded = Environment.GetEnvironmentVariable(variableName, EnvironmentVariableTarget.Process);
        try
        {
            if (string.IsNullOrEmpty(encoded) || encoded.Length > MaximumEnvironmentValueCharacters ||
                encoded.Any(character => character > 0x7f || char.IsWhiteSpace(character)))
            {
                throw new InvalidDataException($"Private-key environment variable '{variableName}' is missing or invalid.");
            }

            byte[] privateKey;
            try
            {
                privateKey = Convert.FromBase64String(encoded);
            }
            catch (FormatException exception)
            {
                throw new InvalidDataException("The private key is not canonical base64.", exception);
            }

            try
            {
                if (!string.Equals(Convert.ToBase64String(privateKey), encoded, StringComparison.Ordinal))
                {
                    throw new InvalidDataException("The private key is not canonical base64.");
                }

                Key key = Key.Import(SignatureAlgorithm.Ed25519, privateKey, KeyBlobFormat.PkixPrivateKey);
                try
                {
                    return new MarketplaceSigningKey(key, CreatePublicDocument(key.PublicKey));
                }
                catch
                {
                    key.Dispose();
                    throw;
                }
            }
            catch (Exception exception) when (exception is CryptographicException or ArgumentException)
            {
                throw new InvalidDataException("The supplied Ed25519 private key is invalid.", exception);
            }
            finally
            {
                CryptographicOperations.ZeroMemory(privateKey);
            }
        }
        finally
        {
            Environment.SetEnvironmentVariable(variableName, null, EnvironmentVariableTarget.Process);
        }
    }

    public string SignBase64(ReadOnlySpan<byte> exactBytes)
    {
        return Convert.ToBase64String(SignatureAlgorithm.Ed25519.Sign(m_Key, exactBytes));
    }

    public void Dispose()
    {
        m_Key.Dispose();
    }

    private static DistributionPublicKeyDocument CreatePublicDocument(PublicKey key)
    {
        byte[] raw = key.Export(KeyBlobFormat.RawPublicKey);
        try
        {
            string fingerprint = Convert.ToHexStringLower(SHA256.HashData(raw));
            return new DistributionPublicKeyDocument
            {
                SchemaVersion = DistributionContract.PublicKeySchemaVersion,
                Algorithm = DistributionContract.SignatureAlgorithm,
                KeyId = $"ed25519-{fingerprint[..32]}",
                PublicKey = Convert.ToBase64String(raw),
                Fingerprint = $"sha256:{fingerprint}",
            };
        }
        finally
        {
            CryptographicOperations.ZeroMemory(raw);
        }
    }
}

public sealed class MarketplaceVerificationKey
{
    private readonly PublicKey m_Key;

    private MarketplaceVerificationKey(DistributionPublicKeyDocument document, PublicKey key)
    {
        Document = document;
        m_Key = key;
    }

    public DistributionPublicKeyDocument Document { get; }

    public static MarketplaceVerificationKey FromFile(string path)
    {
        FileInfo file = new(Path.GetFullPath(path));
        if (!file.Exists || file.Length is <= 0 or > 32 * 1024)
        {
            throw new InvalidDataException("The trusted public-key file is missing or outside its size limit.");
        }

        FileSystemSafety.RejectLink(file);
        DistributionPublicKeyDocument document = DistributionJson.DeserializeStrict<DistributionPublicKeyDocument>(
            File.ReadAllBytes(file.FullName));
        return FromDocument(document);
    }

    public static MarketplaceVerificationKey FromDocument(DistributionPublicKeyDocument document)
    {
        if (document.SchemaVersion != DistributionContract.PublicKeySchemaVersion ||
            !string.Equals(document.Algorithm, DistributionContract.SignatureAlgorithm, StringComparison.Ordinal) ||
            !DistributionPaths.IsKeyId(document.KeyId))
        {
            throw new InvalidDataException("The trusted Ed25519 public-key metadata is invalid.");
        }

        byte[] raw;
        try
        {
            raw = Convert.FromBase64String(document.PublicKey);
        }
        catch (FormatException exception)
        {
            throw new InvalidDataException("The trusted public key is not canonical base64.", exception);
        }

        try
        {
            string fingerprint = Convert.ToHexStringLower(SHA256.HashData(raw));
            if (raw.Length != 32 || !string.Equals(Convert.ToBase64String(raw), document.PublicKey, StringComparison.Ordinal) ||
                !string.Equals(document.KeyId, $"ed25519-{fingerprint[..32]}", StringComparison.Ordinal) ||
                !string.Equals(document.Fingerprint, $"sha256:{fingerprint}", StringComparison.Ordinal))
            {
                throw new InvalidDataException("The trusted Ed25519 public-key identity is invalid.");
            }

            return new MarketplaceVerificationKey(
                document,
                PublicKey.Import(SignatureAlgorithm.Ed25519, raw, KeyBlobFormat.RawPublicKey));
        }
        catch (Exception exception) when (exception is CryptographicException or ArgumentException)
        {
            throw new InvalidDataException("The trusted Ed25519 public key is invalid.", exception);
        }
        finally
        {
            CryptographicOperations.ZeroMemory(raw);
        }
    }

    public bool VerifyBase64(ReadOnlySpan<byte> exactBytes, string signature)
    {
        byte[] raw;
        try
        {
            raw = Convert.FromBase64String(signature);
        }
        catch (FormatException)
        {
            return false;
        }

        try
        {
            return raw.Length == 64 && string.Equals(Convert.ToBase64String(raw), signature, StringComparison.Ordinal) &&
                SignatureAlgorithm.Ed25519.Verify(m_Key, exactBytes, raw);
        }
        finally
        {
            CryptographicOperations.ZeroMemory(raw);
        }
    }
}
