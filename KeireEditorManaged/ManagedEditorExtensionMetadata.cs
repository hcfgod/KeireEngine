using System.Reflection;
using System.Runtime.Loader;
using System.Text;
using System.Text.Json;

namespace Keire.Editor;

internal static class ManagedEditorExtensionMetadata
{
    private const int MaximumAllowlistBytes = 16 * 1024 * 1024;
    private const int MaximumAllowedTypes = 65_536;
    private static EditorExtensionGeneration? s_pending;
    private static EditorExtensionGeneration? s_current;

    private sealed class ExportRequest
    {
        public int SchemaVersion { get; init; }
        public List<AllowedAssembly> Assemblies { get; init; } = [];
    }

    private sealed class AllowedAssembly
    {
        public string Name { get; init; } = string.Empty;
        public List<string> Types { get; init; } = [];
    }

    private sealed record ExportDescriptor(string Id, int Kind, string ExtensionType, string AssemblyName);

    internal static string Export(string requestJson)
    {
        EditorExtensionCatalog catalog = EditorExtensionCatalog.Discover(1, ResolveAllowedTypes(requestJson));
        return Serialize(catalog);
    }

    internal static string Stage(string requestJson, ulong generation)
    {
        if (generation == 0 || s_pending is not null)
            throw new InvalidOperationException("An Editor extension candidate is already staged or has an invalid generation.");
        EditorExtensionGeneration candidate = EditorExtensionGeneration.Stage(generation,
            ResolveAllowedTypes(requestJson));
        s_pending = candidate;
        return Serialize(candidate.Catalog);
    }

    internal static bool Commit(ulong generation)
    {
        if (s_pending?.Generation != generation || s_current is not null)
            return false;
        s_current = s_pending;
        s_pending = null;
        ReportApplicationFailures(s_current, "reload", EditorApplication.PublishReloaded());
        return true;
    }

    internal static bool Cancel(ulong generation)
    {
        if (s_pending?.Generation != generation)
            return false;
        EditorExtensionGeneration candidate = s_pending;
        s_pending = null;
        candidate.Dispose();
        return true;
    }

    internal static bool Update(ulong generation)
    {
        if (s_current?.Generation != generation)
            return false;
        foreach (EditorExtensionDescriptor descriptor in s_current.Catalog.Extensions.Where(value =>
                     value.Kind == EditorExtensionKind.EditorWindow))
        {
            _ = s_current.Invoke(descriptor.Id, "update", instance => ((EditorWindow)instance).Update());
        }
        ReportApplicationFailures(s_current, "update", EditorApplication.PublishUpdate());
        return true;
    }

    internal static bool Shutdown(ulong generation)
    {
        if (s_current?.Generation != generation)
            return false;
        EditorExtensionGeneration current = s_current;
        s_current = null;
        ReportApplicationFailures(current, "reload", EditorApplication.PublishReloading());
        current.Dispose();
        return true;
    }

    private static void ReportApplicationFailures(EditorExtensionGeneration generation, string phase,
                                                   IReadOnlyList<Exception> failures)
    {
        foreach (Exception failure in failures)
        {
            generation.ReportDiagnostic("KEIRE-EDITOR-EXTENSION-0004", $"EditorApplication.{phase}",
                $"An EditorApplication {phase} subscriber failed and was isolated. {failure.Message}");
        }
    }

    private static IReadOnlyList<Type> ResolveAllowedTypes(string requestJson)
    {
        if (string.IsNullOrWhiteSpace(requestJson) || Encoding.UTF8.GetByteCount(requestJson) > MaximumAllowlistBytes)
            throw new InvalidOperationException("Editor extension metadata requires a bounded exact type allowlist.");
        ExportRequest request = JsonSerializer.Deserialize<ExportRequest>(requestJson,
            new JsonSerializerOptions { PropertyNameCaseInsensitive = true }) ??
            throw new InvalidOperationException("Editor extension metadata received a malformed allowlist.");
        if (request.SchemaVersion != 1)
            throw new InvalidOperationException("Editor extension metadata received an unsupported schema.");

        AssemblyLoadContext context = AssemblyLoadContext.GetLoadContext(typeof(ManagedEditorExtensionMetadata).Assembly) ??
                                      AssemblyLoadContext.Default;
        Dictionary<string, Assembly[]> assemblies = context.Assemblies.Where(value => !value.IsDynamic)
            .Where(value => !string.IsNullOrWhiteSpace(value.GetName().Name))
            .GroupBy(value => value.GetName().Name!, StringComparer.Ordinal)
            .ToDictionary(value => value.Key, value => value.ToArray(), StringComparer.Ordinal);
        var allowedTypes = new List<Type>();
        var seenAssemblies = new HashSet<string>(StringComparer.Ordinal);
        foreach (AllowedAssembly allowedAssembly in request.Assemblies.OrderBy(value => value.Name,
                     StringComparer.Ordinal))
        {
            if (string.IsNullOrWhiteSpace(allowedAssembly.Name) || !seenAssemblies.Add(allowedAssembly.Name) ||
                !assemblies.TryGetValue(allowedAssembly.Name, out Assembly[]? matches) || matches.Length != 1)
            {
                throw new InvalidOperationException(
                    $"Editor extension candidate assembly '{allowedAssembly.Name}' is unavailable or ambiguous.");
            }
            Dictionary<string, Type[]> types = SafeTypes(matches[0]).Where(value => value.FullName is not null)
                .GroupBy(value => value.FullName!, StringComparer.Ordinal)
                .ToDictionary(value => value.Key, value => value.ToArray(), StringComparer.Ordinal);
            var seenNames = new HashSet<string>(StringComparer.Ordinal);
            foreach (string name in allowedAssembly.Types.Order(StringComparer.Ordinal))
            {
                if (string.IsNullOrWhiteSpace(name) || !seenNames.Add(name) ||
                    !types.TryGetValue(name, out Type[]? typeMatches) || typeMatches.Length != 1)
                {
                    throw new InvalidOperationException(
                        $"Editor extension candidate type '{name}' in '{allowedAssembly.Name}' is unavailable or ambiguous.");
                }
                allowedTypes.Add(typeMatches[0]);
                if (allowedTypes.Count > MaximumAllowedTypes)
                    throw new InvalidOperationException("Editor extension allowlists cannot exceed 65,536 types.");
            }
        }
        return allowedTypes;
    }

    private static string Serialize(EditorExtensionCatalog catalog)
    {
        ExportDescriptor[] descriptors = catalog.Extensions.Select(value => new ExportDescriptor(
            value.Id.ToString("D"), (int)value.Kind, value.ExtensionType.FullName ?? value.ExtensionType.Name,
            value.AssemblyName)).ToArray();
        return JsonSerializer.Serialize(new { schemaVersion = 1, extensions = descriptors },
            new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.CamelCase });
    }

    private static Type[] SafeTypes(Assembly assembly)
    {
        try
        {
            return assembly.GetTypes();
        }
        catch (ReflectionTypeLoadException exception)
        {
            return exception.Types.Where(value => value is not null).Cast<Type>().ToArray();
        }
    }
}
