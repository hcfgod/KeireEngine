using Keire.Distribution;

namespace Keire.Marketplace.Validation;

public sealed class MarketplacePackageValidator(IMalwareScanner malwareScanner)
{
    private readonly IMalwareScanner m_MalwareScanner = malwareScanner;

    public async Task<MarketplaceValidationReport> ValidateAsync(
        PackageValidationRequest request,
        CancellationToken cancellationToken = default)
    {
        return (await ValidateWithEvidenceAsync(request, cancellationToken)).Report;
    }

    public async Task<MarketplaceValidationOutput> ValidateWithEvidenceAsync(
        PackageValidationRequest request,
        CancellationToken cancellationToken = default)
    {
        ValidateRequest(request);
        List<ValidationDiagnostic> diagnostics = [];
        string malwareStatus = ValidationStatuses.Failed;
        string secretStatus = ValidationStatuses.Failed;
        string managedStatus = ValidationStatuses.Failed;
        string? manifestSha256 = null;
        string? codeFingerprint = null;
        string? malwareEngineVersion = null;
        ExtractedPackageDocument? packageDocument = null;
        string jobRoot = CreateJobRoot(request.WorkRoot);
        string stagingRoot = Path.Combine(jobRoot, "payload");
        string managedBuildRoot = Path.Combine(jobRoot, "managed-build");
        try
        {
            VerifiedPackageInput verifiedInput = await PackageExtractor.VerifyInputAsync(request, cancellationToken);
            MalwareScanResult archiveScan = await m_MalwareScanner.ScanFileAsync(verifiedInput.Path, cancellationToken);
            diagnostics.AddRange(archiveScan.Diagnostics);
            malwareStatus = archiveScan.Status;
            malwareEngineVersion = archiveScan.EngineVersion;
            if (malwareStatus != ValidationStatuses.Clean)
            {
                return CreateOutput();
            }

            (ExtractedPackageDocument document, string packageSha256) = await PackageExtractor.ExtractAsync(
                request,
                stagingRoot,
                cancellationToken);
            if (!string.Equals(packageSha256, request.ExpectedPackageSha256, StringComparison.Ordinal))
            {
                throw new InvalidDataException("The package digest changed during validation.");
            }

            manifestSha256 = document.Archive.ManifestSha256;
            packageDocument = document;
            diagnostics.AddRange(PayloadPolicy.Validate(stagingRoot, document, cancellationToken));
            if (HasErrors(diagnostics))
            {
                return CreateOutput();
            }

            MalwareScanResult payloadScan = await m_MalwareScanner.ScanDirectoryAsync(stagingRoot, cancellationToken);
            diagnostics.AddRange(payloadScan.Diagnostics);
            malwareStatus = payloadScan.Status;
            malwareEngineVersion = payloadScan.EngineVersion ?? malwareEngineVersion;
            if (malwareStatus != ValidationStatuses.Clean)
            {
                return CreateOutput();
            }

            IReadOnlyList<ValidationDiagnostic> secretDiagnostics =
                await SecretScanner.ScanAsync(stagingRoot, cancellationToken);
            diagnostics.AddRange(secretDiagnostics);
            secretStatus = secretDiagnostics.Count == 0 ? ValidationStatuses.Clean : ValidationStatuses.Failed;
            if (secretStatus != ValidationStatuses.Clean)
            {
                return CreateOutput();
            }

            ManagedValidationResult managed = await ManagedAssemblyValidator.ValidateAsync(
                stagingRoot,
                managedBuildRoot,
                document,
                request.DotnetPath,
                request.ManagedApiPath,
                cancellationToken);
            diagnostics.AddRange(managed.Diagnostics);
            managedStatus = managed.Status;
            codeFingerprint = managed.CodeFingerprintSha256;
            return CreateOutput();
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception exception) when (exception is ArgumentException or InvalidDataException or IOException or TimeoutException)
        {
            diagnostics.Add(new ValidationDiagnostic
            {
                Code = "PACKAGE_VALIDATION_FAILED",
                Severity = ValidationSeverities.Error,
                Message = SafeFailureMessage(exception),
            });
            return CreateOutput();
        }
        finally
        {
            DeleteJobRoot(request.WorkRoot, jobRoot);
        }

        MarketplaceValidationOutput CreateOutput()
        {
            bool passed = malwareStatus == ValidationStatuses.Clean && secretStatus == ValidationStatuses.Clean &&
                managedStatus is ValidationStatuses.Passed or ValidationStatuses.NotApplicable && !HasErrors(diagnostics);
            DateTimeOffset completedAt = DateTimeOffset.UtcNow;
            byte[] evidence = CreateEvidence(passed, completedAt, packageDocument);
            if (evidence.Length > MarketplaceValidationContract.MaximumEvidenceBytes)
            {
                diagnostics.Add(new ValidationDiagnostic
                {
                    Code = "REVIEW_EVIDENCE_TOO_LARGE",
                    Severity = ValidationSeverities.Error,
                    Message = "The safe review inventory exceeds the Marketplace evidence limit.",
                });
                passed = false;
                evidence = CreateEvidence(passed, completedAt, null);
            }

            string evidenceSha256 = Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(evidence));
            MarketplaceValidationReport report = new()
            {
                ValidatorFingerprintSha256 = request.ValidatorFingerprintSha256,
                PackageSha256 = request.ExpectedPackageSha256,
                ManifestSha256 = manifestSha256,
                Passed = passed,
                MalwareScanResult = malwareStatus,
                SecretScanResult = secretStatus,
                ManagedValidationResult = managedStatus,
                CodeFingerprintSha256 = codeFingerprint,
                EvidenceStoragePath = $"{request.UploadId:D}/review-evidence-{request.ExpectedPackageSha256}.json",
                EvidenceSha256 = evidenceSha256,
                EvidenceSizeBytes = evidence.LongLength,
                Diagnostics = diagnostics,
                CompletedAt = completedAt,
            };
            return new MarketplaceValidationOutput(report, evidence);

            byte[] CreateEvidence(bool evidencePassed, DateTimeOffset evidenceCompletedAt, ExtractedPackageDocument? document)
            {
                MarketplaceReviewInventory? inventory = document is null ? null : new MarketplaceReviewInventory
                {
                    PackageId = document.PackageId,
                    Version = document.Version,
                    PublisherId = document.PublisherId,
                    DisplayName = document.DisplayName,
                    Summary = document.Summary,
                    Channel = document.Channel,
                    InstallKind = document.InstallKind,
                    Compatibility = document.Compatibility,
                    Dependencies = document.Dependencies,
                    Conflicts = document.Conflicts,
                    Files = document.Files,
                    Assets = document.Assets,
                    Samples = document.Samples,
                    ManagedAssemblies = document.ManagedAssemblies,
                    Licenses = document.Licenses,
                    EntryPoints = document.EntryPoints,
                    InstalledSizeBytes = document.InstalledSizeBytes,
                };
                MarketplaceReviewCounts counts = new(
                    document?.Files.Count ?? 0,
                    document?.Assets.Count ?? 0,
                    document?.Samples.Count ?? 0,
                    document?.ManagedAssemblies.Count ?? 0,
                    document?.Licenses.Count ?? 0,
                    document?.EntryPoints.Count ?? 0);
                return DistributionJson.Serialize(new MarketplaceReviewEvidence
                {
                    PackageSha256 = request.ExpectedPackageSha256,
                    ManifestSha256 = manifestSha256,
                    ValidatorFingerprintSha256 = request.ValidatorFingerprintSha256,
                    MalwareScanResult = malwareStatus,
                    MalwareEngineVersion = malwareEngineVersion,
                    SecretScanResult = secretStatus,
                    ManagedValidationResult = managedStatus,
                    CodeFingerprintSha256 = codeFingerprint,
                    Passed = evidencePassed,
                    CompletedAt = evidenceCompletedAt,
                    Diagnostics = diagnostics,
                    Inventory = inventory,
                    Counts = counts,
                });
            }
        }
    }

    private static void ValidateRequest(PackageValidationRequest request)
    {
        if (request.ExpectedPackageBytes is <= 0 or > MarketplaceValidationContract.MaximumPackageBytes ||
            !DistributionPaths.IsSha256(request.ExpectedPackageSha256) ||
            !DistributionPaths.IsSha256(request.ValidatorFingerprintSha256) || request.UploadId == Guid.Empty ||
            request.VersionId == Guid.Empty || request.StorageBucket is not ("marketplace-packages" or "marketplace-quarantine") ||
            !string.Equals(request.StoragePath, DistributionPaths.NormalizeRelativePath(request.StoragePath),
                StringComparison.Ordinal))
        {
            throw new ArgumentException("Package validation requires canonical immutable size, digest, and validator identity.");
        }

        DirectoryInfo workRoot = new(Path.GetFullPath(request.WorkRoot));
        if (!workRoot.Exists)
        {
            throw new DirectoryNotFoundException("The configured validator work root does not exist.");
        }

        workRoot.Refresh();
        FileSystemSafety.RejectLink(workRoot);
    }

    private static string CreateJobRoot(string workRoot)
    {
        string fullWorkRoot = Path.GetFullPath(workRoot);
        string jobRoot = Path.Combine(fullWorkRoot, $"job-{Guid.NewGuid():N}");
        Directory.CreateDirectory(jobRoot);
        FileSystemSafety.RejectLink(new DirectoryInfo(jobRoot));
        if (!OperatingSystem.IsWindows())
        {
            File.SetUnixFileMode(
                jobRoot,
                UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
        }

        return jobRoot;
    }

    private static void DeleteJobRoot(string workRoot, string jobRoot)
    {
        string fullWorkRoot = Path.GetFullPath(workRoot);
        string fullJobRoot = Path.GetFullPath(jobRoot);
        string prefix = fullWorkRoot.EndsWith(Path.DirectorySeparatorChar)
            ? fullWorkRoot
            : fullWorkRoot + Path.DirectorySeparatorChar;
        StringComparison comparison = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        if (fullJobRoot.StartsWith(prefix, comparison) && Directory.Exists(fullJobRoot))
        {
            Directory.Delete(fullJobRoot, recursive: true);
        }
    }

    private static bool HasErrors(IEnumerable<ValidationDiagnostic> diagnostics)
    {
        return diagnostics.Any(diagnostic => diagnostic.Severity == ValidationSeverities.Error);
    }

    private static string SafeFailureMessage(Exception exception)
    {
        return exception switch
        {
            TimeoutException => "A bounded validation stage exceeded its time limit.",
            InvalidDataException => exception.Message,
            _ => "The package could not be validated safely; no project or marketplace files were changed.",
        };
    }
}
