internal static class ManagedSerializationV3Tests
{
    internal static void Run()
    {
        var root = new SerializationGraphDerived { Name = "root", DerivedValue = 17 };
        var shared = new SerializationGraphDerived { Name = "shared", DerivedValue = 29 };
        root.Next = root;
        root.Alias = shared;
        root.Children.Add(shared);
        root.Children.Add(shared);
        root.Values["zeta"] = [[9, 8]];
        root.Values["alpha"] = [[1, 2, 3]];

        var source = new ManagedSerializationV3Probe
        {
            Root = root,
            SharedRoot = root,
            Count = 41,
            Nested =
            {
                ["zeta"] = [[9, 8]],
                ["alpha"] = [[1, 2, 3]],
            },
        };
        string state = Keire.ManagedStateSerializer.Capture(source, string.Empty, false);
        Assert(state.Contains("\"ReferenceGraph\":true", StringComparison.Ordinal) &&
                   state.Contains("\"ReferenceGraphRoot\":", StringComparison.Ordinal) &&
                   state.Contains("\"Roots\":", StringComparison.Ordinal) &&
                   state.Contains("73616e64-626f-4078-8000-00000000d101", StringComparison.Ordinal),
               "SerializeReference fields must use one v3 root map/object table and stable runtime type IDs.");
        System.Text.Json.Nodes.JsonObject stateDocument =
            System.Text.Json.Nodes.JsonNode.Parse(state)!.AsObject();
        System.Text.Json.Nodes.JsonObject optionalTexture = stateDocument["Fields"]!.AsArray()
            .Select(node => node!.AsObject())
            .Single(field => field["Name"]!.GetValue<string>() == nameof(ManagedSerializationV3Probe.OptionalTexture));
        Assert(optionalTexture.ContainsKey("Value") && optionalTexture["Value"] is null,
               "Nullable engine references must be captured as explicit null field values.");

        var restored = new ManagedSerializationV3Probe { OptionalTexture = new Keire.Texture() };
        _ = Keire.ManagedStateSerializer.Restore(restored, state, false);
        Assert(restored.Root is SerializationGraphDerived restoredRoot && restoredRoot.DerivedValue == 17 &&
                   ReferenceEquals(restoredRoot.Next, restoredRoot) &&
                   ReferenceEquals(restoredRoot.Alias, restoredRoot.Children[0]) &&
                   ReferenceEquals(restoredRoot.Children[0], restoredRoot.Children[1]) &&
                   ReferenceEquals(restored.Root, restored.SharedRoot) && restored.OptionalTexture is null,
               "Reference graph restore must preserve runtime types, cycles, and sharing across root fields.");
        Assert(restored.Nested["alpha"][0].SequenceEqual([1, 2, 3]) && restored.Nested["zeta"][0][1] == 8,
               "Exact Dictionary<TKey,TValue> fields must support recursively nested lists and arrays.");

        var privateNode = SerializationGraphPrivateConstructor.Create(93);
        var privateSource = new ManagedSerializationV3Probe { Root = privateNode, SharedRoot = privateNode };
        string privateState = Keire.ManagedStateSerializer.Capture(privateSource, string.Empty, false);
        var privateRestored = new ManagedSerializationV3Probe();
        _ = Keire.ManagedStateSerializer.Restore(privateRestored, privateState, false);
        Assert(privateRestored.Root is SerializationGraphPrivateConstructor privateResult &&
                   privateResult.Value == 93 && ReferenceEquals(privateRestored.Root, privateRestored.SharedRoot),
               "SerializeReference restore must support registered non-public parameterless constructors.");

        var reordered = new ManagedSerializationV3Probe
        {
            Root = root,
            SharedRoot = root,
            Count = 41,
            Nested =
            {
                ["alpha"] = [[1, 2, 3]],
                ["zeta"] = [[9, 8]],
            },
        };
        string reorderedState = Keire.ManagedStateSerializer.Capture(reordered, string.Empty, false);
        Assert(state == reorderedState,
               "Dictionary insertion order must not affect canonical managed state bytes.");

        ValidateUnknownFieldRetention(source);
        ValidateStringLimits(source, root);
        ValidateStructuredDiagnostics(state, shared);

        var holder = new SerializationGraphHolder { Root = root };
        SerializationGraphHolder clone = Keire.ManagedObjectSerializer.CloneSerializableValueForTests(holder);
        var cloneRoot = (SerializationGraphDerived)clone.Root!;
        Assert(!ReferenceEquals(root, cloneRoot) && ReferenceEquals(cloneRoot.Next, cloneRoot) &&
                   ReferenceEquals(cloneRoot.Children[0], cloneRoot.Children[1]),
               "ScriptableObject state cloning must preserve SerializeReference graph identity.");
        var privateHolder = new PrivateSerializationGraphHolder(root);
        PrivateSerializationGraphHolder privateClone =
            Keire.ManagedObjectSerializer.CloneSerializableValueForTests(privateHolder);
        Assert(privateClone.Root is SerializationGraphDerived privateCloneRoot &&
                   !ReferenceEquals(root, privateCloneRoot) && ReferenceEquals(privateCloneRoot.Next, privateCloneRoot),
               "SerializeReference must opt private fields into transactional graph cloning.");

        var invalid = new InvalidSerializationGraphProbe { Root = new SerializationGraphMissingId() };
        try
        {
            _ = Keire.ManagedStateSerializer.Capture(invalid, string.Empty, false);
            throw new InvalidOperationException("Missing stable graph type IDs must be rejected.");
        }
        catch (Keire.ManagedSerializationException exception)
        {
            Assert(exception.Code == "KEIRE-MANAGED-SERIALIZATION-0002" &&
                       exception.FieldPath.EndsWith(".Root", StringComparison.Ordinal) &&
                       exception.DeclaredType == typeof(SerializationGraphBase) &&
                       exception.RuntimeType == typeof(SerializationGraphMissingId),
                   "Unsupported graph diagnostics must identify the exact field and declared/runtime types.");
        }

        System.Text.Json.Nodes.JsonObject corrupted =
            System.Text.Json.Nodes.JsonNode.Parse(state)!.AsObject();
        System.Text.Json.Nodes.JsonObject countField = corrupted["Fields"]!.AsArray().Select(node => node!.AsObject())
            .Single(field => field["Name"]!.GetValue<string>() == nameof(ManagedSerializationV3Probe.Count));
        countField["Value"] = "not-an-integer";
        restored.Count = 700;
        SerializationGraphBase previousRoot = restored.Root!;
        AssertThrows<InvalidOperationException>(
            () => Keire.ManagedStateSerializer.Restore(restored, corrupted.ToJsonString(), false),
            "Malformed candidate state must fail before committing any field.");
        Assert(restored.Count == 700 && ReferenceEquals(restored.Root, previousRoot),
               "Failed managed state restore must preserve every previous valid field.");

        ValidateReferenceFieldLimit();
        ValidateDepthLimits();
        ValidateFrozenTypeRegistry();
    }

    private static void ValidateUnknownFieldRetention(ManagedSerializationV3Probe source)
    {
        foreach (int version in new[] { 1, 2 })
        {
            string legacy = $$"""
                {"Version":{{version}},"Fields":[{"StableId":"","Name":"LegacyOnly","Type":"System.Int32","Aliases":[],"Value":41}]}
                """;
            string migrated = Keire.ManagedStateSerializer.Capture(source, legacy, false);
            System.Text.Json.Nodes.JsonObject migratedDocument =
                System.Text.Json.Nodes.JsonNode.Parse(migrated)!.AsObject();
            System.Text.Json.Nodes.JsonObject retained = migratedDocument["Fields"]!.AsArray()
                .Select(node => node!.AsObject())
                .Single(field => field["Name"]!.GetValue<string>() == "LegacyOnly");
            Assert(migratedDocument["Version"]!.GetValue<int>() == 3 &&
                       retained["StableId"]!.GetValue<string>() == string.Empty &&
                       retained["Type"]!.GetValue<string>() == "System.Int32" &&
                       retained["Aliases"]!.AsArray().Count == 0 && retained["Value"]!.GetValue<int>() == 41 &&
                       !retained.ContainsKey("ReferenceGraphRoot"),
                   $"Managed state v{version} unknown fields must survive canonical v3 capture unchanged.");

            var restored = new ManagedSerializationV3Probe { Count = -1 };
            _ = Keire.ManagedStateSerializer.Restore(restored, migrated, false);
            Assert(restored.Count == source.Count,
                   "Unknown retained fields must not alter known runtime fields during restore.");
            string recaptured = Keire.ManagedStateSerializer.Capture(restored, migrated, false);
            System.Text.Json.Nodes.JsonObject recapturedDocument =
                System.Text.Json.Nodes.JsonNode.Parse(recaptured)!.AsObject();
            System.Text.Json.Nodes.JsonObject recapturedUnknown = recapturedDocument["Fields"]!.AsArray()
                .Select(node => node!.AsObject())
                .Single(field => field["Name"]!.GetValue<string>() == "LegacyOnly");
            Assert(System.Text.Json.Nodes.JsonNode.DeepEquals(retained, recapturedUnknown),
                   "Restoring and recapturing v3 state must preserve retained unknown field payloads exactly.");
        }
    }

    internal static void ValidatePersistentManagedData(System.Text.Json.JsonElement descriptor)
    {
        System.Text.Json.JsonElement[] properties = descriptor.GetProperty("properties").EnumerateArray().ToArray();
        string rootId = properties.Single(property =>
            property.GetProperty("name").GetString() == nameof(ManagedGraphAuthoringProbe.Root))
            .GetProperty("stableFieldId").GetString()!;
        string sharedId = properties.Single(property =>
            property.GetProperty("name").GetString() == nameof(ManagedGraphAuthoringProbe.SharedRoot))
            .GetProperty("stableFieldId").GetString()!;

        var graph = new SerializationGraphDerived { Name = "persistent", DerivedValue = 71 };
        graph.Next = graph;
        System.Text.Json.JsonElement objectTable = Keire.ManagedReferenceGraphCodec.CaptureRoots(
            [
                new("persistent-root", graph, typeof(SerializationGraphBase),
                    $"{typeof(ManagedGraphAuthoringProbe).FullName}.Root",
                    typeof(ManagedGraphAuthoringProbe).FullName!, nameof(ManagedGraphAuthoringProbe.Root)),
                new("persistent-shared", graph, typeof(SerializationGraphBase),
                    $"{typeof(ManagedGraphAuthoringProbe).FullName}.SharedRoot",
                    typeof(ManagedGraphAuthoringProbe).FullName!, nameof(ManagedGraphAuthoringProbe.SharedRoot)),
            ], Keire.ManagedStateSerializer.SerializerOptions);
        string document = System.Text.Json.JsonSerializer.Serialize(new
        {
            schemaVersion = 3,
            managedTypeId = "73616e64-626f-4078-8000-00000000c003",
            referenceGraph = objectTable,
            fields = new[]
            {
                new { stableId = rootId, name = "Root", referenceGraph = true,
                      referenceGraphRoot = "persistent-root" },
                new { stableId = sharedId, name = "SharedRoot", referenceGraph = true,
                      referenceGraphRoot = "persistent-shared" },
            },
        });
        var target = Keire.ScriptableObject.CreateInstance<ManagedGraphAuthoringProbe>();
        target.RuntimeHydrateManagedData(document);
        Assert(target.Root is SerializationGraphDerived restored && restored.DerivedValue == 71 &&
                   ReferenceEquals(restored.Next, restored) && ReferenceEquals(target.Root, target.SharedRoot),
               "Managed-data v3 must preserve cycles and sharing across ScriptableObject root fields.");

        System.Text.Json.JsonElement legacyGraph = Keire.ManagedReferenceGraphCodec.Capture(
            graph, typeof(SerializationGraphBase), $"{typeof(ManagedGraphAuthoringProbe).FullName}.Root",
            Keire.ManagedStateSerializer.SerializerOptions);
        string legacy = System.Text.Json.JsonSerializer.Serialize(new
        {
            schemaVersion = 1,
            managedTypeId = "73616e64-626f-4078-8000-00000000c003",
            fields = new[]
            {
                new { stableId = rootId, name = "Root", referenceGraph = true, value = legacyGraph },
            },
        });
        var legacyTarget = Keire.ScriptableObject.CreateInstance<ManagedGraphAuthoringProbe>();
        legacyTarget.RuntimeHydrateManagedData(legacy);
        Assert(legacyTarget.Root is SerializationGraphDerived legacyRoot &&
                   ReferenceEquals(legacyRoot.Next, legacyRoot),
               "Managed-data v1 per-field graphs must remain readable after the v3 object-table migration.");

        System.Text.Json.Nodes.JsonObject malformed =
            System.Text.Json.Nodes.JsonNode.Parse(document)!.AsObject();
        System.Text.Json.Nodes.JsonObject malformedNode = malformed["referenceGraph"]!["Objects"]!.AsArray()[0]!
            .AsObject();
        int objectId = malformedNode["Id"]!.GetValue<int>();
        malformedNode["StableTypeId"] = "invalid-persistent-type";
        SerializationGraphBase previous = target.Root!;
        try
        {
            target.RuntimeHydrateManagedData(malformed.ToJsonString());
            throw new InvalidOperationException("Malformed persistent graph types must be rejected.");
        }
        catch (Keire.ManagedSerializationException exception)
        {
            Assert(exception.Phase == "allocate" &&
                       exception.Owner == typeof(ManagedGraphAuthoringProbe).FullName &&
                       exception.RootField == nameof(ManagedGraphAuthoringProbe.Root) &&
                       exception.SerializedTypeId == "invalid-persistent-type" && exception.ObjectId == objectId,
                   "Persistent graph failures must expose structured type, root, phase, and object diagnostics.");
        }
        Assert(ReferenceEquals(target.Root, previous) && ReferenceEquals(target.SharedRoot, previous),
               "Failed managed-data graph restoration must preserve the last valid ScriptableObject state.");
    }

    private static void ValidateStringLimits(ManagedSerializationV3Probe source, SerializationGraphDerived root)
    {
        root.Name = new string('\u00E9', 524_288);
        _ = Keire.ManagedStateSerializer.Capture(source, string.Empty, false);
        root.Name = string.Concat(Enumerable.Repeat("\U0001F600", 262_144));
        _ = Keire.ManagedStateSerializer.Capture(source, string.Empty, false);
        root.Name += "\U0001F600";
        try
        {
            _ = Keire.ManagedStateSerializer.Capture(source, string.Empty, false);
            throw new InvalidOperationException("Strings larger than 1 MiB of UTF-8 must be rejected.");
        }
        catch (Keire.ManagedSerializationException exception)
        {
            Assert(exception.Code == "KEIRE-MANAGED-SERIALIZATION-0002" &&
                       exception.FieldPath.EndsWith(".Root.Name", StringComparison.Ordinal) &&
                       exception.DeclaredType == typeof(string) &&
                       exception.Message.Contains("1048576 UTF-8 bytes", StringComparison.Ordinal),
                   "Multibyte string limits must report the exact graph field, type, and UTF-8 byte contract.");
        }

        root.Name = "\uD800";
        try
        {
            _ = Keire.ManagedStateSerializer.Capture(source, string.Empty, false);
            throw new InvalidOperationException("Unpaired UTF-16 surrogates must be rejected.");
        }
        catch (Keire.ManagedSerializationException exception)
        {
            Assert(exception.Code == "KEIRE-MANAGED-SERIALIZATION-0002" &&
                       exception.FieldPath.EndsWith(".Root.Name", StringComparison.Ordinal) &&
                       exception.DeclaredType == typeof(string) &&
                       exception.Message.Contains("valid Unicode", StringComparison.Ordinal),
                   "Invalid surrogate diagnostics must identify the exact graph field and declared string type.");
        }
        root.Name = "root";
    }

    private static void ValidateStructuredDiagnostics(string state, SerializationGraphDerived shared)
    {
        System.Text.Json.Nodes.JsonObject malformedType =
            System.Text.Json.Nodes.JsonNode.Parse(state)!.AsObject();
        System.Text.Json.Nodes.JsonObject objectTable = malformedType["ReferenceGraph"]!.AsObject();
        System.Text.Json.Nodes.JsonObject malformedObject = objectTable["Objects"]!.AsArray()[0]!.AsObject();
        int malformedObjectId = malformedObject["Id"]!.GetValue<int>();
        malformedObject["StableTypeId"] = "not-a-stable-type-id";
        var target = new ManagedSerializationV3Probe { Root = shared, SharedRoot = shared, Count = 811 };
        try
        {
            _ = Keire.ManagedStateSerializer.Restore(target, malformedType.ToJsonString(), false);
            throw new InvalidOperationException("Malformed stable serialized type IDs must be rejected.");
        }
        catch (Keire.ManagedSerializationException exception)
        {
            Assert(exception.Code == "KEIRE-MANAGED-SERIALIZATION-0002" && exception.Phase == "allocate" &&
                       exception.Owner == typeof(ManagedSerializationV3Probe).FullName &&
                       exception.RootField == nameof(ManagedSerializationV3Probe.Root) &&
                       exception.SerializedTypeId == "not-a-stable-type-id" &&
                       exception.ObjectId == malformedObjectId &&
                       exception.FieldPath.EndsWith(".Root", StringComparison.Ordinal),
                   "Malformed graph types must expose phase, owner, root, stable type ID, object ID, and path.");
        }
        Assert(ReferenceEquals(target.Root, shared) && ReferenceEquals(target.SharedRoot, shared) &&
                   target.Count == 811,
               "Allocate-stage graph failures must preserve the previous Behaviour state.");

        System.Text.Json.Nodes.JsonObject malformedReference =
            System.Text.Json.Nodes.JsonNode.Parse(state)!.AsObject();
        System.Text.Json.Nodes.JsonObject rootMap = malformedReference["ReferenceGraph"]!["Roots"]!.AsArray()
            .Select(node => node!.AsObject()).Single(rootEntry =>
                rootEntry["Key"]!.GetValue<string>().EndsWith("d001", StringComparison.Ordinal));
        rootMap["Value"]!["Reference"] = 999_999;
        try
        {
            _ = Keire.ManagedStateSerializer.Restore(target, malformedReference.ToJsonString(), false);
            throw new InvalidOperationException("Missing graph object references must be rejected.");
        }
        catch (Keire.ManagedSerializationException exception)
        {
            Assert(exception.Phase == "allocate" &&
                       exception.RootField == nameof(ManagedSerializationV3Probe.Root) &&
                       exception.ObjectId == 999_999 &&
                       exception.FieldPath.EndsWith(".Root", StringComparison.Ordinal),
                   "Missing graph references must expose the allocating root, exact path, and object ID.");
        }
    }

    private static void ValidateReferenceFieldLimit()
    {
        Type exactFieldLimit = EmitSerializableType(1_024);
        Keire.ManagedAssetMetadata.ValidateReferenceTypeFieldLimitForTests(exactFieldLimit);
        Keire.ManagedStateSerializer.ValidateFieldLimitForTests(exactFieldLimit);
        Keire.ManagedObjectSerializer.ValidateFieldLimitForTests(exactFieldLimit);
        Keire.ManagedDataHydrator.ValidateFieldLimitForTests(exactFieldLimit);
        Type oversizedReferenceType = EmitSerializableType(1_025);
        AssertFieldLimitRejected(Keire.ManagedAssetMetadata.ValidateReferenceTypeFieldLimitForTests,
                                 oversizedReferenceType, "metadata", "reference metadata");
        AssertFieldLimitRejected(Keire.ManagedStateSerializer.ValidateFieldLimitForTests,
                                 oversizedReferenceType, "validate", "Behaviour state");
        AssertFieldLimitRejected(Keire.ManagedObjectSerializer.ValidateFieldLimitForTests,
                                 oversizedReferenceType, "validate", "by-value member cache");
        AssertFieldLimitRejected(Keire.ManagedDataHydrator.ValidateFieldLimitForTests,
                                 oversizedReferenceType, "validate", "managed-data hydration");
    }

    private static void AssertFieldLimitRejected(Action<Type> validate, Type type, string phase, string boundary)
    {
        try
        {
            validate(type);
            throw new InvalidOperationException($"{boundary} types above 1,024 fields must be rejected.");
        }
        catch (Keire.ManagedSerializationException exception)
        {
            Assert(exception.Code == "KEIRE-MANAGED-SERIALIZATION-0001" && exception.Phase == phase &&
                       exception.Owner == type.FullName && exception.RootField == "Field1024" &&
                       exception.FieldPath.EndsWith(".Field1024", StringComparison.Ordinal) &&
                       exception.Message.Contains("1024 serialized fields", StringComparison.Ordinal),
                   $"{boundary} must reject field 1,025 with an exact structured diagnostic.");
        }
    }

    private static void ValidateDepthLimits()
    {
        (object exactValue, Type exactType) = NestedArray(32);
        Keire.ManagedObjectSerializer.ValidateSerializableValue(exactValue, exactType, "Depth32");
        (object oversizedValue, Type oversizedType) = NestedArray(33);
        try
        {
            Keire.ManagedObjectSerializer.ValidateSerializableValue(oversizedValue, oversizedType, "Depth33");
            throw new InvalidOperationException("Managed values deeper than 32 collection levels must be rejected.");
        }
        catch (Keire.ManagedSerializationException exception)
        {
            Assert(exception.Code == "KEIRE-MANAGED-SERIALIZATION-0001" &&
                       exception.FieldPath.Contains("Depth33", StringComparison.Ordinal) &&
                       exception.Message.Contains("32 nested levels", StringComparison.Ordinal),
                   "Managed depth 33 must fail with the exact 32-level contract and path.");
        }
    }

    private static void ValidateFrozenTypeRegistry()
    {
        Guid stableId = Guid.Parse("73616e64-626f-4078-8000-00000000d101");
        IReadOnlyDictionary<Guid, Type> accepted =
            Keire.ManagedReferenceGraphCodec.TypeRegistryForTests(typeof(ManagedSerializationV3Probe));
        Assert(accepted.TryGetValue(stableId, out Type? acceptedType) &&
                   acceptedType == typeof(SerializationGraphDerived),
               "The accepted managed generation must freeze its registered serialized types.");

        var assembly = System.Reflection.Emit.AssemblyBuilder.DefineDynamicAssembly(
            new System.Reflection.AssemblyName($"LateSerializedTypes_{Guid.NewGuid():N}"),
            System.Reflection.Emit.AssemblyBuilderAccess.Run);
        System.Reflection.Emit.TypeBuilder duplicate = assembly.DefineDynamicModule("Main").DefineType(
            "LateDuplicateStableType", System.Reflection.TypeAttributes.Public |
                                       System.Reflection.TypeAttributes.Class);
        _ = duplicate.DefineDefaultConstructor(System.Reflection.MethodAttributes.Public);
        duplicate.SetCustomAttribute(new System.Reflection.Emit.CustomAttributeBuilder(
            typeof(SerializableAttribute).GetConstructor(Type.EmptyTypes)!, []));
        duplicate.SetCustomAttribute(new System.Reflection.Emit.CustomAttributeBuilder(
            typeof(Keire.StableSerializedTypeIdAttribute).GetConstructor([typeof(string)])!,
            [stableId.ToString("D")]));
        _ = duplicate.CreateType();

        IReadOnlyDictionary<Guid, Type> afterLateLoad =
            Keire.ManagedReferenceGraphCodec.TypeRegistryForTests(typeof(ManagedSerializationV3Probe));
        Assert(ReferenceEquals(accepted, afterLateLoad) && afterLateLoad.Count == accepted.Count &&
                   afterLateLoad[stableId] == typeof(SerializationGraphDerived),
               "Late-loaded assemblies must not mutate an already accepted generation's serialized type registry.");
    }

    private static (object Value, Type Type) NestedArray(int levels)
    {
        object value = 7;
        Type type = typeof(int);
        for (int index = 0; index < levels; ++index)
        {
            Array array = Array.CreateInstance(type, 1);
            array.SetValue(value, 0);
            value = array;
            type = type.MakeArrayType();
        }
        return (value, type);
    }

    private static Type EmitSerializableType(int fieldCount)
    {
        var assembly = System.Reflection.Emit.AssemblyBuilder.DefineDynamicAssembly(
            new System.Reflection.AssemblyName($"KeireFieldLimit{fieldCount}_{Guid.NewGuid():N}"),
            System.Reflection.Emit.AssemblyBuilderAccess.Run);
        System.Reflection.Emit.TypeBuilder type = assembly.DefineDynamicModule("Main").DefineType(
            $"FieldLimit{fieldCount}", System.Reflection.TypeAttributes.Public |
                                      System.Reflection.TypeAttributes.Class);
        for (int index = 0; index < fieldCount; ++index)
            _ = type.DefineField($"Field{index:D4}", typeof(int), System.Reflection.FieldAttributes.Public);
        return type.CreateType()!;
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }

    private static void AssertThrows<TException>(Action action, string message) where TException : Exception
    {
        try
        {
            action();
        }
        catch (TException)
        {
            return;
        }
        throw new InvalidOperationException(message);
    }
}

[Serializable]
internal abstract class SerializationGraphBase
{
    public string Name = string.Empty;
    public SerializationGraphBase? Next;
    public SerializationGraphBase? Alias;
    public List<SerializationGraphBase> Children = [];
    public Dictionary<string, List<int[]>> Values = [];
}

[Serializable]
[Keire.StableSerializedTypeId("73616e64-626f-4078-8000-00000000d101")]
internal sealed class SerializationGraphDerived : SerializationGraphBase
{
    public SerializationGraphDerived() { }

    public int DerivedValue;
}

[Serializable]
internal sealed class SerializationGraphMissingId : SerializationGraphBase
{
    public SerializationGraphMissingId() { }
}

[Serializable]
[Keire.StableSerializedTypeId("73616e64-626f-4078-8000-00000000d102")]
internal sealed class SerializationGraphPrivateConstructor : SerializationGraphBase
{
    private SerializationGraphPrivateConstructor() { }

    public int Value;

    public static SerializationGraphPrivateConstructor Create(int value) => new() { Value = value };
}

[Serializable]
internal sealed class SerializationGraphHolder
{
    [Keire.SerializeReference]
    public SerializationGraphBase? Root;
}

[Serializable]
internal sealed class PrivateSerializationGraphHolder
{
    [Keire.SerializeReference]
    private SerializationGraphBase? _root;

    public PrivateSerializationGraphHolder() { }

    public PrivateSerializationGraphHolder(SerializationGraphBase root) => _root = root;

    public SerializationGraphBase? Root => _root;
}

[Keire.StableComponentId("d3762027-3016-4ec9-b315-67d654f46462")]
internal sealed class ManagedSerializationV3Probe : Keire.Behaviour
{
    [Keire.SerializeReference]
    [Keire.StableFieldId("73616e64-626f-4078-8000-00000000d001")]
    public SerializationGraphBase? Root;

    [Keire.SerializeReference]
    [Keire.StableFieldId("73616e64-626f-4078-8000-00000000d004")]
    public SerializationGraphBase? SharedRoot;

    [Keire.StableFieldId("73616e64-626f-4078-8000-00000000d002")]
    public Dictionary<string, List<int[]>> Nested = [];

    [Keire.StableFieldId("73616e64-626f-4078-8000-00000000d003")]
    public int Count;

    public Keire.Texture? OptionalTexture;
}

[Keire.StableComponentId("d3762027-3016-4ec9-b315-67d654f46463")]
internal sealed class InvalidSerializationGraphProbe : Keire.Behaviour
{
    [Keire.SerializeReference]
    public SerializationGraphBase? Root;
}

[Keire.StableAssetTypeId("73616e64-626f-4078-8000-00000000c003")]
internal sealed class ManagedGraphAuthoringProbe : Keire.ScriptableObject
{
    [Keire.SerializeReference]
    [Keire.StableFieldId("73616e64-626f-4078-8000-00000000c301")]
    public SerializationGraphBase? Root = null;

    [Keire.SerializeReference]
    [Keire.StableFieldId("73616e64-626f-4078-8000-00000000c302")]
    public SerializationGraphBase? SharedRoot = null;
}
