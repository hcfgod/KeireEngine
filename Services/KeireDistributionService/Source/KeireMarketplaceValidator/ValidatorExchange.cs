using System.Globalization;
using System.Security.Cryptography;
using System.Text.Json.Serialization;
using Keire.Distribution;
using Keire.Marketplace.Validation;

internal static class ValidatorExchange
{
    private const int MaximumRequestBytes = 64 * 1024;

    public static async Task<int> RunAsync(
        ValidatorCommandLine commandLine,
        string fingerprint,
        CancellationToken cancellationToken)
    {
        string exchangeRoot = RequireSafeDirectory(commandLine.Require("--exchange-root"));
        string workRoot = RequireSafeDirectory(commandLine.Require("--work-root"));
        string packagesRoot = EnsureChild(exchangeRoot, "packages");
        string requestsRoot = EnsureChild(exchangeRoot, "requests");
        string reportsRoot = EnsureChild(exchangeRoot, "reports");
        RecoverInterruptedRequests(requestsRoot);
        int pollMilliseconds = ParsePollMilliseconds(commandLine.Optional("--poll-ms"));
        MarketplacePackageValidator validator = new(new ClamAvScanner(commandLine.Require("--malware-scanner")));
        Console.WriteLine("Marketplace validator is ready for offline jobs.");
        while (!cancellationToken.IsCancellationRequested)
        {
            bool handled = false;
            foreach (string requestPath in Directory.EnumerateFiles(requestsRoot, "*.json", SearchOption.TopDirectoryOnly)
                         .Order(StringComparer.Ordinal))
            {
                cancellationToken.ThrowIfCancellationRequested();
                try
                {
                    handled = await ProcessRequestAsync(
                        requestPath,
                        packagesRoot,
                        reportsRoot,
                        workRoot,
                        commandLine,
                        fingerprint,
                        validator,
                        cancellationToken) || handled;
                }
                catch (Exception exception) when (exception is not OperationCanceledException)
                {
                    Console.Error.WriteLine($"Rejected an invalid validator exchange request: {exception.Message}");
                    handled = true;
                }
            }

            if (!handled)
            {
                await Task.Delay(pollMilliseconds, cancellationToken);
            }
        }

        return 0;
    }

    private static async Task<bool> ProcessRequestAsync(
        string requestPath,
        string packagesRoot,
        string reportsRoot,
        string workRoot,
        ValidatorCommandLine commandLine,
        string fingerprint,
        MarketplacePackageValidator validator,
        CancellationToken cancellationToken)
    {
        FileInfo requestInfo = new(requestPath);
        requestInfo.Refresh();
        FileSystemSafety.RejectLink(requestInfo);
        if (requestInfo.Length is <= 0 or > MaximumRequestBytes)
        {
            throw new InvalidDataException("A validator exchange request exceeds its size limit.");
        }

        string processingPath = requestPath + ".processing";
        try
        {
            File.Move(requestPath, processingPath);
        }
        catch (IOException)
        {
            return false;
        }

        try
        {
            ValidatorJobRequest job = DistributionJson.DeserializeStrict<ValidatorJobRequest>(
                await File.ReadAllBytesAsync(processingPath, cancellationToken));
            string uploadId = Guid.ParseExact(job.UploadId, "D").ToString("D");
            if (job.SchemaVersion != 1 ||
                job.ExpectedSizeBytes is <= 0 or > MarketplaceValidationContract.MaximumPackageBytes ||
                !DistributionPaths.IsSha256(job.ExpectedSha256))
            {
                throw new InvalidDataException("A validator exchange request has invalid immutable metadata.");
            }

            string packagePath = Path.Combine(packagesRoot, uploadId + ".keireassetpackage");
            string reportPath = Path.Combine(reportsRoot, uploadId + ".json");
            if (File.Exists(reportPath))
            {
                return true;
            }

            PackageValidationRequest request = new(
                packagePath,
                workRoot,
                commandLine.Require("--asset-tool"),
                commandLine.Require("--dotnet"),
                commandLine.Optional("--managed-api"),
                commandLine.Require("--malware-scanner"),
                job.ExpectedSizeBytes,
                job.ExpectedSha256,
                fingerprint);
            MarketplaceValidationReport report = await validator.ValidateAsync(request, cancellationToken);
            ValidatorFiles.WriteNewReport(reportPath, report);
            return true;
        }
        finally
        {
            File.Delete(processingPath);
        }
    }

    private static void RecoverInterruptedRequests(string requestsRoot)
    {
        foreach (string processingPath in Directory.EnumerateFiles(requestsRoot, "*.json.processing", SearchOption.TopDirectoryOnly))
        {
            FileInfo processing = new(processingPath);
            processing.Refresh();
            FileSystemSafety.RejectLink(processing);
            string requestPath = processingPath[..^".processing".Length];
            if (File.Exists(requestPath))
            {
                throw new InvalidDataException("The validator exchange contains conflicting interrupted requests.");
            }

            File.Move(processingPath, requestPath);
        }
    }

    private static string RequireSafeDirectory(string path)
    {
        DirectoryInfo directory = new(Path.GetFullPath(path));
        if (!directory.Exists)
        {
            throw new DirectoryNotFoundException("A configured validator directory does not exist.");
        }

        directory.Refresh();
        FileSystemSafety.RejectLink(directory);
        return directory.FullName;
    }

    private static string EnsureChild(string root, string name)
    {
        string child = Path.Combine(root, name);
        FileSystemSafety.EnsureSafeDirectory(child);
        return Path.GetFullPath(child);
    }

    private static int ParsePollMilliseconds(string? value)
    {
        if (value is null)
        {
            return 500;
        }

        return int.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out int parsed) &&
            parsed is >= 100 and <= 5000
            ? parsed
            : throw new ArgumentException("--poll-ms must be between 100 and 5000.");
    }
}

internal sealed class ValidatorJobRequest
{
    [JsonPropertyName("schemaVersion")]
    public int SchemaVersion { get; init; } = 1;

    [JsonPropertyName("uploadId")]
    public required string UploadId { get; init; }

    [JsonPropertyName("expectedSizeBytes")]
    public required long ExpectedSizeBytes { get; init; }

    [JsonPropertyName("expectedSha256")]
    public required string ExpectedSha256 { get; init; }
}

internal static class ValidatorFiles
{
    public static async Task<string> ComputeSha256Async(string path, CancellationToken cancellationToken)
    {
        await using FileStream stream = new(
            path,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read,
            128 * 1024,
            FileOptions.Asynchronous | FileOptions.SequentialScan);
        return Convert.ToHexStringLower(await SHA256.HashDataAsync(stream, cancellationToken));
    }

    public static void WriteNewReport(string path, MarketplaceValidationReport report)
    {
        string fullPath = Path.GetFullPath(path);
        string parent = Path.GetDirectoryName(fullPath)
            ?? throw new ArgumentException("The report path requires a parent directory.");
        DirectoryInfo parentInfo = new(parent);
        if (!parentInfo.Exists)
        {
            throw new DirectoryNotFoundException("The report parent directory does not exist.");
        }

        FileSystemSafety.RejectLink(parentInfo);
        if (File.Exists(fullPath))
        {
            throw new IOException("The report path must be new for each validation attempt.");
        }

        string temporary = Path.Combine(parent, $".{Path.GetFileName(fullPath)}.{Guid.NewGuid():N}.tmp");
        try
        {
            File.WriteAllBytes(temporary, DistributionJson.Serialize(report));
            if (!OperatingSystem.IsWindows())
            {
                File.SetUnixFileMode(temporary, UnixFileMode.UserRead | UnixFileMode.UserWrite);
            }

            File.Move(temporary, fullPath);
        }
        finally
        {
            File.Delete(temporary);
        }
    }
}
