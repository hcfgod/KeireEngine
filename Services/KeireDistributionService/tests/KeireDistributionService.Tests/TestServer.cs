using Keire.Distribution.Service;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting.Server;
using Microsoft.AspNetCore.Hosting.Server.Features;
using Microsoft.Extensions.DependencyInjection;

namespace Keire.Distribution.Tests;

internal sealed class TestServer : IAsyncDisposable
{
    private readonly WebApplication m_Application;

    private TestServer(WebApplication application, HttpClient client)
    {
        m_Application = application;
        Client = client;
    }

    public HttpClient Client { get; }

    public static async Task<TestServer> StartAsync(
        string storageRoot,
        int metadataRequestsPerMinute = 10_000,
        CancellationToken cancellationToken = default)
    {
        string[] arguments =
        [
            $"--Distribution:StorageRoot={storageRoot}",
            "--Distribution:BindUrl=http://127.0.0.1:0",
            "--Distribution:SnapshotPollSeconds=1",
            $"--Distribution:MetadataRequestsPerMinute={metadataRequestsPerMinute}",
            "--Distribution:PackageConcurrentStreams=2",
            "--Distribution:PackageQueueLimit=2",
            "--Logging:LogLevel:Default=Warning",
        ];
        WebApplication application = DistributionApplication.Build(arguments);
        await application.StartAsync(cancellationToken);
        IServer server = application.Services.GetRequiredService<IServer>();
        IServerAddressesFeature addresses = server.Features.Get<IServerAddressesFeature>()
            ?? throw new InvalidOperationException("Test server did not publish an address feature.");
        string address = addresses.Addresses.Single();
        HttpClient client = new()
        {
            BaseAddress = new Uri(address, UriKind.Absolute),
            Timeout = TimeSpan.FromSeconds(10),
        };
        return new TestServer(application, client);
    }

    public async Task WaitUntilReadyAsync(CancellationToken cancellationToken = default)
    {
        DateTimeOffset deadline = DateTimeOffset.UtcNow.AddSeconds(10);
        while (DateTimeOffset.UtcNow < deadline)
        {
            using HttpResponseMessage response = await Client.GetAsync("/health/ready", cancellationToken);
            if (response.IsSuccessStatusCode)
            {
                return;
            }

            await Task.Delay(100, cancellationToken);
        }

        throw new TimeoutException("Distribution service did not become ready.");
    }

    public async ValueTask DisposeAsync()
    {
        Client.Dispose();
        await m_Application.StopAsync();
        await m_Application.DisposeAsync();
    }
}
