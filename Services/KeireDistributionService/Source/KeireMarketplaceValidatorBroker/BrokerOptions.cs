using System.Text.RegularExpressions;
using Keire.Distribution;

namespace Keire.Marketplace.Validator.Broker;

internal sealed partial class BrokerOptions
{
    public required Uri SupabaseUrl { get; init; }

    public required string BrokerSecret { get; init; }

    public required string ExpectedValidatorFingerprintSha256 { get; init; }

    public required string ExchangeRoot { get; init; }

    public required string WorkerId { get; init; }

    public required int LeaseSeconds { get; init; }

    public required TimeSpan IdlePollInterval { get; init; }

    public required TimeSpan ReportPollInterval { get; init; }

    public required TimeSpan ValidationTimeout { get; init; }

    public static BrokerOptions FromEnvironment()
    {
        string rawUrl = Require("KEIRE_SUPABASE_URL");
        if (!Uri.TryCreate(rawUrl, UriKind.Absolute, out Uri? url) || url.Scheme != Uri.UriSchemeHttps ||
            !string.IsNullOrEmpty(url.UserInfo) || !string.IsNullOrEmpty(url.Query) || !string.IsNullOrEmpty(url.Fragment))
        {
            throw new InvalidOperationException("KEIRE_SUPABASE_URL must be an HTTPS origin without credentials or query data.");
        }

        string secret = Require("KEIRE_VALIDATOR_BROKER_SECRET");
        if (secret.Length is < 32 or > 256 || secret.Any(char.IsWhiteSpace))
        {
            throw new InvalidOperationException("KEIRE_VALIDATOR_BROKER_SECRET has an invalid shape.");
        }

        string expectedFingerprint = Require("KEIRE_VALIDATOR_EXPECTED_FINGERPRINT_SHA256");
        if (!DistributionPaths.IsSha256(expectedFingerprint))
        {
            throw new InvalidOperationException("KEIRE_VALIDATOR_EXPECTED_FINGERPRINT_SHA256 must be a lowercase SHA-256 digest.");
        }

        string exchangeRoot = Path.GetFullPath(Require("KEIRE_VALIDATOR_EXCHANGE_ROOT"));
        DirectoryInfo exchange = new(exchangeRoot);
        if (!exchange.Exists)
        {
            throw new DirectoryNotFoundException("KEIRE_VALIDATOR_EXCHANGE_ROOT does not exist.");
        }

        exchange.Refresh();
        FileSystemSafety.RejectLink(exchange);
        string worker = Environment.GetEnvironmentVariable("KEIRE_VALIDATOR_WORKER_ID") ?? DefaultWorkerId();
        if (!WorkerIdRegex().IsMatch(worker))
        {
            throw new InvalidOperationException("KEIRE_VALIDATOR_WORKER_ID contains unsupported characters.");
        }

        int leaseSeconds = ParseInteger("KEIRE_VALIDATOR_LEASE_SECONDS", 900, 60, 1800);
        int idlePollMilliseconds = ParseInteger("KEIRE_VALIDATOR_IDLE_POLL_MS", 5000, 250, 60000);
        int reportPollMilliseconds = ParseInteger("KEIRE_VALIDATOR_REPORT_POLL_MS", 500, 100, 5000);
        int validationTimeoutSeconds = ParseInteger("KEIRE_VALIDATOR_JOB_TIMEOUT_SECONDS", 1800, 60, 14400);
        return new BrokerOptions
        {
            SupabaseUrl = new Uri(url.GetLeftPart(UriPartial.Authority) + "/", UriKind.Absolute),
            BrokerSecret = secret,
            ExpectedValidatorFingerprintSha256 = expectedFingerprint,
            ExchangeRoot = exchangeRoot,
            WorkerId = worker,
            LeaseSeconds = leaseSeconds,
            IdlePollInterval = TimeSpan.FromMilliseconds(idlePollMilliseconds),
            ReportPollInterval = TimeSpan.FromMilliseconds(reportPollMilliseconds),
            ValidationTimeout = TimeSpan.FromSeconds(validationTimeoutSeconds),
        };
    }

    private static string Require(string name)
    {
        string? value = Environment.GetEnvironmentVariable(name);
        return !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new InvalidOperationException($"Required environment variable '{name}' is missing.");
    }

    private static int ParseInteger(string name, int defaultValue, int minimum, int maximum)
    {
        string? value = Environment.GetEnvironmentVariable(name);
        if (value is null)
        {
            return defaultValue;
        }

        return int.TryParse(value, System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out int parsed) &&
            parsed >= minimum && parsed <= maximum
            ? parsed
            : throw new InvalidOperationException($"Environment variable '{name}' is outside its allowed range.");
    }

    private static string DefaultWorkerId()
    {
        string host = NonWorkerCharacterRegex().Replace(Environment.MachineName, "-").Trim('-');
        if (host.Length < 1)
        {
            host = "host";
        }

        if (host.Length > 64)
        {
            host = host[..64];
        }

        return $"validator-{host}-{Guid.NewGuid():N}";
    }

    [GeneratedRegex("^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$", RegexOptions.CultureInvariant | RegexOptions.NonBacktracking)]
    private static partial Regex WorkerIdRegex();

    [GeneratedRegex("[^A-Za-z0-9._:-]+", RegexOptions.CultureInvariant | RegexOptions.NonBacktracking)]
    private static partial Regex NonWorkerCharacterRegex();
}
