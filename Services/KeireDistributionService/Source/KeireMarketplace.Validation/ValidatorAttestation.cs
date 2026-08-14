using System.Text;
using Keire.Distribution;
using Keire.Marketplace.Security;

namespace Keire.Marketplace.Validation;

public static class ValidatorAttestation
{
    public static SignedValidatorAttestation Sign(
        PackageValidationRequest request,
        MarketplaceValidationReport report,
        MarketplaceSigningKey signingKey)
    {
        ValidatorAttestationDocument document = CreateDocument(request, report, signingKey.PublicDocument.KeyId);
        string documentText = Encoding.UTF8.GetString(DistributionJson.Serialize(document));
        return new SignedValidatorAttestation
        {
            Document = documentText,
            Signature = new ValidatorAttestationSignature
            {
                KeyId = signingKey.PublicDocument.KeyId,
                Value = signingKey.SignBase64(Encoding.UTF8.GetBytes(documentText)),
            },
        };
    }

    public static ValidatorAttestationDocument Verify(
        Guid uploadId,
        Guid versionId,
        string storageBucket,
        string storagePath,
        long packageSizeBytes,
        string packageSha256,
        MarketplaceValidationReport report,
        MarketplaceVerificationKey verificationKey)
    {
        SignedValidatorAttestation attestation = report.Attestation
            ?? throw new InvalidDataException("The validator report has no signed attestation.");
        if (attestation.SchemaVersion != MarketplaceValidationContract.AttestationSchemaVersion ||
            !string.Equals(attestation.Signature.Algorithm, "ed25519", StringComparison.Ordinal) ||
            !string.Equals(attestation.Signature.KeyId, verificationKey.Document.KeyId, StringComparison.Ordinal) ||
            !verificationKey.VerifyBase64(Encoding.UTF8.GetBytes(attestation.Document), attestation.Signature.Value))
        {
            throw new InvalidDataException("The validator attestation signature is invalid.");
        }

        ValidatorAttestationDocument document;
        try
        {
            document = DistributionJson.DeserializeStrict<ValidatorAttestationDocument>(
                Encoding.UTF8.GetBytes(attestation.Document));
        }
        catch (System.Text.Json.JsonException exception)
        {
            throw new InvalidDataException("The validator attestation document is malformed.", exception);
        }

        ValidatorAttestationDocument expected = CreateDocument(
            uploadId,
            versionId,
            storageBucket,
            storagePath,
            packageSizeBytes,
            packageSha256,
            report,
            verificationKey.Document.KeyId);
        if (!DistributionJson.Serialize(document).AsSpan().SequenceEqual(DistributionJson.Serialize(expected)) ||
            !string.Equals(attestation.Document, Encoding.UTF8.GetString(DistributionJson.Serialize(document)),
                StringComparison.Ordinal))
        {
            throw new InvalidDataException("The validator attestation does not match its package and evidence.");
        }

        return document;
    }

    private static ValidatorAttestationDocument CreateDocument(
        PackageValidationRequest request,
        MarketplaceValidationReport report,
        string keyId)
    {
        return CreateDocument(
            request.UploadId,
            request.VersionId,
            request.StorageBucket,
            request.StoragePath,
            request.ExpectedPackageBytes,
            request.ExpectedPackageSha256,
            report,
            keyId);
    }

    private static ValidatorAttestationDocument CreateDocument(
        Guid uploadId,
        Guid versionId,
        string storageBucket,
        string storagePath,
        long packageSizeBytes,
        string packageSha256,
        MarketplaceValidationReport report,
        string keyId)
    {
        return new ValidatorAttestationDocument
        {
            KeyId = keyId,
            UploadId = uploadId.ToString("D"),
            VersionId = versionId.ToString("D"),
            StorageBucket = storageBucket,
            StoragePath = storagePath,
            PackageSha256 = packageSha256,
            PackageSizeBytes = packageSizeBytes,
            ManifestSha256 = report.ManifestSha256,
            EvidenceStoragePath = report.EvidenceStoragePath,
            EvidenceSha256 = report.EvidenceSha256,
            EvidenceSizeBytes = report.EvidenceSizeBytes,
            ValidatorVersion = report.ValidatorVersion,
            ValidatorFingerprintSha256 = report.ValidatorFingerprintSha256,
            PolicyVersion = report.PolicyVersion,
            MalwareScanResult = report.MalwareScanResult,
            SecretScanResult = report.SecretScanResult,
            ManagedValidationResult = report.ManagedValidationResult,
            CodeFingerprintSha256 = report.CodeFingerprintSha256,
            Passed = report.Passed,
            CompletedAt = report.CompletedAt,
        };
    }
}
