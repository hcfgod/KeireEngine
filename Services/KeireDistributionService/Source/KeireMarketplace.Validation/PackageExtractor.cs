using System.Security.Cryptography;
using Keire.Distribution;

namespace Keire.Marketplace.Validation;

internal static class PackageExtractor
{
    public static async Task<VerifiedPackageInput> VerifyInputAsync(
        PackageValidationRequest request,
        CancellationToken cancellationToken)
    {
        FileInfo package = new(Path.GetFullPath(request.PackagePath));
        if (!package.Exists)
        {
            throw new FileNotFoundException("The quarantined package does not exist.", package.FullName);
        }

        package.Refresh();
        FileSystemSafety.RejectLink(package);
        if (!string.Equals(package.Extension, ".keireassetpackage", StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("Marketplace validation accepts only .keireassetpackage archives.");
        }

        if (package.Length <= 0 || package.Length > MarketplaceValidationContract.MaximumPackageBytes)
        {
            throw new InvalidDataException("The package is empty or exceeds the marketplace archive-size limit.");
        }

        if (package.Length != request.ExpectedPackageBytes)
        {
            throw new InvalidDataException("The package size does not match the immutable upload declaration.");
        }

        string packageSha256 = await ComputeSha256Async(package.FullName, cancellationToken);
        if (!string.Equals(packageSha256, request.ExpectedPackageSha256, StringComparison.Ordinal))
        {
            throw new InvalidDataException("The package digest does not match the immutable upload declaration.");
        }

        return new VerifiedPackageInput(package.FullName, package.Length, packageSha256);
    }

    public static async Task<(ExtractedPackageDocument Document, string PackageSha256)> ExtractAsync(
        PackageValidationRequest request,
        string stagingRoot,
        CancellationToken cancellationToken)
    {
        VerifiedPackageInput package = await VerifyInputAsync(request, cancellationToken);

        List<string> arguments =
        [
            "extract-asset-package",
            "--input",
            package.Path,
            "--output",
            stagingRoot,
            "--size",
            package.SizeBytes.ToString(System.Globalization.CultureInfo.InvariantCulture),
            "--sha256",
            package.Sha256,
        ];
        ProcessResult extraction = await BoundedProcess.RunAsync(
            request.AssetToolPath,
            arguments,
            Path.GetDirectoryName(request.AssetToolPath) ?? request.WorkRoot,
            environment: null,
            TimeSpan.FromMinutes(15),
            cancellationToken);
        if (extraction.ExitCode != 0)
        {
            throw new InvalidDataException("The authoritative Kéire package parser rejected the archive.");
        }

        byte[] output = System.Text.Encoding.UTF8.GetBytes(extraction.StandardOutput);
        ExtractedPackageDocument document = DistributionJson.DeserializeStrict<ExtractedPackageDocument>(output);
        if (!string.Equals(document.Archive.Sha256, package.Sha256, StringComparison.Ordinal) ||
            document.Archive.SizeBytes != (ulong)package.SizeBytes ||
            !DistributionPaths.IsSha256(document.Archive.ManifestSha256))
        {
            throw new InvalidDataException("The package parser returned inconsistent archive metadata.");
        }

        if (!PathsEqual(stagingRoot, document.StagingRoot))
        {
            throw new InvalidDataException("The package parser returned an unexpected staging directory.");
        }

        return (document, package.Sha256);
    }

    private static async Task<string> ComputeSha256Async(string path, CancellationToken cancellationToken)
    {
        await using FileStream stream = new(
            path,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read,
            128 * 1024,
            FileOptions.Asynchronous | FileOptions.SequentialScan);
        byte[] digest = await SHA256.HashDataAsync(stream, cancellationToken);
        return Convert.ToHexStringLower(digest);
    }

    private static bool PathsEqual(string first, string second)
    {
        StringComparison comparison = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        return string.Equals(Path.GetFullPath(first), Path.GetFullPath(second), comparison);
    }
}

internal sealed record VerifiedPackageInput(string Path, long SizeBytes, string Sha256);
