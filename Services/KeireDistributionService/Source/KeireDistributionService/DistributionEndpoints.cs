using Keire.Distribution;
using System.Text.Json;

namespace Keire.Distribution.Service;

public static class DistributionEndpoints
{
    public const string MetadataRateLimitPolicy = "distribution-metadata";
    public const string PackageRateLimitPolicy = "distribution-packages";

    public static void MapDistributionEndpoints(this WebApplication app)
    {
        app.MapGet("/health/live", WriteLiveAsync);
        app.MapGet("/health/ready", WriteReadyAsync);

        app.MapMethods(
                "/v1/catalog/{channel}/{platform}/{architecture}",
                [HttpMethods.Get, HttpMethods.Head],
                (HttpContext context, SnapshotProvider snapshots, DistributionOptions options, string channel,
                    string platform, string architecture) =>
                    SendCatalogAsync(context, snapshots, options, channel, platform, architecture, compact: false))
            .RequireRateLimiting(MetadataRateLimitPolicy);

        app.MapMethods(
                "/v2/catalog/{channel}/{platform}/{architecture}",
                [HttpMethods.Get, HttpMethods.Head],
                (HttpContext context, SnapshotProvider snapshots, DistributionOptions options, string channel,
                    string platform, string architecture) =>
                    SendCatalogAsync(context, snapshots, options, channel, platform, architecture, compact: true))
            .RequireRateLimiting(MetadataRateLimitPolicy);

        app.MapMethods(
                "/v1/content/{locale}",
                [HttpMethods.Get, HttpMethods.Head],
                (HttpContext context, SnapshotProvider snapshots, DistributionOptions options, string locale) =>
                    SendContentAsync(context, snapshots, options, locale))
            .RequireRateLimiting(MetadataRateLimitPolicy);

        app.MapMethods(
                "/v1/manifests/{sha256}",
                [HttpMethods.Get, HttpMethods.Head],
                (HttpContext context, SnapshotProvider snapshots, DistributionOptions options, string sha256) =>
                    SendManifestAsync(context, snapshots, options, sha256))
            .RequireRateLimiting(MetadataRateLimitPolicy);

        app.MapMethods(
                "/v1/packages/{sha256}",
                [HttpMethods.Get, HttpMethods.Head],
                (HttpContext context, SnapshotProvider snapshots, DistributionOptions options, string sha256) =>
                    SendPackageAsync(context, snapshots, options, sha256))
            .RequireRateLimiting(PackageRateLimitPolicy);

        app.MapFallback((HttpContext context) =>
            WriteErrorAsync(context, StatusCodes.Status404NotFound, "distribution.route_not_found", "Route not found."));
    }

    public static Task WriteErrorAsync(HttpContext context, int statusCode, string code, string message)
    {
        context.Response.StatusCode = statusCode;
        return context.Response.WriteAsJsonAsync(new
        {
            code,
            message,
        },
            options: (JsonSerializerOptions?)null,
            contentType: "application/problem+json; charset=utf-8",
            cancellationToken: context.RequestAborted);
    }

    private static Task WriteLiveAsync(HttpContext context)
    {
        return context.Response.WriteAsJsonAsync(new
        {
            status = "live",
        }, cancellationToken: context.RequestAborted);
    }

    private static Task WriteReadyAsync(HttpContext context, SnapshotProvider snapshots)
    {
        SnapshotProviderState state = snapshots.State;
        if (state.Current is null)
        {
            context.Response.StatusCode = StatusCodes.Status503ServiceUnavailable;
            return context.Response.WriteAsJsonAsync(new
            {
                status = "not-ready",
                error = "No validated distribution snapshot is available.",
            }, cancellationToken: context.RequestAborted);
        }

        return context.Response.WriteAsJsonAsync(new
        {
            status = state.LastRefreshError is null ? "ready" : "ready-degraded",
            snapshot = state.Current.SnapshotId,
            snapshotCreatedAt = state.Current.CreatedAt,
            lastRefreshAttempt = state.LastRefreshAttempt,
        }, cancellationToken: context.RequestAborted);
    }

    private static async Task SendCatalogAsync(
        HttpContext context,
        SnapshotProvider snapshots,
        DistributionOptions options,
        string channel,
        string platform,
        string architecture,
        bool compact)
    {
        string path;
        try
        {
            path = compact
                ? DistributionPaths.CompactCatalogPath(channel, platform, architecture)
                : DistributionPaths.CatalogPath(channel, platform, architecture);
        }
        catch (InvalidDataException)
        {
            await WriteErrorAsync(context, StatusCodes.Status404NotFound, "distribution.catalog_not_found", "Catalog not found.");
            return;
        }

        await SendDocumentAsync(context, snapshots, options, path, "distribution.catalog_not_found", "Catalog not found.");
    }

    private static async Task SendContentAsync(
        HttpContext context,
        SnapshotProvider snapshots,
        DistributionOptions options,
        string locale)
    {
        string path;
        try
        {
            path = DistributionPaths.ContentPath(locale);
        }
        catch (InvalidDataException)
        {
            await WriteErrorAsync(context, StatusCodes.Status404NotFound, "distribution.content_not_found", "Content catalog not found.");
            return;
        }

        await SendDocumentAsync(
            context,
            snapshots,
            options,
            path,
            "distribution.content_not_found",
            "Content catalog not found.");
    }

    private static async Task SendDocumentAsync(
        HttpContext context,
        SnapshotProvider snapshots,
        DistributionOptions options,
        string path,
        string notFoundCode,
        string notFoundMessage)
    {
        SnapshotIndex? snapshot = await RequireSnapshotAsync(context, snapshots);
        if (snapshot is null)
        {
            return;
        }

        if (!snapshot.TryGet(path, out DistributionFile? file) || file is null)
        {
            await WriteErrorAsync(context, StatusCodes.Status404NotFound, notFoundCode, notFoundMessage);
            return;
        }

        await HttpFileResponder.SendDocumentAsync(context, file, options.StreamBufferBytes, context.RequestAborted);
    }

    private static async Task SendPackageAsync(
        HttpContext context,
        SnapshotProvider snapshots,
        DistributionOptions options,
        string sha256)
    {
        string path;
        try
        {
            path = DistributionPaths.PackagePath(sha256);
        }
        catch (InvalidDataException)
        {
            await WriteErrorAsync(context, StatusCodes.Status404NotFound, "distribution.package_not_found", "Package not found.");
            return;
        }

        SnapshotIndex? snapshot = await RequireSnapshotAsync(context, snapshots);
        if (snapshot is null)
        {
            return;
        }

        if (!snapshot.TryGet(path, out DistributionFile? file) || file is null)
        {
            await WriteErrorAsync(context, StatusCodes.Status404NotFound, "distribution.package_not_found", "Package not found.");
            return;
        }

        await HttpFileResponder.SendPackageAsync(context, file, options.StreamBufferBytes, context.RequestAborted);
    }

    private static async Task SendManifestAsync(
        HttpContext context,
        SnapshotProvider snapshots,
        DistributionOptions options,
        string sha256)
    {
        string path;
        try
        {
            path = DistributionPaths.ManifestPath(sha256);
        }
        catch (InvalidDataException)
        {
            await WriteErrorAsync(context, StatusCodes.Status404NotFound, "distribution.manifest_not_found", "Package manifest not found.");
            return;
        }

        SnapshotIndex? snapshot = await RequireSnapshotAsync(context, snapshots);
        if (snapshot is null)
        {
            return;
        }

        if (!snapshot.TryGet(path, out DistributionFile? file) || file is null)
        {
            await WriteErrorAsync(context, StatusCodes.Status404NotFound, "distribution.manifest_not_found", "Package manifest not found.");
            return;
        }

        await HttpFileResponder.SendManifestAsync(context, file, options.StreamBufferBytes, context.RequestAborted);
    }

    private static async Task<SnapshotIndex?> RequireSnapshotAsync(HttpContext context, SnapshotProvider snapshots)
    {
        SnapshotIndex? snapshot = snapshots.State.Current;
        if (snapshot is not null)
        {
            return snapshot;
        }

        await WriteErrorAsync(
            context,
            StatusCodes.Status503ServiceUnavailable,
            "distribution.snapshot_unavailable",
            "No validated distribution snapshot is available.");
        return null;
    }
}
