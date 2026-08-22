using System.Globalization;
using System.Security.Cryptography;
using Keire.Distribution;
using Keire.Marketplace.Validation;
using Keire.Marketplace.Security;

return await ValidatorProgram.RunAsync(args);

internal static class ValidatorProgram
{
    public static async Task<int> RunAsync(string[] args)
    {
        using CancellationTokenSource cancellation = new();
        Console.CancelKeyPress += (_, eventArguments) =>
        {
            eventArguments.Cancel = true;
            cancellation.Cancel();
        };

        try
        {
            ValidatorCommandLine commandLine = ValidatorCommandLine.Parse(args);
            if (commandLine.Help)
            {
                PrintUsage();
                return 0;
            }

            if (!string.Equals(
                    Environment.GetEnvironmentVariable("KEIRE_VALIDATOR_NETWORK_ISOLATED"),
                    "1",
                    StringComparison.Ordinal))
            {
                throw new InvalidOperationException(
                    "The validator requires an OS-isolated launcher and KEIRE_VALIDATOR_NETWORK_ISOLATED=1.");
            }

            string processPath = Environment.ProcessPath
                ?? throw new InvalidOperationException("The validator executable path is unavailable.");
            string fingerprint = await ValidatorFiles.ComputeSha256Async(processPath, cancellation.Token);
            using MarketplaceSigningKey attestationKey =
                MarketplaceSigningKey.FromEnvironment("KEIRE_VALIDATOR_ATTESTATION_PRIVATE_KEY");
            if (commandLine.Command == "watch")
            {
                return await ValidatorExchange.RunAsync(commandLine, fingerprint, attestationKey, cancellation.Token);
            }

            PackageValidationRequest request = new(
                Guid.Parse(commandLine.Require("--upload-id")),
                Guid.Parse(commandLine.Require("--version-id")),
                commandLine.Require("--storage-bucket"),
                commandLine.Require("--storage-path"),
                commandLine.Require("--package"),
                commandLine.Require("--work-root"),
                commandLine.Require("--asset-tool"),
                commandLine.Require("--dotnet"),
                commandLine.Optional("--managed-api"),
                commandLine.Require("--malware-scanner"),
                ParseSize(commandLine.Require("--expected-size")),
                ParseSha256(commandLine.Require("--expected-sha256")),
                fingerprint);
            MarketplacePackageValidator validator = new(new ClamAvScanner(request.MalwareScannerPath));
            MarketplaceValidationOutput output = await validator.ValidateWithEvidenceAsync(request, cancellation.Token);
            MarketplaceValidationReport report = output.Report with
            {
                Attestation = ValidatorAttestation.Sign(request, output.Report, attestationKey),
            };
            ValidatorFiles.WriteNewFile(commandLine.Require("--evidence"), output.ReviewEvidence);
            ValidatorFiles.WriteNewReport(commandLine.Require("--report"), report);
            Console.WriteLine(report.Passed ? "Marketplace package validation passed." : "Marketplace package validation failed.");
            return report.Passed ? 0 : 3;
        }
        catch (OperationCanceledException)
        {
            Console.Error.WriteLine("Marketplace package validation was cancelled.");
            return 130;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"Marketplace validator configuration failed: {exception.Message}");
            return 2;
        }
    }

    private static long ParseSize(string value)
    {
        return long.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out long parsed) &&
            parsed is > 0 and <= MarketplaceValidationContract.MaximumPackageBytes
            ? parsed
            : throw new ArgumentException("--expected-size must be a positive byte count within the package limit.");
    }

    private static string ParseSha256(string value)
    {
        return DistributionPaths.IsSha256(value)
            ? value
            : throw new ArgumentException("--expected-sha256 must be 64 lowercase hexadecimal characters.");
    }

    private static void PrintUsage()
    {
        Console.WriteLine("""
            Kéire Marketplace Validator 0.4.0

            validate-local --package <quarantined.keireassetpackage>
                           --upload-id <uuid> --version-id <uuid>
                           --storage-bucket <marketplace-packages-or-quarantine> --storage-path <relative-path>
                           --expected-size <bytes> --expected-sha256 <digest>
                           --work-root <private-existing-directory> --report <new-report.json>
                           --evidence <new-review-evidence.json>
                           --asset-tool <KeireAssetTool> --malware-scanner <clamscan-or-clamdscan>
                           --dotnet <pinned-dotnet> [--managed-api <Keire.Managed.dll>]

            watch          --exchange-root <private-existing-directory> --work-root <private-existing-directory>
                           --asset-tool <KeireAssetTool> --malware-scanner <clamscan-or-clamdscan>
                           --dotnet <pinned-dotnet> [--managed-api <Keire.Managed.dll>] [--poll-ms <100-5000>]

            The process accepts local immutable input only. Launch it with OS-level outbound-network denial and set
            KEIRE_VALIDATOR_NETWORK_ISOLATED=1 and KEIRE_VALIDATOR_ATTESTATION_PRIVATE_KEY to a protected PKCS#8
            base64 value. The networked lease/download broker is a separate process.
            """);
    }
}

internal sealed class ValidatorCommandLine
{
    private readonly Dictionary<string, string> m_Values;

    private ValidatorCommandLine(bool help, string command, Dictionary<string, string> values)
    {
        Help = help;
        Command = command;
        m_Values = values;
    }

    public bool Help { get; }

    public string Command { get; }

    public static ValidatorCommandLine Parse(string[] args)
    {
        if (args.Length == 0 || args[0] is "-h" or "--help")
        {
            return new ValidatorCommandLine(true, "help", new Dictionary<string, string>(StringComparer.Ordinal));
        }

        if (args[0] is not ("validate-local" or "watch"))
        {
            throw new ArgumentException($"Unknown command '{args[0]}'.");
        }

        HashSet<string> allowed = args[0] == "validate-local"
            ?
            [
                "--package",
                "--upload-id",
                "--version-id",
                "--storage-bucket",
                "--storage-path",
                "--expected-size",
                "--expected-sha256",
                "--work-root",
                "--report",
                "--evidence",
                "--asset-tool",
                "--malware-scanner",
                "--dotnet",
                "--managed-api",
            ]
            :
            [
                "--exchange-root",
                "--work-root",
                "--asset-tool",
                "--malware-scanner",
                "--dotnet",
                "--managed-api",
                "--poll-ms",
            ];
        Dictionary<string, string> values = new(StringComparer.Ordinal);
        for (int index = 1; index < args.Length; index += 2)
        {
            string option = args[index];
            if (!allowed.Contains(option) || index + 1 >= args.Length || args[index + 1].StartsWith("--", StringComparison.Ordinal))
            {
                throw new ArgumentException($"Unexpected or incomplete option '{option}'.");
            }

            if (!values.TryAdd(option, args[index + 1]))
            {
                throw new ArgumentException($"Duplicate option '{option}'.");
            }
        }

        return new ValidatorCommandLine(false, args[0], values);
    }

    public string Require(string option)
    {
        return m_Values.TryGetValue(option, out string? value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"Required option '{option}' was not provided.");
    }

    public string? Optional(string option)
    {
        return m_Values.TryGetValue(option, out string? value) && !string.IsNullOrWhiteSpace(value) ? value : null;
    }
}
