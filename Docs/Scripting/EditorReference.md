# Editor API Member Reference

[Scripting home](README.md) · [Workflow map](WorkflowMap.md) · [Cookbook](Cookbook.md) · [API index](ApiIndex.md)

These declarations belong to `Keire.Editor.Managed.dll`. Use them in an Editor-classified assembly. Read [Managed Extensibility](ManagedExtensibility.md) for registration, transactions, and current integration limits.

## How To Read This Reference

Search for a type or member name, or select a type below. Each entry links to its defining source. Signatures retain parameter names, defaults, generic constraints, attributes, and accessor visibility. Method bodies and field/property initializers are omitted. Base types supply inherited members; follow their entries when a method is not declared on a derived type. Partial declarations are combined under one entry. Positional record parameters also declare record properties; compiler-generated equality, deconstruction, and implicit constructors are not repeated.

Protected members are subclass extension points. `internal` and `private` members are excluded. Public bridge interfaces, identity records, serialization codecs, and `Runtime*` methods/fields are advanced host/integration surfaces: ordinary scripts use the object and component APIs instead of installing bridges, invoking runtime lifecycle dispatch, or editing diagnostic transport fields.

This is a generated declaration catalog, not a replacement for the workflow guides. Regenerate or check it using [the documented commands](ApiIndex.md#maintaining-the-member-reference).

## Type Directory

- [`Keire.Editor.AnimationImportArtifact`](#keireeditoranimationimportartifact)
- [`Keire.Editor.AnimationKeyframe`](#keireeditoranimationkeyframe)
- [`Keire.Editor.AssetChangeBatch`](#keireeditorassetchangebatch)
- [`Keire.Editor.AssetDatabase`](#keireeditorassetdatabase)
- [`Keire.Editor.AssetImportArtifact`](#keireeditorassetimportartifact)
- [`Keire.Editor.AssetImportArtifactKind`](#keireeditorassetimportartifactkind)
- [`Keire.Editor.AssetImportContext`](#keireeditorassetimportcontext)
- [`Keire.Editor.AssetImportDiagnostic`](#keireeditorassetimportdiagnostic)
- [`Keire.Editor.AssetImportDiagnosticSeverity`](#keireeditorassetimportdiagnosticseverity)
- [`Keire.Editor.AssetImporterEditor`](#keireeditorassetimportereditor)
- [`Keire.Editor.AssetPostprocessor`](#keireeditorassetpostprocessor)
- [`Keire.Editor.AudioImportArtifact`](#keireeditoraudioimportartifact)
- [`Keire.Editor.BinaryImportArtifact`](#keireeditorbinaryimportartifact)
- [`Keire.Editor.BuildContext`](#keireeditorbuildcontext)
- [`Keire.Editor.BuildDescription`](#keireeditorbuilddescription)
- [`Keire.Editor.BuildExtensionPipeline`](#keireeditorbuildextensionpipeline)
- [`Keire.Editor.BuildExtensionResult`](#keireeditorbuildextensionresult)
- [`Keire.Editor.BuildTargetPlatform`](#keireeditorbuildtargetplatform)
- [`Keire.Editor.CanEditMultipleObjectsAttribute`](#keireeditorcaneditmultipleobjectsattribute)
- [`Keire.Editor.CustomEditorAttribute`](#keireeditorcustomeditorattribute)
- [`Keire.Editor.CustomPropertyDecoratorAttribute`](#keireeditorcustompropertydecoratorattribute)
- [`Keire.Editor.CustomPropertyDrawerAttribute`](#keireeditorcustompropertydrawerattribute)
- [`Keire.Editor.DrawGizmoAttribute`](#keireeditordrawgizmoattribute)
- [`Keire.Editor.Editor`](#keireeditoreditor)
- [`Keire.Editor.EditorApplication`](#keireeditoreditorapplication)
- [`Keire.Editor.EditorExtension`](#keireeditoreditorextension)
- [`Keire.Editor.EditorExtensionCatalog`](#keireeditoreditorextensioncatalog)
- [`Keire.Editor.EditorExtensionDescriptor`](#keireeditoreditorextensiondescriptor)
- [`Keire.Editor.EditorExtensionDiagnostic`](#keireeditoreditorextensiondiagnostic)
- [`Keire.Editor.EditorExtensionGeneration`](#keireeditoreditorextensiongeneration)
- [`Keire.Editor.EditorExtensionIdAttribute`](#keireeditoreditorextensionidattribute)
- [`Keire.Editor.EditorExtensionKind`](#keireeditoreditorextensionkind)
- [`Keire.Editor.EditorExtensionLifetime`](#keireeditoreditorextensionlifetime)
- [`Keire.Editor.EditorExtensionPlatform`](#keireeditoreditorextensionplatform)
- [`Keire.Editor.EditorPreferences`](#keireeditoreditorpreferences)
- [`Keire.Editor.EditorTool`](#keireeditoreditortool)
- [`Keire.Editor.EditorToolAttribute`](#keireeditoreditortoolattribute)
- [`Keire.Editor.EditorWindow`](#keireeditoreditorwindow)
- [`Keire.Editor.EditorWindowAttribute`](#keireeditoreditorwindowattribute)
- [`Keire.Editor.GizmoSelection`](#keireeditorgizmoselection)
- [`Keire.Editor.IBuildProcessor`](#keireeditoribuildprocessor)
- [`Keire.Editor.IPostprocessBuild`](#keireeditoripostprocessbuild)
- [`Keire.Editor.IPreprocessBuild`](#keireeditoripreprocessbuild)
- [`Keire.Editor.ImportTargetPlatform`](#keireeditorimporttargetplatform)
- [`Keire.Editor.ManagedDataImportArtifact`](#keireeditormanageddataimportartifact)
- [`Keire.Editor.MaterialImportArtifact`](#keireeditormaterialimportartifact)
- [`Keire.Editor.MenuItemAttribute`](#keireeditormenuitemattribute)
- [`Keire.Editor.MeshImportArtifact`](#keireeditormeshimportartifact)
- [`Keire.Editor.PrefabImportArtifact`](#keireeditorprefabimportartifact)
- [`Keire.Editor.ProjectSettingsSingleton<T>`](#keireeditorprojectsettingssingleton-of-t)
- [`Keire.Editor.PropertyAttribute`](#keireeditorpropertyattribute)
- [`Keire.Editor.PropertyDecorator`](#keireeditorpropertydecorator)
- [`Keire.Editor.PropertyDrawer`](#keireeditorpropertydrawer)
- [`Keire.Editor.PropertyField`](#keireeditorpropertyfield)
- [`Keire.Editor.SceneToolContext`](#keireeditorscenetoolcontext)
- [`Keire.Editor.ScriptedImportRequest`](#keireeditorscriptedimportrequest)
- [`Keire.Editor.ScriptedImportResponse`](#keireeditorscriptedimportresponse)
- [`Keire.Editor.ScriptedImporter`](#keireeditorscriptedimporter)
- [`Keire.Editor.ScriptedImporterAttribute`](#keireeditorscriptedimporterattribute)
- [`Keire.Editor.Selection`](#keireeditorselection)
- [`Keire.Editor.SerializedObject`](#keireeditorserializedobject)
- [`Keire.Editor.SerializedProperty`](#keireeditorserializedproperty)
- [`Keire.Editor.SerializedPropertyKind`](#keireeditorserializedpropertykind)
- [`Keire.Editor.SettingsProvider`](#keireeditorsettingsprovider)
- [`Keire.Editor.TextImportArtifact`](#keireeditortextimportartifact)
- [`Keire.Editor.TextureImportArtifact`](#keireeditortextureimportartifact)
- [`Keire.Editor.Undo`](#keireeditorundo)

## Keire.Editor.AnimationImportArtifact

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public sealed record AnimationImportArtifact(float Duration, IReadOnlyDictionary<string, IReadOnlyList<AnimationKeyframe>> Tracks) : AssetImportArtifact(AssetImportArtifactKind.Animation)
{
}
```

## Keire.Editor.AnimationKeyframe

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public sealed record AnimationKeyframe(float Time, Vector3 Position, Quaternion Rotation, Vector3 Scale)
{
}
```

## Keire.Editor.AssetChangeBatch

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public sealed record AssetChangeBatch(IReadOnlyList<string> Imported, IReadOnlyList<string> Deleted, IReadOnlyList<string> Moved, IReadOnlyList<string> MovedFrom, bool ReloadedExtensions)
{
}
```

## Keire.Editor.AssetDatabase

Sources: [EditorTooling.cs](../../KeireEditorManaged/EditorTooling.cs)

```csharp
public static class AssetDatabase
{
    public static AssetId CreateAsset(string projectRelativePath, ReadOnlyMemory<byte> source);
    public static void MoveAsset(AssetId asset, string destination);
    public static void RenameAsset(AssetId asset, string name);
    public static void TrashAsset(AssetId asset);
    public static void RequestReimport(AssetId asset);
}
```

## Keire.Editor.AssetImportArtifact

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public abstract record AssetImportArtifact(AssetImportArtifactKind Kind)
{
    public string Name { get; init; }
}
```

## Keire.Editor.AssetImportArtifactKind

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
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
```

## Keire.Editor.AssetImportContext

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public sealed class AssetImportContext
{
    public AssetImportContext(AssetId asset, string assetPath, ImportTargetPlatform targetPlatform, CancellationToken cancellationToken, Func<string, ReadOnlyMemory<byte>> sourceReader);
    public AssetId Asset { get; }
    public string AssetPath { get; }
    public ImportTargetPlatform TargetPlatform { get; }
    public CancellationToken CancellationToken { get; }
    public IReadOnlyList<string> SourceDependencies { get; }
    public IReadOnlyList<AssetId> AssetDependencies { get; }
    public IReadOnlyList<AssetImportDiagnostic> Diagnostics { get; }
    public IReadOnlyDictionary<string, AssetImportArtifact> Outputs { get; }
    public string? MainOutput { get; }
    public ReadOnlyMemory<byte> ReadSourceBytes(string projectRelativePath);
    public string ReadSourceText(string projectRelativePath);
    public void DependsOnSource(string projectRelativePath);
    public void DependsOnAsset(AssetId asset);
    public void AddObject(string key, AssetImportArtifact artifact);
    public void SetMainObject(string key);
    public AssetId ResolveSubAssetId(string key);
    public void LogInformation(string code, string message);
    public void LogWarning(string code, string message);
    public void LogError(string code, string message);
}
```

## Keire.Editor.AssetImportDiagnostic

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public sealed record AssetImportDiagnostic(AssetImportDiagnosticSeverity Severity, string Code, string Message, uint Line = 0, uint Column = 0)
{
}
```

## Keire.Editor.AssetImportDiagnosticSeverity

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public enum AssetImportDiagnosticSeverity : byte
{
    Information,
    Warning,
    Error
}
```

## Keire.Editor.AssetImporterEditor

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public abstract class AssetImporterEditor : Editor
{
}
```

## Keire.Editor.AssetPostprocessor

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public abstract class AssetPostprocessor : EditorExtension
{
    public virtual void OnPostprocessAssets(AssetChangeBatch changes);
}
```

## Keire.Editor.AudioImportArtifact

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public sealed record AudioImportArtifact(uint SampleRate, ushort Channels, ReadOnlyMemory<float> Samples) : AssetImportArtifact(AssetImportArtifactKind.Audio)
{
}
```

## Keire.Editor.BinaryImportArtifact

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public sealed record BinaryImportArtifact(ReadOnlyMemory<byte> Bytes) : AssetImportArtifact(AssetImportArtifactKind.Binary)
{
}
```

## Keire.Editor.BuildContext

Sources: [BuildExtensions.cs](../../KeireEditorManaged/BuildExtensions.cs)

```csharp
public sealed class BuildContext
{
    public BuildContext(BuildDescription description, CancellationToken cancellationToken, Action<string, ReadOnlyMemory<byte>> writer);
    public BuildDescription Description { get; }
    public CancellationToken CancellationToken { get; }
    public IReadOnlyList<AssetImportDiagnostic> Diagnostics { get; }
    public void WriteStagedFile(string relativePath, ReadOnlyMemory<byte> bytes);
    public void LogWarning(string code, string message);
    public void LogError(string code, string message);
}
```

## Keire.Editor.BuildDescription

Sources: [BuildExtensions.cs](../../KeireEditorManaged/BuildExtensions.cs)

```csharp
public sealed record BuildDescription(string ProductName, string Version, BuildTargetPlatform Platform, string Configuration, string StagingDirectory, IReadOnlyList<string> Scenes)
{
}
```

## Keire.Editor.BuildExtensionPipeline

Sources: [BuildPipeline.cs](../../KeireEditorManaged/BuildPipeline.cs)

```csharp
public static class BuildExtensionPipeline
{
    public static BuildExtensionResult Execute(BuildDescription description, IEnumerable<IBuildProcessor> processors, CancellationToken cancellationToken, Action<IReadOnlyDictionary<string, ReadOnlyMemory<byte>>> packageValidator, Action<IReadOnlyDictionary<string, ReadOnlyMemory<byte>>> publisher);
}
```

## Keire.Editor.BuildExtensionResult

Sources: [BuildPipeline.cs](../../KeireEditorManaged/BuildPipeline.cs)

```csharp
public sealed record BuildExtensionResult(IReadOnlyDictionary<string, ReadOnlyMemory<byte>> Files, IReadOnlyList<AssetImportDiagnostic> Diagnostics)
{
}
```

## Keire.Editor.BuildTargetPlatform

Sources: [BuildExtensions.cs](../../KeireEditorManaged/BuildExtensions.cs)

```csharp
public enum BuildTargetPlatform : byte
{
    Windows,
    Linux,
    MacOS
}
```

## Keire.Editor.CanEditMultipleObjectsAttribute

Sources: [InspectorContracts.cs](../../KeireEditorManaged/InspectorContracts.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, Inherited = true)]
public sealed class CanEditMultipleObjectsAttribute : Attribute
{
}
```

## Keire.Editor.CustomEditorAttribute

Sources: [InspectorContracts.cs](../../KeireEditorManaged/InspectorContracts.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class CustomEditorAttribute(Type targetType, bool editorForChildClasses = false) : Attribute
{
    public Type TargetType { get; }
    public bool EditorForChildClasses { get; }
}
```

## Keire.Editor.CustomPropertyDecoratorAttribute

Sources: [InspectorContracts.cs](../../KeireEditorManaged/InspectorContracts.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class CustomPropertyDecoratorAttribute(Type attributeType) : Attribute
{
    public Type AttributeType { get; }
}
```

## Keire.Editor.CustomPropertyDrawerAttribute

Sources: [InspectorContracts.cs](../../KeireEditorManaged/InspectorContracts.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class CustomPropertyDrawerAttribute(Type targetType, bool useForChildren = false) : Attribute
{
    public Type TargetType { get; }
    public bool UseForChildren { get; }
}
```

## Keire.Editor.DrawGizmoAttribute

Sources: [EditorTooling.cs](../../KeireEditorManaged/EditorTooling.cs)

```csharp
[AttributeUsage(AttributeTargets.Method, AllowMultiple = true)]
public sealed class DrawGizmoAttribute(Type targetType, GizmoSelection selection = GizmoSelection.Always) : Attribute
{
    public Type TargetType { get; }
    public GizmoSelection Selection { get; }
}
```

## Keire.Editor.Editor

Sources: [InspectorContracts.cs](../../KeireEditorManaged/InspectorContracts.cs)

```csharp
public abstract class Editor : EditorExtension
{
    public SerializedObject SerializedObject { get; internal set; }
    public object Target { get; }
    public IReadOnlyList<object> Targets { get; }
    public abstract VisualElement CreateInspectorGUI();
}
```

## Keire.Editor.EditorApplication

Sources: [EditorTooling.cs](../../KeireEditorManaged/EditorTooling.cs)

```csharp
public static class EditorApplication
{
    public static event Action? Update;
    public static event Action? ProjectChanged;
    public static event Action? ExtensionsReloading;
    public static event Action? ExtensionsReloaded;
    public static bool IsPlaying { get; internal set; }
    public static bool IsCompiling { get; internal set; }
}
```

## Keire.Editor.EditorExtension

Sources: [ExtensionFoundation.cs](../../KeireEditorManaged/ExtensionFoundation.cs)

```csharp
public abstract class EditorExtension : IDisposable
{
    public EditorExtensionLifetime Lifetime { get; }
    protected virtual void OnEnable();
    protected virtual void OnDisable();
    public void Dispose();
}
```

## Keire.Editor.EditorExtensionCatalog

Sources: [ExtensionFoundation.cs](../../KeireEditorManaged/ExtensionFoundation.cs)

```csharp
public sealed class EditorExtensionCatalog
{
    public ulong Generation { get; }
    public IReadOnlyList<EditorExtensionDescriptor> Extensions { get; }
    public static EditorExtensionCatalog Discover(ulong generation, IEnumerable<Type> allowedTypes);
}
```

## Keire.Editor.EditorExtensionDescriptor

Sources: [ExtensionFoundation.cs](../../KeireEditorManaged/ExtensionFoundation.cs)

```csharp
public sealed record EditorExtensionDescriptor(Guid Id, EditorExtensionKind Kind, Type ExtensionType, string AssemblyName)
{
}
```

## Keire.Editor.EditorExtensionDiagnostic

Sources: [ExtensionFoundation.cs](../../KeireEditorManaged/ExtensionFoundation.cs)

```csharp
public sealed record EditorExtensionDiagnostic(string Code, string ExtensionType, string Message)
{
}
```

## Keire.Editor.EditorExtensionGeneration

Sources: [ExtensionGeneration.cs](../../KeireEditorManaged/ExtensionGeneration.cs)

```csharp
public sealed class EditorExtensionGeneration : IDisposable
{
    public EditorExtensionCatalog Catalog { get; }
    public ulong Generation { get; }
    public IReadOnlyList<EditorExtensionDiagnostic> Diagnostics { get; }
    public IReadOnlyCollection<Guid> QuarantinedExtensions { get; }
    public static EditorExtensionGeneration Stage(ulong generation, IEnumerable<Type> exactAllowedTypes);
    public T? Get<T>(Guid extensionId)
        where T : class;
    public bool Invoke(Guid extensionId, string operation, Action<object> callback, string? stablePropertyPath = null);
    public void Dispose();
}
```

## Keire.Editor.EditorExtensionIdAttribute

Sources: [ExtensionFoundation.cs](../../KeireEditorManaged/ExtensionFoundation.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class EditorExtensionIdAttribute : Attribute
{
    public EditorExtensionIdAttribute(string id);
    public Guid Id { get; }
}
```

## Keire.Editor.EditorExtensionKind

Sources: [ExtensionFoundation.cs](../../KeireEditorManaged/ExtensionFoundation.cs)

```csharp
public enum EditorExtensionKind : byte
{
    PropertyDrawer,
    PropertyDecorator,
    CustomEditor,
    ScriptedImporter,
    AssetPostprocessor,
    EditorWindow,
    EditorTool,
    SettingsProvider,
    BuildProcessor
}
```

## Keire.Editor.EditorExtensionLifetime

Sources: [ExtensionFoundation.cs](../../KeireEditorManaged/ExtensionFoundation.cs)

```csharp
public sealed class EditorExtensionLifetime : IDisposable
{
    public ulong Generation { get; }
    public bool IsValid { get; }
    public CancellationToken CancellationToken { get; }
    public void ThrowIfInvalid();
    public void Dispose();
}
```

## Keire.Editor.EditorExtensionPlatform

Sources: [ExtensionGeneration.cs](../../KeireEditorManaged/ExtensionGeneration.cs)

```csharp
public sealed class EditorExtensionPlatform : IDisposable
{
    public EditorExtensionGeneration? Current { get; }
    public bool SafeModeRecommended { get; }
    public IReadOnlyList<EditorExtensionDiagnostic> Diagnostics { get; }
    public bool PublishCandidate(ulong generation, IEnumerable<Type> exactAllowedTypes, bool disableProjectExtensions = false);
    public void Dispose();
}
```

## Keire.Editor.EditorPreferences

Sources: [EditorTooling.cs](../../KeireEditorManaged/EditorTooling.cs)

```csharp
public static class EditorPreferences
{
    public static string GetString(string key, string defaultValue = "");
    public static void SetString(string key, string value);
    public static bool DeleteKey(string key);
}
```

## Keire.Editor.EditorTool

Sources: [EditorTooling.cs](../../KeireEditorManaged/EditorTooling.cs)

```csharp
public abstract class EditorTool : EditorExtension
{
    public virtual void OnActivated(SceneToolContext context);
    public virtual void OnSceneGUI(SceneToolContext context);
    public virtual void OnDeactivated(SceneToolContext context);
}
```

## Keire.Editor.EditorToolAttribute

Sources: [EditorTooling.cs](../../KeireEditorManaged/EditorTooling.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class EditorToolAttribute(string displayName, params Type[] targetTypes) : Attribute
{
    public string DisplayName { get; }
    public IReadOnlyList<Type> TargetTypes { get; }
}
```

## Keire.Editor.EditorWindow

Sources: [EditorTooling.cs](../../KeireEditorManaged/EditorTooling.cs)

```csharp
public abstract class EditorWindow : EditorExtension
{
    protected EditorWindow();
    public VisualElement RootVisualElement { get; }
    public bool IsVisible { get; internal set; }
    public virtual void CreateGUI();
    public virtual void Update();
}
```

## Keire.Editor.EditorWindowAttribute

Sources: [EditorTooling.cs](../../KeireEditorManaged/EditorTooling.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class EditorWindowAttribute(string title, bool defaultVisible = false) : Attribute
{
    public string Title { get; }
    public bool DefaultVisible { get; }
}
```

## Keire.Editor.GizmoSelection

Sources: [EditorTooling.cs](../../KeireEditorManaged/EditorTooling.cs)

```csharp
public enum GizmoSelection : byte
{
    Always,
    Selected,
    Active
}
```

## Keire.Editor.IBuildProcessor

Sources: [BuildExtensions.cs](../../KeireEditorManaged/BuildExtensions.cs)

```csharp
public interface IBuildProcessor
{
    int Order { get; }
}
```

## Keire.Editor.IPostprocessBuild

Sources: [BuildExtensions.cs](../../KeireEditorManaged/BuildExtensions.cs)

```csharp
public interface IPostprocessBuild : IBuildProcessor
{
    void OnPostprocessBuild(BuildContext context);
}
```

## Keire.Editor.IPreprocessBuild

Sources: [BuildExtensions.cs](../../KeireEditorManaged/BuildExtensions.cs)

```csharp
public interface IPreprocessBuild : IBuildProcessor
{
    void OnPreprocessBuild(BuildContext context);
}
```

## Keire.Editor.ImportTargetPlatform

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public enum ImportTargetPlatform : byte
{
    Windows,
    Linux,
    MacOS
}
```

## Keire.Editor.ManagedDataImportArtifact

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public sealed record ManagedDataImportArtifact(ScriptableObject Value) : AssetImportArtifact(AssetImportArtifactKind.ManagedData)
{
}
```

## Keire.Editor.MaterialImportArtifact

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public sealed record MaterialImportArtifact(string Shader, IReadOnlyDictionary<string, object?> Parameters) : AssetImportArtifact(AssetImportArtifactKind.Material)
{
}
```

## Keire.Editor.MenuItemAttribute

Sources: [EditorTooling.cs](../../KeireEditorManaged/EditorTooling.cs)

```csharp
[AttributeUsage(AttributeTargets.Method, AllowMultiple = true)]
public sealed class MenuItemAttribute : Attribute
{
    public MenuItemAttribute(string path, int priority = 0, bool validate = false);
    public string Path { get; }
    public int Priority { get; }
    public bool Validate { get; }
}
```

## Keire.Editor.MeshImportArtifact

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public sealed record MeshImportArtifact(IReadOnlyList<Vector3> Positions, IReadOnlyList<Vector3> Normals, IReadOnlyList<Vector2> TextureCoordinates, IReadOnlyList<uint> Indices) : AssetImportArtifact(AssetImportArtifactKind.Mesh)
{
}
```

## Keire.Editor.PrefabImportArtifact

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public sealed record PrefabImportArtifact(string CanonicalSceneDocument) : AssetImportArtifact(AssetImportArtifactKind.Prefab)
{
}
```

## Keire.Editor.ProjectSettingsSingleton of T

Sources: [EditorTooling.cs](../../KeireEditorManaged/EditorTooling.cs)

```csharp
public abstract class ProjectSettingsSingleton<T>
    where T : ProjectSettingsSingleton<T>, new()
{
    public static T Instance { get; }
}
```

## Keire.Editor.PropertyAttribute

Sources: [InspectorContracts.cs](../../KeireEditorManaged/InspectorContracts.cs)

```csharp
public abstract class PropertyAttribute : Attribute
{
    public int Order { get; init; }
}
```

## Keire.Editor.PropertyDecorator

Sources: [InspectorContracts.cs](../../KeireEditorManaged/InspectorContracts.cs)

```csharp
public abstract class PropertyDecorator : EditorExtension
{
    public abstract VisualElement Decorate(SerializedProperty property, VisualElement content, PropertyAttribute attribute);
}
```

## Keire.Editor.PropertyDrawer

Sources: [InspectorContracts.cs](../../KeireEditorManaged/InspectorContracts.cs)

```csharp
public abstract class PropertyDrawer : EditorExtension
{
    public abstract VisualElement CreatePropertyGUI(SerializedProperty property);
}
```

## Keire.Editor.PropertyField

Sources: [InspectorContracts.cs](../../KeireEditorManaged/InspectorContracts.cs)

```csharp
public sealed class PropertyField : BindableElement
{
    public PropertyField(SerializedProperty property);
    public SerializedProperty Property { get; }
    public string Label { get; set; }
}
```

## Keire.Editor.SceneToolContext

Sources: [EditorTooling.cs](../../KeireEditorManaged/EditorTooling.cs)

```csharp
public readonly record struct SceneToolContext(IReadOnlyList<EngineObject> Selection, CancellationToken LifetimeToken)
{
}
```

## Keire.Editor.ScriptedImportRequest

Sources: [ScriptedImportProtocol.cs](../../KeireEditorManaged/ScriptedImportProtocol.cs)

```csharp
public sealed record ScriptedImportRequest(Guid ImporterId, uint ImporterVersion, string AssemblyFingerprint, string NormalizedSettings, ImportTargetPlatform TargetPlatform, string SourcePath, string SourceDigest, IReadOnlyList<string> Dependencies, int MaximumResponseBytes = 256 * 1024 * 1024)
{
    public string CacheKey();
}
```

## Keire.Editor.ScriptedImportResponse

Sources: [ScriptedImportProtocol.cs](../../KeireEditorManaged/ScriptedImportProtocol.cs)

```csharp
public sealed record ScriptedImportResponse(bool Succeeded, IReadOnlyDictionary<string, AssetImportArtifact> Outputs, string? MainOutput, IReadOnlyList<AssetImportDiagnostic> Diagnostics, IReadOnlyList<string> SourceDependencies, IReadOnlyList<AssetId> AssetDependencies)
{
}
```

## Keire.Editor.ScriptedImporter

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public abstract class ScriptedImporter : EditorExtension
{
    public abstract void OnImportAsset(AssetImportContext context);
}
```

## Keire.Editor.ScriptedImporterAttribute

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class ScriptedImporterAttribute : Attribute
{
    public ScriptedImporterAttribute(uint version, params string[] extensions);
    public uint Version { get; }
    public IReadOnlyList<string> Extensions { get; }
    public int QueuePriority { get; init; }
    public bool AllowCaching { get; init; }
}
```

## Keire.Editor.Selection

Sources: [EditorTooling.cs](../../KeireEditorManaged/EditorTooling.cs)

```csharp
public static class Selection
{
    public static event Action? Changed;
    public static IReadOnlyList<EngineObject> Objects { get; }
    public static EngineObject? ActiveObject { get; }
}
```

## Keire.Editor.SerializedObject

Sources: [InspectorContracts.cs](../../KeireEditorManaged/InspectorContracts.cs)

```csharp
public sealed class SerializedObject : IDisposable
{
    public SerializedObject(object target, ulong generation = 1);
    public SerializedObject(IReadOnlyList<object> targets, ulong generation = 1);
    public SerializedObject(IReadOnlyList<object> targets, EditorExtensionLifetime lifetime);
    public ulong Generation { get; }
    public IReadOnlyList<object> Targets { get; }
    public bool HasModifiedProperties { get; }
    public IEnumerable<SerializedProperty> Properties { get; }
    public SerializedProperty? FindProperty(string name);
    public void Update();
    public bool ApplyModifiedProperties(string undoName);
    public void Dispose();
}
```

## Keire.Editor.SerializedProperty

Sources: [InspectorContracts.cs](../../KeireEditorManaged/InspectorContracts.cs)

```csharp
public sealed class SerializedProperty
{
    public string Name { get; }
    public string DisplayName { get; }
    public string Path { get; }
    public Guid? StableId { get; }
    public Type DeclaredType { get; }
    public bool IsReadOnly { get; }
    public bool IsMixedValue { get; }
    public SerializedPropertyKind Kind { get; }
    public IReadOnlyList<SerializedProperty> Children { get; }
    public object? BoxedValue { get; set; }
}
```

## Keire.Editor.SerializedPropertyKind

Sources: [InspectorContracts.cs](../../KeireEditorManaged/InspectorContracts.cs)

```csharp
public enum SerializedPropertyKind : byte
{
    Null,
    Boolean,
    SignedInteger,
    UnsignedInteger,
    Number,
    String,
    Enum,
    Value,
    EngineReference,
    Array,
    List,
    Dictionary,
    Object
}
```

## Keire.Editor.SettingsProvider

Sources: [EditorTooling.cs](../../KeireEditorManaged/EditorTooling.cs)

```csharp
public abstract class SettingsProvider : EditorExtension
{
    protected SettingsProvider(string path);
    public string Path { get; }
    public abstract VisualElement CreateSettingsGUI();
}
```

## Keire.Editor.TextImportArtifact

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public sealed record TextImportArtifact(string Text) : AssetImportArtifact(AssetImportArtifactKind.Text)
{
}
```

## Keire.Editor.TextureImportArtifact

Sources: [AssetImporting.cs](../../KeireEditorManaged/AssetImporting.cs)

```csharp
public sealed record TextureImportArtifact(uint Width, uint Height, ReadOnlyMemory<byte> Rgba8, bool Srgb = true) : AssetImportArtifact(AssetImportArtifactKind.Texture)
{
}
```

## Keire.Editor.Undo

Sources: [EditorTooling.cs](../../KeireEditorManaged/EditorTooling.cs)

```csharp
public static class Undo
{
    public static bool Perform(string name, Action mutation);
}
```
