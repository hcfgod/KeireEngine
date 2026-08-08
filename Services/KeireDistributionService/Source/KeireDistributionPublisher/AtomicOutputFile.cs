using System.Security.Cryptography;
using Keire.Distribution;

namespace Keire.Distribution.Publisher;

internal static class AtomicOutputFile
{
    public static void WriteNew(string path, ReadOnlySpan<byte> bytes, bool privateKey)
    {
        string fullPath = Path.GetFullPath(path);
        if (File.Exists(fullPath) || Directory.Exists(fullPath))
        {
            throw new IOException($"Output already exists: '{fullPath}'.");
        }

        string? parent = Path.GetDirectoryName(fullPath);
        if (string.IsNullOrEmpty(parent))
        {
            throw new InvalidDataException("The output path has no parent directory.");
        }

        FileSystemSafety.EnsureSafeDirectory(parent);
        string temporaryPath = Path.Combine(parent, $".{Path.GetFileName(fullPath)}.{Guid.NewGuid():N}.tmp");
        try
        {
            CreateEmpty(temporaryPath, privateKey);
            using (FileStream stream = new(
                temporaryPath,
                FileMode.Open,
                FileAccess.Write,
                FileShare.None,
                16 * 1024,
                FileOptions.WriteThrough))
            {
                stream.Write(bytes);
                stream.Flush(flushToDisk: true);
            }

            if (privateKey)
            {
                PrivateKeyPermissions.Validate(temporaryPath);
            }

            File.Move(temporaryPath, fullPath, overwrite: false);
            if (privateKey)
            {
                PrivateKeyPermissions.Validate(fullPath);
            }
        }
        finally
        {
            if (File.Exists(temporaryPath))
            {
                if (privateKey)
                {
                    try
                    {
                        long length = new FileInfo(temporaryPath).Length;
                        if (length is > 0 and <= 1024 * 1024)
                        {
                            byte[] zeroes = new byte[checked((int)length)];
                            File.WriteAllBytes(temporaryPath, zeroes);
                            CryptographicOperations.ZeroMemory(zeroes);
                        }
                    }
                    catch
                    {
                        // Best-effort overwrite must not hide the original publishing failure.
                    }
                }

                File.Delete(temporaryPath);
            }
        }
    }

    private static void CreateEmpty(string path, bool privateKey)
    {
        if (privateKey && !OperatingSystem.IsWindows())
        {
            FileStreamOptions options = new()
            {
                Mode = FileMode.CreateNew,
                Access = FileAccess.Write,
                Share = FileShare.None,
                Options = FileOptions.WriteThrough,
                UnixCreateMode = UnixFileMode.UserRead | UnixFileMode.UserWrite,
            };
            using FileStream ignored = new(path, options);
            return;
        }

        using (FileStream ignored = new(path, FileMode.CreateNew, FileAccess.Write, FileShare.None))
        {
        }

        if (privateKey)
        {
            PrivateKeyPermissions.Apply(path);
        }
    }
}
