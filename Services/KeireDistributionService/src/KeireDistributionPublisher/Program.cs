using System.Globalization;
using Keire.Distribution;
using Keire.Distribution.Publisher;

return await PublisherProgram.RunAsync(args);

internal static class PublisherProgram
{
    public static Task<int> RunAsync(string[] args)
    {
        try
        {
            CommandLine commandLine = CommandLine.Parse(args);
            switch (commandLine.Command)
            {
                case "publish":
                    {
                        commandLine.RejectUnknown(
                            "--source",
                            "--root",
                            "--snapshot",
                            "--public-key",
                            "--minimum-sequence",
                            "--minimum-validity-hours",
                            "--activate");
                        SnapshotIndex index = DistributionSigning.PublishVerified(
                            commandLine.Require("--source"),
                            commandLine.Require("--root"),
                            commandLine.Require("--snapshot"),
                            commandLine.HasFlag("--activate"),
                            commandLine.Require("--public-key"),
                            CreatePolicy(commandLine));
                        Console.WriteLine($"Published immutable snapshot '{index.SnapshotId}'.");
                        return Task.FromResult(0);
                    }
                case "generate-key":
                    {
                        commandLine.RejectUnknown("--private-key", "--public-key");
                        DistributionPublicKeyDocument publicKey = DistributionSigning.GenerateKey(
                            commandLine.Require("--private-key"),
                            commandLine.Require("--public-key"));
                        Console.WriteLine($"Generated Ed25519 signing key '{publicKey.KeyId}'.");
                        return Task.FromResult(0);
                    }
                case "derive-public":
                    {
                        commandLine.RejectUnknown("--private-key", "--private-key-env", "--public-key");
                        DistributionPublicKeyDocument publicKey = DistributionSigning.DerivePublicKey(
                            CreatePrivateKeySource(commandLine),
                            commandLine.Require("--public-key"));
                        Console.WriteLine($"Derived trusted public key '{publicKey.KeyId}'.");
                        return Task.FromResult(0);
                    }
                case "sign":
                    {
                        commandLine.RejectUnknown(
                            "--source",
                            "--output",
                            "--private-key",
                            "--private-key-env",
                            "--minimum-sequence",
                            "--minimum-validity-hours");
                        SigningResult result = DistributionSigning.SignDirectory(
                            commandLine.Require("--source"),
                            commandLine.Require("--output"),
                            CreatePrivateKeySource(commandLine),
                            CreatePolicy(commandLine));
                        Console.WriteLine($"Signed {result.DocumentCount} document(s) with key '{result.KeyId}'.");
                        return Task.FromResult(0);
                    }
                case "verify":
                    {
                        commandLine.RejectUnknown(
                            "--source",
                            "--signatures",
                            "--public-key",
                            "--minimum-sequence",
                            "--minimum-validity-hours");
                        SigningResult result = DistributionSigning.VerifyDirectory(
                            commandLine.Require("--source"),
                            commandLine.Require("--signatures"),
                            commandLine.Require("--public-key"),
                            CreatePolicy(commandLine));
                        Console.WriteLine($"Verified {result.DocumentCount} document(s) with key '{result.KeyId}'.");
                        return Task.FromResult(0);
                    }
                case "activate":
                    {
                        commandLine.RejectUnknown("--root", "--snapshot");
                        SnapshotIndex index = SnapshotPublisher.Activate(
                            commandLine.Require("--root"),
                            commandLine.Require("--snapshot"));
                        Console.WriteLine($"Activated snapshot '{index.SnapshotId}'.");
                        return Task.FromResult(0);
                    }
                case "validate":
                    {
                        commandLine.RejectUnknown("--root");
                        SnapshotIndex index = SnapshotValidator.ValidateCurrent(commandLine.Require("--root"));
                        Console.WriteLine($"Validated active snapshot '{index.SnapshotId}'.");
                        return Task.FromResult(0);
                    }
                case "help":
                    PrintUsage();
                    return Task.FromResult(0);
                default:
                    throw new ArgumentException($"Unknown command '{commandLine.Command}'.");
            }
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"Distribution publishing failed: {exception.Message}");
            return Task.FromResult(2);
        }
    }

    private static SigningKeySource CreatePrivateKeySource(CommandLine commandLine)
    {
        string? file = commandLine.Optional("--private-key");
        string? environment = commandLine.Optional("--private-key-env");
        if ((file is null) == (environment is null))
        {
            throw new ArgumentException(
                "Provide exactly one of '--private-key <path>' or '--private-key-env <environment-variable-name>'.");
        }

        return file is not null ? SigningKeySource.FromFile(file) : SigningKeySource.FromEnvironment(environment!);
    }

    private static SigningPolicy CreatePolicy(CommandLine commandLine)
    {
        long minimumSequence = ParseLong(commandLine.Optional("--minimum-sequence"), 1, "--minimum-sequence");
        double validityHours = ParseDouble(
            commandLine.Optional("--minimum-validity-hours"),
            24.0,
            "--minimum-validity-hours");
        if (minimumSequence < 1 || validityHours < 0.0 || validityHours > TimeSpan.FromDays(3650).TotalHours)
        {
            throw new ArgumentException("Signing policy options are outside their allowed ranges.");
        }

        return new SigningPolicy
        {
            MinimumSequence = minimumSequence,
            MinimumRemainingValidity = TimeSpan.FromHours(validityHours),
            Now = DateTimeOffset.UtcNow,
        };
    }

    private static long ParseLong(string? value, long defaultValue, string option)
    {
        if (value is null)
        {
            return defaultValue;
        }

        return long.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out long parsed)
            ? parsed
            : throw new ArgumentException($"Option '{option}' must be an integer.");
    }

    private static double ParseDouble(string? value, double defaultValue, string option)
    {
        if (value is null)
        {
            return defaultValue;
        }

        return double.TryParse(value, NumberStyles.AllowDecimalPoint, CultureInfo.InvariantCulture, out double parsed) &&
            double.IsFinite(parsed)
            ? parsed
            : throw new ArgumentException($"Option '{option}' must be a finite number.");
    }

    private static void PrintUsage()
    {
        Console.WriteLine("""
            Kéire Distribution Publisher

            generate-key  --private-key <external.pem> --public-key <trusted-key.json>
            derive-public (--private-key <external.pem> | --private-key-env <name>) --public-key <trusted-key.json>
            sign          --source <directory> --output <signatures.json>
                          (--private-key <external.pem> | --private-key-env <name>)
                          [--minimum-sequence <n>] [--minimum-validity-hours <hours>]
            verify        --source <directory> --signatures <signatures.json> --public-key <trusted-key.json>
                          [--minimum-sequence <n>] [--minimum-validity-hours <hours>]
            publish       --source <directory> --root <directory> --snapshot <id>
                          --public-key <trusted-key.json> [--activate]
                          [--minimum-sequence <n>] [--minimum-validity-hours <hours>]
            activate      --root <directory> --snapshot <id>
            validate      --root <directory>

            The source must contain catalogs/, content/, packages/, and signatures.json.
            Private key values are accepted only through a protected external file or an explicitly named
            process environment variable. The online distribution service never receives a private key.
            """);
    }
}
