using System.Net.Http;
using Keire.Marketplace.PublicationSigner;
using Keire.Marketplace.Security;

namespace Keire.Marketplace.Validator.Tests;

internal static partial class Program
{
    private static async Task PublicationSignerSelectsBackendAsync()
    {
        using MarketplaceSigningKey key = MarketplaceSigningKey.Create();
        foreach (string provider in new[] { "supabase", "mysql" })
        {
            (Uri origin, string queuePath) = SignerOptions.ResolveEndpoint(provider, "https://platform.invalid/");
            SignerOptions options = new()
            {
                ApiOrigin = origin,
                QueuePath = queuePath,
                QueueSecret = "isolated-publication-secret-1234567890",
                WorkerId = "publication-fixture",
                LeaseSeconds = 300,
                IdlePollInterval = TimeSpan.FromSeconds(1),
                PublicationVerificationKey = MarketplaceVerificationKey.FromDocument(key.PublicDocument),
                ValidatorVerificationKey = MarketplaceVerificationKey.FromDocument(key.PublicDocument),
            };
            RecordingHandler handler = new(_ => JsonResponse("{\"data\":{\"lease\":null}}"));
            using PublicationQueueApi api = new(options, handler);
            Assert.True(await api.LeaseAsync(key.PublicDocument.KeyId, default) is null, "Expected an empty queue.");
            HttpRequestMessage request = handler.Requests.Single();
            Assert.Equal(provider == "mysql" ? "/internal/publication/v1/queue/"
                : "/functions/v1/marketplace-publication-queue", request.RequestUri!.AbsolutePath,
                "The publication backend selected the wrong endpoint.");
            Assert.Equal(options.QueueSecret, request.Headers.GetValues("x-keire-publication-secret").Single(),
                "The dedicated publication credential was not retained.");
            Assert.True(request.Headers.Authorization is null && !request.Headers.Contains("apikey"),
                "The signer sent unrelated API credentials.");
        }

        foreach (string unsafeOrigin in new[]
        {
            "http://platform.invalid/", "https://user:password@platform.invalid/",
            "https://platform.invalid/path", "https://platform.invalid/?query=1", "https://platform.invalid/#fragment",
        })
        {
            Assert.Throws<InvalidOperationException>(() => SignerOptions.ResolveEndpoint("mysql", unsafeOrigin));
        }
        Assert.Throws<InvalidOperationException>(() => SignerOptions.ResolveEndpoint("unknown", "https://platform.invalid/"));
    }
}
