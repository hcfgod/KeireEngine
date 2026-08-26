internal static class ManagedSerializationV3BoundaryTests
{
    private const int MaximumDocumentBytes = 16 * 1024 * 1024;

    internal static void Run()
    {
        ValidateDocumentLimitRollback();
        ValidateCollectionEntryLimit();
        ValidateObjectLimit();
        ValidateEdgeLimit();
        ValidateCustomComparerRejection();
        ValidateStableExternalReference();
        ValidateFormerNameAndStableIdMigration();
        ValidateRegistryAndInspectorChoiceLimits();
    }

    private static void ValidateDocumentLimitRollback()
    {
        var previous = new SerializationBoundaryLeaf();
        var target = new ManagedSerializationBoundaryProbe { Root = previous, Count = 41 };
        InvalidOperationException exception = ExpectFailure<InvalidOperationException>(
            () => Keire.ManagedStateSerializer.Restore(target, new string('x', MaximumDocumentBytes + 1), false),
            "A 16 MiB + 1 managed state document must be rejected.");
        Assert(exception.Message.Contains(MaximumDocumentBytes.ToString(), StringComparison.Ordinal),
               "Oversized managed state diagnostics must name the exact byte limit.");
        Assert(target.Count == 41 && ReferenceEquals(target.Root, previous),
               "Rejecting an oversized document must preserve every previous field value.");
    }

    private static void ValidateCollectionEntryLimit()
    {
        var root = new SerializationBoundaryCollectionRoot
        {
            Items = Enumerable.Range(0, 16_385).Select(_ => new SerializationBoundaryLeaf()).ToList(),
        };
        var source = new ManagedSerializationBoundaryProbe { Root = root, Count = 51 };
        Keire.ManagedSerializationException exception = ExpectSerializationFailure(
            () => _ = Keire.ManagedStateSerializer.Capture(source, string.Empty, false),
            "A graph collection with 16,385 entries must be rejected.");
        Assert(exception.Code == "KEIRE-MANAGED-SERIALIZATION-0002" && exception.Phase == "capture" &&
                   exception.Owner == typeof(ManagedSerializationBoundaryProbe).FullName &&
                   exception.RootField == nameof(ManagedSerializationBoundaryProbe.Root) &&
                   exception.FieldPath.EndsWith(".Root.Items", StringComparison.Ordinal) &&
                   exception.DeclaredType == typeof(List<SerializationBoundaryLeaf>) &&
                   exception.RuntimeType == typeof(List<SerializationBoundaryLeaf>) &&
                   exception.Reason.Contains("16384 entries", StringComparison.Ordinal),
               "Collection overflow diagnostics must retain the exact owner, root, path, type, phase, and limit.");
        Assert(ReferenceEquals(source.Root, root) && root.Items.Count == 16_385 && source.Count == 51,
               "A failed collection capture must leave the valid source graph unchanged.");
    }

    private static void ValidateObjectLimit()
    {
        const int leavesPerList = 16_383;
        var root = new SerializationBoundaryObjectRoot
        {
            First = CreateLeaves(leavesPerList),
            Second = CreateLeaves(leavesPerList),
            Third = CreateLeaves(leavesPerList),
            Fourth = CreateLeaves(leavesPerList),
        };
        // One root + four list nodes + 65,532 leaves is exactly 65,537 graph objects.
        var source = new ManagedSerializationBoundaryProbe { Root = root, Count = 61 };
        Keire.ManagedSerializationException exception = ExpectSerializationFailure(
            () => _ = Keire.ManagedStateSerializer.Capture(source, string.Empty, false),
            "A reference graph with 65,537 objects must be rejected.");
        string rootPath = $"{typeof(ManagedSerializationBoundaryProbe).FullName}.Root.";
        Assert(exception.Code == "KEIRE-MANAGED-SERIALIZATION-0002" && exception.Phase == "capture" &&
                   exception.Owner == typeof(ManagedSerializationBoundaryProbe).FullName &&
                   exception.RootField == nameof(ManagedSerializationBoundaryProbe.Root) &&
                   exception.FieldPath.StartsWith(rootPath, StringComparison.Ordinal) &&
                   exception.FieldPath.Contains('[') &&
                   exception.DeclaredType == typeof(SerializationBoundaryLeaf) &&
                   exception.RuntimeType == typeof(SerializationBoundaryLeaf) &&
                   exception.Reason.Contains("65536 objects", StringComparison.Ordinal),
               "Object overflow diagnostics must retain the exact owner, root, leaf path, type, phase, and limit.");
        Assert(ReferenceEquals(source.Root, root) && source.Count == 61 &&
                   root.First.Count + root.Second.Count + root.Third.Count + root.Fourth.Count == 65_532,
               "A failed object-limit capture must leave all source collections unchanged.");
    }

    private static void ValidateEdgeLimit()
    {
        var shared = new SerializationBoundaryLeaf();
        var root = new SerializationBoundaryEdgeRoot
        {
            First = Repeat(shared, 16_384),
            Second = Repeat(shared, 16_384),
            Third = Repeat(shared, 16_384),
            Fourth = Repeat(shared, 16_384),
            Fifth = Repeat(shared, 16_384),
            Sixth = Repeat(shared, 16_384),
            Seventh = Repeat(shared, 16_384),
            Eighth = Repeat(shared, 16_376),
        };
        // One root edge + eight list edges + 131,064 item edges is exactly 131,073 edges.
        var source = new ManagedSerializationBoundaryProbe { Root = root, Count = 71 };
        Keire.ManagedSerializationException exception = ExpectSerializationFailure(
            () => _ = Keire.ManagedStateSerializer.Capture(source, string.Empty, false),
            "A reference graph with 131,073 edges must be rejected.");
        string rootPath = $"{typeof(ManagedSerializationBoundaryProbe).FullName}.Root.";
        Assert(exception.Code == "KEIRE-MANAGED-SERIALIZATION-0002" && exception.Phase == "capture" &&
                   exception.Owner == typeof(ManagedSerializationBoundaryProbe).FullName &&
                   exception.RootField == nameof(ManagedSerializationBoundaryProbe.Root) &&
                   exception.FieldPath.StartsWith(rootPath, StringComparison.Ordinal) &&
                   exception.FieldPath.Contains('[') &&
                   exception.DeclaredType == typeof(SerializationBoundaryLeaf) &&
                   exception.RuntimeType == typeof(SerializationBoundaryLeaf) &&
                   exception.Reason.Contains("131072 edges", StringComparison.Ordinal),
               "Edge overflow diagnostics must retain the exact owner, root, leaf path, type, phase, and limit.");
        Assert(ReferenceEquals(source.Root, root) && source.Count == 71 &&
                   ReferenceEquals(root.First[0], root.Eighth[^1]) &&
                   root.First.Count + root.Second.Count + root.Third.Count + root.Fourth.Count + root.Fifth.Count +
                       root.Sixth.Count + root.Seventh.Count + root.Eighth.Count == 131_064,
               "A failed edge-limit capture must preserve the shared source graph exactly.");
    }

    private static void ValidateCustomComparerRejection()
    {
        var root = new SerializationBoundaryComparerRoot
        {
            Values = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase) { ["Key"] = 17 },
        };
        var source = new ManagedSerializationBoundaryProbe { Root = root, Count = 81 };
        Keire.ManagedSerializationException exception = ExpectSerializationFailure(
            () => _ = Keire.ManagedStateSerializer.Capture(source, string.Empty, false),
            "A graph dictionary with a custom comparer must be rejected.");
        Assert(exception.Code == "KEIRE-MANAGED-SERIALIZATION-0002" && exception.Phase == "capture" &&
                   exception.RootField == nameof(ManagedSerializationBoundaryProbe.Root) &&
                   exception.FieldPath.EndsWith(".Root.Values", StringComparison.Ordinal) &&
                   exception.DeclaredType == typeof(Dictionary<string, int>) &&
                   exception.Reason.Contains("custom comparers", StringComparison.Ordinal),
               "Custom-comparer rejection must identify the exact graph dictionary and actionable reason.");
        Assert(ReferenceEquals(source.Root, root) && source.Count == 81 && root.Values["key"] == 17,
                "Rejecting a custom comparer must preserve the source dictionary and comparer.");
    }

    private static void ValidateStableExternalReference()
    {
        var assetId = new Keire.AssetId(0x73616E64626F4078UL, 0xA000000000000101UL);
        Keire.Texture texture = Keire.Asset.FromId<Keire.Texture>(assetId)!;
        var source = new ManagedSerializationBoundaryProbe
        {
            Root = new SerializationBoundaryExternalReferenceRoot { Texture = texture },
            Count = 91,
        };
        string state = Keire.ManagedStateSerializer.Capture(source, string.Empty, false);
        var previous = new SerializationBoundaryLeaf();
        var target = new ManagedSerializationBoundaryProbe { Root = previous, Count = -1 };
        _ = Keire.ManagedStateSerializer.Restore(target, state, false);
        Assert(target.Root is SerializationBoundaryExternalReferenceRoot restored &&
                   restored.Texture is not null && restored.Texture.Id == assetId &&
                   ReferenceEquals(restored.Texture, texture) && target.Count == 91,
               "Engine asset references inside reference graphs must restore by stable external identity.");
    }

    private static void ValidateFormerNameAndStableIdMigration()
    {
        var source = new ManagedSerializationRenameProbe { RenamedCount = 101 };
        System.Text.Json.Nodes.JsonObject renamed =
            System.Text.Json.Nodes.JsonNode.Parse(
                Keire.ManagedStateSerializer.Capture(source, string.Empty, false))!.AsObject();
        System.Text.Json.Nodes.JsonObject field = renamed["Fields"]!.AsArray().Select(node => node!.AsObject())
            .Single(candidate => candidate["Name"]!.GetValue<string>() ==
                                 nameof(ManagedSerializationRenameProbe.RenamedCount));
        field["Name"] = "NameBeforeStableRename";

        var stableTarget = new ManagedSerializationRenameProbe { RenamedCount = -1 };
        string stableWarnings = Keire.ManagedStateSerializer.Restore(stableTarget, renamed.ToJsonString(), false);
        Assert(stableTarget.RenamedCount == 101 && string.IsNullOrEmpty(stableWarnings),
               "A stable field ID must restore renamed data without using a name fallback.");

        const string legacy =
            "{\"Version\":2,\"Fields\":[{\"StableId\":\"\",\"Name\":\"LegacyCount\",\"Type\":\"System.Int32\"," +
            "\"Aliases\":[],\"Value\":103}]}";
        var aliasTarget = new ManagedSerializationRenameProbe { RenamedCount = -3 };
        string aliasWarnings = Keire.ManagedStateSerializer.Restore(aliasTarget, legacy, false);
        string expectedPath = $"{typeof(ManagedSerializationRenameProbe).FullName}.RenamedCount";
        Assert(aliasTarget.RenamedCount == 103 &&
                   aliasWarnings.Contains($"{expectedPath} restored through a rename fallback.",
                                          StringComparison.Ordinal),
               "FormerlySerializedAs must migrate legacy field names and emit the actionable fallback path.");
    }

    private static void ValidateRegistryAndInspectorChoiceLimits()
    {
        Type[] types = EmitStableTypes(4_097);
        Keire.ManagedSerializationException registryFailure = ExpectSerializationFailure(
            () => _ = Keire.ManagedReferenceGraphCodec.TypeRegistryForTests(
                types, typeof(SerializationRegistryBoundaryBase), "BoundaryRegistry4097"),
            "A serialized type registry with 4,097 entries must be rejected.");
        Assert(registryFailure.Code == "KEIRE-MANAGED-SERIALIZATION-0002" &&
                   registryFailure.Phase == "validate" && registryFailure.FieldPath == "BoundaryRegistry4097" &&
                   registryFailure.RuntimeType == types[^1] &&
                   registryFailure.Reason.Contains("4096 types", StringComparison.Ordinal),
               "Registry overflow must report the exact path, runtime type, phase, and 4,096-type limit.");

        IReadOnlyDictionary<Guid, Type> accepted = Keire.ManagedReferenceGraphCodec.TypeRegistryForTests(
            types.Take(256), typeof(SerializationRegistryBoundaryBase), "BoundaryChoices256");
        Keire.ManagedAssetMetadata.ValidateReferenceTypeChoiceLimitForTests(
            typeof(ManagedSerializationBoundaryProbe), typeof(SerializationRegistryBoundaryBase),
            "ManagedSerializationBoundaryProbe.Root", accepted);
        Assert(accepted.Count == 256,
               "The production registry and Inspector metadata path must accept exactly 256 slot choices.");

        IReadOnlyDictionary<Guid, Type> rejected = Keire.ManagedReferenceGraphCodec.TypeRegistryForTests(
            types.Take(257), typeof(SerializationRegistryBoundaryBase), "BoundaryChoices257");
        Keire.ManagedSerializationException choiceFailure = ExpectSerializationFailure(
            () => Keire.ManagedAssetMetadata.ValidateReferenceTypeChoiceLimitForTests(
                typeof(ManagedSerializationBoundaryProbe), typeof(SerializationRegistryBoundaryBase),
                "ManagedSerializationBoundaryProbe.Root", rejected),
            "An Inspector reference slot with 257 concrete choices must be rejected.");
        Assert(choiceFailure.Code == "KEIRE-MANAGED-SERIALIZATION-0001" &&
                   choiceFailure.Phase == "metadata" &&
                   choiceFailure.Owner == typeof(ManagedSerializationBoundaryProbe).FullName &&
                   choiceFailure.RootField == "ManagedSerializationBoundaryProbe.Root" &&
                   choiceFailure.FieldPath == "ManagedSerializationBoundaryProbe.Root" &&
                   choiceFailure.DeclaredType == typeof(SerializationRegistryBoundaryBase) &&
                   choiceFailure.Reason.Contains("256 concrete types", StringComparison.Ordinal),
               "Inspector choice overflow must report the exact owner, root, path, type, phase, and limit.");
        Assert(accepted.Count == 256 && rejected.Count == 257,
               "Rejecting the 257th Inspector choice must not mutate either accepted registry snapshot.");
    }

    private static List<SerializationBoundaryLeaf> CreateLeaves(int count) =>
        Enumerable.Range(0, count).Select(_ => new SerializationBoundaryLeaf()).ToList();

    private static List<SerializationBoundaryLeaf> Repeat(SerializationBoundaryLeaf value, int count) =>
        Enumerable.Repeat(value, count).ToList();

    private static Type[] EmitStableTypes(int count)
    {
        var assembly = System.Reflection.Emit.AssemblyBuilder.DefineDynamicAssembly(
            new System.Reflection.AssemblyName($"KeireRegistryBoundary_{Guid.NewGuid():N}"),
            System.Reflection.Emit.AssemblyBuilderAccess.Run);
        System.Reflection.Emit.ModuleBuilder module = assembly.DefineDynamicModule("Main");
        var result = new Type[count];
        for (int index = 0; index < count; ++index)
        {
            System.Reflection.Emit.TypeBuilder builder = module.DefineType(
                $"KeireRegistryBoundary.Type{index:D4}",
                System.Reflection.TypeAttributes.Public | System.Reflection.TypeAttributes.Class,
                typeof(SerializationRegistryBoundaryBase));
            _ = builder.DefineDefaultConstructor(System.Reflection.MethodAttributes.Public);
            builder.SetCustomAttribute(new System.Reflection.Emit.CustomAttributeBuilder(
                typeof(SerializableAttribute).GetConstructor(Type.EmptyTypes)!, []));
            Guid stableId = Guid.Parse($"73616e64-626f-4078-a000-{index + 1:x12}");
            builder.SetCustomAttribute(new System.Reflection.Emit.CustomAttributeBuilder(
                typeof(Keire.StableSerializedTypeIdAttribute).GetConstructor([typeof(string)])!,
                [stableId.ToString("D")]));
            result[index] = builder.CreateType()!;
        }
        return result;
    }

    private static Keire.ManagedSerializationException ExpectSerializationFailure(Action action, string message) =>
        ExpectFailure<Keire.ManagedSerializationException>(action, message);

    private static TException ExpectFailure<TException>(Action action, string message) where TException : Exception
    {
        try
        {
            action();
        }
        catch (TException exception)
        {
            return exception;
        }
        throw new InvalidOperationException(message);
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}

[Serializable]
internal abstract class SerializationBoundaryGraphBase;

[Serializable]
[Keire.StableSerializedTypeId("73616e64-626f-4078-a000-000000000001")]
internal sealed class SerializationBoundaryLeaf : SerializationBoundaryGraphBase;

[Serializable]
[Keire.StableSerializedTypeId("73616e64-626f-4078-a000-000000000002")]
internal sealed class SerializationBoundaryCollectionRoot : SerializationBoundaryGraphBase
{
    public List<SerializationBoundaryLeaf> Items = [];
}

[Serializable]
[Keire.StableSerializedTypeId("73616e64-626f-4078-a000-000000000003")]
internal sealed class SerializationBoundaryObjectRoot : SerializationBoundaryGraphBase
{
    public List<SerializationBoundaryLeaf> First = [];
    public List<SerializationBoundaryLeaf> Second = [];
    public List<SerializationBoundaryLeaf> Third = [];
    public List<SerializationBoundaryLeaf> Fourth = [];
}

[Serializable]
[Keire.StableSerializedTypeId("73616e64-626f-4078-a000-000000000004")]
internal sealed class SerializationBoundaryEdgeRoot : SerializationBoundaryGraphBase
{
    public List<SerializationBoundaryLeaf> First = [];
    public List<SerializationBoundaryLeaf> Second = [];
    public List<SerializationBoundaryLeaf> Third = [];
    public List<SerializationBoundaryLeaf> Fourth = [];
    public List<SerializationBoundaryLeaf> Fifth = [];
    public List<SerializationBoundaryLeaf> Sixth = [];
    public List<SerializationBoundaryLeaf> Seventh = [];
    public List<SerializationBoundaryLeaf> Eighth = [];
}

[Serializable]
[Keire.StableSerializedTypeId("73616e64-626f-4078-a000-000000000005")]
internal sealed class SerializationBoundaryComparerRoot : SerializationBoundaryGraphBase
{
    public Dictionary<string, int> Values = [];
}

[Serializable]
[Keire.StableSerializedTypeId("73616e64-626f-4078-a000-000000000006")]
internal sealed class SerializationBoundaryExternalReferenceRoot : SerializationBoundaryGraphBase
{
    public Keire.Texture? Texture;
}

public abstract class SerializationRegistryBoundaryBase;

[Keire.StableComponentId("73616e64-626f-4078-a000-000000000101")]
internal sealed class ManagedSerializationBoundaryProbe : Keire.Behaviour
{
    [Keire.SerializeReference]
    [Keire.StableFieldId("73616e64-626f-4078-a000-000000000102")]
    public SerializationBoundaryGraphBase? Root;

    [Keire.StableFieldId("73616e64-626f-4078-a000-000000000103")]
    public int Count;
}

[Keire.StableComponentId("73616e64-626f-4078-a000-000000000104")]
internal sealed class ManagedSerializationRenameProbe : Keire.Behaviour
{
    [Keire.StableFieldId("73616e64-626f-4078-a000-000000000105")]
    [Keire.FormerlySerializedAs("LegacyCount")]
    public int RenamedCount;
}
