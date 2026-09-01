namespace Keire.Editor;

public enum BuildTargetPlatform : byte
{
    Windows,
    Linux,
    MacOS
}

public sealed record BuildDescription(string ProductName, string Version, BuildTargetPlatform Platform,
                                      string Configuration, string StagingDirectory,
                                      IReadOnlyList<string> Scenes);

public interface IBuildProcessor
{
    int Order { get; }
}

public interface IPreprocessBuild : IBuildProcessor
{
    void OnPreprocessBuild(BuildContext context);
}

public interface IPostprocessBuild : IBuildProcessor
{
    void OnPostprocessBuild(BuildContext context);
}

public sealed class BuildContext
{
    private readonly Action<string, ReadOnlyMemory<byte>> _writer;
    private readonly List<AssetImportDiagnostic> _diagnostics = [];

    public BuildContext(BuildDescription description, CancellationToken cancellationToken,
                        Action<string, ReadOnlyMemory<byte>> writer)
    {
        Description = description ?? throw new ArgumentNullException(nameof(description));
        CancellationToken = cancellationToken;
        _writer = writer ?? throw new ArgumentNullException(nameof(writer));
    }

    public BuildDescription Description { get; }
    public CancellationToken CancellationToken { get; }
    public IReadOnlyList<AssetImportDiagnostic> Diagnostics => _diagnostics;

    public void WriteStagedFile(string relativePath, ReadOnlyMemory<byte> bytes)
    {
        CancellationToken.ThrowIfCancellationRequested();
        string path = Normalize(relativePath);
        _writer(path, bytes);
    }

    public void LogWarning(string code, string message) =>
        _diagnostics.Add(new AssetImportDiagnostic(AssetImportDiagnosticSeverity.Warning, code, message));

    public void LogError(string code, string message) =>
        _diagnostics.Add(new AssetImportDiagnostic(AssetImportDiagnosticSeverity.Error, code, message));

    private static string Normalize(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        string result = path.Replace('\\', '/').Trim();
        if (Path.IsPathRooted(result) || result.StartsWith("/", StringComparison.Ordinal) ||
            result.Split('/').Any(segment => segment is "" or "." or ".."))
        {
            throw new ArgumentException("Build output paths must remain relative to the staging directory.",
                                        nameof(path));
        }
        return result;
    }
}
