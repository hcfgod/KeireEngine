using System.Security.Cryptography;
using System.Text;
using Keire.Distribution.Publisher;

namespace Keire.Distribution.Tests;

internal sealed record PublishedFixture(
    string StorageRoot,
    string SourceRoot,
    byte[] CatalogBytes,
    byte[] ContentBytes,
    byte[] PackageBytes,
    string PackageSha256,
    string Signature,
    string PublicKeyPath);

internal static class TestDistribution
{
    public static PublishedFixture Create(string root, string snapshotId, string marker, bool activate = true)
    {
        string source = Path.Combine(root, $"source-{snapshotId}");
        string storage = Path.Combine(root, "distribution");
        Directory.CreateDirectory(Path.Combine(source, "catalogs", "stable", "windows"));
        Directory.CreateDirectory(Path.Combine(source, "content"));
        Directory.CreateDirectory(Path.Combine(source, "packages"));

        string privateKeyPath = Path.Combine(root, $"{snapshotId}-private.pem");
        string publicKeyPath = Path.Combine(root, $"{snapshotId}-public.json");
        DistributionPublicKeyDocument publicKey = DistributionSigning.GenerateKey(privateKeyPath, publicKeyPath);
        byte[] catalog = Encoding.UTF8.GetBytes(
            $"{{  \"schemaVersion\" : 1, \"keyId\" : \"{publicKey.KeyId}\", \"sequence\" : 7, " +
            $"\"expiresAt\" : \"2035-01-01T00:00:00Z\", \"marker\" : \"{marker}\" }}\n");
        byte[] content = Encoding.UTF8.GetBytes(
            $"{{\n  \"schemaVersion\": 1,\n  \"keyId\": \"{publicKey.KeyId}\",\n  \"sequence\": 8,\n" +
            $"  \"expiresAt\": \"2035-01-01T00:00:00Z\",\n  \"marker\": \"{marker}\"\n}}\n");
        byte[] package = Encoding.ASCII.GetBytes("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        string packageHash = Convert.ToHexStringLower(SHA256.HashData(package));
        File.WriteAllBytes(Path.Combine(source, "catalogs", "stable", "windows", "x86_64.json"), catalog);
        File.WriteAllBytes(Path.Combine(source, "content", "en-US.json"), content);
        File.WriteAllBytes(Path.Combine(source, "packages", packageHash), package);

        string signaturesPath = Path.Combine(source, DistributionContract.SignaturesFileName);
        SigningPolicy policy = new()
        {
            MinimumSequence = 1,
            MinimumRemainingValidity = TimeSpan.Zero,
            Now = DateTimeOffset.UtcNow,
        };
        DistributionSigning.SignDirectory(
            source,
            signaturesPath,
            SigningKeySource.FromFile(privateKeyPath),
            policy);
        DistributionSignaturesManifest signatures = DistributionJson.DeserializeStrict<DistributionSignaturesManifest>(
            File.ReadAllBytes(signaturesPath));

        DistributionSigning.PublishVerified(source, storage, snapshotId, activate, publicKeyPath, policy);
        return new PublishedFixture(
            storage,
            source,
            catalog,
            content,
            package,
            packageHash,
            signatures.Documents[0].Signature,
            publicKeyPath);
    }

    public static string CreateTemporaryRoot()
    {
        string path = Path.Combine(Path.GetTempPath(), $"keire-distribution-tests-{Guid.NewGuid():N}");
        Directory.CreateDirectory(path);
        return path;
    }

    public static void DeleteTemporaryRoot(string path)
    {
        if (Directory.Exists(path))
        {
            Directory.Delete(path, recursive: true);
        }
    }
}
