using System.Net;
using System.Text;
using Keire.Distribution;
using Keire.Distribution.Publisher;

namespace Keire.Distribution.Tests;

internal static class Program
{
    public static async Task<int> Main()
    {
        (string Name, Func<Task> Test)[] tests =
        [
            ("publisher and snapshot validation", PublisherAndSnapshotValidationAsync),
            ("offline Ed25519 exact-byte signing", SigningTests.ExactByteSigningAsync),
            ("signed identity and replay policy", SigningTests.IdentityAndPolicyAsync),
            ("private-key sources and permissions", SigningTests.PrivateKeySourcesAndPermissionsAsync),
            ("live and not-ready health", LiveAndNotReadyHealthAsync),
            ("exact signed documents and conditional requests", ExactDocumentsAndConditionalRequestsAsync),
            ("immutable package ranges and validators", PackageRangesAndValidatorsAsync),
            ("invalid snapshot retains last-known-good", InvalidSnapshotRetainsLastKnownGoodAsync),
            ("metadata rate limiting", MetadataRateLimitingAsync),
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

        Console.WriteLine($"{tests.Length - failures}/{tests.Length} distribution service tests passed.");
        return failures == 0 ? 0 : 1;
    }

    private static Task PublisherAndSnapshotValidationAsync()
    {
        string root = TestDistribution.CreateTemporaryRoot();
        try
        {
            PublishedFixture fixture = TestDistribution.Create(root, "snapshot-a", "alpha");
            SnapshotIndex index = SnapshotValidator.ValidateCurrent(fixture.StorageRoot);
            TestAssert.Equal("snapshot-a", index.SnapshotId, "Active snapshot ID was not preserved.");
            TestAssert.True(
                index.TryGet($"packages/{fixture.PackageSha256}", out DistributionFile? package) && package is not null,
                "Published package was not indexed.");
            TestAssert.True(
                index.TryGet($"manifests/{fixture.ManifestSha256}.json", out DistributionFile? manifest) &&
                    manifest is not null,
                "Published package manifest was not indexed.");
            TestAssert.Throws<InvalidDataException>(
                () => DistributionPaths.NormalizeRelativePath("../outside"),
                "Traversal path was accepted.");
            TestAssert.Throws<InvalidDataException>(
                () =>
                {
                    using System.Text.Json.JsonDocument ignored = DistributionJson.ParseStrict(
                        Encoding.UTF8.GetBytes("{\"schemaVersion\":1,\"schemaVersion\":1}"), 24);
                },
                "Duplicate JSON properties were accepted.");
            TestAssert.Throws<InvalidDataException>(
                () => SignedDocumentIdentity.Parse(
                    Encoding.UTF8.GetBytes(
                        "{\"schemaVersion\":1,\"keyId\":\"test-release-key\",\"sequence\":1," +
                        "\"expiresAt\":\"2035-01-01T00:00:00\"}"),
                    "timezone-less.json"),
                "A signed-document expiry without an explicit UTC offset was accepted.");
            TestAssert.Throws<InvalidDataException>(
                () => SnapshotValidator.ValidateSignature(new SignedDocumentMetadata
                {
                    Algorithm = DistributionContract.SignatureAlgorithm,
                    KeyId = "test-release-key",
                    Value = fixture.CatalogSignature + "\n",
                    Sequence = 7,
                    ExpiresAt = new DateTimeOffset(2035, 1, 1, 0, 0, 0, TimeSpan.Zero),
                }),
                "Non-canonical base64 signature was accepted.");

            string packagePath = Path.Combine(fixture.SourceRoot, "packages", fixture.PackageSha256);
            string invalidPackagePath = Path.Combine(fixture.SourceRoot, "packages", new string('0', 64));
            File.Move(packagePath, invalidPackagePath);
            TestAssert.Throws<InvalidDataException>(
                () => DistributionSigning.PublishVerified(
                    fixture.SourceRoot,
                    fixture.StorageRoot,
                    "snapshot-invalid",
                    activate: false,
                    fixture.PublicKeyPath,
                    new SigningPolicy
                    {
                        MinimumSequence = 1,
                        MinimumRemainingValidity = TimeSpan.Zero,
                        Now = DateTimeOffset.UtcNow,
                    }),
                "Publisher accepted a package whose filename did not match its digest.");
            return Task.CompletedTask;
        }
        finally
        {
            TestDistribution.DeleteTemporaryRoot(root);
        }
    }

    private static async Task LiveAndNotReadyHealthAsync()
    {
        string root = TestDistribution.CreateTemporaryRoot();
        string emptyStorage = Path.Combine(root, "empty");
        Directory.CreateDirectory(emptyStorage);
        await using TestServer server = await TestServer.StartAsync(emptyStorage);
        try
        {
            using HttpResponseMessage live = await server.Client.GetAsync("/health/live");
            await TestAssert.StatusAsync(HttpStatusCode.OK, live);
            using HttpResponseMessage ready = await server.Client.GetAsync("/health/ready");
            await TestAssert.StatusAsync(HttpStatusCode.ServiceUnavailable, ready);
        }
        finally
        {
            TestDistribution.DeleteTemporaryRoot(root);
        }
    }

    private static async Task ExactDocumentsAndConditionalRequestsAsync()
    {
        string root = TestDistribution.CreateTemporaryRoot();
        PublishedFixture fixture = TestDistribution.Create(root, "snapshot-a", "alpha");
        await using TestServer server = await TestServer.StartAsync(fixture.StorageRoot);
        try
        {
            await server.WaitUntilReadyAsync();
            using HttpResponseMessage catalog = await server.Client.GetAsync("/v1/catalog/stable/windows/x86_64");
            await TestAssert.StatusAsync(HttpStatusCode.OK, catalog);
            byte[] actualCatalog = await catalog.Content.ReadAsByteArrayAsync();
            TestAssert.BytesEqual(fixture.CatalogBytes, actualCatalog, "Catalog bytes were rewritten in transit.");
            TestAssert.Equal(
                fixture.CatalogSignature,
                catalog.Headers.GetValues("X-Keire-Signature").Single(),
                "Detached signature header was not preserved.");
            string etag = catalog.Headers.ETag?.Tag
                ?? throw new InvalidOperationException("Catalog ETag was missing.");

            using HttpRequestMessage conditionalRequest = new(HttpMethod.Get, "/v1/catalog/stable/windows/x86_64");
            conditionalRequest.Headers.TryAddWithoutValidation("If-None-Match", etag);
            using HttpResponseMessage conditional = await server.Client.SendAsync(conditionalRequest);
            await TestAssert.StatusAsync(HttpStatusCode.NotModified, conditional);
            TestAssert.Equal(0L, conditional.Content.Headers.ContentLength ?? 0, "304 response carried a body length.");

            using HttpResponseMessage compactCatalog =
                await server.Client.GetAsync("/v2/catalog/stable/windows/x86_64");
            await TestAssert.StatusAsync(HttpStatusCode.OK, compactCatalog);
            byte[] actualCompactCatalog = await compactCatalog.Content.ReadAsByteArrayAsync();
            TestAssert.BytesEqual(
                fixture.CompactCatalogBytes,
                actualCompactCatalog,
                "Compact catalog bytes were rewritten in transit.");
            TestAssert.Equal(
                fixture.CompactCatalogSignature,
                compactCatalog.Headers.GetValues("X-Keire-Signature").Single(),
                "Compact catalog detached signature header was not preserved.");

            using HttpResponseMessage content = await server.Client.GetAsync("/v1/content/en-US");
            await TestAssert.StatusAsync(HttpStatusCode.OK, content);
            byte[] actualContent = await content.Content.ReadAsByteArrayAsync();
            TestAssert.BytesEqual(
                fixture.ContentBytes,
                actualContent,
                "Content catalog bytes were rewritten in transit.");

            using HttpResponseMessage manifest =
                await server.Client.GetAsync($"/v1/manifests/{fixture.ManifestSha256}");
            await TestAssert.StatusAsync(HttpStatusCode.OK, manifest);
            byte[] actualManifest = await manifest.Content.ReadAsByteArrayAsync();
            TestAssert.BytesEqual(
                fixture.ManifestBytes,
                actualManifest,
                "Package manifest bytes were rewritten in transit.");
            TestAssert.Equal(
                "application/json",
                manifest.Content.Headers.ContentType?.MediaType ?? string.Empty,
                "Package manifest response used the wrong media type.");
            TestAssert.True(
                manifest.Headers.CacheControl?.Extensions.Any(
                    extension => string.Equals(extension.Name, "immutable", StringComparison.OrdinalIgnoreCase)) == true,
                "Content-addressed package manifest was not immutable-cacheable.");

            using HttpResponseMessage malformed = await server.Client.GetAsync("/v1/content/en%2FUS");
            TestAssert.True(
                malformed.StatusCode is HttpStatusCode.NotFound or HttpStatusCode.BadRequest,
                "Encoded path separator reached distribution storage.");
        }
        finally
        {
            TestDistribution.DeleteTemporaryRoot(root);
        }
    }

    private static async Task PackageRangesAndValidatorsAsync()
    {
        string root = TestDistribution.CreateTemporaryRoot();
        PublishedFixture fixture = TestDistribution.Create(root, "snapshot-a", "alpha");
        await using TestServer server = await TestServer.StartAsync(fixture.StorageRoot);
        try
        {
            await server.WaitUntilReadyAsync();
            string path = $"/v1/packages/{fixture.PackageSha256}";
            using HttpResponseMessage full = await server.Client.GetAsync(path);
            await TestAssert.StatusAsync(HttpStatusCode.OK, full);
            byte[] actualPackage = await full.Content.ReadAsByteArrayAsync();
            TestAssert.BytesEqual(
                fixture.PackageBytes,
                actualPackage,
                "Full package response was incorrect.");
            string etag = full.Headers.ETag?.Tag
                ?? throw new InvalidOperationException("Package ETag was missing.");

            using HttpRequestMessage headRequest = new(HttpMethod.Head, path);
            using HttpResponseMessage head = await server.Client.SendAsync(headRequest);
            await TestAssert.StatusAsync(HttpStatusCode.OK, head);
            TestAssert.Equal(
                (long)fixture.PackageBytes.Length,
                head.Content.Headers.ContentLength ?? -1,
                "HEAD package response did not preserve content length.");
            TestAssert.Equal(
                0,
                (await head.Content.ReadAsByteArrayAsync()).Length,
                "HEAD package response carried a body.");

            await AssertRangeAsync(server.Client, path, "bytes=2-7", etag, 2, 7);
            await AssertRangeAsync(server.Client, path, "bytes=30-", etag, 30, fixture.PackageBytes.Length - 1);
            await AssertRangeAsync(
                server.Client,
                path,
                "bytes=-5",
                etag,
                fixture.PackageBytes.Length - 5,
                fixture.PackageBytes.Length - 1);

            using HttpRequestMessage changedValidator = RangeRequest(path, "bytes=2-7", "\"different\"");
            using HttpResponseMessage changed = await server.Client.SendAsync(changedValidator);
            await TestAssert.StatusAsync(HttpStatusCode.OK, changed);
            byte[] restartedPackage = await changed.Content.ReadAsByteArrayAsync();
            TestAssert.BytesEqual(
                fixture.PackageBytes,
                restartedPackage,
                "Changed If-Range validator did not restart the complete response.");

            foreach (string invalidRange in new[] { "bytes=100-101", "bytes=7-2", "bytes=0-1,3-4", "items=0-1" })
            {
                using HttpRequestMessage invalidRequest = RangeRequest(path, invalidRange, etag);
                using HttpResponseMessage invalid = await server.Client.SendAsync(invalidRequest);
                await TestAssert.StatusAsync(HttpStatusCode.RequestedRangeNotSatisfiable, invalid);
                TestAssert.Equal(
                    $"bytes */{fixture.PackageBytes.Length}",
                    invalid.Content.Headers.ContentRange?.ToString() ?? string.Empty,
                    "416 response did not advertise the current package size.");
            }

            using HttpRequestMessage notModifiedRequest = new(HttpMethod.Get, path);
            notModifiedRequest.Headers.TryAddWithoutValidation("If-None-Match", etag);
            using HttpResponseMessage notModified = await server.Client.SendAsync(notModifiedRequest);
            await TestAssert.StatusAsync(HttpStatusCode.NotModified, notModified);

            using HttpResponseMessage invalidHash = await server.Client.GetAsync($"/v1/packages/{new string('A', 64)}");
            await TestAssert.StatusAsync(HttpStatusCode.NotFound, invalidHash);
            TestAssert.Equal(
                "application/problem+json",
                invalidHash.Content.Headers.ContentType?.MediaType ?? string.Empty,
                "Typed distribution error used the wrong media type.");
        }
        finally
        {
            TestDistribution.DeleteTemporaryRoot(root);
        }
    }

    private static async Task InvalidSnapshotRetainsLastKnownGoodAsync()
    {
        string root = TestDistribution.CreateTemporaryRoot();
        PublishedFixture first = TestDistribution.Create(root, "snapshot-a", "alpha");
        await using TestServer server = await TestServer.StartAsync(first.StorageRoot);
        try
        {
            await server.WaitUntilReadyAsync();
            string invalidDirectory = Path.Combine(first.StorageRoot, "snapshots", "snapshot-broken");
            Directory.CreateDirectory(invalidDirectory);
            File.WriteAllText(Path.Combine(first.StorageRoot, "current"), "snapshot-broken\n", Encoding.UTF8);
            await Task.Delay(TimeSpan.FromSeconds(1.5));

            using HttpResponseMessage retained = await server.Client.GetAsync("/v1/catalog/stable/windows/x86_64");
            await TestAssert.StatusAsync(HttpStatusCode.OK, retained);
            byte[] retainedCatalog = await retained.Content.ReadAsByteArrayAsync();
            TestAssert.BytesEqual(
                first.CatalogBytes,
                retainedCatalog,
                "An invalid current pointer displaced the last-known-good snapshot.");
            using HttpResponseMessage degraded = await server.Client.GetAsync("/health/ready");
            await TestAssert.StatusAsync(HttpStatusCode.OK, degraded);
            string degradedJson = await degraded.Content.ReadAsStringAsync();
            TestAssert.True(degradedJson.Contains("ready-degraded", StringComparison.Ordinal), "Degraded readiness was not reported.");

            PublishedFixture second = TestDistribution.Create(root, "snapshot-b", "bravo");
            DateTimeOffset deadline = DateTimeOffset.UtcNow.AddSeconds(10);
            while (DateTimeOffset.UtcNow < deadline)
            {
                using HttpResponseMessage response = await server.Client.GetAsync("/v1/catalog/stable/windows/x86_64");
                byte[] bytes = await response.Content.ReadAsByteArrayAsync();
                if (bytes.SequenceEqual(second.CatalogBytes))
                {
                    return;
                }

                await Task.Delay(100);
            }

            throw new TimeoutException("Service did not activate the next valid snapshot.");
        }
        finally
        {
            TestDistribution.DeleteTemporaryRoot(root);
        }
    }

    private static async Task MetadataRateLimitingAsync()
    {
        string root = TestDistribution.CreateTemporaryRoot();
        PublishedFixture fixture = TestDistribution.Create(root, "snapshot-a", "alpha");
        await using TestServer server = await TestServer.StartAsync(fixture.StorageRoot, metadataRequestsPerMinute: 1);
        try
        {
            await server.WaitUntilReadyAsync();
            using HttpResponseMessage first = await server.Client.GetAsync("/v1/catalog/stable/windows/x86_64");
            await TestAssert.StatusAsync(HttpStatusCode.OK, first);
            using HttpResponseMessage second = await server.Client.GetAsync("/v1/catalog/stable/windows/x86_64");
            await TestAssert.StatusAsync(HttpStatusCode.TooManyRequests, second);
        }
        finally
        {
            TestDistribution.DeleteTemporaryRoot(root);
        }
    }

    private static async Task AssertRangeAsync(
        HttpClient client,
        string path,
        string range,
        string etag,
        int expectedStart,
        int expectedEnd)
    {
        using HttpRequestMessage request = RangeRequest(path, range, etag);
        using HttpResponseMessage response = await client.SendAsync(request);
        await TestAssert.StatusAsync(HttpStatusCode.PartialContent, response);
        byte[] bytes = await response.Content.ReadAsByteArrayAsync();
        byte[] expected = Encoding.ASCII.GetBytes("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ")[expectedStart..(expectedEnd + 1)];
        TestAssert.BytesEqual(expected, bytes, $"Range response for '{range}' was incorrect.");
        TestAssert.Equal(
            $"bytes {expectedStart}-{expectedEnd}/36",
            response.Content.Headers.ContentRange?.ToString() ?? string.Empty,
            "Range response Content-Range was incorrect.");
    }

    private static HttpRequestMessage RangeRequest(string path, string range, string ifRange)
    {
        HttpRequestMessage request = new(HttpMethod.Get, path);
        request.Headers.TryAddWithoutValidation("Range", range);
        request.Headers.TryAddWithoutValidation("If-Range", ifRange);
        return request;
    }
}
