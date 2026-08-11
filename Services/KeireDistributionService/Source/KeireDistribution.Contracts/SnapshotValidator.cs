using System.Buffers;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Keire.Distribution;

public sealed class SnapshotValidationOptions
{
    public int MaximumFiles { get; init; } = 100_000;
    public long MaximumManifestBytes { get; init; } = 8 * 1024 * 1024;
    public long MaximumDocumentBytes { get; init; } = 32 * 1024 * 1024;
}

public static class SnapshotValidator
{
    public static SnapshotIndex ValidateCurrent(
        string storageRoot,
        SnapshotValidationOptions? options = null,
        CancellationToken cancellationToken = default)
    {
        SnapshotValidationOptions validationOptions = options ?? new SnapshotValidationOptions();
        string snapshotId = ReadCurrentSnapshotId(storageRoot);
        string root = Path.GetFullPath(storageRoot);
        string snapshotsRoot = Path.Combine(root, "snapshots");
        return ValidateSnapshotDirectory(
            Path.Combine(snapshotsRoot, snapshotId), snapshotId, validationOptions, cancellationToken);
    }

    public static string ReadCurrentSnapshotId(string storageRoot)
    {
        string root = Path.GetFullPath(storageRoot);
        DirectoryInfo rootInfo = new(root);
        if (!rootInfo.Exists)
        {
            throw new DirectoryNotFoundException($"Distribution root does not exist: '{root}'.");
        }

        FileSystemSafety.RejectLink(rootInfo);
        string currentPath = Path.Combine(root, DistributionContract.CurrentPointerFileName);
        FileInfo currentInfo = new(currentPath);
        if (!currentInfo.Exists || currentInfo.Length is <= 0 or > 256)
        {
            throw new InvalidDataException("The distribution current pointer is missing or invalid.");
        }

        FileSystemSafety.RejectLink(currentInfo);
        string snapshotId = File.ReadAllText(currentPath, Encoding.UTF8).Trim();
        if (!DistributionPaths.IsSnapshotId(snapshotId))
        {
            throw new InvalidDataException("The distribution current pointer contains an invalid snapshot ID.");
        }

        string snapshotsRoot = Path.Combine(root, "snapshots");
        DirectoryInfo snapshotsInfo = new(snapshotsRoot);
        if (!snapshotsInfo.Exists)
        {
            throw new InvalidDataException("The distribution snapshots directory is missing.");
        }

        FileSystemSafety.RejectLink(snapshotsInfo);
        return snapshotId;
    }

    public static SnapshotIndex ValidateSnapshotDirectory(
        string snapshotDirectory,
        string expectedSnapshotId,
        SnapshotValidationOptions? options = null,
        CancellationToken cancellationToken = default)
    {
        SnapshotValidationOptions validationOptions = options ?? new SnapshotValidationOptions();
        string root = Path.GetFullPath(snapshotDirectory);
        DirectoryInfo rootInfo = new(root);
        if (!rootInfo.Exists)
        {
            throw new DirectoryNotFoundException($"Distribution snapshot does not exist: '{expectedSnapshotId}'.");
        }

        FileSystemSafety.RejectLink(rootInfo);
        string manifestPath = Path.Combine(root, DistributionContract.SnapshotManifestFileName);
        FileInfo manifestInfo = new(manifestPath);
        if (!manifestInfo.Exists || manifestInfo.Length is <= 0 || manifestInfo.Length > validationOptions.MaximumManifestBytes)
        {
            throw new InvalidDataException("The snapshot manifest is missing or outside its size limit.");
        }

        FileSystemSafety.RejectLink(manifestInfo);
        byte[] manifestBytes = File.ReadAllBytes(manifestPath);
        DistributionSnapshotManifest manifest;
        try
        {
            manifest = DistributionJson.DeserializeStrict<DistributionSnapshotManifest>(manifestBytes);
        }
        catch (JsonException exception)
        {
            throw new InvalidDataException("The snapshot manifest is malformed.", exception);
        }

        ValidateManifestHeader(manifest, expectedSnapshotId, validationOptions);

        HashSet<string> declaredPaths = new(StringComparer.Ordinal);
        HashSet<string> portablePaths = new(StringComparer.OrdinalIgnoreCase);
        List<DistributionFile> files = new(manifest.Files.Count);
        foreach (DistributionFileManifest? entry in manifest.Files)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (entry is null)
            {
                throw new InvalidDataException("The snapshot manifest contains a null file entry.");
            }

            string relativePath = DistributionPaths.NormalizeRelativePath(entry.Path);
            string expectedKind = DistributionPaths.ClassifyPath(relativePath);
            if (!string.Equals(entry.Kind, expectedKind, StringComparison.Ordinal))
            {
                throw new InvalidDataException($"Distribution file '{relativePath}' has an invalid kind.");
            }

            if (!declaredPaths.Add(relativePath) || !portablePaths.Add(relativePath))
            {
                throw new InvalidDataException($"Duplicate or case-colliding distribution path '{relativePath}'.");
            }

            ValidateEntryMetadata(entry, expectedKind, validationOptions);
            string fullPath = DistributionPaths.ResolveConfined(root, relativePath);
            FileInfo fileInfo = new(fullPath);
            if (!fileInfo.Exists)
            {
                throw new InvalidDataException($"Declared distribution file is missing: '{relativePath}'.");
            }

            FileSystemSafety.RejectLink(fileInfo);
            if (fileInfo.Length != entry.Size)
            {
                throw new InvalidDataException($"Distribution file size does not match its manifest: '{relativePath}'.");
            }

            string digest = ComputeSha256(fullPath, cancellationToken);
            if (!string.Equals(digest, entry.Sha256, StringComparison.Ordinal))
            {
                throw new InvalidDataException($"Distribution file digest does not match its manifest: '{relativePath}'.");
            }

            if (expectedKind is DistributionFileKinds.Catalog or DistributionFileKinds.Content)
            {
                ValidateSignedJsonDocument(
                    fullPath,
                    validationOptions.MaximumDocumentBytes,
                    relativePath,
                    entry.Signature!);
            }

            files.Add(new DistributionFile(
                relativePath,
                fullPath,
                expectedKind,
                entry.Size,
                entry.Sha256,
                fileInfo.LastWriteTimeUtc,
                entry.Signature));
        }

        int maximumFilesystemEntries = checked(validationOptions.MaximumFiles * 2 + 1024);
        IReadOnlyList<string> onDiskFiles = FileSystemSafety.EnumerateRegularFiles(
            root, maximumFilesystemEntries, cancellationToken);
        HashSet<string> expectedFiles = new(declaredPaths, StringComparer.Ordinal)
        {
            DistributionContract.SnapshotManifestFileName,
        };
        if (!onDiskFiles.ToHashSet(StringComparer.Ordinal).SetEquals(expectedFiles))
        {
            throw new InvalidDataException("The snapshot contains undeclared or missing files.");
        }

        return new SnapshotIndex(manifest.SnapshotId, manifest.CreatedAt, files);
    }

    public static string ComputeSha256(string path, CancellationToken cancellationToken = default)
    {
        using FileStream stream = new(path, FileMode.Open, FileAccess.Read, FileShare.Read, 128 * 1024,
            FileOptions.SequentialScan);
        using IncrementalHash hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        byte[] buffer = ArrayPool<byte>.Shared.Rent(128 * 1024);
        try
        {
            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();
                int count = stream.Read(buffer, 0, buffer.Length);
                if (count == 0)
                {
                    break;
                }

                hash.AppendData(buffer, 0, count);
            }

            return Convert.ToHexStringLower(hash.GetHashAndReset());
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(buffer);
        }
    }

    public static void ValidateSignature(SignedDocumentMetadata signature)
    {
        if (!string.Equals(signature.Algorithm, DistributionContract.SignatureAlgorithm, StringComparison.Ordinal))
        {
            throw new InvalidDataException("Signed documents must use Ed25519.");
        }

        if (!DistributionPaths.IsKeyId(signature.KeyId))
        {
            throw new InvalidDataException("Signed document key ID is invalid.");
        }

        if (string.IsNullOrEmpty(signature.Value))
        {
            throw new InvalidDataException("Signed document signature is missing.");
        }

        byte[] signatureBytes;
        try
        {
            signatureBytes = Convert.FromBase64String(signature.Value);
        }
        catch (FormatException exception)
        {
            throw new InvalidDataException("Signed document signature is not valid base64.", exception);
        }

        if (signatureBytes.Length != 64)
        {
            throw new InvalidDataException("An Ed25519 signature must contain exactly 64 bytes.");
        }

        if (!string.Equals(Convert.ToBase64String(signatureBytes), signature.Value, StringComparison.Ordinal))
        {
            throw new InvalidDataException("Signed document signature must use canonical base64 without whitespace.");
        }

        if (signature.Sequence < 0 || signature.ExpiresAt == default || signature.ExpiresAt.Offset != TimeSpan.Zero)
        {
            throw new InvalidDataException("Signed document sequence or UTC expiry is invalid.");
        }
    }

    private static void ValidateManifestHeader(
        DistributionSnapshotManifest manifest,
        string expectedSnapshotId,
        SnapshotValidationOptions options)
    {
        if (manifest.SchemaVersion != DistributionContract.SnapshotSchemaVersion || manifest.Files is null ||
            !string.Equals(manifest.SnapshotId, expectedSnapshotId, StringComparison.Ordinal) ||
            !DistributionPaths.IsSnapshotId(manifest.SnapshotId) || manifest.CreatedAt == default ||
            manifest.CreatedAt.Offset != TimeSpan.Zero || manifest.Files.Count == 0 ||
            manifest.Files.Count > options.MaximumFiles)
        {
            throw new InvalidDataException("The snapshot manifest header is invalid.");
        }
    }

    private static void ValidateEntryMetadata(
        DistributionFileManifest entry,
        string expectedKind,
        SnapshotValidationOptions options)
    {
        if (entry.Size < 0 || !DistributionPaths.IsSha256(entry.Sha256))
        {
            throw new InvalidDataException($"Distribution file metadata is invalid: '{entry.Path}'.");
        }

        if (expectedKind is DistributionFileKinds.Catalog or DistributionFileKinds.Content)
        {
            if (entry.Size > options.MaximumDocumentBytes || entry.Signature is null)
            {
                throw new InvalidDataException($"Signed distribution document metadata is invalid: '{entry.Path}'.");
            }

            ValidateSignature(entry.Signature);
        }
        else
        {
            if (entry.Signature is not null)
            {
                throw new InvalidDataException($"Immutable payload files may not carry document signature metadata: '{entry.Path}'.");
            }

            if (expectedKind == DistributionFileKinds.Manifest &&
                (entry.Size <= 0 || entry.Size > options.MaximumManifestBytes))
            {
                throw new InvalidDataException($"Package manifest is outside its size limit: '{entry.Path}'.");
            }

            string digestName = expectedKind == DistributionFileKinds.Manifest
                ? Path.GetFileNameWithoutExtension(entry.Path)
                : Path.GetFileName(entry.Path);
            if (!string.Equals(digestName, entry.Sha256, StringComparison.Ordinal))
            {
                throw new InvalidDataException($"Immutable payload filename does not match its digest: '{entry.Path}'.");
            }
        }
    }

    private static void ValidateSignedJsonDocument(
        string path,
        long maximumBytes,
        string relativePath,
        SignedDocumentMetadata signature)
    {
        FileInfo file = new(path);
        if (file.Length is <= 0 || file.Length > maximumBytes)
        {
            throw new InvalidDataException($"Distribution document is outside its size limit: '{relativePath}'.");
        }

        SignedDocumentIdentity identity = SignedDocumentIdentity.Parse(File.ReadAllBytes(path), relativePath);
        identity.ValidateMatches(signature, relativePath);
    }
}
