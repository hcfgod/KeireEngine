using System.Diagnostics;
using System.Text;
using Keire.Distribution;

namespace Keire.Marketplace.Validation;

internal sealed record ProcessResult(int ExitCode, string StandardOutput, string StandardError);

internal static class BoundedProcess
{
    private const int MaximumCapturedCharacters = 1024 * 1024;

    public static async Task<ProcessResult> RunAsync(
        string executable,
        IEnumerable<string> arguments,
        string workingDirectory,
        IReadOnlyDictionary<string, string?>? environment,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        if (timeout <= TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(timeout));
        }

        string fullExecutable = Path.GetFullPath(executable);
        FileInfo executableInfo = new(fullExecutable);
        if (!executableInfo.Exists)
        {
            throw new FileNotFoundException("A required validator executable was not found.", fullExecutable);
        }

        FileSystemSafety.RejectLink(executableInfo);
        ProcessStartInfo startInfo = new()
        {
            FileName = fullExecutable,
            WorkingDirectory = Path.GetFullPath(workingDirectory),
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        foreach (string argument in arguments)
        {
            startInfo.ArgumentList.Add(argument);
        }

        if (environment is not null)
        {
            foreach ((string name, string? value) in environment)
            {
                startInfo.Environment[name] = value;
            }
        }

        using Process process = new() { StartInfo = startInfo };
        if (!process.Start())
        {
            throw new InvalidOperationException("A validator subprocess could not be started.");
        }

        Task<string> standardOutput = CaptureAsync(process.StandardOutput, cancellationToken);
        Task<string> standardError = CaptureAsync(process.StandardError, cancellationToken);
        using CancellationTokenSource timeoutSource = new(timeout);
        using CancellationTokenSource linkedSource =
            CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, timeoutSource.Token);
        try
        {
            await process.WaitForExitAsync(linkedSource.Token);
        }
        catch (OperationCanceledException) when (timeoutSource.IsCancellationRequested && !cancellationToken.IsCancellationRequested)
        {
            Kill(process);
            await DrainAsync(standardOutput, standardError);
            throw new TimeoutException($"Validator subprocess exceeded its {timeout.TotalSeconds:0}-second limit.");
        }
        catch
        {
            Kill(process);
            await DrainAsync(standardOutput, standardError);
            throw;
        }

        return new ProcessResult(process.ExitCode, await standardOutput, await standardError);
    }

    private static async Task<string> CaptureAsync(StreamReader reader, CancellationToken cancellationToken)
    {
        char[] buffer = new char[4096];
        StringBuilder result = new(MaximumCapturedCharacters);
        while (true)
        {
            int count = await reader.ReadAsync(buffer.AsMemory(), cancellationToken);
            if (count == 0)
            {
                return result.ToString();
            }

            int remaining = MaximumCapturedCharacters - result.Length;
            if (remaining > 0)
            {
                result.Append(buffer, 0, Math.Min(count, remaining));
            }
        }
    }

    private static async Task DrainAsync(Task<string> standardOutput, Task<string> standardError)
    {
        try
        {
            await Task.WhenAll(standardOutput, standardError);
        }
        catch (OperationCanceledException)
        {
        }
    }

    private static void Kill(Process process)
    {
        if (!process.HasExited)
        {
            process.Kill(entireProcessTree: true);
        }
    }
}
