using System.Text;
using System.Text.Json;
using Keire.Distribution;
using Keire.Marketplace.Security;
using Keire.Marketplace.Validation;
using Keire.Marketplace.Validator.Broker;

namespace Keire.Marketplace.Validator.Tests;

internal static partial class Program
{
    // Parent owns isolated MySQL tables and registers only the ephemeral public key.
    // Credentials arrive through private stdin, never command-line arguments.
    private static async Task<int> RunLiveValidatorAsync()
    {
        try
        {
            using CancellationTokenSource timeout = new(TimeSpan.FromSeconds(45));
            string input = await Console.In.ReadLineAsync(timeout.Token)
                ?? throw new InvalidDataException("Missing isolated fixture configuration.");
            using JsonDocument configuration = JsonDocument.Parse(input);
            Uri origin = new(configuration.RootElement.GetProperty("origin").GetString()!);
            if (origin.Scheme != Uri.UriSchemeHttps || origin.Host != "localhost" ||
                origin.AbsolutePath != "/" || origin.UserInfo.Length != 0 ||
                origin.Query.Length != 0 || origin.Fragment.Length != 0)
            {
                throw new InvalidDataException("Live acceptance requires a localhost HTTPS origin.");
            }

            using TemporaryDirectory directory = new();
            using MarketplaceSigningKey key = MarketplaceSigningKey.Create();
            Console.WriteLine(JsonSerializer.Serialize(key.PublicDocument,
                new JsonSerializerOptions(DistributionJson.Options) { WriteIndented = false }));
            if (await Console.In.ReadLineAsync(timeout.Token) != "ready")
            {
                throw new InvalidDataException("The isolated public key was not registered.");
            }

            BrokerOptions options = new()
            {
                ApiOrigin = origin,
                QueuePath = "internal/validator/v1/queue/",
                BrokerSecret = configuration.RootElement.GetProperty("brokerSecret").GetString()!,
                ExpectedValidatorFingerprintSha256 = new string('a', 64),
                AttestationVerificationKey = MarketplaceVerificationKey.FromDocument(key.PublicDocument),
                ExchangeRoot = directory.Path,
                WorkerId = "csharp-https-fixture",
                LeaseSeconds = 900,
                IdlePollInterval = TimeSpan.FromMilliseconds(100),
                ReportPollInterval = TimeSpan.FromMilliseconds(100),
                ValidationTimeout = TimeSpan.FromSeconds(30),
            };
            using ValidatorQueueApi api = new(options);
            ValidationLease lease = await api.LeaseAsync(timeout.Token)
                ?? throw new InvalidDataException("No isolated validation lease.");
            string destination = Path.Combine(directory.Path, "package.keireassetpackage");
            await api.DownloadAsync(lease, destination, timeout.Token);
            await api.RenewAsync(lease.UploadId, timeout.Token);
            byte[] evidence = Encoding.UTF8.GetBytes("{\"schemaVersion\":1,\"files\":[]}");
            MarketplaceValidationReport unsigned = new()
            {
                ValidatorFingerprintSha256 = options.ExpectedValidatorFingerprintSha256,
                PackageSha256 = lease.ExpectedSha256,
                ManifestSha256 = new string('b', 64),
                Passed = true,
                MalwareScanResult = ValidationStatuses.Clean,
                SecretScanResult = ValidationStatuses.Clean,
                ManagedValidationResult = ValidationStatuses.NotApplicable,
                EvidenceStoragePath = lease.EvidenceStoragePath,
                EvidenceSha256 = Sha256(evidence),
                EvidenceSizeBytes = evidence.Length,
                Diagnostics = [],
                CompletedAt = DateTimeOffset.UtcNow,
            };
            PackageValidationRequest request = new(
                lease.UploadId, lease.VersionId, lease.StorageBucket, lease.StoragePath,
                destination, Path.Combine(directory.Path, "work"), "fixture-only", "dotnet",
                null, "fixture-only", lease.ExpectedSizeBytes, lease.ExpectedSha256,
                options.ExpectedValidatorFingerprintSha256);
            MarketplaceValidationReport report = unsigned with
            {
                Attestation = ValidatorAttestation.Sign(request, unsigned, key),
            };
            await api.UploadEvidenceAsync(lease, evidence, timeout.Token);
            await api.CompleteAsync(lease, DistributionJson.Serialize(report), timeout.Token);
            await api.CompleteAsync(lease, DistributionJson.Serialize(report), timeout.Token);
            Console.WriteLine("{\"status\":\"csharp-validator-https-passed\"}");
            return 0;
        }
        catch
        {
            Console.Error.WriteLine("Isolated C# validator HTTPS acceptance failed; private diagnostics suppressed.");
            return 1;
        }
    }
}
