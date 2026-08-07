using System.Text.Json;
using System.Threading.RateLimiting;
using Microsoft.AspNetCore.RateLimiting;

namespace Keire.Distribution.Service;

public static class DistributionApplication
{
    public static WebApplication Build(string[] args)
    {
        WebApplicationBuilder builder = WebApplication.CreateBuilder(args);
        DistributionOptions distributionOptions = DistributionOptions.Load(builder.Configuration);

        builder.Logging.ClearProviders();
        builder.Logging.AddJsonConsole(options =>
        {
            options.IncludeScopes = true;
            options.TimestampFormat = "O";
            options.UseUtcTimestamp = true;
            options.JsonWriterOptions = new JsonWriterOptions
            {
                Indented = false,
            };
        });
        builder.WebHost.UseUrls(distributionOptions.BindUrl);
        builder.WebHost.ConfigureKestrel(options =>
        {
            options.AddServerHeader = false;
            options.Limits.MaxRequestBodySize = 0;
            options.Limits.MaxRequestHeadersTotalSize = 32 * 1024;
            options.Limits.RequestHeadersTimeout = TimeSpan.FromSeconds(15);
            options.Limits.KeepAliveTimeout = TimeSpan.FromMinutes(2);
        });

        builder.Services.AddSingleton(distributionOptions);
        builder.Services.AddSingleton<SnapshotProvider>();
        builder.Services.AddHostedService(provider => provider.GetRequiredService<SnapshotProvider>());
        builder.Services.AddRateLimiter(options =>
        {
            options.RejectionStatusCode = StatusCodes.Status429TooManyRequests;
            options.OnRejected = async (context, _) =>
            {
                await DistributionEndpoints.WriteErrorAsync(
                    context.HttpContext,
                    StatusCodes.Status429TooManyRequests,
                    "distribution.rate_limited",
                    "Request limit exceeded. Retry later.");
            };
            options.AddFixedWindowLimiter(DistributionEndpoints.MetadataRateLimitPolicy, limiter =>
            {
                limiter.PermitLimit = distributionOptions.MetadataRequestsPerMinute;
                limiter.Window = TimeSpan.FromMinutes(1);
                limiter.AutoReplenishment = true;
                limiter.QueueLimit = 0;
            });
            options.AddConcurrencyLimiter(DistributionEndpoints.PackageRateLimitPolicy, limiter =>
            {
                limiter.PermitLimit = distributionOptions.PackageConcurrentStreams;
                limiter.QueueLimit = distributionOptions.PackageQueueLimit;
                limiter.QueueProcessingOrder = QueueProcessingOrder.OldestFirst;
            });
        });

        WebApplication app = builder.Build();
        app.UseRateLimiter();
        app.MapDistributionEndpoints();
        return app;
    }
}
