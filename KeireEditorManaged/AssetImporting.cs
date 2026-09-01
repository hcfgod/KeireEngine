using System.Collections.ObjectModel;
using System.Text;

namespace Keire.Editor;

[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class ScriptedImporterAttribute : Attribute
{
    public ScriptedImporterAttribute(uint version, params string[] extensions)
    {
        if (version == 0)
            throw new ArgumentOutOfRangeException(nameof(version));
        ArgumentNullException.ThrowIfNull(extensions);
        if (extensions.Length == 0 || extensions.Length > 64)
            throw new ArgumentOutOfRangeException(nameof(extensions), "Scripted importers require 1..64 extensions.");
        Version = version;
        Extensions = extensions.Select(NormalizeExtension).Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        if (Extensions.Count != extensions.Length)
            throw new ArgumentException("Scripted importer extensions must be unique.", nameof(extensions));
    }

    public uint Version { get; }
    public IReadOnlyList<string> Extensions { get; }
    public int QueuePriority { get; init; }
    public bool AllowCaching { get; init; } = true;

    private static string NormalizeExtension(string extension)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(extension);
        string result = extension.Trim().TrimStart('.').ToLowerInvariant();
        if (result.Length is 0 or > 32 || result.Any(character =>
                !char.IsAsciiLetterOrDigit(character) && character is not '_' and not '-'))
        {
            throw new ArgumentException($"Scripted importer extension '{extension}' is invalid.", nameof(extension));
        }
        return result;
    }
}

public enum ImportTargetPlatform : byte
{
    Windows,
    Linux,
    MacOS
}

public enum AssetImportDiagnosticSeverity : byte
{
    Information,
    Warning,
    Error
}

public sealed record AssetImportDiagnostic(AssetImportDiagnosticSeverity Severity, string Code, string Message,
                                            uint Line = 0, uint Column = 0);

public enum AssetImportArtifactKind : byte
{
    ManagedData,
    Text,
    Binary,
    Texture,
    Mesh,
    Material,
    Audio,
    Animation,
    Prefab
}

public abstract record AssetImportArtifact(AssetImportArtifactKind Kind)
{
    public string Name { get; init; } = string.Empty;
}

public sealed record ManagedDataImportArtifact(ScriptableObject Value)
    : AssetImportArtifact(AssetImportArtifactKind.ManagedData);

public sealed record TextImportArtifact(string Text) : AssetImportArtifact(AssetImportArtifactKind.Text);

public sealed record BinaryImportArtifact(ReadOnlyMemory<byte> Bytes)
    : AssetImportArtifact(AssetImportArtifactKind.Binary);

public sealed record TextureImportArtifact(uint Width, uint Height, ReadOnlyMemory<byte> Rgba8, bool Srgb = true)
    : AssetImportArtifact(AssetImportArtifactKind.Texture);

public sealed record MeshImportArtifact(IReadOnlyList<Vector3> Positions, IReadOnlyList<Vector3> Normals,
                                        IReadOnlyList<Vector2> TextureCoordinates, IReadOnlyList<uint> Indices)
    : AssetImportArtifact(AssetImportArtifactKind.Mesh);

public sealed record MaterialImportArtifact(string Shader, IReadOnlyDictionary<string, object?> Parameters)
    : AssetImportArtifact(AssetImportArtifactKind.Material);

public sealed record AudioImportArtifact(uint SampleRate, ushort Channels, ReadOnlyMemory<float> Samples)
    : AssetImportArtifact(AssetImportArtifactKind.Audio);

public sealed record AnimationKeyframe(float Time, Vector3 Position, Quaternion Rotation, Vector3 Scale);

public sealed record AnimationImportArtifact(float Duration,
                                              IReadOnlyDictionary<string, IReadOnlyList<AnimationKeyframe>> Tracks)
    : AssetImportArtifact(AssetImportArtifactKind.Animation);

public sealed record PrefabImportArtifact(string CanonicalSceneDocument)
    : AssetImportArtifact(AssetImportArtifactKind.Prefab);

public sealed class AssetImportContext
{
    private const int MaximumDiagnostics = 4_096;
    private const int MaximumOutputs = 65_536;
    private readonly Func<string, ReadOnlyMemory<byte>> _sourceReader;
    private readonly List<string> _sourceDependencies = [];
    private readonly List<AssetId> _assetDependencies = [];
    private readonly List<AssetImportDiagnostic> _diagnostics = [];
    private readonly SortedDictionary<string, AssetImportArtifact> _outputs = new(StringComparer.Ordinal);
    private string? _mainOutput;

    public AssetImportContext(AssetId asset, string assetPath, ImportTargetPlatform targetPlatform,
                              CancellationToken cancellationToken,
                              Func<string, ReadOnlyMemory<byte>> sourceReader)
    {
        if (!asset.IsValid)
            throw new ArgumentException("Import contexts require a valid asset ID.", nameof(asset));
        Asset = asset;
        AssetPath = NormalizeProjectPath(assetPath);
        TargetPlatform = targetPlatform;
        CancellationToken = cancellationToken;
        _sourceReader = sourceReader ?? throw new ArgumentNullException(nameof(sourceReader));
    }

    public AssetId Asset { get; }
    public string AssetPath { get; }
    public ImportTargetPlatform TargetPlatform { get; }
    public CancellationToken CancellationToken { get; }
    public IReadOnlyList<string> SourceDependencies => _sourceDependencies.AsReadOnly();
    public IReadOnlyList<AssetId> AssetDependencies => _assetDependencies.AsReadOnly();
    public IReadOnlyList<AssetImportDiagnostic> Diagnostics => _diagnostics.AsReadOnly();
    public IReadOnlyDictionary<string, AssetImportArtifact> Outputs =>
        new ReadOnlyDictionary<string, AssetImportArtifact>(_outputs);
    public string? MainOutput => _mainOutput;

    public ReadOnlyMemory<byte> ReadSourceBytes(string projectRelativePath)
    {
        CancellationToken.ThrowIfCancellationRequested();
        string path = NormalizeProjectPath(projectRelativePath);
        ReadOnlyMemory<byte> result = _sourceReader(path);
        if (result.Length > 64 * 1024 * 1024)
            throw new InvalidOperationException("Managed import dependency reads cannot exceed 64 MiB.");
        return result;
    }

    public string ReadSourceText(string projectRelativePath)
    {
        ReadOnlyMemory<byte> bytes = ReadSourceBytes(projectRelativePath);
        return new UTF8Encoding(false, true).GetString(bytes.Span);
    }

    public void DependsOnSource(string projectRelativePath)
    {
        string path = NormalizeProjectPath(projectRelativePath);
        if (!_sourceDependencies.Contains(path, StringComparer.Ordinal))
            _sourceDependencies.Add(path);
    }

    public void DependsOnAsset(AssetId asset)
    {
        if (!asset.IsValid)
            throw new ArgumentException("Asset dependencies require a valid ID.", nameof(asset));
        if (!_assetDependencies.Contains(asset))
            _assetDependencies.Add(asset);
    }

    public void AddObject(string key, AssetImportArtifact artifact)
    {
        CancellationToken.ThrowIfCancellationRequested();
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
        ArgumentNullException.ThrowIfNull(artifact);
        if (_outputs.Count >= MaximumOutputs)
            throw new InvalidOperationException($"Managed importers cannot emit more than {MaximumOutputs} outputs.");
        string normalized = key.Trim();
        if (Encoding.UTF8.GetByteCount(normalized) > 256)
            throw new ArgumentOutOfRangeException(nameof(key), "Import output keys cannot exceed 256 UTF-8 bytes.");
        ValidateArtifact(artifact);
        if (!_outputs.TryAdd(normalized, artifact))
            throw new InvalidOperationException($"Import output key '{normalized}' is already registered.");
    }

    public void SetMainObject(string key)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
        if (!_outputs.ContainsKey(key))
            throw new InvalidOperationException($"Main import output '{key}' has not been added.");
        _mainOutput = key;
    }

    public AssetId ResolveSubAssetId(string key)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
        byte[] keyBytes = Encoding.UTF8.GetBytes(key);
        if (keyBytes.Length > 256)
            throw new ArgumentOutOfRangeException(nameof(key));
        Span<byte> source = stackalloc byte[16 + keyBytes.Length];
        BitConverter.TryWriteBytes(source, Asset.High);
        BitConverter.TryWriteBytes(source[8..], Asset.Low);
        keyBytes.CopyTo(source[16..]);
        byte[] hash = System.Security.Cryptography.SHA256.HashData(source);
        return new AssetId(BitConverter.ToUInt64(hash, 0), BitConverter.ToUInt64(hash, 8));
    }

    public void LogInformation(string code, string message) =>
        AddDiagnostic(AssetImportDiagnosticSeverity.Information, code, message);
    public void LogWarning(string code, string message) =>
        AddDiagnostic(AssetImportDiagnosticSeverity.Warning, code, message);
    public void LogError(string code, string message) =>
        AddDiagnostic(AssetImportDiagnosticSeverity.Error, code, message);

    private void AddDiagnostic(AssetImportDiagnosticSeverity severity, string code, string message)
    {
        if (_diagnostics.Count >= MaximumDiagnostics)
            throw new InvalidOperationException($"Managed importers cannot emit more than {MaximumDiagnostics} diagnostics.");
        ArgumentException.ThrowIfNullOrWhiteSpace(code);
        ArgumentException.ThrowIfNullOrWhiteSpace(message);
        _diagnostics.Add(new AssetImportDiagnostic(severity, code.Trim(), message.Trim()));
    }

    private static void ValidateArtifact(AssetImportArtifact artifact)
    {
        switch (artifact)
        {
            case ManagedDataImportArtifact { Value: null }:
                throw new InvalidOperationException("Managed-data import outputs cannot be null.");
            case TextImportArtifact text when Encoding.UTF8.GetByteCount(text.Text) > 16 * 1024 * 1024:
                throw new InvalidOperationException("Text import outputs cannot exceed 16 MiB.");
            case BinaryImportArtifact binary when binary.Bytes.Length > 1024 * 1024 * 1024:
                throw new InvalidOperationException("Binary import outputs cannot exceed 1 GiB.");
            case TextureImportArtifact texture when texture.Width == 0 || texture.Height == 0 ||
                                                          texture.Rgba8.Length != checked((long)texture.Width *
                                                              texture.Height * 4):
                throw new InvalidOperationException("Texture import outputs require exact finite RGBA8 dimensions.");
            case MeshImportArtifact mesh when mesh.Positions.Count == 0 || mesh.Indices.Count % 3 != 0 ||
                                              mesh.Indices.Any(index => index >= mesh.Positions.Count):
                throw new InvalidOperationException("Mesh import outputs require valid indexed triangles.");
            case AudioImportArtifact audio when audio.SampleRate is < 8_000 or > 384_000 ||
                                                audio.Channels is 0 or > 16 ||
                                                audio.Samples.Length % audio.Channels != 0:
                throw new InvalidOperationException("Audio import outputs require supported sample rates and channels.");
            case AnimationImportArtifact animation when !float.IsFinite(animation.Duration) || animation.Duration < 0:
                throw new InvalidOperationException("Animation import outputs require a finite non-negative duration.");
            case PrefabImportArtifact prefab when string.IsNullOrWhiteSpace(prefab.CanonicalSceneDocument):
                throw new InvalidOperationException("Prefab import outputs require a canonical scene document.");
        }
    }

    private static string NormalizeProjectPath(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        string value = path.Replace('\\', '/').Trim();
        if (value.StartsWith("/", StringComparison.Ordinal) || Path.IsPathRooted(value) ||
            value.Split('/').Any(segment => segment is "" or "." or ".."))
        {
            throw new ArgumentException("Asset paths must be normalized project-relative paths.", nameof(path));
        }
        return value;
    }
}

public abstract class ScriptedImporter : EditorExtension
{
    public abstract void OnImportAsset(AssetImportContext context);
}

public sealed record AssetChangeBatch(IReadOnlyList<string> Imported, IReadOnlyList<string> Deleted,
                                      IReadOnlyList<string> Moved, IReadOnlyList<string> MovedFrom,
                                      bool ReloadedExtensions);

public abstract class AssetPostprocessor : EditorExtension
{
    public virtual void OnPostprocessAssets(AssetChangeBatch changes) { }
}

public abstract class AssetImporterEditor : Editor;
