using System.Text;
using System.Text.Json;
using Keire.Distribution;
using Keire.Marketplace.PublicationSigner;
using Keire.Marketplace.Security;

namespace Keire.Marketplace.Validator.Tests;

internal static partial class Program
{
    private static async Task<int> RunLivePublicationAsync()
    {
        try
        {
            using CancellationTokenSource timeout = new(TimeSpan.FromSeconds(45));
            string input = await Console.In.ReadLineAsync(timeout.Token)
                ?? throw new InvalidDataException("Missing isolated publication configuration.");
            using JsonDocument configuration = JsonDocument.Parse(input);
            Uri origin = new(configuration.RootElement.GetProperty("origin").GetString()!);
            if (origin.Scheme != Uri.UriSchemeHttps || origin.Host != "localhost" ||
                origin.AbsolutePath != "/" || origin.UserInfo.Length != 0 ||
                origin.Query.Length != 0 || origin.Fragment.Length != 0)
            {
                throw new InvalidDataException("Publication acceptance requires localhost HTTPS.");
            }

            DistributionPublicKeyDocument validatorKey = DistributionJson.DeserializeStrict<DistributionPublicKeyDocument>(
                Encoding.UTF8.GetBytes(configuration.RootElement.GetProperty("validatorKey").GetRawText()));
            using MarketplaceSigningKey key = MarketplaceSigningKey.Create();
            Console.WriteLine(JsonSerializer.Serialize(key.PublicDocument,
                new JsonSerializerOptions(DistributionJson.Options) { WriteIndented = false }));
            if (await Console.In.ReadLineAsync(timeout.Token) != "ready")
            {
                throw new InvalidDataException("The isolated publication key was not registered.");
            }

            SignerOptions options = new()
            {
                ApiOrigin = origin,
                QueuePath = "internal/publication/v1/queue/",
                QueueSecret = configuration.RootElement.GetProperty("queueSecret").GetString()!,
                WorkerId = "csharp-publication-fixture",
                LeaseSeconds = 300,
                IdlePollInterval = TimeSpan.FromMilliseconds(100),
                PublicationVerificationKey = MarketplaceVerificationKey.FromDocument(key.PublicDocument),
                ValidatorVerificationKey = MarketplaceVerificationKey.FromDocument(validatorKey),
            };
            using PublicationQueueApi api = new(options);
            PublicationLease lease = await api.LeaseAsync(key.PublicDocument.KeyId, timeout.Token)
                ?? throw new InvalidDataException("No isolated publication lease.");
            Assert.Equal(9007199254740993L, lease.PublicationSequence, "The HTTP publication sequence lost precision.");
            PublicationSignerProgram.VerifyValidatorAttestation(lease, options.ValidatorVerificationKey);
            await api.RenewAsync(lease.JobId, timeout.Token);
            string signedManifest = PublicationSignerProgram.SignPublication(lease, key);
            await api.CompleteAsync(lease.JobId, signedManifest, timeout.Token);
            await api.CompleteAsync(lease.JobId, signedManifest, timeout.Token);
            Assert.True(await api.LeaseAsync(key.PublicDocument.KeyId, timeout.Token) is null,
                "A completed publication was leased again.");
            Console.WriteLine("{\"status\":\"csharp-publication-https-passed\"}");
            return 0;
        }
        catch
        {
            Console.Error.WriteLine("Isolated C# publication HTTPS acceptance failed.");
            return 1;
        }
    }
}
