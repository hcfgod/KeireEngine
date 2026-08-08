namespace Keire.Distribution;

public static class FileSystemSafety
{
    public static IReadOnlyList<string> EnumerateRegularFiles(
        string root,
        int maximumEntries = 1_000_000,
        CancellationToken cancellationToken = default)
    {
        if (maximumEntries <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(maximumEntries));
        }

        string fullRoot = Path.GetFullPath(root);
        DirectoryInfo rootInfo = new(fullRoot);
        if (!rootInfo.Exists)
        {
            throw new DirectoryNotFoundException($"Distribution directory does not exist: '{fullRoot}'.");
        }

        RejectLink(rootInfo);
        List<string> files = [];
        Stack<DirectoryInfo> pending = new();
        pending.Push(rootInfo);
        int entryCount = 0;

        while (pending.Count != 0)
        {
            cancellationToken.ThrowIfCancellationRequested();
            DirectoryInfo directory = pending.Pop();
            foreach (FileSystemInfo entry in directory.EnumerateFileSystemInfos())
            {
                cancellationToken.ThrowIfCancellationRequested();
                if (++entryCount > maximumEntries)
                {
                    throw new InvalidDataException("Distribution directory exceeds its filesystem-entry limit.");
                }

                entry.Refresh();
                RejectLink(entry);
                if (entry is DirectoryInfo childDirectory)
                {
                    pending.Push(childDirectory);
                    continue;
                }

                if (entry is not FileInfo file || !file.Exists)
                {
                    throw new InvalidDataException($"Unsupported filesystem entry '{entry.FullName}'.");
                }

                string relativePath = Path.GetRelativePath(fullRoot, file.FullName).Replace('\\', '/');
                files.Add(DistributionPaths.NormalizeRelativePath(relativePath));
            }
        }

        files.Sort(StringComparer.Ordinal);
        return files;
    }

    public static void RejectLink(FileSystemInfo entry)
    {
        if ((entry.Attributes & FileAttributes.ReparsePoint) != 0 || entry.LinkTarget is not null)
        {
            throw new InvalidDataException($"Symbolic links and reparse points are not allowed: '{entry.FullName}'.");
        }
    }

    public static void EnsureSafeDirectory(string path)
    {
        DirectoryInfo directory = Directory.CreateDirectory(path);
        directory.Refresh();
        RejectLink(directory);
    }
}
