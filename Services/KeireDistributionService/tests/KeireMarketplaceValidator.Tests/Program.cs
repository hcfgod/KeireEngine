using System.Security.Cryptography;
using System.Text;
using System.Net;
using System.Text.Json;
using Keire.Marketplace.Validation;
using Keire.Marketplace.Validator.Broker;

namespace Keire.Marketplace.Validator.Tests;

internal static class Program
{
    public static async Task<int> Main()
    {
        (string Name, Func<Task> Test)[] tests =
        [
            ("secret findings redact values", SecretFindingsRedactValuesAsync),
            ("native payload policy fails closed", NativePayloadPolicyFailsClosedAsync),
            ("managed NuGet references are rejected offline", ManagedPackagesAreRejectedAsync),
            ("managed sources compile from generated offline projects", ManagedSourcesCompileOfflineAsync),
            ("malware rejection stops before extraction", MalwareRejectionStopsExtractionAsync),
            ("validation failures remove private staging", ValidationFailureRemovesStagingAsync),
            ("broker keeps its scoped secret out of Supabase API auth", BrokerUsesScopedSecretHeaderAsync),
            ("broker verifies quarantine bytes and commits reports", BrokerVerifiesAndCommitsAsync),
        ];
        int failures = 0;
        foreach ((string name, Func<Task> test) in tests)
        {
            try
            {
                await test();
                Console.WriteLine($"PASS: {name}");
            }
            catch (Exception exception)
            {
                ++failures;
                Console.Error.WriteLine($"FAIL: {name}: {exception}");
            }
        }

        Console.WriteLine($"{tests.Length - failures}/{tests.Length} marketplace validator tests passed.");
        return failures == 0 ? 0 : 1;
    }

    private static async Task SecretFindingsRedactValuesAsync()
    {
        using TemporaryDirectory fixture = new();
        string secret = "ghp_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN";
        await File.WriteAllTextAsync(Path.Combine(fixture.Path, "settings.txt"), $"access_token={secret}\n");
        IReadOnlyList<ValidationDiagnostic> diagnostics = await SecretScanner.ScanAsync(fixture.Path, default);
        Assert.True(diagnostics.Count != 0, "The test credential was not detected.");
        Assert.True(
            diagnostics.All(diagnostic => !diagnostic.Message.Contains(secret, StringComparison.Ordinal)),
            "A secret value leaked into validation diagnostics.");
        Assert.Equal("settings.txt", diagnostics[0].Path, "The relative finding path was not retained.");
    }

    private static async Task NativePayloadPolicyFailsClosedAsync()
    {
        using TemporaryDirectory fixture = new();
        string relativePath = "Assets/texture.bin";
        string fullPath = Path.Combine(fixture.Path, "Assets", "texture.bin");
        Directory.CreateDirectory(Path.GetDirectoryName(fullPath)!);
        await File.WriteAllBytesAsync(fullPath, [(byte)'M', (byte)'Z', 0, 0]);
        ExtractedPackageDocument manifest = CreateManifest(
            [new PackageFile { Path = relativePath, SizeBytes = 4, Sha256 = Sha256([77, 90, 0, 0]), Mode = 0 }]);
        IReadOnlyList<ValidationDiagnostic> diagnostics = PayloadPolicy.Validate(fixture.Path, manifest, default);
        Assert.Contains("EXECUTABLE_SIGNATURE", diagnostics, "A disguised PE payload was accepted.");
    }

    private static async Task ManagedPackagesAreRejectedAsync()
    {
        using TemporaryDirectory fixture = new();
        string definitionPath = "Assets/Scripts/Game.keireasm";
        string sourcePath = "Assets/Scripts/Runtime/Game.cs";
        Directory.CreateDirectory(Path.Combine(fixture.Path, "Assets", "Scripts", "Runtime"));
        await File.WriteAllTextAsync(Path.Combine(fixture.Path, sourcePath.Replace('/', Path.DirectorySeparatorChar)), "public class Game {}\n");
        await File.WriteAllTextAsync(
            Path.Combine(fixture.Path, definitionPath.Replace('/', Path.DirectorySeparatorChar)),
            """
            {
              "schemaVersion": 2,
              "name": "Gameplay",
              "rootNamespace": "Game",
              "classification": "runtime",
              "sourceRoots": ["Assets/Scripts/Runtime"],
              "references": [],
              "packages": [{"name":"Unsafe.Dependency","version":"1.0.0"}],
              "defineSymbols": [],
              "allowUnsafe": false
            }
            """);
        string managedApi = Path.Combine(fixture.Path, "Keire.Managed.dll");
        await File.WriteAllTextAsync(managedApi, "placeholder");
        ExtractedPackageDocument manifest = CreateManifest([]);
        manifest.Assets.Add(new PackageAsset
        {
            Id = "11111111-1111-4111-8111-111111111111",
            Type = "22222222-2222-4222-8222-222222222222",
            Source = definitionPath,
            Metadata = definitionPath + ".keiremeta",
            Dependencies = [],
        });
        manifest.ManagedAssemblies.Add(new PackageManagedAssembly
        {
            Name = "Gameplay",
            Definition = definitionPath,
            Scope = "runtime",
        });
        ManagedValidationResult result = await ManagedAssemblyValidator.ValidateAsync(
            fixture.Path,
            Path.Combine(fixture.Path, "build"),
            manifest,
            Path.Combine(fixture.Path, "unused-dotnet"),
            managedApi,
            default);
        Assert.Equal(ValidationStatuses.Failed, result.Status, "A publisher-selected NuGet package was accepted.");
        Assert.Contains("MANAGED_DEFINITION_INVALID", result.Diagnostics, "The NuGet policy failure was not diagnosed.");
    }

    private static async Task ManagedSourcesCompileOfflineAsync()
    {
        using TemporaryDirectory fixture = new();
        string definitionPath = "Assets/Scripts/Game.keireasm";
        string sourcePath = "Assets/Scripts/Runtime/Game.cs";
        Directory.CreateDirectory(Path.Combine(fixture.Path, "Assets", "Scripts", "Runtime"));
        await File.WriteAllTextAsync(
            Path.Combine(fixture.Path, sourcePath.Replace('/', Path.DirectorySeparatorChar)),
            "namespace Game; public sealed class GameRoot {}\n");
        await File.WriteAllTextAsync(
            Path.Combine(fixture.Path, definitionPath.Replace('/', Path.DirectorySeparatorChar)),
            """
            {
              "schemaVersion": 2,
              "name": "Gameplay",
              "rootNamespace": "Game",
              "classification": "runtime",
              "sourceRoots": ["Assets/Scripts/Runtime"],
              "references": [],
              "packages": [],
              "defineSymbols": ["KEIRE_MARKETPLACE_VALIDATION"],
              "allowUnsafe": false
            }
            """);
        ExtractedPackageDocument manifest = CreateManifest([]);
        manifest.Assets.Add(new PackageAsset
        {
            Id = "11111111-1111-4111-8111-111111111111",
            Type = "22222222-2222-4222-8222-222222222222",
            Source = definitionPath,
            Metadata = definitionPath + ".keiremeta",
            Dependencies = [],
        });
        manifest.ManagedAssemblies.Add(new PackageManagedAssembly
        {
            Name = "Gameplay",
            Definition = definitionPath,
            Scope = "runtime",
        });
        string repositoryRoot = FindRepositoryRoot();
        string dotnet = Path.Combine(
            repositoryRoot,
            "Build",
            "Dependencies",
            "dotnet-sdk",
            OperatingSystem.IsWindows() ? "dotnet.exe" : "dotnet");
        string managedApi = Path.Combine(repositoryRoot, "Build", "Managed", "Keire.Managed.dll");
        ManagedValidationResult result = await ManagedAssemblyValidator.ValidateAsync(
            fixture.Path,
            Path.Combine(fixture.Path, "build"),
            manifest,
            dotnet,
            managedApi,
            default);
        Assert.Equal(ValidationStatuses.Passed, result.Status, "Valid managed source did not compile offline.");
        Assert.True(result.CodeFingerprintSha256 is { Length: 64 }, "Managed validation did not produce a code fingerprint.");
        string generatedProject = await File.ReadAllTextAsync(Path.Combine(fixture.Path, "build", "Gameplay.csproj"));
        foreach (string contract in new[]
                 {
                     "<ImportDirectoryBuildProps>false</ImportDirectoryBuildProps>",
                     "<ImportDirectoryBuildTargets>false</ImportDirectoryBuildTargets>",
                     "<RunAnalyzers>false</RunAnalyzers>",
                     "<RestoreSources>",
                 })
        {
            Assert.True(generatedProject.Contains(contract, StringComparison.Ordinal),
                $"The generated managed project is missing policy contract '{contract}'.");
        }
    }

    private static async Task MalwareRejectionStopsExtractionAsync()
    {
        using TemporaryDirectory fixture = new();
        string package = await CreatePackagePlaceholderAsync(fixture.Path);
        PackageValidationRequest request = CreateRequest(fixture.Path, package);
        MarketplacePackageValidator validator = new(new FakeMalwareScanner(ValidationStatuses.Infected));
        MarketplaceValidationReport report = await validator.ValidateAsync(request);
        Assert.True(!report.Passed, "An infected archive passed validation.");
        Assert.Equal(ValidationStatuses.Infected, report.MalwareScanResult, "The infected result was not retained.");
        Assert.Equal(1, Directory.GetDirectories(fixture.Path).Count(path => Path.GetFileName(path) == "work"), "Unexpected work directories remain.");
        Assert.Equal(0, Directory.GetDirectories(Path.Combine(fixture.Path, "work")).Length, "Private staging was not removed.");
    }

    private static async Task ValidationFailureRemovesStagingAsync()
    {
        using TemporaryDirectory fixture = new();
        string package = await CreatePackagePlaceholderAsync(fixture.Path);
        PackageValidationRequest request = CreateRequest(fixture.Path, package);
        MarketplacePackageValidator validator = new(new FakeMalwareScanner(ValidationStatuses.Clean));
        MarketplaceValidationReport report = await validator.ValidateAsync(request);
        Assert.True(!report.Passed, "An invalid archive passed validation.");
        Assert.Contains("PACKAGE_VALIDATION_FAILED", report.Diagnostics, "The parser failure was not diagnosed.");
        Assert.Equal(0, Directory.GetDirectories(Path.Combine(fixture.Path, "work")).Length, "Failed validation left private staging behind.");
    }

    private static async Task BrokerUsesScopedSecretHeaderAsync()
    {
        using TemporaryDirectory fixture = new();
        RecordingHandler handler = new(_ => JsonResponse("{\"data\":{\"lease\":null}}"));
        using SupabaseValidatorApi api = new(CreateBrokerOptions(fixture.Path), handler);
        ValidationLease? lease = await api.LeaseAsync(default);
        Assert.True(lease is null, "An empty validator queue returned a lease.");
        HttpRequestMessage request = handler.Requests.Single();
        Assert.Equal("validator_fixture_secret_12345678901234567890",
            request.Headers.GetValues("x-keire-validator-secret").Single(),
            "The broker did not send its scoped queue credential.");
        Assert.True(!request.Headers.Contains("apikey"), "The broker sent a full Supabase API credential.");
        Assert.True(request.Headers.Authorization is null, "The broker sent a bearer credential.");
    }

    private static async Task BrokerVerifiesAndCommitsAsync()
    {
        using TemporaryDirectory fixture = new();
        byte[] packageBytes = Encoding.ASCII.GetBytes("immutable-package");
        Guid uploadId = Guid.Parse("11111111-1111-4111-8111-111111111111");
        string digest = Sha256(packageBytes);
        int queueCalls = 0;
        RecordingHandler handler = new(request =>
        {
            string path = request.RequestUri!.AbsolutePath;
            if (path.EndsWith("marketplace-validator-queue", StringComparison.Ordinal) && ++queueCalls == 1)
            {
                return JsonResponse(JsonSerializer.Serialize(new
                {
                    data = new
                    {
                        lease = new
                        {
                            uploadId,
                            versionId = Guid.Parse("22222222-2222-4222-8222-222222222222"),
                            storagePath = "publisher/upload.keireassetpackage",
                            expectedSizeBytes = packageBytes.Length,
                            expectedSha256 = digest,
                            leaseExpiresAt = DateTimeOffset.UtcNow.AddMinutes(15),
                            downloadUrl = "https://example.supabase.co/storage/v1/object/sign/marketplace-quarantine/file?token=fixture",
                        },
                    },
                }));
            }

            if (path.Contains("/storage/v1/object/sign/", StringComparison.Ordinal))
            {
                return new HttpResponseMessage(HttpStatusCode.OK) { Content = new ByteArrayContent(packageBytes) };
            }

            if (path.EndsWith("marketplace-validator-queue", StringComparison.Ordinal))
            {
                return JsonResponse($"{{\"data\":{{\"reportId\":\"{Guid.NewGuid():D}\"}}}}");
            }

            throw new InvalidOperationException("Unexpected broker request.");
        });
        using SupabaseValidatorApi api = new(CreateBrokerOptions(fixture.Path), handler);
        ValidationLease lease = await api.LeaseAsync(default)
            ?? throw new InvalidOperationException("The fixture lease was not returned.");
        string destination = Path.Combine(fixture.Path, "download.keireassetpackage");
        await api.DownloadAsync(lease, destination, default);
        Assert.True(File.ReadAllBytes(destination).SequenceEqual(packageBytes), "The verified quarantine bytes changed.");
        MarketplaceValidationReport report = new()
        {
            ValidatorFingerprintSha256 = new string('a', 64),
            PackageSha256 = digest,
            ManifestSha256 = new string('b', 64),
            Passed = true,
            MalwareScanResult = ValidationStatuses.Clean,
            SecretScanResult = ValidationStatuses.Clean,
            ManagedValidationResult = ValidationStatuses.NotApplicable,
            Diagnostics = [],
            CompletedAt = DateTimeOffset.UtcNow,
        };
        await api.CompleteAsync(lease, Keire.Distribution.DistributionJson.Serialize(report), default);
        Assert.True(
            handler.Requests.Any(request => request.RequestUri!.AbsolutePath.EndsWith(
                "marketplace-validator-queue", StringComparison.Ordinal)) && queueCalls == 2,
            "The verified report was not committed through the completion transition.");
    }

    private static async Task<string> CreatePackagePlaceholderAsync(string root)
    {
        string package = Path.Combine(root, "fixture.keireassetpackage");
        await File.WriteAllBytesAsync(package, Encoding.ASCII.GetBytes("not an archive"));
        Directory.CreateDirectory(Path.Combine(root, "work"));
        return package;
    }

    private static PackageValidationRequest CreateRequest(string root, string package)
    {
        FileInfo file = new(package);
        return new PackageValidationRequest(
            package,
            Path.Combine(root, "work"),
            Path.Combine(root, "missing-asset-tool"),
            Path.Combine(root, "missing-dotnet"),
            null,
            Path.Combine(root, "missing-malware-scanner"),
            file.Length,
            Sha256(File.ReadAllBytes(package)),
            new string('a', 64));
    }

    private static BrokerOptions CreateBrokerOptions(string exchangeRoot)
    {
        return new BrokerOptions
        {
            SupabaseUrl = new Uri("https://example.supabase.co/"),
            BrokerSecret = "validator_fixture_secret_12345678901234567890",
            ExpectedValidatorFingerprintSha256 = new string('a', 64),
            ExchangeRoot = exchangeRoot,
            WorkerId = "validator-fixture",
            LeaseSeconds = 900,
            IdlePollInterval = TimeSpan.FromMilliseconds(10),
            ReportPollInterval = TimeSpan.FromMilliseconds(10),
            ValidationTimeout = TimeSpan.FromSeconds(5),
        };
    }

    private static string FindRepositoryRoot()
    {
        DirectoryInfo? directory = new(Directory.GetCurrentDirectory());
        while (directory is not null)
        {
            if (File.Exists(Path.Combine(directory.FullName, "Config", "Dependencies.lock")))
            {
                return directory.FullName;
            }

            directory = directory.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate the Kéire repository root.");
    }

    private static HttpResponseMessage JsonResponse(string value)
    {
        return new HttpResponseMessage(HttpStatusCode.OK)
        {
            Content = new StringContent(value, Encoding.UTF8, "application/json"),
        };
    }

    private static ExtractedPackageDocument CreateManifest(List<PackageFile> files)
    {
        return new ExtractedPackageDocument
        {
            SchemaVersion = 1,
            PackageId = "com.keire.fixture",
            Version = "1.0.0",
            PublisherId = "fixture",
            DisplayName = "Fixture",
            Summary = "Fixture",
            Channel = "stable",
            InstallKind = "assets",
            Compatibility = new PackageCompatibility
            {
                MinimumEngineVersion = "0.3.1",
                Platforms = [],
                Architectures = [],
                RendererCapabilities = [],
                ManagedApiVersion = "0.3.1",
            },
            Dependencies = [],
            Conflicts = [],
            Files = files,
            Assets = [],
            Samples = [],
            ManagedAssemblies = [],
            Licenses = [],
            EntryPoints = [],
            InstalledSizeBytes = (ulong)files.Sum(file => (long)file.SizeBytes),
            SignatureKeyId = string.Empty,
            Archive = new PackageArchive
            {
                SizeBytes = 1,
                Sha256 = new string('a', 64),
                ManifestSha256 = new string('b', 64),
            },
            StagingRoot = string.Empty,
        };
    }

    private static string Sha256(ReadOnlySpan<byte> value)
    {
        return Convert.ToHexStringLower(SHA256.HashData(value));
    }

    private sealed class FakeMalwareScanner(string status) : IMalwareScanner
    {
        private readonly string m_Status = status;

        public Task<MalwareScanResult> ScanFileAsync(string path, CancellationToken cancellationToken)
        {
            return Task.FromResult(Result());
        }

        public Task<MalwareScanResult> ScanDirectoryAsync(string path, CancellationToken cancellationToken)
        {
            return Task.FromResult(Result());
        }

        private MalwareScanResult Result()
        {
            IReadOnlyList<ValidationDiagnostic> diagnostics = m_Status == ValidationStatuses.Clean
                ? []
                : [new ValidationDiagnostic
                {
                    Code = "MALWARE_DETECTED",
                    Severity = ValidationSeverities.Error,
                    Message = "The fixture scanner rejected the input.",
                }];
            return new MalwareScanResult(m_Status, "fixture", diagnostics);
        }
    }

    private sealed class TemporaryDirectory : IDisposable
    {
        public TemporaryDirectory()
        {
            Path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), $"keire-validator-tests-{Guid.NewGuid():N}");
            Directory.CreateDirectory(Path);
        }

        public string Path { get; }

        public void Dispose()
        {
            Directory.Delete(Path, recursive: true);
        }
    }

    private sealed class RecordingHandler(Func<HttpRequestMessage, HttpResponseMessage> response) : HttpMessageHandler
    {
        private readonly Func<HttpRequestMessage, HttpResponseMessage> m_Response = response;

        public List<HttpRequestMessage> Requests { get; } = [];

        protected override Task<HttpResponseMessage> SendAsync(
            HttpRequestMessage request,
            CancellationToken cancellationToken)
        {
            HttpRequestMessage recorded = new(request.Method, request.RequestUri);
            foreach ((string key, IEnumerable<string> values) in request.Headers)
            {
                recorded.Headers.TryAddWithoutValidation(key, values);
            }

            Requests.Add(recorded);
            return Task.FromResult(m_Response(request));
        }
    }

    private static class Assert
    {
        public static void True(bool value, string message)
        {
            if (!value)
            {
                throw new InvalidOperationException(message);
            }
        }

        public static void Equal<T>(T expected, T actual, string message)
        {
            if (!EqualityComparer<T>.Default.Equals(expected, actual))
            {
                throw new InvalidOperationException($"{message} Expected '{expected}', received '{actual}'.");
            }
        }

        public static void Contains(string code, IEnumerable<ValidationDiagnostic> diagnostics, string message)
        {
            if (!diagnostics.Any(diagnostic => diagnostic.Code == code))
            {
                throw new InvalidOperationException(message);
            }
        }
    }
}
