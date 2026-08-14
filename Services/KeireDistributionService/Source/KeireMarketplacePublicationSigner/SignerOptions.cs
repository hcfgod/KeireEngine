using System.Text.RegularExpressions;
using Keire.Marketplace.Security;

namespace Keire.Marketplace.PublicationSigner;

internal sealed partial class SignerOptions
{
    public required Uri SupabaseUrl { get; init; }

    public required string QueueSecret { get; init; }

    public required string WorkerId { get; init; }

    public required int LeaseSeconds { get; init; }

    public required TimeSpan IdlePollInterval { get; init; }

    public required MarketplaceVerificationKey PublicationVerificationKey { get; init; }

    public required MarketplaceVerificationKey ValidatorVerificationKey { get; init; }

    public static SignerOptions FromEnvironment()
    {
        string rawUrl = Require("KEIRE_SUPABASE_URL");
        if (!Uri.TryCreate(rawUrl, UriKind.Absolute, out Uri? url) || url.Scheme != Uri.UriSchemeHttps ||
            !string.IsNullOrEmpty(url.UserInfo) || !string.IsNullOrEmpty(url.Query) || !string.IsNullOrEmpty(url.Fragment))
        {
            throw new InvalidOperationException("KEIRE_SUPABASE_URL must be an HTTPS origin without credentials or query data.");
        }

        string secret = Require("KEIRE_MARKETPLACE_PUBLICATION_SIGNER_SECRET");
        if (secret.Length is < 32 or > 256 || secret.Any(char.IsWhiteSpace))
        {
            throw new InvalidOperationException("KEIRE_MARKETPLACE_PUBLICATION_SIGNER_SECRET has an invalid shape.");
        }

        string worker = Environment.GetEnvironmentVariable("KEIRE_MARKETPLACE_PUBLICATION_WORKER_ID") ?? DefaultWorkerId();
        if (!WorkerIdRegex().IsMatch(worker))
        {
            throw new InvalidOperationException("KEIRE_MARKETPLACE_PUBLICATION_WORKER_ID contains unsupported characters.");
        }

        return new SignerOptions
        {
            SupabaseUrl = new Uri(url.GetLeftPart(UriPartial.Authority) + "/", UriKind.Absolute),
            QueueSecret = secret,
            WorkerId = worker,
            LeaseSeconds = ParseInteger("KEIRE_MARKETPLACE_PUBLICATION_LEASE_SECONDS", 300, 60, 900),
            IdlePollInterval = TimeSpan.FromMilliseconds(
                ParseInteger("KEIRE_MARKETPLACE_PUBLICATION_IDLE_POLL_MS", 5000, 250, 60000)),
            PublicationVerificationKey = MarketplaceVerificationKey.FromFile(
                Require("KEIRE_MARKETPLACE_PUBLICATION_PUBLIC_KEY")),
            ValidatorVerificationKey = MarketplaceVerificationKey.FromFile(
                Require("KEIRE_VALIDATOR_ATTESTATION_PUBLIC_KEY")),
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

        return int.TryParse(value, System.Globalization.NumberStyles.None,
                   System.Globalization.CultureInfo.InvariantCulture, out int parsed) &&
               parsed >= minimum && parsed <= maximum
            ? parsed
            : throw new InvalidOperationException($"Environment variable '{name}' is outside its allowed range.");
    }

    private static string DefaultWorkerId()
    {
        string host = NonWorkerCharacterRegex().Replace(Environment.MachineName, "-").Trim('-');
        host = string.IsNullOrEmpty(host) ? "host" : host[..Math.Min(host.Length, 64)];
        return $"publication-{host}-{Guid.NewGuid():N}";
    }

    [GeneratedRegex("^[A-Za-z0-9][A-Za-z0-9._:-]{2,127}$", RegexOptions.CultureInvariant | RegexOptions.NonBacktracking)]
    private static partial Regex WorkerIdRegex();

    [GeneratedRegex("[^A-Za-z0-9._:-]+", RegexOptions.CultureInvariant | RegexOptions.NonBacktracking)]
    private static partial Regex NonWorkerCharacterRegex();
}
