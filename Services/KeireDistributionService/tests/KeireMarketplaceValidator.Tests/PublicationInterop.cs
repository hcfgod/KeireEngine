using System.Diagnostics;
using System.Globalization;
using Keire.Distribution;
using Keire.Marketplace.PublicationSigner;
using Keire.Marketplace.Security;

namespace Keire.Marketplace.Validator.Tests;

internal static partial class Program
{
    private static async Task VerifyNodePublicationAsync(PublicationLease lease, MarketplaceSigningKey key)
    {
        ProcessStartInfo start = new("node")
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
        };
        start.ArgumentList.Add(Path.Combine(FindRepositoryRoot(), "Services", "KeireDistributionService",
            "DocumentationSite", "scripts", "verify-publication-interop.mjs"));
        using Process process = Process.Start(start)
            ?? throw new InvalidOperationException("Could not start publication interoperability acceptance.");
        Task<string> output = process.StandardOutput.ReadToEndAsync();
        Task<string> errors = process.StandardError.ReadToEndAsync();
        byte[] fixture = DistributionJson.Serialize(new
        {
            signedManifest = PublicationSignerProgram.SignPublication(lease, key),
            key = key.PublicDocument,
            release = new
            {
                signing_key_id = key.PublicDocument.KeyId,
                publication_sequence = lease.PublicationSequence.ToString(CultureInfo.InvariantCulture),
                publication_expires_at = lease.PublicationExpiresAt.UtcDateTime.ToString(
                    "yyyy-MM-dd'T'HH:mm:ss.fffffff'Z'", CultureInfo.InvariantCulture),
                product_id = lease.ProductId.ToString("D"),
                version_id = lease.VersionId.ToString("D"),
                artifact_sha256 = lease.ArtifactSha256,
                artifact_size_bytes = lease.ArtifactSizeBytes.ToString(CultureInfo.InvariantCulture),
                manifest_sha256 = lease.ManifestSha256,
                storage_path = lease.StoragePath,
            },
        });
        await process.StandardInput.BaseStream.WriteAsync(fixture);
        process.StandardInput.Close();
        using CancellationTokenSource deadline = new(TimeSpan.FromSeconds(30));
        try
        {
            await process.WaitForExitAsync(deadline.Token);
        }
        catch (OperationCanceledException)
        {
            process.Kill(entireProcessTree: true);
            throw new InvalidOperationException("Publication interoperability acceptance timed out.");
        }

        Assert.Equal(0, process.ExitCode, $"Node publication verification failed: {await errors}");
        Console.Write(await output);
    }
}
