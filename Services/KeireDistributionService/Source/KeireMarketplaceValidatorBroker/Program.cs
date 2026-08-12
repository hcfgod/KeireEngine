using Keire.Distribution;
using Keire.Marketplace.Validator.Broker;

return await BrokerProgram.RunAsync();

internal static class BrokerProgram
{
    private const int MaximumReportBytes = 1024 * 1024;

    public static async Task<int> RunAsync()
    {
        using CancellationTokenSource cancellation = new();
        Console.CancelKeyPress += (_, eventArguments) =>
        {
            eventArguments.Cancel = true;
            cancellation.Cancel();
        };
        try
        {
            BrokerOptions options = BrokerOptions.FromEnvironment();
            ExchangePaths exchange = ExchangePaths.Create(options.ExchangeRoot);
            using SupabaseValidatorApi api = new(options);
            Console.WriteLine($"Marketplace validator broker '{options.WorkerId}' is ready.");
            while (!cancellation.IsCancellationRequested)
            {
                try
                {
                    ValidationLease? lease = await api.LeaseAsync(cancellation.Token);
                    if (lease is null)
                    {
                        await Task.Delay(options.IdlePollInterval, cancellation.Token);
                        continue;
                    }

                    try
                    {
                        await ProcessLeaseAsync(api, options, exchange, lease, cancellation.Token);
                    }
                    catch (Exception exception) when (exception is not OperationCanceledException)
                    {
                        Console.Error.WriteLine(
                            $"Validation attempt for upload {lease.UploadId:D} failed safely: {exception.Message}");
                        await Task.Delay(options.IdlePollInterval, cancellation.Token);
                    }
                }
                catch (Exception exception) when (exception is not OperationCanceledException)
                {
                    Console.Error.WriteLine($"Validator queue polling failed safely: {exception.Message}");
                    await Task.Delay(options.IdlePollInterval, cancellation.Token);
                }
            }

            return 0;
        }
        catch (OperationCanceledException)
        {
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"Marketplace validator broker stopped: {exception.Message}");
            return 2;
        }
    }

    private static async Task ProcessLeaseAsync(
        SupabaseValidatorApi api,
        BrokerOptions options,
        ExchangePaths exchange,
        ValidationLease lease,
        CancellationToken cancellationToken)
    {
        string uploadId = lease.UploadId.ToString("D");
        string package = Path.Combine(exchange.Packages, uploadId + ".keireassetpackage");
        string downloading = package + ".downloading";
        string request = Path.Combine(exchange.Requests, uploadId + ".json");
        string report = Path.Combine(exchange.Reports, uploadId + ".json");
        DeleteJobFiles(exchange.Root, downloading, package, request, report);
        using CancellationTokenSource leaseLost = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        Task renewal = RenewLeaseAsync(api, options, lease.UploadId, leaseLost);
        bool renewalStopped = false;
        try
        {
            await api.DownloadAsync(lease, downloading, leaseLost.Token);
            File.Move(downloading, package);
            WriteJobRequest(request, lease);
            DateTimeOffset reportDeadline = DateTimeOffset.UtcNow + options.ValidationTimeout;
            while (!File.Exists(report))
            {
                if (DateTimeOffset.UtcNow >= reportDeadline)
                {
                    throw new TimeoutException("The offline validator did not return a report within its job limit.");
                }

                await Task.Delay(options.ReportPollInterval, leaseLost.Token);
            }

            FileInfo reportInfo = new(report);
            reportInfo.Refresh();
            FileSystemSafety.RejectLink(reportInfo);
            if (reportInfo.Length is <= 0 or > MaximumReportBytes)
            {
                throw new InvalidDataException("The offline validator report exceeds its size limit.");
            }

            byte[] reportBytes = await File.ReadAllBytesAsync(report, leaseLost.Token);
            leaseLost.Cancel();
            await AwaitRenewalAsync(renewal);
            renewalStopped = true;
            await api.CompleteAsync(lease, reportBytes, cancellationToken);
            Console.WriteLine($"Committed marketplace validation for upload {uploadId}.");
        }
        finally
        {
            if (!renewalStopped)
            {
                leaseLost.Cancel();
                await AwaitRenewalAsync(renewal);
            }

            DeleteJobFiles(exchange.Root, downloading, package, request, report);
        }
    }

    private static async Task AwaitRenewalAsync(Task renewal)
    {
        try
        {
            await renewal;
        }
        catch (OperationCanceledException)
        {
        }
    }

    private static async Task RenewLeaseAsync(
        SupabaseValidatorApi api,
        BrokerOptions options,
        Guid uploadId,
        CancellationTokenSource leaseLost)
    {
        TimeSpan interval = TimeSpan.FromSeconds(Math.Max(30, options.LeaseSeconds / 3));
        while (!leaseLost.IsCancellationRequested)
        {
            await Task.Delay(interval, leaseLost.Token);
            try
            {
                await api.RenewAsync(uploadId, leaseLost.Token);
            }
            catch
            {
                leaseLost.Cancel();
                throw;
            }
        }
    }

    private static void WriteJobRequest(string path, ValidationLease lease)
    {
        string parent = Path.GetDirectoryName(path)!;
        string temporary = Path.Combine(parent, $".{Path.GetFileName(path)}.{Guid.NewGuid():N}.tmp");
        try
        {
            File.WriteAllBytes(temporary, DistributionJson.Serialize(new BrokerJobRequest
            {
                UploadId = lease.UploadId.ToString("D"),
                ExpectedSizeBytes = lease.ExpectedSizeBytes,
                ExpectedSha256 = lease.ExpectedSha256,
            }));
            if (!OperatingSystem.IsWindows())
            {
                File.SetUnixFileMode(temporary, UnixFileMode.UserRead | UnixFileMode.UserWrite);
            }

            File.Move(temporary, path);
        }
        finally
        {
            File.Delete(temporary);
        }
    }

    private static void DeleteJobFiles(string exchangeRoot, params string[] paths)
    {
        string root = Path.GetFullPath(exchangeRoot);
        string prefix = root.EndsWith(Path.DirectorySeparatorChar) ? root : root + Path.DirectorySeparatorChar;
        StringComparison comparison = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        foreach (string path in paths)
        {
            string fullPath = Path.GetFullPath(path);
            if (!fullPath.StartsWith(prefix, comparison))
            {
                throw new InvalidOperationException("A validator exchange cleanup path escaped its root.");
            }

            File.Delete(fullPath);
        }
    }
}

internal sealed class BrokerJobRequest
{
    public int SchemaVersion { get; init; } = 1;

    public required string UploadId { get; init; }

    public required long ExpectedSizeBytes { get; init; }

    public required string ExpectedSha256 { get; init; }
}

internal sealed record ExchangePaths(string Root, string Packages, string Requests, string Reports)
{
    public static ExchangePaths Create(string root)
    {
        string fullRoot = Path.GetFullPath(root);
        return new ExchangePaths(
            fullRoot,
            EnsureChild(fullRoot, "packages"),
            EnsureChild(fullRoot, "requests"),
            EnsureChild(fullRoot, "reports"));
    }

    private static string EnsureChild(string root, string name)
    {
        string child = Path.Combine(root, name);
        FileSystemSafety.EnsureSafeDirectory(child);
        return child;
    }
}
