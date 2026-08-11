using System.Collections.Frozen;

namespace Keire.Distribution;

public sealed record DistributionFile(
    string RelativePath,
    string AbsolutePath,
    string Kind,
    long Size,
    string Sha256,
    DateTime LastWriteTimeUtc,
    SignedDocumentMetadata? Signature);

public sealed class SnapshotIndex
{
    private readonly FrozenDictionary<string, DistributionFile> m_Files;

    public SnapshotIndex(string snapshotId, DateTimeOffset createdAt, IEnumerable<DistributionFile> files)
    {
        SnapshotId = snapshotId;
        CreatedAt = createdAt;
        m_Files = files.ToFrozenDictionary(file => file.RelativePath, StringComparer.Ordinal);
    }

    public string SnapshotId { get; }
    public DateTimeOffset CreatedAt { get; }

    public bool TryGet(string relativePath, out DistributionFile? file)
    {
        return m_Files.TryGetValue(relativePath, out file);
    }
}
