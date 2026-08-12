namespace Keire.Marketplace.Validation;

public sealed class ClamAvScanner(string executable) : IMalwareScanner
{
    private readonly string m_Executable = Path.GetFullPath(executable);

    public Task<MalwareScanResult> ScanFileAsync(string path, CancellationToken cancellationToken)
    {
        return ScanAsync(Path.GetFullPath(path), recursive: false, cancellationToken);
    }

    public Task<MalwareScanResult> ScanDirectoryAsync(string path, CancellationToken cancellationToken)
    {
        return ScanAsync(Path.GetFullPath(path), recursive: true, cancellationToken);
    }

    private async Task<MalwareScanResult> ScanAsync(string path, bool recursive, CancellationToken cancellationToken)
    {
        string scannerName = Path.GetFileNameWithoutExtension(m_Executable);
        bool daemonScanner = string.Equals(scannerName, "clamdscan", StringComparison.OrdinalIgnoreCase);
        List<string> arguments = daemonScanner
            ? ["--fdpass", "--multiscan", "--no-summary"]
            : ["--infected", "--no-summary"];
        if (recursive && !daemonScanner)
        {
            arguments.Add("--recursive=yes");
        }

        arguments.Add("--");
        arguments.Add(path);
        ProcessResult scan = await BoundedProcess.RunAsync(
            m_Executable,
            arguments,
            Path.GetDirectoryName(path) ?? path,
            environment: null,
            TimeSpan.FromMinutes(15),
            cancellationToken);
        string? engineVersion = await ReadVersionAsync(cancellationToken);
        if (scan.ExitCode == 0)
        {
            return new MalwareScanResult(ValidationStatuses.Clean, engineVersion, []);
        }

        if (scan.ExitCode == 1)
        {
            return new MalwareScanResult(
                ValidationStatuses.Infected,
                engineVersion,
                [Error("MALWARE_DETECTED", "Malware scanning detected an unsafe payload.")]);
        }

        return new MalwareScanResult(
            ValidationStatuses.Failed,
            engineVersion,
            [Error("MALWARE_SCANNER_FAILED", "The malware scanner failed closed before it could approve the payload.")]);
    }

    private async Task<string?> ReadVersionAsync(CancellationToken cancellationToken)
    {
        try
        {
            ProcessResult version = await BoundedProcess.RunAsync(
                m_Executable,
                ["--version"],
                Path.GetDirectoryName(m_Executable) ?? Directory.GetCurrentDirectory(),
                environment: null,
                TimeSpan.FromSeconds(15),
                cancellationToken);
            return version.ExitCode == 0 ? FirstLine(version.StandardOutput) : null;
        }
        catch (Exception exception) when (exception is not OperationCanceledException)
        {
            return null;
        }
    }

    private static string? FirstLine(string value)
    {
        string? line = value.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries).FirstOrDefault();
        return line is { Length: > 256 } ? line[..256] : line;
    }

    private static ValidationDiagnostic Error(string code, string message)
    {
        return new ValidationDiagnostic
        {
            Code = code,
            Severity = ValidationSeverities.Error,
            Message = message,
        };
    }
}
