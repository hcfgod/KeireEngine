using System.Security.Cryptography;
using System.Text;

namespace Keire.Editor;

public sealed record ScriptedImportRequest(
    Guid ImporterId,
    uint ImporterVersion,
    string AssemblyFingerprint,
    string NormalizedSettings,
    ImportTargetPlatform TargetPlatform,
    string SourcePath,
    string SourceDigest,
    IReadOnlyList<string> Dependencies,
    int MaximumResponseBytes = 256 * 1024 * 1024)
{
    public string CacheKey()
    {
        if (ImporterId == Guid.Empty || ImporterVersion == 0 ||
            string.IsNullOrWhiteSpace(AssemblyFingerprint) || string.IsNullOrWhiteSpace(SourceDigest) ||
            MaximumResponseBytes is < 1 or > 1024 * 1024 * 1024)
        {
            throw new InvalidOperationException("Scripted import requests require complete bounded cache inputs.");
        }
        var canonical = new StringBuilder();
        canonical.Append(ImporterId.ToString("D")).Append('\n')
            .Append(ImporterVersion).Append('\n')
            .Append(AssemblyFingerprint.Trim()).Append('\n')
            .Append(NormalizedSettings).Append('\n')
            .Append((byte)TargetPlatform).Append('\n')
            .Append(SourcePath.Replace('\\', '/')).Append('\n')
            .Append(SourceDigest.Trim()).Append('\n');
        foreach (string dependency in Dependencies.Order(StringComparer.Ordinal))
            canonical.Append(dependency.Replace('\\', '/')).Append('\n');
        return Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(canonical.ToString()))).ToLowerInvariant();
    }
}

public sealed record ScriptedImportResponse(
    bool Succeeded,
    IReadOnlyDictionary<string, AssetImportArtifact> Outputs,
    string? MainOutput,
    IReadOnlyList<AssetImportDiagnostic> Diagnostics,
    IReadOnlyList<string> SourceDependencies,
    IReadOnlyList<AssetId> AssetDependencies);
