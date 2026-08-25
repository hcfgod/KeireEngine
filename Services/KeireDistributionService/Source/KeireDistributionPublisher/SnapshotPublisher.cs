using System.Text;
using Keire.Distribution;

namespace Keire.Distribution.Publisher;

public static class SnapshotPublisher
{
    private const long MaximumSignaturesBytes = 8 * 1024 * 1024;
    internal const int MaximumDirectoryMoveAttempts = 8;
    private const int InitialDirectoryMoveDelayMilliseconds = 10;
    private const int MaximumDirectoryMoveDelayMilliseconds = 200;

    internal static SnapshotIndex Publish(
        string sourceDirectory,
        string storageRoot,
        string snapshotId,
        bool activate,
        Action<string, string, SignedDocumentMetadata>? documentVerifier)
    {
        if (!DistributionPaths.IsSnapshotId(snapshotId))
        {
            throw new InvalidDataException("Snapshot ID is invalid.");
        }

        string source = Path.GetFullPath(sourceDirectory);
        string root = Path.GetFullPath(storageRoot);
        DirectoryInfo sourceInfo = new(source);
        if (!sourceInfo.Exists)
        {
            throw new DirectoryNotFoundException($"Publishing source does not exist: '{source}'.");
        }

        FileSystemSafety.RejectLink(sourceInfo);
        FileSystemSafety.EnsureSafeDirectory(root);
        string snapshotsRoot = Path.Combine(root, "snapshots");
        FileSystemSafety.EnsureSafeDirectory(snapshotsRoot);

        string finalDirectory = Path.Combine(snapshotsRoot, snapshotId);
        if (Directory.Exists(finalDirectory) || File.Exists(finalDirectory))
        {
            throw new IOException($"Immutable snapshot '{snapshotId}' already exists.");
        }

        DistributionSignaturesManifest signatures = LoadSignatures(source);
        Dictionary<string, SignedDocumentMetadata> signatureByPath = ValidateSignatures(signatures);
        IReadOnlyList<string> sourceFiles = FileSystemSafety.EnumerateRegularFiles(source, 200_000);
        List<string> payloadPaths = sourceFiles
            .Where(path => !string.Equals(path, DistributionContract.SignaturesFileName, StringComparison.Ordinal))
            .ToList();
        if (payloadPaths.Count == 0)
        {
            throw new InvalidDataException("Publishing source contains no distribution files.");
        }

        string stagingDirectory = Path.Combine(snapshotsRoot, $".{snapshotId}.staging-{Guid.NewGuid():N}");
        Directory.CreateDirectory(stagingDirectory);
        try
        {
            List<DistributionFileManifest> entries = [];
            HashSet<string> portablePaths = new(StringComparer.OrdinalIgnoreCase);
            foreach (string relativePath in payloadPaths)
            {
                string kind = DistributionPaths.ClassifyPath(relativePath);
                if (!portablePaths.Add(relativePath))
                {
                    throw new InvalidDataException($"Publishing source contains a case-colliding path: '{relativePath}'.");
                }

                string sourcePath = DistributionPaths.ResolveConfined(source, relativePath);
                string digest = SnapshotValidator.ComputeSha256(sourcePath);
                FileInfo sourceFile = new(sourcePath);
                SignedDocumentMetadata? signature = null;
                if (kind is DistributionFileKinds.Catalog or DistributionFileKinds.Content)
                {
                    if (!signatureByPath.Remove(relativePath, out signature))
                    {
                        throw new InvalidDataException($"Signed document is missing detached signature metadata: '{relativePath}'.");
                    }

                    SnapshotValidator.ValidateSignature(signature);
                }
                else
                {
                    string digestName = kind == DistributionFileKinds.Manifest
                        ? Path.GetFileNameWithoutExtension(relativePath)
                        : Path.GetFileName(relativePath);
                    if (!string.Equals(digestName, digest, StringComparison.Ordinal))
                    {
                        throw new InvalidDataException(
                            $"Content-addressed filename must equal its SHA-256 digest: '{relativePath}'.");
                    }
                }

                string destinationPath = DistributionPaths.ResolveConfined(stagingDirectory, relativePath);
                FileSystemSafety.EnsureSafeDirectory(Path.GetDirectoryName(destinationPath)!);
                File.Copy(sourcePath, destinationPath, overwrite: false);
                if (signature is not null)
                {
                    documentVerifier?.Invoke(destinationPath, relativePath, signature);
                }

                entries.Add(new DistributionFileManifest
                {
                    Path = relativePath,
                    Kind = kind,
                    Size = sourceFile.Length,
                    Sha256 = digest,
                    Signature = signature,
                });
            }

            if (signatureByPath.Count != 0)
            {
                throw new InvalidDataException($"Signature metadata references an undeclared document: '{signatureByPath.Keys.First()}'.");
            }

            DistributionSnapshotManifest manifest = new()
            {
                SchemaVersion = DistributionContract.SnapshotSchemaVersion,
                SnapshotId = snapshotId,
                CreatedAt = DateTimeOffset.UtcNow,
                Files = entries.OrderBy(entry => entry.Path, StringComparer.Ordinal).ToList(),
            };
            File.WriteAllBytes(
                Path.Combine(stagingDirectory, DistributionContract.SnapshotManifestFileName),
                DistributionJson.Serialize(manifest));

            SnapshotIndex index = SnapshotValidator.ValidateSnapshotDirectory(stagingDirectory, snapshotId);
            MoveStagingDirectory(stagingDirectory, finalDirectory);
            if (activate)
            {
                Activate(root, snapshotId);
            }

            return index;
        }
        catch
        {
            if (Directory.Exists(stagingDirectory))
            {
                Directory.Delete(stagingDirectory, recursive: true);
            }

            throw;
        }
    }

    public static SnapshotIndex Activate(string storageRoot, string snapshotId)
    {
        if (!DistributionPaths.IsSnapshotId(snapshotId))
        {
            throw new InvalidDataException("Snapshot ID is invalid.");
        }

        string root = Path.GetFullPath(storageRoot);
        DirectoryInfo rootInfo = new(root);
        if (!rootInfo.Exists)
        {
            throw new DirectoryNotFoundException($"Distribution root does not exist: '{root}'.");
        }

        FileSystemSafety.RejectLink(rootInfo);
        SnapshotIndex index = SnapshotValidator.ValidateSnapshotDirectory(
            Path.Combine(root, "snapshots", snapshotId), snapshotId);
        WriteCurrentPointer(root, snapshotId);
        return index;
    }

    internal static void MoveStagingDirectory(
        string stagingDirectory,
        string finalDirectory,
        Action<string, string> moveDirectory,
        Action<TimeSpan> delay)
    {
        ArgumentNullException.ThrowIfNull(moveDirectory);
        ArgumentNullException.ThrowIfNull(delay);

        int delayMilliseconds = InitialDirectoryMoveDelayMilliseconds;
        for (int attempt = 1; attempt <= MaximumDirectoryMoveAttempts; ++attempt)
        {
            try
            {
                moveDirectory(stagingDirectory, finalDirectory);
                return;
            }
            catch (IOException) when (
                attempt < MaximumDirectoryMoveAttempts && Directory.Exists(stagingDirectory) &&
                !Directory.Exists(finalDirectory) && !File.Exists(finalDirectory))
            {
                // Filesystem filters can briefly hold newly written snapshot files without delete sharing.
                delay(TimeSpan.FromMilliseconds(delayMilliseconds));
                delayMilliseconds = Math.Min(delayMilliseconds * 2, MaximumDirectoryMoveDelayMilliseconds);
            }
        }
    }

    private static void MoveStagingDirectory(string stagingDirectory, string finalDirectory)
    {
        MoveStagingDirectory(stagingDirectory, finalDirectory, Directory.Move, Thread.Sleep);
    }

    private static DistributionSignaturesManifest LoadSignatures(string source)
    {
        string path = Path.Combine(source, DistributionContract.SignaturesFileName);
        FileInfo file = new(path);
        if (!file.Exists || file.Length is <= 0 or > MaximumSignaturesBytes)
        {
            throw new InvalidDataException("Publishing source signatures.json is missing or outside its size limit.");
        }

        FileSystemSafety.RejectLink(file);
        return DistributionJson.DeserializeStrict<DistributionSignaturesManifest>(File.ReadAllBytes(path));
    }

    private static Dictionary<string, SignedDocumentMetadata> ValidateSignatures(DistributionSignaturesManifest manifest)
    {
        if (manifest.SchemaVersion != DistributionContract.SignaturesSchemaVersion || manifest.Documents is null ||
            manifest.Documents.Count == 0 ||
            manifest.Documents.Count > 100_000)
        {
            throw new InvalidDataException("Publishing signatures manifest header is invalid.");
        }

        Dictionary<string, SignedDocumentMetadata> signatures = new(StringComparer.Ordinal);
        HashSet<string> portablePaths = new(StringComparer.OrdinalIgnoreCase);
        foreach (SignedDocumentEntry? document in manifest.Documents)
        {
            if (document is null)
            {
                throw new InvalidDataException("Publishing signatures manifest contains a null document entry.");
            }

            string path = DistributionPaths.NormalizeRelativePath(document.Path);
            string kind = DistributionPaths.ClassifyPath(path);
            if (kind is not (DistributionFileKinds.Catalog or DistributionFileKinds.Content))
            {
                throw new InvalidDataException($"Signature metadata targets a non-document path: '{path}'.");
            }

            SignedDocumentMetadata metadata = document.ToMetadata();
            SnapshotValidator.ValidateSignature(metadata);
            if (!signatures.TryAdd(path, metadata) || !portablePaths.Add(path))
            {
                throw new InvalidDataException($"Duplicate or case-colliding signature path: '{path}'.");
            }
        }

        return signatures;
    }

    private static void WriteCurrentPointer(string root, string snapshotId)
    {
        string pointerPath = Path.Combine(root, DistributionContract.CurrentPointerFileName);
        FileInfo existingPointer = new(pointerPath);
        if (existingPointer.Exists)
        {
            FileSystemSafety.RejectLink(existingPointer);
        }

        string temporaryPath = Path.Combine(root, $".{DistributionContract.CurrentPointerFileName}.{Guid.NewGuid():N}.tmp");
        try
        {
            File.WriteAllText(temporaryPath, snapshotId + "\n", new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
            File.Move(temporaryPath, pointerPath, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporaryPath))
            {
                File.Delete(temporaryPath);
            }
        }
    }
}
