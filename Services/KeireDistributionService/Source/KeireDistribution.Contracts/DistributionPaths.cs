using System.Text.RegularExpressions;

namespace Keire.Distribution;

public static partial class DistributionPaths
{
    public static bool IsSnapshotId(string? value)
    {
        return !string.IsNullOrEmpty(value) && SnapshotIdRegex().IsMatch(value);
    }

    public static bool IsRouteToken(string? value)
    {
        return !string.IsNullOrEmpty(value) && RouteTokenRegex().IsMatch(value);
    }

    public static bool IsLocale(string? value)
    {
        return !string.IsNullOrEmpty(value) && LocaleRegex().IsMatch(value);
    }

    public static bool IsSha256(string? value)
    {
        return !string.IsNullOrEmpty(value) && Sha256Regex().IsMatch(value);
    }

    public static bool IsKeyId(string? value)
    {
        return !string.IsNullOrEmpty(value) && KeyIdRegex().IsMatch(value);
    }

    public static string NormalizeRelativePath(string value)
    {
        if (string.IsNullOrWhiteSpace(value) || value.IndexOf('\0') >= 0 || value.Contains('\\'))
        {
            throw new InvalidDataException($"Unsafe distribution path '{value}'.");
        }

        string normalized = value.Replace('\\', '/');
        if (normalized.StartsWith('/') || normalized.EndsWith('/') || Path.IsPathRooted(normalized))
        {
            throw new InvalidDataException($"Distribution paths must be confined relative paths: '{value}'.");
        }

        string[] segments = normalized.Split('/');
        if (segments.Any(segment => segment.Length == 0 || segment is "." or ".."))
        {
            throw new InvalidDataException($"Distribution path contains an unsafe segment: '{value}'.");
        }

        return string.Join('/', segments);
    }

    public static string CatalogPath(string channel, string platform, string architecture)
    {
        return CatalogPath("catalogs", channel, platform, architecture);
    }

    public static string CompactCatalogPath(string channel, string platform, string architecture)
    {
        return CatalogPath("catalogs-v2", channel, platform, architecture);
    }

    private static string CatalogPath(string root, string channel, string platform, string architecture)
    {
        if (!IsRouteToken(channel) || !IsRouteToken(platform) || !IsRouteToken(architecture))
        {
            throw new InvalidDataException("Catalog route tokens are invalid.");
        }

        return $"{root}/{channel}/{platform}/{architecture}.json";
    }

    public static string ContentPath(string locale)
    {
        if (!IsLocale(locale))
        {
            throw new InvalidDataException("Content locale is invalid.");
        }

        return $"content/{locale}.json";
    }

    public static string PackagePath(string sha256)
    {
        if (!IsSha256(sha256))
        {
            throw new InvalidDataException("Package digest is invalid.");
        }

        return $"packages/{sha256}";
    }

    public static string ManifestPath(string sha256)
    {
        if (!IsSha256(sha256))
        {
            throw new InvalidDataException("Package manifest digest is invalid.");
        }

        return $"manifests/{sha256}.json";
    }

    public static string ClassifyPath(string relativePath)
    {
        string path = NormalizeRelativePath(relativePath);
        string[] segments = path.Split('/');

        if (segments.Length == 4 && segments[0] is "catalogs" or "catalogs-v2" && IsRouteToken(segments[1]) &&
            IsRouteToken(segments[2]) && segments[3].EndsWith(".json", StringComparison.Ordinal))
        {
            string architecture = segments[3][..^5];
            if (IsRouteToken(architecture))
            {
                return DistributionFileKinds.Catalog;
            }
        }

        if (segments.Length == 2 && segments[0] == "content" &&
            segments[1].EndsWith(".json", StringComparison.Ordinal))
        {
            string locale = segments[1][..^5];
            if (IsLocale(locale))
            {
                return DistributionFileKinds.Content;
            }
        }

        if (segments.Length == 2 && segments[0] == "packages" && IsSha256(segments[1]))
        {
            return DistributionFileKinds.Package;
        }

        if (segments.Length == 2 && segments[0] == "manifests" &&
            segments[1].EndsWith(".json", StringComparison.Ordinal) &&
            IsSha256(segments[1][..^5]))
        {
            return DistributionFileKinds.Manifest;
        }

        throw new InvalidDataException($"Unsupported distribution file path '{relativePath}'.");
    }

    public static string ResolveConfined(string root, string relativePath)
    {
        string fullRoot = Path.GetFullPath(root);
        string fullPath = Path.GetFullPath(Path.Combine(fullRoot, NormalizeRelativePath(relativePath).Replace('/', Path.DirectorySeparatorChar)));
        string rootPrefix = fullRoot.EndsWith(Path.DirectorySeparatorChar)
            ? fullRoot
            : fullRoot + Path.DirectorySeparatorChar;
        StringComparison comparison = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        if (!fullPath.StartsWith(rootPrefix, comparison))
        {
            throw new InvalidDataException($"Distribution path escapes its root: '{relativePath}'.");
        }

        return fullPath;
    }

    [GeneratedRegex("^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$", RegexOptions.CultureInvariant)]
    private static partial Regex SnapshotIdRegex();

    [GeneratedRegex("^[a-z0-9][a-z0-9._-]{0,63}$", RegexOptions.CultureInvariant)]
    private static partial Regex RouteTokenRegex();

    [GeneratedRegex("^[A-Za-z0-9][A-Za-z0-9-]{0,31}$", RegexOptions.CultureInvariant)]
    private static partial Regex LocaleRegex();

    [GeneratedRegex("^[0-9a-f]{64}$", RegexOptions.CultureInvariant)]
    private static partial Regex Sha256Regex();

    [GeneratedRegex("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$", RegexOptions.CultureInvariant)]
    private static partial Regex KeyIdRegex();
}
