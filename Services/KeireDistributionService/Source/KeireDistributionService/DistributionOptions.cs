using Keire.Distribution;

namespace Keire.Distribution.Service;

public sealed class DistributionOptions
{
    public required string StorageRoot { get; init; }
    public required string BindUrl { get; init; }
    public required TimeSpan SnapshotPollInterval { get; init; }
    public required SnapshotValidationOptions Validation { get; init; }
    public int MetadataRequestsPerMinute { get; init; }
    public int PackageConcurrentStreams { get; init; }
    public int PackageQueueLimit { get; init; }
    public int StreamBufferBytes { get; init; }

    public static DistributionOptions Load(IConfiguration configuration)
    {
        IConfigurationSection section = configuration.GetSection("Distribution");
        string configuredRoot = section["StorageRoot"] ?? "distribution-data";
        string root = Path.IsPathRooted(configuredRoot)
            ? Path.GetFullPath(configuredRoot)
            : Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, configuredRoot));
        string bindUrl = section["BindUrl"] ?? "http://127.0.0.1:5088";
        if (!Uri.TryCreate(bindUrl, UriKind.Absolute, out Uri? uri) ||
            uri.Scheme is not ("http" or "https") || string.IsNullOrWhiteSpace(uri.Host))
        {
            throw new InvalidOperationException("Distribution:BindUrl must be an absolute HTTP or HTTPS URL.");
        }

        return new DistributionOptions
        {
            StorageRoot = root,
            BindUrl = bindUrl,
            SnapshotPollInterval = TimeSpan.FromSeconds(BoundedInt(section, "SnapshotPollSeconds", 2, 1, 300)),
            Validation = new SnapshotValidationOptions
            {
                MaximumFiles = BoundedInt(section, "MaximumSnapshotFiles", 100_000, 1, 1_000_000),
                MaximumManifestBytes = BoundedLong(section, "MaximumManifestBytes", 8 * 1024 * 1024, 1024, 64 * 1024 * 1024),
                MaximumDocumentBytes = BoundedLong(section, "MaximumDocumentBytes", 32 * 1024 * 1024, 1024, 256 * 1024 * 1024),
            },
            MetadataRequestsPerMinute = BoundedInt(section, "MetadataRequestsPerMinute", 120, 1, 100_000),
            PackageConcurrentStreams = BoundedInt(section, "PackageConcurrentStreams", 16, 1, 256),
            PackageQueueLimit = BoundedInt(section, "PackageQueueLimit", 32, 0, 4096),
            StreamBufferBytes = BoundedInt(section, "StreamBufferBytes", 128 * 1024, 16 * 1024, 1024 * 1024),
        };
    }

    private static int BoundedInt(IConfigurationSection section, string name, int fallback, int minimum, int maximum)
    {
        string? value = section[name];
        if (value is null)
        {
            return fallback;
        }

        return int.TryParse(value, System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture,
            out int parsed) && parsed >= minimum && parsed <= maximum
            ? parsed
            : throw new InvalidOperationException($"Distribution:{name} must be between {minimum} and {maximum}.");
    }

    private static long BoundedLong(IConfigurationSection section, string name, long fallback, long minimum, long maximum)
    {
        string? value = section[name];
        if (value is null)
        {
            return fallback;
        }

        return long.TryParse(value, System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture,
            out long parsed) && parsed >= minimum && parsed <= maximum
            ? parsed
            : throw new InvalidOperationException($"Distribution:{name} must be between {minimum} and {maximum}.");
    }
}
