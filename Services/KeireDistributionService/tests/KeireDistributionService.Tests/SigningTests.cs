using System.Runtime.Versioning;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using Keire.Distribution.Publisher;

namespace Keire.Distribution.Tests;

internal static class SigningTests
{
    private static readonly DateTimeOffset s_Now = new(2026, 8, 6, 12, 0, 0, TimeSpan.Zero);

    public static Task ExactByteSigningAsync()
    {
        string root = TestDistribution.CreateTemporaryRoot();
        try
        {
            KeyFixture key = CreateKey(root, "release");
            SigningSource source = CreateSource(
                root,
                "exact-source",
                key.PublicKey.KeyId,
                sequence: 42,
                s_Now.AddDays(30));
            string signatures = Path.Combine(source.Root, DistributionContract.SignaturesFileName);
            SigningPolicy policy = Policy(minimumSequence: 40);

            SigningResult signed = DistributionSigning.SignDirectory(
                source.Root,
                signatures,
                SigningKeySource.FromFile(key.PrivateKeyPath),
                policy);
            TestAssert.Equal(2, signed.DocumentCount, "The signer did not cover every catalog/content document.");
            TestAssert.Equal(key.PublicKey.KeyId, signed.KeyId, "The signer reported the wrong key ID.");

            SigningResult verified = DistributionSigning.VerifyDirectory(
                source.Root,
                signatures,
                key.PublicKeyPath,
                policy);
            TestAssert.Equal(2, verified.DocumentCount, "The verifier did not validate every signed document.");

            string storage = Path.Combine(root, "published");
            SnapshotIndex index = DistributionSigning.PublishVerified(
                source.Root,
                storage,
                "signed-snapshot",
                activate: false,
                key.PublicKeyPath,
                policy);
            TestAssert.Equal("signed-snapshot", index.SnapshotId, "Verified publishing returned the wrong snapshot.");
            byte[] publishedCatalog = File.ReadAllBytes(
                Path.Combine(storage, "snapshots", "signed-snapshot", "catalogs", "stable", "windows", "x86_64.json"));
            TestAssert.BytesEqual(source.CatalogBytes, publishedCatalog, "Verified publishing rewrote signed bytes.");

            File.WriteAllBytes(source.CatalogPath, [.. source.CatalogBytes, (byte)' ']);
            TestAssert.Throws<InvalidDataException>(
                () => DistributionSigning.VerifyDirectory(source.Root, signatures, key.PublicKeyPath, policy),
                "A one-byte document modification passed Ed25519 verification.");
            File.WriteAllBytes(source.CatalogPath, source.CatalogBytes);

            KeyFixture wrongKey = CreateKey(root, "wrong");
            TestAssert.Throws<InvalidDataException>(
                () => DistributionSigning.VerifyDirectory(source.Root, signatures, wrongKey.PublicKeyPath, policy),
                "A signature was accepted with the wrong Ed25519 public key.");
            return Task.CompletedTask;
        }
        finally
        {
            TestDistribution.DeleteTemporaryRoot(root);
        }
    }

    public static Task IdentityAndPolicyAsync()
    {
        string root = TestDistribution.CreateTemporaryRoot();
        try
        {
            KeyFixture key = CreateKey(root, "policy");

            SigningSource expired = CreateSource(
                root,
                "expired-source",
                key.PublicKey.KeyId,
                sequence: 10,
                s_Now.AddMinutes(-1));
            string expiredSignatures = Path.Combine(expired.Root, DistributionContract.SignaturesFileName);
            DistributionSigning.SignDirectory(
                expired.Root,
                expiredSignatures,
                SigningKeySource.FromFile(key.PrivateKeyPath),
                new SigningPolicy
                {
                    MinimumSequence = 1,
                    MinimumRemainingValidity = TimeSpan.Zero,
                    Now = s_Now.AddDays(-1),
                });
            TestAssert.Throws<InvalidDataException>(
                () => DistributionSigning.VerifyDirectory(
                    expired.Root,
                    expiredSignatures,
                    key.PublicKeyPath,
                    Policy(minimumSequence: 1, minimumValidity: TimeSpan.Zero)),
                "An expired distribution document signature was accepted.");

            SigningSource replay = CreateSource(
                root,
                "replay-source",
                key.PublicKey.KeyId,
                sequence: 4,
                s_Now.AddDays(2));
            string replaySignatures = Path.Combine(replay.Root, DistributionContract.SignaturesFileName);
            DistributionSigning.SignDirectory(
                replay.Root,
                replaySignatures,
                SigningKeySource.FromFile(key.PrivateKeyPath),
                Policy(minimumSequence: 1));
            TestAssert.Throws<InvalidDataException>(
                () => DistributionSigning.VerifyDirectory(
                    replay.Root,
                    replaySignatures,
                    key.PublicKeyPath,
                    Policy(minimumSequence: 5)),
                "A distribution document below the sequence floor was accepted.");

            SigningSource wrongIdentity = CreateSource(
                root,
                "identity-source",
                "ed25519-00000000000000000000000000000000",
                sequence: 10,
                s_Now.AddDays(2));
            TestAssert.Throws<InvalidDataException>(
                () => DistributionSigning.SignDirectory(
                    wrongIdentity.Root,
                    Path.Combine(wrongIdentity.Root, DistributionContract.SignaturesFileName),
                    SigningKeySource.FromFile(key.PrivateKeyPath),
                    Policy()),
                "A document identity naming a different key was signed.");

            SigningSource valid = CreateSource(
                root,
                "metadata-source",
                key.PublicKey.KeyId,
                sequence: 10,
                s_Now.AddDays(2));
            string signatures = Path.Combine(valid.Root, DistributionContract.SignaturesFileName);
            DistributionSigning.SignDirectory(
                valid.Root,
                signatures,
                SigningKeySource.FromFile(key.PrivateKeyPath),
                Policy());
            DistributionSignaturesManifest manifest = DistributionJson.DeserializeStrict<DistributionSignaturesManifest>(
                File.ReadAllBytes(signatures));
            SignedDocumentEntry first = manifest.Documents[0];
            manifest.Documents[0] = new SignedDocumentEntry
            {
                Path = first.Path,
                Algorithm = first.Algorithm,
                KeyId = first.KeyId,
                Signature = first.Signature,
                Sequence = first.Sequence + 1,
                ExpiresAt = first.ExpiresAt,
            };
            File.WriteAllBytes(signatures, DistributionJson.Serialize(manifest));
            TestAssert.Throws<InvalidDataException>(
                () => DistributionSigning.VerifyDirectory(valid.Root, signatures, key.PublicKeyPath, Policy()),
                "Detached metadata that disagreed with the signed identity was accepted.");
            return Task.CompletedTask;
        }
        finally
        {
            TestDistribution.DeleteTemporaryRoot(root);
        }
    }

    public static Task PrivateKeySourcesAndPermissionsAsync()
    {
        string root = TestDistribution.CreateTemporaryRoot();
        try
        {
            KeyFixture key = CreateKey(root, "source");
            AssertPrivateKeyIsRestricted(key.PrivateKeyPath);
            string derivedPath = Path.Combine(root, "derived-public.json");
            DistributionPublicKeyDocument derived = DistributionSigning.DerivePublicKey(
                SigningKeySource.FromFile(key.PrivateKeyPath),
                derivedPath);
            AssertSamePublicKey(key.PublicKey, derived, "File-based public-key derivation was not deterministic.");

            string environmentName = $"KEIRE_SIGNING_TEST_{Guid.NewGuid():N}";
            string pem = File.ReadAllText(key.PrivateKeyPath, Encoding.ASCII);
            string base64Der = string.Concat(
                pem.Split('\n', StringSplitOptions.RemoveEmptyEntries)
                    .Where(line => !line.StartsWith("-----", StringComparison.Ordinal))
                    .Select(line => line.TrimEnd('\r')));
            byte[] der = Convert.FromBase64String(base64Der);
            try
            {
                Environment.SetEnvironmentVariable(
                    environmentName,
                    Convert.ToBase64String(der),
                    EnvironmentVariableTarget.Process);
                string environmentPublicPath = Path.Combine(root, "environment-public.json");
                DistributionPublicKeyDocument environmentDerived = DistributionSigning.DerivePublicKey(
                    SigningKeySource.FromEnvironment(environmentName),
                    environmentPublicPath);
                AssertSamePublicKey(
                    key.PublicKey,
                    environmentDerived,
                    "Environment-based public-key derivation was not deterministic.");
                TestAssert.True(
                    Environment.GetEnvironmentVariable(environmentName, EnvironmentVariableTarget.Process) is null,
                    "The private-key process environment variable was not cleared after use.");
            }
            finally
            {
                System.Security.Cryptography.CryptographicOperations.ZeroMemory(der);
                Environment.SetEnvironmentVariable(environmentName, null, EnvironmentVariableTarget.Process);
            }

            MakePrivateKeyInsecure(key.PrivateKeyPath);
            TestAssert.Throws<InvalidDataException>(
                () => PrivateKeyPermissions.Validate(key.PrivateKeyPath),
                "The private-key permission validator accepted an untrusted identity.");
            TestAssert.Throws<InvalidDataException>(
                () => DistributionSigning.DerivePublicKey(
                    SigningKeySource.FromFile(key.PrivateKeyPath),
                    Path.Combine(root, "unsafe-public.json")),
                "An insecure private-key file was accepted.");
            return Task.CompletedTask;
        }
        finally
        {
            TestDistribution.DeleteTemporaryRoot(root);
        }
    }

    private static void AssertPrivateKeyIsRestricted(string path)
    {
        PrivateKeyPermissions.Validate(path);
        if (!OperatingSystem.IsWindows())
        {
            UnixFileMode mode = File.GetUnixFileMode(path);
            TestAssert.True(
                mode is UnixFileMode.UserRead or (UnixFileMode.UserRead | UnixFileMode.UserWrite),
                "The generated private key did not use owner-only Unix permissions.");
            return;
        }

        AssertPrivateKeyIsRestrictedWindows(path);
    }

    [SupportedOSPlatform("windows")]
    private static void AssertPrivateKeyIsRestrictedWindows(string path)
    {
        using WindowsIdentity identity = WindowsIdentity.GetCurrent(TokenAccessLevels.Query);
        SecurityIdentifier currentUser = identity.User ??
            throw new InvalidDataException("The current Windows user has no security identifier.");
        HashSet<SecurityIdentifier> allowed =
        [
            currentUser,
            new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null),
            new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null),
        ];

        FileSecurity security = new FileInfo(path).GetAccessControl(
            AccessControlSections.Owner | AccessControlSections.Access);
        TestAssert.True(security.AreAccessRulesProtected, "The generated private key retained inherited access rules.");
        TestAssert.True(
            security.GetOwner(typeof(SecurityIdentifier)) is SecurityIdentifier owner && allowed.Contains(owner),
            "The generated private key has an untrusted owner.");

        AuthorizationRuleCollection rules = security.GetAccessRules(
            includeExplicit: true,
            includeInherited: true,
            targetType: typeof(SecurityIdentifier));
        TestAssert.True(
            rules.Cast<FileSystemAccessRule>().All(rule =>
                !rule.IsInherited &&
                rule.AccessControlType == AccessControlType.Allow &&
                rule.IdentityReference is SecurityIdentifier sid &&
                allowed.Contains(sid)),
            "The generated private key retained an inherited, denied, or untrusted ACL entry.");
    }

    private static KeyFixture CreateKey(string root, string name)
    {
        string privatePath = Path.Combine(root, $"{name}-private.pem");
        string publicPath = Path.Combine(root, $"{name}-public.json");
        DistributionPublicKeyDocument publicKey = DistributionSigning.GenerateKey(privatePath, publicPath);
        TestAssert.True(
            publicKey.KeyId.StartsWith("ed25519-", StringComparison.Ordinal) && publicKey.KeyId.Length == 40,
            "The derived signing key ID was not a deterministic SHA-256 prefix.");
        return new KeyFixture(privatePath, publicPath, publicKey);
    }

    private static SigningSource CreateSource(
        string root,
        string name,
        string keyId,
        long sequence,
        DateTimeOffset expiresAt)
    {
        string source = Path.Combine(root, name);
        string catalogPath = Path.Combine(source, "catalogs", "stable", "windows", "x86_64.json");
        string contentPath = Path.Combine(source, "content", "en-US.json");
        Directory.CreateDirectory(Path.GetDirectoryName(catalogPath)!);
        Directory.CreateDirectory(Path.GetDirectoryName(contentPath)!);
        string expiry = expiresAt.ToString("O", System.Globalization.CultureInfo.InvariantCulture);
        byte[] catalog = Encoding.UTF8.GetBytes(
            $"{{  \"schemaVersion\" : 1, \"keyId\" : \"{keyId}\", \"sequence\" : {sequence}, " +
            $"\"expiresAt\" : \"{expiry}\", \"marker\" : \"exact bytes\" }}\n");
        byte[] content = Encoding.UTF8.GetBytes(
            $"{{\n  \"schemaVersion\": 1,\n  \"keyId\": \"{keyId}\",\n  \"sequence\": {sequence + 1},\n" +
            $"  \"expiresAt\": \"{expiry}\",\n  \"items\": []\n}}\n");
        File.WriteAllBytes(catalogPath, catalog);
        File.WriteAllBytes(contentPath, content);
        return new SigningSource(source, catalogPath, catalog);
    }

    private static SigningPolicy Policy(long minimumSequence = 1, TimeSpan? minimumValidity = null)
    {
        return new SigningPolicy
        {
            MinimumSequence = minimumSequence,
            MinimumRemainingValidity = minimumValidity ?? TimeSpan.FromHours(1),
            Now = s_Now,
        };
    }

    private static void AssertSamePublicKey(
        DistributionPublicKeyDocument expected,
        DistributionPublicKeyDocument actual,
        string message)
    {
        TestAssert.True(
            expected.SchemaVersion == actual.SchemaVersion &&
            string.Equals(expected.Algorithm, actual.Algorithm, StringComparison.Ordinal) &&
            string.Equals(expected.KeyId, actual.KeyId, StringComparison.Ordinal) &&
            string.Equals(expected.PublicKey, actual.PublicKey, StringComparison.Ordinal) &&
            string.Equals(expected.Fingerprint, actual.Fingerprint, StringComparison.Ordinal),
            message);
    }

    private static void MakePrivateKeyInsecure(string path)
    {
        if (OperatingSystem.IsWindows())
        {
            MakePrivateKeyInsecureWindows(path);
            return;
        }

        File.SetUnixFileMode(
            path,
            UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.GroupRead | UnixFileMode.OtherRead);
    }

    [SupportedOSPlatform("windows")]
    private static void MakePrivateKeyInsecureWindows(string path)
    {
        FileInfo file = new(path);
        FileSecurity security = file.GetAccessControl();
        security.AddAccessRule(new FileSystemAccessRule(
            new SecurityIdentifier(WellKnownSidType.WorldSid, null),
            FileSystemRights.ReadData | FileSystemRights.ReadAttributes | FileSystemRights.ReadPermissions,
            AccessControlType.Allow));
        file.SetAccessControl(security);
    }

    private sealed record KeyFixture(
        string PrivateKeyPath,
        string PublicKeyPath,
        DistributionPublicKeyDocument PublicKey);

    private sealed record SigningSource(string Root, string CatalogPath, byte[] CatalogBytes);
}
