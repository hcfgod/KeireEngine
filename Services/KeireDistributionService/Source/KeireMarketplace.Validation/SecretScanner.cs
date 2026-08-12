using System.Text;
using System.Text.RegularExpressions;
using Keire.Distribution;

namespace Keire.Marketplace.Validation;

internal static partial class SecretScanner
{
    private const int ChunkBytes = 64 * 1024;
    private const int OverlapBytes = 1024;

    public static async Task<IReadOnlyList<ValidationDiagnostic>> ScanAsync(
        string stagingRoot,
        CancellationToken cancellationToken)
    {
        List<ValidationDiagnostic> diagnostics = [];
        IReadOnlyList<string> paths = FileSystemSafety.EnumerateRegularFiles(
            stagingRoot,
            MarketplaceValidationContract.MaximumExtractedEntries,
            cancellationToken);
        foreach (string path in paths)
        {
            cancellationToken.ThrowIfCancellationRequested();
            string fullPath = DistributionPaths.ResolveConfined(stagingRoot, path);
            await ScanFileAsync(fullPath, path, diagnostics, cancellationToken);
        }

        return diagnostics;
    }

    private static async Task ScanFileAsync(
        string fullPath,
        string relativePath,
        List<ValidationDiagnostic> diagnostics,
        CancellationToken cancellationToken)
    {
        await using FileStream stream = new(
            fullPath,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read,
            ChunkBytes,
            FileOptions.Asynchronous | FileOptions.SequentialScan);
        byte[] buffer = new byte[ChunkBytes];
        byte[] overlap = [];
        HashSet<string> detected = new(StringComparer.Ordinal);
        while (true)
        {
            int count = await stream.ReadAsync(buffer, cancellationToken);
            if (count == 0)
            {
                return;
            }

            byte[] scanBytes = new byte[overlap.Length + count];
            overlap.CopyTo(scanBytes, 0);
            buffer.AsSpan(0, count).CopyTo(scanBytes.AsSpan(overlap.Length));
            string text = Encoding.Latin1.GetString(scanBytes);
            Detect(PrivateKeyRegex(), "SECRET_PRIVATE_KEY", "A private key was detected.", text);
            Detect(GitHubTokenRegex(), "SECRET_GITHUB_TOKEN", "A GitHub access token was detected.", text);
            Detect(AwsAccessKeyRegex(), "SECRET_AWS_ACCESS_KEY", "An AWS access-key identifier was detected.", text);
            Detect(SlackTokenRegex(), "SECRET_SLACK_TOKEN", "A Slack access token was detected.", text);
            Detect(LabeledSecretRegex(), "SECRET_LABELED_VALUE", "A credential-like labeled value was detected.", text);

            int overlapCount = Math.Min(OverlapBytes, scanBytes.Length);
            overlap = scanBytes[^overlapCount..];
        }

        void Detect(Regex expression, string code, string message, string text)
        {
            if (detected.Add(code) && expression.IsMatch(text))
            {
                diagnostics.Add(new ValidationDiagnostic
                {
                    Code = code,
                    Severity = ValidationSeverities.Error,
                    Message = message,
                    Path = relativePath,
                });
            }
        }
    }

    [GeneratedRegex(
        "-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----",
        RegexOptions.CultureInvariant | RegexOptions.NonBacktracking)]
    private static partial Regex PrivateKeyRegex();

    [GeneratedRegex(
        "(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9_]{30,}",
        RegexOptions.CultureInvariant | RegexOptions.NonBacktracking)]
    private static partial Regex GitHubTokenRegex();

    [GeneratedRegex("AKIA[0-9A-Z]{16}", RegexOptions.CultureInvariant | RegexOptions.NonBacktracking)]
    private static partial Regex AwsAccessKeyRegex();

    [GeneratedRegex(
        "xox(?:b|p|a|r|s)-[A-Za-z0-9-]{20,}",
        RegexOptions.CultureInvariant | RegexOptions.NonBacktracking)]
    private static partial Regex SlackTokenRegex();

    [GeneratedRegex(
        "(?:password|passwd|secret|service[_-]?role|api[_-]?key|access[_-]?token)\\s*[:=]\\s*[\\\"']?[A-Za-z0-9_./+\\-=]{20,}",
        RegexOptions.CultureInvariant | RegexOptions.IgnoreCase | RegexOptions.NonBacktracking)]
    private static partial Regex LabeledSecretRegex();
}
