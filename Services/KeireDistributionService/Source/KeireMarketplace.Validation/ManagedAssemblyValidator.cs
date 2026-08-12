using System.Security;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Keire.Distribution;

namespace Keire.Marketplace.Validation;

internal sealed record ManagedValidationResult(
    string Status,
    string? CodeFingerprintSha256,
    IReadOnlyList<ValidationDiagnostic> Diagnostics);

internal static partial class ManagedAssemblyValidator
{
    private const string RequiredDotnetSdkVersion = "10.0.302";

    public static async Task<ManagedValidationResult> ValidateAsync(
        string stagingRoot,
        string buildRoot,
        ExtractedPackageDocument manifest,
        string dotnetPath,
        string? managedApiPath,
        CancellationToken cancellationToken)
    {
        List<ValidationDiagnostic> diagnostics = [];
        IReadOnlyList<string> allFiles = FileSystemSafety.EnumerateRegularFiles(
            stagingRoot,
            MarketplaceValidationContract.MaximumExtractedEntries,
            cancellationToken);
        List<string> csharpFiles = allFiles.Where(path => path.EndsWith(".cs", StringComparison.OrdinalIgnoreCase)).ToList();
        if (manifest.ManagedAssemblies.Count == 0)
        {
            if (csharpFiles.Count != 0)
            {
                diagnostics.Add(Error(
                    "UNDECLARED_MANAGED_CODE",
                    "C# source is present but the package declares no managed assembly.",
                    csharpFiles[0]));
                return new ManagedValidationResult(ValidationStatuses.Failed, null, diagnostics);
            }

            return new ManagedValidationResult(ValidationStatuses.NotApplicable, null, diagnostics);
        }

        if (string.IsNullOrWhiteSpace(managedApiPath) || !File.Exists(managedApiPath))
        {
            diagnostics.Add(Error(
                "MANAGED_API_MISSING",
                "Managed validation requires the pinned Kéire managed API assembly."));
            return new ManagedValidationResult(ValidationStatuses.Failed, null, diagnostics);
        }

        FileInfo managedApi = new(Path.GetFullPath(managedApiPath));
        FileSystemSafety.RejectLink(managedApi);
        List<ValidatedAssembly> assemblies = [];
        Dictionary<string, string> assetByDefinition = new(StringComparer.Ordinal);
        foreach (PackageAsset asset in manifest.Assets)
        {
            string source = DistributionPaths.NormalizeRelativePath(asset.Source);
            assetByDefinition.TryAdd(source, NormalizeAssetId(asset.Id));
        }

        HashSet<string> names = new(StringComparer.Ordinal);
        HashSet<string> definitionPaths = new(StringComparer.Ordinal);
        foreach (PackageManagedAssembly declared in manifest.ManagedAssemblies)
        {
            cancellationToken.ThrowIfCancellationRequested();
            try
            {
                string definitionPath = DistributionPaths.NormalizeRelativePath(declared.Definition);
                if (!definitionPaths.Add(definitionPath))
                {
                    throw new InvalidDataException("A managed assembly definition is declared more than once.");
                }

                if (!assetByDefinition.TryGetValue(definitionPath, out string? assetId))
                {
                    throw new InvalidDataException("A managed assembly definition is not represented by a manifest asset.");
                }

                string fullDefinitionPath = DistributionPaths.ResolveConfined(stagingRoot, definitionPath);
                FileInfo definitionFile = new(fullDefinitionPath);
                if (!definitionFile.Exists || definitionFile.Length <= 0 ||
                    definitionFile.Length > MarketplaceValidationContract.MaximumManagedDefinitionBytes)
                {
                    throw new InvalidDataException("A managed assembly definition is missing or exceeds its size limit.");
                }

                byte[] bytes = await File.ReadAllBytesAsync(fullDefinitionPath, cancellationToken);
                ManagedAssemblyDefinitionDocument definition = DecodeDefinition(bytes);
                ValidateDefinition(declared, definition);
                if (!names.Add(definition.Name))
                {
                    throw new InvalidDataException("Managed assembly names must be unique.");
                }

                List<string> roots = definition.SourceRoots.Select(DistributionPaths.NormalizeRelativePath).ToList();
                List<string> sources = FindSources(csharpFiles, roots);
                if (sources.Count == 0)
                {
                    throw new InvalidDataException("A managed assembly contains no C# source files.");
                }

                assemblies.Add(new ValidatedAssembly(
                    assetId,
                    definitionPath,
                    definition,
                    roots,
                    sources,
                    (definition.References ?? []).Select(NormalizeAssetId).ToList()));
            }
            catch (Exception exception) when (exception is ArgumentException or InvalidDataException or IOException)
            {
                diagnostics.Add(Error(
                    "MANAGED_DEFINITION_INVALID",
                    exception.Message,
                    SafeRelativePath(declared.Definition)));
            }
        }

        if (diagnostics.Count != 0)
        {
            return new ManagedValidationResult(ValidationStatuses.Failed, null, diagnostics);
        }

        ValidateGraphAndCoverage(assemblies, csharpFiles, diagnostics);
        if (diagnostics.Count != 0)
        {
            return new ManagedValidationResult(ValidationStatuses.Failed, null, diagnostics);
        }

        string fingerprint = await ComputeCodeFingerprintAsync(stagingRoot, assemblies, cancellationToken);
        try
        {
            await CompileAsync(
                stagingRoot,
                buildRoot,
                assemblies,
                Path.GetFullPath(dotnetPath),
                managedApi.FullName,
                cancellationToken);
        }
        catch (Exception exception) when (exception is InvalidDataException or IOException or TimeoutException)
        {
            diagnostics.Add(Error("MANAGED_COMPILATION_FAILED", exception.Message));
        }

        return new ManagedValidationResult(
            diagnostics.Count == 0 ? ValidationStatuses.Passed : ValidationStatuses.Failed,
            fingerprint,
            diagnostics);
    }

    private static void ValidateDefinition(PackageManagedAssembly declared, ManagedAssemblyDefinitionDocument definition)
    {
        if ((object?)definition.Name is null || (object?)definition.SourceRoots is null)
        {
            throw new InvalidDataException("A managed assembly definition is missing a required value.");
        }

        if (definition.SchemaVersion is not (1 or 2))
        {
            throw new InvalidDataException("The managed assembly schema version is unsupported.");
        }

        string rootNamespace = definition.RootNamespace ?? definition.Name;
        string classification = definition.Classification ?? "runtime";
        if (!IdentifierRegex().IsMatch(definition.Name) || !NamespaceRegex().IsMatch(rootNamespace) ||
            !string.Equals(definition.Name, declared.Name, StringComparison.Ordinal))
        {
            throw new InvalidDataException("The managed assembly name or root namespace is invalid.");
        }

        string expectedScope = classification switch
        {
            "runtime" => "runtime",
            "editor" => "editor",
            "tests" => "test",
            _ => throw new InvalidDataException("The managed assembly classification is invalid."),
        };
        if (!string.Equals(declared.Scope, expectedScope, StringComparison.Ordinal))
        {
            throw new InvalidDataException("The package assembly scope disagrees with its definition.");
        }

        if (definition.SourceRoots.Count is < 1 or > 64 ||
            (definition.References?.Count ?? 0) > 256 ||
            (definition.Packages?.Count ?? 0) > 256 ||
            (definition.DefineSymbols?.Count ?? 0) > 256)
        {
            throw new InvalidDataException("The managed assembly exceeds a collection limit.");
        }

        if (definition.SchemaVersion == 1 &&
            ((definition.Packages?.Count ?? 0) != 0 || (definition.DefineSymbols?.Count ?? 0) != 0 ||
             definition.AllowUnsafe == true))
        {
            throw new InvalidDataException("Schema-1 managed assemblies may not use schema-2 settings.");
        }

        if ((definition.Packages?.Count ?? 0) != 0)
        {
            throw new InvalidDataException(
                "Marketplace managed assemblies may not restore publisher-selected NuGet packages in validator 0.3.1.");
        }

        if (definition.AllowUnsafe == true)
        {
            throw new InvalidDataException("Unsafe managed code is not accepted by the marketplace validation policy.");
        }

        if ((definition.DefineSymbols ?? []).Any(symbol => !IdentifierRegex().IsMatch(symbol)) ||
            (definition.DefineSymbols ?? []).Distinct(StringComparer.Ordinal).Count() != (definition.DefineSymbols?.Count ?? 0))
        {
            throw new InvalidDataException("Managed define symbols must be unique C# identifiers.");
        }
    }

    private static ManagedAssemblyDefinitionDocument DecodeDefinition(byte[] bytes)
    {
        using JsonDocument document = DistributionJson.ParseStrict(bytes, 24);
        if (document.RootElement.ValueKind != JsonValueKind.Object)
        {
            throw new InvalidDataException("The managed assembly definition must be a JSON object.");
        }

        foreach (string propertyName in new[]
                 {
                     "schemaVersion",
                     "name",
                     "rootNamespace",
                     "classification",
                     "sourceRoots",
                     "references",
                     "packages",
                     "defineSymbols",
                     "allowUnsafe",
                 })
        {
            if (document.RootElement.TryGetProperty(propertyName, out JsonElement value) && value.ValueKind == JsonValueKind.Null)
            {
                throw new InvalidDataException($"Managed assembly property '{propertyName}' may not be null.");
            }
        }

        return JsonSerializer.Deserialize<ManagedAssemblyDefinitionDocument>(document.RootElement, DistributionJson.Options)
            ?? throw new InvalidDataException("The managed assembly definition is empty.");
    }

    private static List<string> FindSources(IReadOnlyList<string> csharpFiles, IReadOnlyList<string> roots)
    {
        HashSet<string> uniqueRoots = new(StringComparer.Ordinal);
        foreach (string root in roots)
        {
            if (!uniqueRoots.Add(root))
            {
                throw new InvalidDataException("Managed assembly source roots must be unique.");
            }
        }

        return csharpFiles.Where(path => roots.Any(root => IsUnder(path, root))).ToList();
    }

    private static void ValidateGraphAndCoverage(
        IReadOnlyList<ValidatedAssembly> assemblies,
        IReadOnlyList<string> csharpFiles,
        List<ValidationDiagnostic> diagnostics)
    {
        Dictionary<string, ValidatedAssembly> byId = assemblies.ToDictionary(assembly => assembly.AssetId, StringComparer.Ordinal);
        foreach (ValidatedAssembly assembly in assemblies)
        {
            if (assembly.References.Count != assembly.References.Distinct(StringComparer.Ordinal).Count())
            {
                diagnostics.Add(Error("MANAGED_REFERENCE_DUPLICATE", "Managed assembly references must be unique.", assembly.DefinitionPath));
            }

            foreach (string reference in assembly.References)
            {
                if (!byId.TryGetValue(reference, out ValidatedAssembly? target))
                {
                    diagnostics.Add(Error(
                        "MANAGED_REFERENCE_MISSING",
                        "A managed assembly references a definition outside this validated package.",
                        assembly.DefinitionPath));
                    continue;
                }

                string sourceClass = assembly.Definition.Classification ?? "runtime";
                string targetClass = target.Definition.Classification ?? "runtime";
                if ((sourceClass == "runtime" && targetClass != "runtime") ||
                    (sourceClass == "editor" && targetClass == "tests"))
                {
                    diagnostics.Add(Error(
                        "MANAGED_SCOPE_VIOLATION",
                        "A managed reference violates runtime/editor/test isolation.",
                        assembly.DefinitionPath));
                }
            }
        }

        foreach (string source in csharpFiles)
        {
            int owners = assemblies.Count(assembly => assembly.Sources.Contains(source, StringComparer.Ordinal));
            if (owners != 1)
            {
                diagnostics.Add(Error(
                    owners == 0 ? "UNDECLARED_MANAGED_CODE" : "AMBIGUOUS_MANAGED_CODE",
                    owners == 0
                        ? "A C# source file is outside every declared assembly root."
                        : "A C# source file belongs to more than one declared assembly root.",
                    source));
            }
        }

        Dictionary<string, int> state = new(StringComparer.Ordinal);
        foreach (ValidatedAssembly assembly in assemblies)
        {
            Visit(assembly);
        }

        void Visit(ValidatedAssembly assembly)
        {
            int current = state.GetValueOrDefault(assembly.AssetId);
            if (current == 1)
            {
                diagnostics.Add(Error(
                    "MANAGED_REFERENCE_CYCLE",
                    "The managed assembly graph contains a dependency cycle.",
                    assembly.DefinitionPath));
                return;
            }

            if (current == 2)
            {
                return;
            }

            state[assembly.AssetId] = 1;
            foreach (string reference in assembly.References)
            {
                if (byId.TryGetValue(reference, out ValidatedAssembly? target))
                {
                    Visit(target);
                }
            }

            state[assembly.AssetId] = 2;
        }
    }

    private static async Task CompileAsync(
        string stagingRoot,
        string buildRoot,
        IReadOnlyList<ValidatedAssembly> assemblies,
        string dotnetPath,
        string managedApiPath,
        CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(buildRoot);
        FileSystemSafety.RejectLink(new DirectoryInfo(buildRoot));
        string emptyFeed = Path.Combine(buildRoot, "empty-feed");
        string packages = Path.Combine(buildRoot, "packages");
        Directory.CreateDirectory(emptyFeed);
        Directory.CreateDirectory(packages);
        await File.WriteAllTextAsync(
            Path.Combine(buildRoot, "global.json"),
            $$"""
            {
              "sdk": {
                "version": "{{RequiredDotnetSdkVersion}}",
                "rollForward": "disable",
                "allowPrerelease": false
              }
            }
            """ + "\n",
            cancellationToken);
        await File.WriteAllTextAsync(
            Path.Combine(buildRoot, "NuGet.Config"),
            "<configuration><packageSources><clear /></packageSources></configuration>\n",
            cancellationToken);
        await File.WriteAllTextAsync(Path.Combine(buildRoot, "Directory.Build.props"), "<Project />\n", cancellationToken);
        await File.WriteAllTextAsync(Path.Combine(buildRoot, "Directory.Build.targets"), "<Project />\n", cancellationToken);

        Dictionary<string, string> projects = assemblies.ToDictionary(
            assembly => assembly.AssetId,
            assembly => Path.Combine(buildRoot, assembly.Definition.Name + ".csproj"),
            StringComparer.Ordinal);
        foreach (ValidatedAssembly assembly in assemblies)
        {
            string project = CreateProject(stagingRoot, assembly, assemblies, projects, managedApiPath, emptyFeed);
            await File.WriteAllTextAsync(projects[assembly.AssetId], project, new UTF8Encoding(false), cancellationToken);
        }

        IReadOnlyDictionary<string, string?> environment = new Dictionary<string, string?>(StringComparer.Ordinal)
        {
            ["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1",
            ["DOTNET_NOLOGO"] = "1",
            ["DOTNET_SKIP_FIRST_TIME_EXPERIENCE"] = "1",
            ["NUGET_PACKAGES"] = packages,
        };
        ProcessResult version = await BoundedProcess.RunAsync(
            dotnetPath,
            ["--version"],
            buildRoot,
            environment,
            TimeSpan.FromSeconds(30),
            cancellationToken);
        if (version.ExitCode != 0 || !string.Equals(version.StandardOutput.Trim(), RequiredDotnetSdkVersion, StringComparison.Ordinal))
        {
            throw new InvalidDataException($"Managed validation requires the pinned .NET SDK {RequiredDotnetSdkVersion}.");
        }

        foreach (ValidatedAssembly assembly in TopologicalOrder(assemblies))
        {
            string project = projects[assembly.AssetId];
            ProcessResult restore = await BoundedProcess.RunAsync(
                dotnetPath,
                ["restore", project, "--configfile", Path.Combine(buildRoot, "NuGet.Config"), "--packages", packages, "--disable-parallel"],
                buildRoot,
                environment,
                TimeSpan.FromMinutes(3),
                cancellationToken);
            if (restore.ExitCode != 0)
            {
                throw new InvalidDataException($"Managed assembly '{assembly.Definition.Name}' could not be restored from the empty offline feed.");
            }

            ProcessResult build = await BoundedProcess.RunAsync(
                dotnetPath,
                ["build", project, "--no-restore", "--configuration", "Release", "--nologo", "--verbosity", "minimal"],
                buildRoot,
                environment,
                TimeSpan.FromMinutes(5),
                cancellationToken);
            if (build.ExitCode != 0)
            {
                throw new InvalidDataException($"Managed assembly '{assembly.Definition.Name}' did not compile under the marketplace policy.");
            }
        }
    }

    private static string CreateProject(
        string stagingRoot,
        ValidatedAssembly assembly,
        IReadOnlyList<ValidatedAssembly> assemblies,
        IReadOnlyDictionary<string, string> projects,
        string managedApiPath,
        string emptyFeed)
    {
        StringBuilder project = new();
        project.AppendLine("<Project Sdk=\"Microsoft.NET.Sdk\">");
        project.AppendLine("  <PropertyGroup>");
        project.AppendLine("    <TargetFramework>net10.0</TargetFramework>");
        project.AppendLine("    <LangVersion>14.0</LangVersion>");
        project.AppendLine("    <ImplicitUsings>enable</ImplicitUsings>");
        project.AppendLine("    <Nullable>enable</Nullable>");
        project.AppendLine("    <Deterministic>true</Deterministic>");
        project.AppendLine("    <ContinuousIntegrationBuild>true</ContinuousIntegrationBuild>");
        project.AppendLine("    <TreatWarningsAsErrors>true</TreatWarningsAsErrors>");
        project.AppendLine("    <AllowUnsafeBlocks>false</AllowUnsafeBlocks>");
        project.AppendLine("    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>");
        project.AppendLine("    <ImportDirectoryBuildProps>false</ImportDirectoryBuildProps>");
        project.AppendLine("    <ImportDirectoryBuildTargets>false</ImportDirectoryBuildTargets>");
        project.AppendLine("    <RunAnalyzers>false</RunAnalyzers>");
        project.AppendLine("    <EnableNETAnalyzers>false</EnableNETAnalyzers>");
        project.AppendLine("    <RestoreAdditionalProjectSources></RestoreAdditionalProjectSources>");
        project.AppendLine($"    <RestoreSources>{Xml(emptyFeed)}</RestoreSources>");
        project.AppendLine($"    <AssemblyName>{Xml(assembly.Definition.Name)}</AssemblyName>");
        project.AppendLine($"    <RootNamespace>{Xml(assembly.Definition.RootNamespace ?? assembly.Definition.Name)}</RootNamespace>");
        if ((assembly.Definition.DefineSymbols?.Count ?? 0) != 0)
        {
            project.AppendLine($"    <DefineConstants>{Xml(string.Join(';', assembly.Definition.DefineSymbols!))}</DefineConstants>");
        }

        project.AppendLine("  </PropertyGroup>");
        project.AppendLine("  <ItemGroup>");
        foreach (string source in assembly.Sources)
        {
            project.AppendLine($"    <Compile Include=\"{Xml(DistributionPaths.ResolveConfined(stagingRoot, source))}\" />");
        }

        project.AppendLine("    <Reference Include=\"Keire.Managed\">");
        project.AppendLine($"      <HintPath>{Xml(managedApiPath)}</HintPath>");
        project.AppendLine("      <Private>false</Private>");
        project.AppendLine("    </Reference>");
        project.AppendLine("  </ItemGroup>");
        if (assembly.References.Count != 0)
        {
            project.AppendLine("  <ItemGroup>");
            foreach (string reference in assembly.References)
            {
                ValidatedAssembly target = assemblies.Single(candidate => candidate.AssetId == reference);
                project.AppendLine($"    <ProjectReference Include=\"{Xml(projects[target.AssetId])}\" />");
            }

            project.AppendLine("  </ItemGroup>");
        }

        project.AppendLine("</Project>");
        return project.ToString();
    }

    private static IReadOnlyList<ValidatedAssembly> TopologicalOrder(IReadOnlyList<ValidatedAssembly> assemblies)
    {
        Dictionary<string, ValidatedAssembly> byId = assemblies.ToDictionary(assembly => assembly.AssetId, StringComparer.Ordinal);
        HashSet<string> visited = new(StringComparer.Ordinal);
        List<ValidatedAssembly> result = [];
        foreach (ValidatedAssembly assembly in assemblies.OrderBy(value => value.Definition.Name, StringComparer.Ordinal))
        {
            Visit(assembly);
        }

        return result;

        void Visit(ValidatedAssembly assembly)
        {
            if (!visited.Add(assembly.AssetId))
            {
                return;
            }

            foreach (string reference in assembly.References)
            {
                Visit(byId[reference]);
            }

            result.Add(assembly);
        }
    }

    private static async Task<string> ComputeCodeFingerprintAsync(
        string stagingRoot,
        IReadOnlyList<ValidatedAssembly> assemblies,
        CancellationToken cancellationToken)
    {
        using IncrementalHash hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        IEnumerable<string> paths = assemblies
            .SelectMany(assembly => assembly.Sources.Append(assembly.DefinitionPath))
            .Distinct(StringComparer.Ordinal)
            .Order(StringComparer.Ordinal);
        foreach (string path in paths)
        {
            hash.AppendData(Encoding.UTF8.GetBytes(path));
            hash.AppendData([0]);
            await using FileStream stream = new(
                DistributionPaths.ResolveConfined(stagingRoot, path),
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read,
                128 * 1024,
                FileOptions.Asynchronous | FileOptions.SequentialScan);
            byte[] buffer = new byte[128 * 1024];
            while (true)
            {
                int count = await stream.ReadAsync(buffer, cancellationToken);
                if (count == 0)
                {
                    break;
                }

                hash.AppendData(buffer.AsSpan(0, count));
            }

            hash.AppendData([0]);
        }

        return Convert.ToHexStringLower(hash.GetHashAndReset());
    }

    private static bool IsUnder(string path, string root)
    {
        return path.Length > root.Length && path.StartsWith(root, StringComparison.Ordinal) && path[root.Length] == '/';
    }

    private static string NormalizeAssetId(string value)
    {
        if (!Guid.TryParseExact(value, "D", out Guid parsed) && !Guid.TryParseExact(value, "N", out parsed))
        {
            throw new InvalidDataException("A managed assembly asset ID is not a canonical 128-bit identifier.");
        }

        if (parsed == Guid.Empty)
        {
            throw new InvalidDataException("A managed assembly asset ID may not be empty.");
        }

        return parsed.ToString("D");
    }

    private static string SafeRelativePath(string value)
    {
        try
        {
            return DistributionPaths.NormalizeRelativePath(value);
        }
        catch (InvalidDataException)
        {
            return "<invalid-path>";
        }
    }

    private static string Xml(string value)
    {
        return SecurityElement.Escape(value) ?? string.Empty;
    }

    private static ValidationDiagnostic Error(string code, string message, string? path = null)
    {
        return new ValidationDiagnostic
        {
            Code = code,
            Severity = ValidationSeverities.Error,
            Message = message,
            Path = path,
        };
    }

    [GeneratedRegex("^[A-Za-z_][A-Za-z0-9_]{0,127}$", RegexOptions.CultureInvariant | RegexOptions.NonBacktracking)]
    private static partial Regex IdentifierRegex();

    [GeneratedRegex(
        "^[A-Za-z_][A-Za-z0-9_]{0,127}(?:\\.[A-Za-z_][A-Za-z0-9_]{0,127})*$",
        RegexOptions.CultureInvariant | RegexOptions.NonBacktracking)]
    private static partial Regex NamespaceRegex();

    private sealed record ValidatedAssembly(
        string AssetId,
        string DefinitionPath,
        ManagedAssemblyDefinitionDocument Definition,
        IReadOnlyList<string> SourceRoots,
        IReadOnlyList<string> Sources,
        IReadOnlyList<string> References);
}
