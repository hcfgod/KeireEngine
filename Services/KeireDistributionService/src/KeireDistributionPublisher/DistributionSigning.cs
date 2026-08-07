using System.Security.Cryptography;
using System.Text.Json;
using Keire.Distribution;

namespace Keire.Distribution.Publisher;

public sealed record SigningResult(string KeyId, int DocumentCount);

public static class DistributionSigning
{
    private const long MaximumDocumentBytes = 32 * 1024 * 1024;
    private const long MaximumSignaturesBytes = 8 * 1024 * 1024;
    private const int MaximumDocuments = 100_000;

    public static DistributionPublicKeyDocument GenerateKey(string privateKeyPath, string publicKeyPath)
    {
        EnsureDistinctOutputs(privateKeyPath, publicKeyPath);
        EnsureOutputDoesNotExist(privateKeyPath);
        EnsureOutputDoesNotExist(publicKeyPath);

        using LoadedSigningKey key = LoadedSigningKey.Create();
        byte[] privateKey = key.ExportPrivateKey();
        byte[] pem = [];
        try
        {
            pem = SigningKeySource.EncodePem(privateKey);
            AtomicOutputFile.WriteNew(privateKeyPath, pem, privateKey: true);
            AtomicOutputFile.WriteNew(publicKeyPath, DistributionJson.Serialize(key.PublicDocument), privateKey: false);
            return key.PublicDocument;
        }
        finally
        {
            CryptographicOperations.ZeroMemory(privateKey);
            CryptographicOperations.ZeroMemory(pem);
        }
    }

    public static DistributionPublicKeyDocument DerivePublicKey(SigningKeySource source, string publicKeyPath)
    {
        ArgumentNullException.ThrowIfNull(source);
        EnsureOutputDoesNotExist(publicKeyPath);
        using LoadedSigningKey key = source.Load();
        AtomicOutputFile.WriteNew(publicKeyPath, DistributionJson.Serialize(key.PublicDocument), privateKey: false);
        return key.PublicDocument;
    }

    public static SigningResult SignDirectory(
        string sourceDirectory,
        string signaturesPath,
        SigningKeySource keySource,
        SigningPolicy policy)
    {
        ArgumentNullException.ThrowIfNull(keySource);
        ArgumentNullException.ThrowIfNull(policy);
        EnsureOutputDoesNotExist(signaturesPath);

        string source = ValidateSourceDirectory(sourceDirectory);
        IReadOnlyList<string> documents = EnumerateDocuments(source, excludedPath: null);
        using LoadedSigningKey key = keySource.Load();
        List<SignedDocumentEntry> entries = new(documents.Count);
        foreach (string relativePath in documents)
        {
            byte[] bytes = ReadDocument(source, relativePath);
            SignedDocumentIdentity identity = SignedDocumentIdentity.Parse(bytes, relativePath);
            policy.Validate(identity, relativePath);
            if (!string.Equals(identity.KeyId, key.PublicDocument.KeyId, StringComparison.Ordinal))
            {
                throw new InvalidDataException(
                    $"Distribution document key ID does not match the supplied private key: '{relativePath}'.");
            }

            byte[] signature = key.Sign(bytes);
            try
            {
                entries.Add(new SignedDocumentEntry
                {
                    Path = relativePath,
                    Algorithm = DistributionContract.SignatureAlgorithm,
                    KeyId = identity.KeyId,
                    Signature = Convert.ToBase64String(signature),
                    Sequence = identity.Sequence,
                    ExpiresAt = identity.ExpiresAt,
                });
            }
            finally
            {
                CryptographicOperations.ZeroMemory(signature);
            }
        }

        DistributionSignaturesManifest manifest = new()
        {
            SchemaVersion = DistributionContract.SignaturesSchemaVersion,
            Documents = entries,
        };
        AtomicOutputFile.WriteNew(signaturesPath, DistributionJson.Serialize(manifest), privateKey: false);
        return new SigningResult(key.PublicDocument.KeyId, entries.Count);
    }

    public static SigningResult VerifyDirectory(
        string sourceDirectory,
        string signaturesPath,
        string publicKeyPath,
        SigningPolicy policy)
    {
        ArgumentNullException.ThrowIfNull(policy);
        string source = ValidateSourceDirectory(sourceDirectory);
        string signatures = Path.GetFullPath(signaturesPath);
        IReadOnlyList<string> documents = EnumerateDocuments(source, signatures);
        Dictionary<string, SignedDocumentMetadata> signaturesByPath = LoadSignatures(signatures);
        LoadedPublicKey key = DistributionPublicKeys.Load(publicKeyPath);
        foreach (string relativePath in documents)
        {
            if (!signaturesByPath.Remove(relativePath, out SignedDocumentMetadata? metadata))
            {
                throw new InvalidDataException($"Signed document is missing detached signature metadata: '{relativePath}'.");
            }

            byte[] bytes = ReadDocument(source, relativePath);
            VerifyDocument(bytes, relativePath, metadata, key, policy);
        }

        if (signaturesByPath.Count != 0)
        {
            throw new InvalidDataException(
                $"Signature metadata references an undeclared document: '{signaturesByPath.Keys.First()}'.");
        }

        return new SigningResult(key.Document.KeyId, documents.Count);
    }

    public static SnapshotIndex PublishVerified(
        string sourceDirectory,
        string storageRoot,
        string snapshotId,
        bool activate,
        string publicKeyPath,
        SigningPolicy policy)
    {
        ArgumentNullException.ThrowIfNull(policy);
        LoadedPublicKey key = DistributionPublicKeys.Load(publicKeyPath);
        return SnapshotPublisher.Publish(
            sourceDirectory,
            storageRoot,
            snapshotId,
            activate,
            (path, relativePath, metadata) =>
            {
                byte[] bytes = ReadDocumentFile(path, relativePath);
                VerifyDocument(bytes, relativePath, metadata, key, policy);
            });
    }

    private static void VerifyDocument(
        byte[] bytes,
        string relativePath,
        SignedDocumentMetadata metadata,
        LoadedPublicKey key,
        SigningPolicy policy)
    {
        SnapshotValidator.ValidateSignature(metadata);
        SignedDocumentIdentity identity = SignedDocumentIdentity.Parse(bytes, relativePath);
        identity.ValidateMatches(metadata, relativePath);
        policy.Validate(identity, relativePath);
        if (!string.Equals(metadata.KeyId, key.Document.KeyId, StringComparison.Ordinal))
        {
            throw new InvalidDataException(
                $"Distribution document signature uses an untrusted key ID: '{relativePath}'.");
        }

        byte[] signature;
        try
        {
            signature = Convert.FromBase64String(metadata.Value);
        }
        catch (FormatException exception)
        {
            throw new InvalidDataException($"Distribution document signature is malformed: '{relativePath}'.", exception);
        }

        try
        {
            if (!key.Verify(bytes, signature))
            {
                throw new InvalidDataException(
                    $"Distribution document Ed25519 signature verification failed: '{relativePath}'.");
            }
        }
        finally
        {
            CryptographicOperations.ZeroMemory(signature);
        }
    }

    private static Dictionary<string, SignedDocumentMetadata> LoadSignatures(string path)
    {
        FileInfo file = new(path);
        if (!file.Exists || file.Length is <= 0 or > MaximumSignaturesBytes)
        {
            throw new InvalidDataException("The signatures manifest is missing or outside its size limit.");
        }

        FileSystemSafety.RejectLink(file);
        DistributionSignaturesManifest manifest;
        try
        {
            manifest = DistributionJson.DeserializeStrict<DistributionSignaturesManifest>(File.ReadAllBytes(path));
        }
        catch (JsonException exception)
        {
            throw new InvalidDataException("The signatures manifest is malformed.", exception);
        }

        if (manifest.SchemaVersion != DistributionContract.SignaturesSchemaVersion || manifest.Documents is null ||
            manifest.Documents.Count == 0 || manifest.Documents.Count > MaximumDocuments)
        {
            throw new InvalidDataException("The signatures manifest header is invalid.");
        }

        Dictionary<string, SignedDocumentMetadata> signatures = new(StringComparer.Ordinal);
        HashSet<string> portablePaths = new(StringComparer.OrdinalIgnoreCase);
        foreach (SignedDocumentEntry? entry in manifest.Documents)
        {
            if (entry is null)
            {
                throw new InvalidDataException("The signatures manifest contains a null document entry.");
            }

            string relativePath = DistributionPaths.NormalizeRelativePath(entry.Path);
            string kind = DistributionPaths.ClassifyPath(relativePath);
            if (kind is not (DistributionFileKinds.Catalog or DistributionFileKinds.Content))
            {
                throw new InvalidDataException($"Signature metadata targets a non-document path: '{relativePath}'.");
            }

            SignedDocumentMetadata metadata = entry.ToMetadata();
            SnapshotValidator.ValidateSignature(metadata);
            if (!signatures.TryAdd(relativePath, metadata) || !portablePaths.Add(relativePath))
            {
                throw new InvalidDataException($"Duplicate or case-colliding signature path: '{relativePath}'.");
            }
        }

        return signatures;
    }

    private static IReadOnlyList<string> EnumerateDocuments(string source, string? excludedPath)
    {
        string? excluded = excludedPath is null ? null : Path.GetFullPath(excludedPath);
        StringComparison comparison = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        List<string> documents = [];
        HashSet<string> portablePaths = new(StringComparer.OrdinalIgnoreCase);
        foreach (string relativePath in FileSystemSafety.EnumerateRegularFiles(source, MaximumDocuments * 2 + 1024))
        {
            string fullPath = DistributionPaths.ResolveConfined(source, relativePath);
            if (excluded is not null && string.Equals(fullPath, excluded, comparison))
            {
                continue;
            }

            string kind = DistributionPaths.ClassifyPath(relativePath);
            if (!portablePaths.Add(relativePath))
            {
                throw new InvalidDataException($"Distribution source contains a case-colliding path: '{relativePath}'.");
            }

            if (kind is DistributionFileKinds.Catalog or DistributionFileKinds.Content)
            {
                if (documents.Count == MaximumDocuments)
                {
                    throw new InvalidDataException("Distribution source exceeds its signed-document limit.");
                }

                documents.Add(relativePath);
            }
        }

        if (documents.Count == 0)
        {
            throw new InvalidDataException("Distribution source contains no catalog or content documents.");
        }

        documents.Sort(StringComparer.Ordinal);
        return documents;
    }

    private static byte[] ReadDocument(string source, string relativePath)
    {
        return ReadDocumentFile(DistributionPaths.ResolveConfined(source, relativePath), relativePath);
    }

    private static byte[] ReadDocumentFile(string path, string relativePath)
    {
        FileInfo file = new(path);
        if (!file.Exists || file.Length is <= 0 or > MaximumDocumentBytes)
        {
            throw new InvalidDataException($"Distribution document is outside its size limit: '{relativePath}'.");
        }

        FileSystemSafety.RejectLink(file);
        return File.ReadAllBytes(path);
    }

    private static string ValidateSourceDirectory(string sourceDirectory)
    {
        string source = Path.GetFullPath(sourceDirectory);
        DirectoryInfo directory = new(source);
        if (!directory.Exists)
        {
            throw new DirectoryNotFoundException($"Distribution source does not exist: '{source}'.");
        }

        FileSystemSafety.RejectLink(directory);
        return source;
    }

    private static void EnsureDistinctOutputs(string first, string second)
    {
        string firstPath = Path.GetFullPath(first);
        string secondPath = Path.GetFullPath(second);
        StringComparison comparison = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        if (string.Equals(firstPath, secondPath, comparison))
        {
            throw new ArgumentException("Private and public key output paths must be different.");
        }
    }

    private static void EnsureOutputDoesNotExist(string path)
    {
        string fullPath = Path.GetFullPath(path);
        if (File.Exists(fullPath) || Directory.Exists(fullPath))
        {
            throw new IOException($"Output already exists: '{fullPath}'.");
        }
    }
}
