using Keire.Distribution;

namespace Keire.Marketplace.Validation;

internal static class PayloadPolicy
{
    private static readonly HashSet<string> ForbiddenExtensions = new(StringComparer.OrdinalIgnoreCase)
    {
        ".app",
        ".bat",
        ".cmd",
        ".com",
        ".csproj",
        ".dll",
        ".dylib",
        ".exe",
        ".js",
        ".msi",
        ".ps1",
        ".props",
        ".sh",
        ".so",
        ".sln",
        ".targets",
        ".vbs",
    };

    public static IReadOnlyList<ValidationDiagnostic> Validate(
        string stagingRoot,
        ExtractedPackageDocument document,
        CancellationToken cancellationToken)
    {
        List<ValidationDiagnostic> diagnostics = [];
        IReadOnlyList<string> files = FileSystemSafety.EnumerateRegularFiles(
            stagingRoot,
            MarketplaceValidationContract.MaximumExtractedEntries,
            cancellationToken);
        Dictionary<string, PackageFile> declared = new(StringComparer.Ordinal);
        foreach (PackageFile file in document.Files)
        {
            string path = DistributionPaths.NormalizeRelativePath(file.Path);
            if (!declared.TryAdd(path, file))
            {
                diagnostics.Add(Error("DUPLICATE_FILE", "The manifest declares the same payload path more than once.", path));
                continue;
            }

            if ((file.Mode & 73U) != 0)
            {
                diagnostics.Add(Error("EXECUTABLE_MODE", "Marketplace payload files may not carry executable mode bits.", path));
            }

            if (IsForbiddenPath(path))
            {
                diagnostics.Add(Error(
                    "EXECUTABLE_PAYLOAD",
                    "Native binaries, scripts, and publisher-controlled build files are not accepted.",
                    path));
            }

            string fullPath = DistributionPaths.ResolveConfined(stagingRoot, path);
            if (File.Exists(fullPath) && HasExecutableMagic(fullPath))
            {
                diagnostics.Add(Error(
                    "EXECUTABLE_SIGNATURE",
                    "A file has an executable or script signature even though its extension is not executable.",
                    path));
            }
        }

        HashSet<string> actual = files.ToHashSet(StringComparer.Ordinal);
        if (!actual.SetEquals(declared.Keys))
        {
            diagnostics.Add(Error(
                "PAYLOAD_INVENTORY_MISMATCH",
                "The extracted payload does not exactly match the authoritative manifest inventory."));
        }

        return diagnostics;
    }

    private static bool IsForbiddenPath(string path)
    {
        string fileName = Path.GetFileName(path);
        return ForbiddenExtensions.Contains(Path.GetExtension(fileName)) ||
            string.Equals(fileName, "Directory.Build.props", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(fileName, "Directory.Build.targets", StringComparison.OrdinalIgnoreCase) ||
            string.Equals(fileName, "NuGet.Config", StringComparison.OrdinalIgnoreCase);
    }

    private static bool HasExecutableMagic(string path)
    {
        Span<byte> header = stackalloc byte[4];
        using FileStream stream = new(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        int count = stream.Read(header);
        if (count < 2)
        {
            return false;
        }

        if (header[0] == (byte)'M' && header[1] == (byte)'Z')
        {
            return true;
        }

        if (header[0] == (byte)'#' && header[1] == (byte)'!')
        {
            return true;
        }

        if (count < 4)
        {
            return false;
        }

        uint magic = System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(header);
        return magic is 0x7f454c46 or 0xfeedface or 0xfeedfacf or 0xcefaedfe or 0xcffaedfe or 0xcafebabe or
            0xbebafeca;
    }

    private static ValidationDiagnostic Error(string code, string message, string? path = null)
    {
        return new ValidationDiagnostic
        {
            Code = code,
            Severity = ValidationSeverities.Error,
            Message = message,
            Path = path,
        };
    }
}
