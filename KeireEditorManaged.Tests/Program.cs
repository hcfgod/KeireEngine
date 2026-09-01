using Keire.Editor;
using Keire.UI;

var tests = new (string Name, Action Run)[]
{
    ("Extension catalogs require stable unique deterministic registrations", ExtensionCatalogContract),
    ("SerializedObject commits and rolls back multi-target edits", SerializedObjectContract),
    ("Scripted import contexts bound paths outputs and deterministic identities", ImportContextContract),
    ("Build contexts constrain writes to the staging tree", BuildContextContract),
    ("Extension generations publish last-good state and quarantine callback failures", ExtensionGenerationContract),
    ("Build processors and importer cache inputs remain deterministic and transactional", PipelineContract),
};

foreach ((string name, Action run) in tests)
{
    try
    {
        run();
        Console.WriteLine($"PASS {name}");
    }
    catch (Exception exception)
    {
        Console.Error.WriteLine($"FAIL {name}: {exception}");
        Environment.ExitCode = 1;
    }
}

static void ExtensionCatalogContract()
{
    EditorExtensionCatalog catalog = EditorExtensionCatalog.Discover(7,
        [typeof(HealthDrawer), typeof(HealthEditor), typeof(DialogueImporter), typeof(BuildProbe)]);
    Check(catalog.Generation == 7 && catalog.Extensions.Count == 4,
          "The catalog must retain its generation and every explicit extension type.");
    Check(catalog.Extensions.Select(value => value.Id).SequenceEqual(
              catalog.Extensions.Select(value => value.Id).Order()),
          "The catalog must publish extensions in stable-ID order.");
    Expect<InvalidOperationException>(() => EditorExtensionCatalog.Discover(8,
        [typeof(HealthDrawer), typeof(DuplicateHealthDrawer)]));
    Expect<InvalidOperationException>(() => EditorExtensionCatalog.Discover(8, [typeof(MissingIdentityDrawer)]));
}

static void SerializedObjectContract()
{
    var first = new HealthData { Points = 10 };
    var second = new HealthData { Points = 20 };
    using var serialized = new SerializedObject([first, second], 3);
    SerializedProperty property = serialized.FindProperty(nameof(HealthData.Points)) ??
                                  throw new InvalidOperationException("Health property is missing.");
    Check(property.IsMixedValue, "Distinct multi-edit values must report mixed state.");
    property.BoxedValue = 42;
    Check(serialized.ApplyModifiedProperties("Set Health") && first.Points == 42 && second.Points == 42,
          "A successful apply must commit every target atomically.");

    var rejecting = new RejectingData { Value = 1 };
    using var rejected = new SerializedObject(rejecting, 4);
    rejected.FindProperty(nameof(RejectingData.Value))!.BoxedValue = -1;
    Expect<System.Reflection.TargetInvocationException>(() => rejected.ApplyModifiedProperties("Reject"));
    Check(rejecting.Value == 1, "A validation failure must restore the previous field value.");

    var nested = new NestedInspectorData
    {
        Stats = new NestedStats { Armor = 3 },
        Values = [4, 5],
        Named = new Dictionary<string, int> { ["alpha"] = 6 },
    };
    using var nestedSerialized = new SerializedObject(nested, 5);
    string root = $"{typeof(NestedInspectorData).FullName}";
    SerializedProperty armor = nestedSerialized.FindProperty($"{root}.Stats.Armor") ??
        throw new InvalidOperationException("Nested armor property is missing.");
    SerializedProperty listValue = nestedSerialized.FindProperty($"{root}.Values[1]") ??
        throw new InvalidOperationException("Nested list property is missing.");
    SerializedProperty dictionaryValue = nestedSerialized.FindProperty($"{root}.Named[\"alpha\"]") ??
        throw new InvalidOperationException("Nested dictionary property is missing.");
    armor.BoxedValue = 30;
    listValue.BoxedValue = 50;
    dictionaryValue.BoxedValue = 60;
    Check(nestedSerialized.ApplyModifiedProperties("Nested edit") && nested.Stats.Armor == 30 &&
              nested.Values[1] == 50 && nested.Named["alpha"] == 60,
          "Nested object, list, and dictionary paths must stage and commit through one atomic snapshot.");
}

static void ImportContextContract()
{
    var source = new Dictionary<string, byte[]>(StringComparer.Ordinal)
    {
        ["Assets/Data.dialogue"] = "hello"u8.ToArray(),
    };
    var context = new AssetImportContext(new Keire.AssetId(11, 12), "Assets/Data.dialogue",
        ImportTargetPlatform.Windows, CancellationToken.None,
        path => source.TryGetValue(path, out byte[]? bytes) ? bytes : throw new FileNotFoundException(path));
    Check(context.ReadSourceText("Assets/Data.dialogue") == "hello", "Source reads must use the supplied broker.");
    context.DependsOnSource("Assets/Data.dialogue");
    context.AddObject("main", new TextImportArtifact("canonical"));
    context.SetMainObject("main");
    Check(context.MainOutput == "main" && context.Outputs.Count == 1 && context.SourceDependencies.Count == 1,
          "Import contexts must publish explicit outputs and dependencies.");
    Check(context.ResolveSubAssetId("child") == context.ResolveSubAssetId("child") &&
              context.ResolveSubAssetId("child") != context.ResolveSubAssetId("other"),
          "Sub-asset identities must be deterministic and key-specific.");
    Expect<ArgumentException>(() => context.ReadSourceBytes("../outside"));
    Expect<InvalidOperationException>(() =>
        context.AddObject("main", new TextImportArtifact("duplicate")));
}

static void BuildContextContract()
{
    var writes = new List<string>();
    var context = new BuildContext(new BuildDescription("Game", "1.0", BuildTargetPlatform.Windows,
        "Release", "Stage", []), CancellationToken.None, (path, _) => writes.Add(path));
    context.WriteStagedFile("Data/generated.bin", new byte[] { 1, 2, 3 });
    Check(writes.SequenceEqual(["Data/generated.bin"]), "Build processors must write through the staging broker.");
    Expect<ArgumentException>(() => context.WriteStagedFile("../outside.bin", ReadOnlyMemory<byte>.Empty));
}

static void ExtensionGenerationContract()
{
    using var platform = new EditorExtensionPlatform();
    Check(platform.PublishCandidate(1, [typeof(ThrowingWindow)]) && platform.Current?.Generation == 1,
          "A valid extension candidate must publish atomically.");
    Guid windowId = Guid.Parse("30000000-0000-0000-0000-000000000006");
    Check(platform.Current!.Invoke(windowId, "update", _ => throw new InvalidOperationException("expected")) == false &&
              platform.Current.QuarantinedExtensions.Contains(windowId),
          "A callback exception must quarantine only its registration for the active generation.");
    for (ulong generation = 2; generation <= 4; ++generation)
    {
        Check(!platform.PublishCandidate(generation, [typeof(MissingIdentityDrawer)]) &&
                  platform.Current.Generation == 1,
              "A rejected catalog must preserve the exact last-good generation.");
    }
    Check(platform.SafeModeRecommended, "Repeated catalog failures must make extension-safe-mode available.");

    LifecycleWindow.EnableCount = 0;
    LifecycleWindow.UpdateCount = 0;
    LifecycleWindow.DisableCount = 0;
    string assemblyName = typeof(LifecycleWindow).Assembly.GetName().Name ??
                          throw new InvalidOperationException("The Editor test assembly has no name.");
    string request = System.Text.Json.JsonSerializer.Serialize(new
    {
        schemaVersion = 1,
        assemblies = new[] { new { name = assemblyName, types = new[] { typeof(LifecycleWindow).FullName } } },
    });
    _ = ManagedEditorExtensionMetadata.Stage(request, 11);
    Check(LifecycleWindow.EnableCount == 1 && !ManagedEditorExtensionMetadata.Update(11),
          "A staged Editor generation must activate transactionally but reject callbacks before publication.");
    Check(ManagedEditorExtensionMetadata.Commit(11) && ManagedEditorExtensionMetadata.Update(11) &&
              LifecycleWindow.UpdateCount == 1,
          "A published Editor generation must receive generation-checked updates.");
    Check(!ManagedEditorExtensionMetadata.Update(10) && ManagedEditorExtensionMetadata.Shutdown(11) &&
              LifecycleWindow.DisableCount == 1,
          "The Editor bridge must reject stale calls and dispose the active generation explicitly.");
}

static void PipelineContract()
{
    var order = new List<string>();
    bool published = false;
    BuildExtensionResult result = BuildExtensionPipeline.Execute(
        new BuildDescription("Game", "1.0", BuildTargetPlatform.Linux, "Release", "Stage", []),
        [new OrderedBuildProcessor(20, "second", order), new OrderedBuildProcessor(10, "first", order)],
        CancellationToken.None, files => Check(files.Count == 2, "Validation must observe only staged output."),
        _ => published = true);
    Check(published && order.SequenceEqual(["first", "second"]) &&
              result.Files.Keys.SequenceEqual(["first.bin", "second.bin"]),
          "Build processors must run in stable order and publish only after staging validation succeeds.");

    bool rejectedPublish = false;
    Expect<InvalidOperationException>(() => BuildExtensionPipeline.Execute(
        new BuildDescription("Game", "1.0", BuildTargetPlatform.Linux, "Release", "Stage", []),
        [new FailingBuildProcessor()], CancellationToken.None, _ => { }, _ => rejectedPublish = true));
    Check(!rejectedPublish, "A failed processor must discard staging output without invoking publication.");

    var request = new ScriptedImportRequest(
        Guid.Parse("30000000-0000-0000-0000-000000000004"), 2, "assembly", "settings",
        ImportTargetPlatform.Linux, "Assets/Data.dialogue", "source", ["z", "a"]);
    Check(request.CacheKey() == (request with { Dependencies = ["a", "z"] }).CacheKey(),
          "Importer cache keys must canonicalize dependency ordering.");
}

static void Check(bool condition, string message)
{
    if (!condition)
        throw new InvalidOperationException(message);
}

static void Expect<T>(Action action) where T : Exception
{
    try
    {
        action();
    }
    catch (T)
    {
        return;
    }
    throw new InvalidOperationException($"Expected {typeof(T).Name}.");
}

[Serializable]
internal sealed class HealthData
{
    public int Points;
}

[Serializable]
internal sealed class NestedStats
{
    public int Armor;
}

[Serializable]
internal sealed class NestedInspectorData
{
    public NestedStats Stats = new();
    public List<int> Values = [];
    public Dictionary<string, int> Named = [];
}

internal sealed class RejectingData : Keire.ScriptableObject
{
    public int Value;

    protected override void OnValidate()
    {
        if (Value < 0)
            throw new InvalidOperationException("Negative values are rejected.");
    }
}

[EditorExtensionId("30000000-0000-0000-0000-000000000001")]
[CustomPropertyDrawer(typeof(HealthData))]
internal sealed class HealthDrawer : PropertyDrawer
{
    public override VisualElement CreatePropertyGUI(SerializedProperty property) => new PropertyField(property);
}

[EditorExtensionId("30000000-0000-0000-0000-000000000002")]
[CustomPropertyDrawer(typeof(HealthData))]
internal sealed class DuplicateHealthDrawer : PropertyDrawer
{
    public override VisualElement CreatePropertyGUI(SerializedProperty property) => new PropertyField(property);
}

[CustomPropertyDrawer(typeof(HealthData))]
internal sealed class MissingIdentityDrawer : PropertyDrawer
{
    public override VisualElement CreatePropertyGUI(SerializedProperty property) => new PropertyField(property);
}

[EditorExtensionId("30000000-0000-0000-0000-000000000003")]
[CustomEditor(typeof(HealthData))]
internal sealed class HealthEditor : Keire.Editor.Editor
{
    public override VisualElement CreateInspectorGUI() => new VisualElement();
}

[EditorExtensionId("30000000-0000-0000-0000-000000000004")]
[ScriptedImporter(1, "dialogue")]
internal sealed class DialogueImporter : ScriptedImporter
{
    public override void OnImportAsset(AssetImportContext context) { }
}

[EditorExtensionId("30000000-0000-0000-0000-000000000005")]
internal sealed class BuildProbe : IBuildProcessor
{
    public int Order => 0;
}

[EditorExtensionId("30000000-0000-0000-0000-000000000006")]
[EditorWindow("Throwing")]
internal sealed class ThrowingWindow : EditorWindow;

[EditorExtensionId("30000000-0000-0000-0000-000000000007")]
[EditorWindow("Lifecycle")]
internal sealed class LifecycleWindow : EditorWindow
{
    internal static int EnableCount { get; set; }
    internal static int UpdateCount { get; set; }
    internal static int DisableCount { get; set; }

    protected override void OnEnable() => ++EnableCount;
    public override void Update() => ++UpdateCount;
    protected override void OnDisable() => ++DisableCount;
}

internal sealed class OrderedBuildProcessor(int order, string name, List<string> events) : IPostprocessBuild
{
    public int Order { get; } = order;

    public void OnPostprocessBuild(BuildContext context)
    {
        events.Add(name);
        context.WriteStagedFile($"{name}.bin", new byte[] { 1 });
    }
}

internal sealed class FailingBuildProcessor : IPreprocessBuild
{
    public int Order => 0;

    public void OnPreprocessBuild(BuildContext context) =>
        throw new InvalidOperationException("Expected processor failure.");
}
