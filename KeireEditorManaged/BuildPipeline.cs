using System.Collections.ObjectModel;

namespace Keire.Editor;

public sealed record BuildExtensionResult(IReadOnlyDictionary<string, ReadOnlyMemory<byte>> Files,
                                          IReadOnlyList<AssetImportDiagnostic> Diagnostics);

public static class BuildExtensionPipeline
{
    public static BuildExtensionResult Execute(
        BuildDescription description,
        IEnumerable<IBuildProcessor> processors,
        CancellationToken cancellationToken,
        Action<IReadOnlyDictionary<string, ReadOnlyMemory<byte>>> packageValidator,
        Action<IReadOnlyDictionary<string, ReadOnlyMemory<byte>>> publisher)
    {
        ArgumentNullException.ThrowIfNull(description);
        ArgumentNullException.ThrowIfNull(processors);
        ArgumentNullException.ThrowIfNull(packageValidator);
        ArgumentNullException.ThrowIfNull(publisher);
        var staged = new SortedDictionary<string, ReadOnlyMemory<byte>>(StringComparer.Ordinal);
        var diagnostics = new List<AssetImportDiagnostic>();
        IBuildProcessor[] ordered = processors.Distinct()
            .OrderBy(processor => processor.Order)
            .ThenBy(processor => processor.GetType().Assembly.GetName().Name, StringComparer.Ordinal)
            .ThenBy(processor => processor.GetType().FullName, StringComparer.Ordinal)
            .ToArray();
        var context = new BuildContext(description, cancellationToken,
            (path, bytes) => staged[path] = bytes.ToArray());
        foreach (IPreprocessBuild processor in ordered.OfType<IPreprocessBuild>())
        {
            cancellationToken.ThrowIfCancellationRequested();
            processor.OnPreprocessBuild(context);
        }
        foreach (IPostprocessBuild processor in ordered.OfType<IPostprocessBuild>())
        {
            cancellationToken.ThrowIfCancellationRequested();
            processor.OnPostprocessBuild(context);
        }
        diagnostics.AddRange(context.Diagnostics);
        if (diagnostics.Any(value => value.Severity == AssetImportDiagnosticSeverity.Error))
            throw new InvalidOperationException("A managed build processor reported an error; staging was discarded.");
        var snapshot = new ReadOnlyDictionary<string, ReadOnlyMemory<byte>>(staged);
        packageValidator(snapshot);
        cancellationToken.ThrowIfCancellationRequested();
        publisher(snapshot);
        return new BuildExtensionResult(snapshot, diagnostics.AsReadOnly());
    }
}
