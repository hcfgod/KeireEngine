var tests = new (string Name, Action Run)[]
{
    ("Prefab assets expose typed stable identity", PrefabAssetMarkerContract),
    ("Unity-shaped object API replaces public handles and marker components", UnityShapedObjectApiContract),
    ("Managed state v3 tags direct entity component and asset references", DirectReferenceStateContract),
    ("Managed serialization v3 preserves graphs nested dictionaries and transactions", ManagedSerializationV3Tests.Run),
    ("Managed serialization v3 rejects exact boundary overflows atomically", ManagedSerializationV3BoundaryTests.Run),
    ("VFX ranges normalize and validate", VfxRangesNormalizeAndValidate),
    ("Inspector attributes validate production editing metadata", InspectorAttributeContract),
    ("ScriptableObject authoring discovers and hydrates Unity-style public fields", ScriptableObjectAuthoringContract),
    ("Managed metadata allowlists isolate Feature Graph catalogs across concurrent failures",
     ManagedMetadataAllowlistContract),
    ("VFX range setters expose every supported type", VfxRangeSettersExposeEverySupportedType),
    ("Runtime asset operations preserve typed diagnostics and explicit leases", RuntimeAssetHandleContract),
    ("Character Controller uses the native stable component contract", CharacterControllerStableContract),
    ("Entity exposes the production layer contract", EntityLayerContract),
    ("Behaviour lifecycle contracts are synchronized", BehaviourLifecycleContract),
    ("Managed state ignores computed math properties", ManagedStateMathContract),
    ("Managed math values format without recursive computed properties", ManagedMathFormattingContract),
    ("Coroutines schedule phases and dispose deterministically", CoroutineContract),
    ("Transform and rigid body components expose writable runtime state", GameplayHandleContract),
    ("Animator exposes transient foot-grounding control", AnimatorFootGroundingContract),
    ("Managed physics shape queries validate and preserve native results", ManagedPhysicsQueryContract),
    ("Managed input devices rebinding persistence and rumble preserve native contracts", ManagedInputDeviceContract),
    ("Managed input actions and direct controls expose Unity-style lifecycle contracts", ManagedInputActionContract),
    ("UI Documents query and mutate source-backed native elements safely", NativeUiDocumentBridgeContract),
    ("UI Toolkit visual trees query propagate bind and virtualize deterministically", UiToolkitTests.Run),
    ("Managed rendering objects preserve camera lights materials and shader overrides", ManagedRenderingContract),
    ("Managed scene loads and render settings preserve native world transactions", ManagedWorldContract),
    ("Managed jobs execute delegates and publish terminal states", ManagedJobExecutionContract),
    ("Managed job cancellation remains exception-safe", ManagedJobCancellationExceptionContract),
    ("Managed jobs preserve terminal dependency semantics", ManagedJobDependencyContract),
    ("Application, time, and screen use the native foundation contract", RuntimeFoundationContract),
    ("Player preferences persist typed values atomically", PlayerPreferencesPersistenceContract),
    ("Player preferences reject invalid and corrupt data", PlayerPreferencesValidationContract),
};

static void ManagedMathFormattingContract()
{
    System.Globalization.CultureInfo previousCulture = System.Globalization.CultureInfo.CurrentCulture;
    NativeLogFixture.Install();
    try
    {
        System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.GetCultureInfo("fr-FR");

        Assert($"Position: {new Keire.Vector2(1.25f, -2.5f)}" ==
                   "Position: Vector2 { X = 1.25, Y = -2.5 }",
               "Vector2 interpolation must format only its stored components without recursing through Normalized.");
        Assert(new Keire.Vector3(1.25f, -2.5f, 3.75f).ToString() ==
                   "Vector3 { X = 1.25, Y = -2.5, Z = 3.75 }",
               "Vector3 formatting must omit its self-typed Normalized property.");
        Assert(new Keire.Vector4(1.25f, -2.5f, 3.75f, -4.5f).ToString() ==
                   "Vector4 { X = 1.25, Y = -2.5, Z = 3.75, W = -4.5 }",
               "Vector4 formatting must remain component-only and culture-invariant.");
        Assert(new Keire.Quaternion(0.25f, -0.5f, 0.75f, 1.0f).ToString() ==
                   "Quaternion { X = 0.25, Y = -0.5, Z = 0.75, W = 1 }",
               "Quaternion formatting must omit its self-typed Normalized property.");
        Assert(new Keire.Color(0.1f, 0.2f, 0.3f, 0.4f).ToString() ==
                   "Color { Red = 0.1, Green = 0.2, Blue = 0.3, Alpha = 0.4 }",
               "Color formatting must remain component-only and culture-invariant.");

        Keire.Log.Info($"Position: {new Keire.Vector2(1.25f, -2.5f)}");
        Keire.Debug.Log(new Keire.Vector2(1.25f, -2.5f));
        Assert(NativeLogFixture.WriteCount == 2,
               "Interpolated and object-based Vector2 logging must both reach the native log without recursion.");
    }
    finally
    {
        NativeLogFixture.Uninstall();
        System.Globalization.CultureInfo.CurrentCulture = previousCulture;
    }
}

static void UnityShapedObjectApiContract()
{
    Type[] publicTypes = typeof(Keire.EngineObject).Assembly.GetExportedTypes();
    string[] handles = publicTypes.Where(type => type.Name.EndsWith("Handle", StringComparison.Ordinal))
        .Select(type => type.FullName ?? type.Name).ToArray();
    Assert(handles.Length == 0, $"Public managed handles remain: {string.Join(", ", handles)}");
    Assert(publicTypes.All(type => !type.Name.StartsWith("AssetReference", StringComparison.Ordinal)),
           "AssetReference<T> must not remain in the public authoring API.");
    Assert(publicTypes.All(type => type == typeof(Keire.Component) ||
                                   !type.Name.EndsWith("Component", StringComparison.Ordinal)),
           "Built-in marker types ending in Component must be replaced by concrete component objects.");

    Assert(typeof(Keire.Entity).IsSealed && typeof(Keire.EngineObject).IsAssignableFrom(typeof(Keire.Entity)) &&
               typeof(Keire.Component).IsAssignableFrom(typeof(Keire.AudioSource)) &&
               typeof(Keire.Component).IsAssignableFrom(typeof(Keire.Behaviour)),
           "Entity, native components, and behaviours must share the EngineObject hierarchy.");
    Assert(typeof(Keire.Entity).GetMethods().Any(method => method.Name == nameof(Keire.Entity.GetComponent) &&
                                                           method.IsGenericMethodDefinition) &&
               typeof(Keire.Component).GetMethods().Any(method =>
                   method.Name == nameof(Keire.Component.GetComponentsInChildren) && method.IsGenericMethodDefinition),
           "Entity and Component must expose the generic Unity-shaped lookup family.");
    Type[] nativeComponents = publicTypes.Where(type => typeof(Keire.Component).IsAssignableFrom(type) &&
                                                        !type.IsAbstract &&
                                                        type.IsDefined(typeof(Keire.StableComponentIdAttribute), false))
        .ToArray();
    Assert(nativeComponents.Length == 21,
           $"Every authorable native registry entry needs a concrete managed component; found {nativeComponents.Length}.");
    Assert(typeof(Keire.UI.UIDocument).GetProperty(nameof(Keire.UI.UIDocument.VisualTreeAsset)) is
               { CanRead: true, CanWrite: true } &&
               typeof(Keire.UI.UIDocument).GetProperty(nameof(Keire.UI.UIDocument.PanelSettings)) is
               { CanRead: true, CanWrite: true },
           "UI Toolkit documents must expose visual-tree and panel-settings assets as one native component.");
    string[] retiredUiTypes =
    [
        "Keire.Canvas", "Keire.RectTransform", "Keire.UiText", "Keire.UiImage", "Keire.UiButton",
        "Keire.UiLayout", "Keire.UiSlider", "Keire.UiToggle", "Keire.UiInputField", "Keire.UiScrollView",
        "Keire.UiAccessibility", "Keire.RuntimeCanvas", "Keire.RuntimeUi"
    ];
    Assert(retiredUiTypes.All(name => typeof(Keire.Component).Assembly.GetType(name, throwOnError: false) is not { IsPublic: true }),
           "Retired Canvas and runtime UI authoring types must not remain in the public managed API.");

    Keire.Entity canonical = Keire.Entity.FromId(73, new Keire.EntityId(11, 12))!;
    Assert(ReferenceEquals(canonical, Keire.Entity.FromId(73, new Keire.EntityId(11, 12))),
           "Repeated entity lookup must preserve wrapper reference identity.");
    Keire.Prefab prefab = Keire.Asset.FromId<Keire.Prefab>(new Keire.AssetId(21, 22))!;
    Assert(ReferenceEquals(prefab, Keire.Asset.FromId<Keire.Prefab>(new Keire.AssetId(21, 22))),
           "Repeated asset lookup must preserve wrapper reference identity.");
}

static void DirectReferenceStateContract()
{
    Keire.Entity entity = Keire.Entity.FromId(73, new Keire.EntityId(31, 32))!;
    Keire.Prefab prefab = Keire.Asset.FromId<Keire.Prefab>(new Keire.AssetId(41, 42))!;
    var source = new DirectReferenceStateProbe();
    source.SetReferences(entity, new Keire.AudioSource(entity), prefab);

    string state = Keire.ManagedStateSerializer.Capture(source, string.Empty, false);
    Assert(state.Contains("\"Version\":3", StringComparison.Ordinal) ||
               state.Contains("\"version\":3", StringComparison.Ordinal),
           "Managed authoring state must use the v3 format while retaining direct reference records.");
    Assert(state.Contains("\"$ref\":\"entity\"", StringComparison.Ordinal) &&
               state.Contains("\"$ref\":\"component\"", StringComparison.Ordinal) &&
               state.Contains("\"$ref\":\"asset\"", StringComparison.Ordinal),
           "Direct references must be explicitly tagged as entity, component, and asset records.");
    Assert(state.Contains("73616e64-626f-4078-8000-00000000b002", StringComparison.Ordinal) &&
               !state.Contains("NotSerializedByUnityRules", StringComparison.Ordinal),
           "[SerializeField] private fields must serialize while plain private fields remain excluded.");
}

static void PrefabAssetMarkerContract()
{
    var marker = (Keire.StableAssetTypeIdAttribute?)Attribute.GetCustomAttribute(
        typeof(Keire.Prefab), typeof(Keire.StableAssetTypeIdAttribute));
    Assert(marker?.Id == Guid.Parse("4b454952-4550-5245-4641-424153535401"),
           "Prefab must expose the native prefab asset type ID for serialized references.");

    Keire.Prefab reference = Keire.Asset.FromId<Keire.Prefab>(new Keire.AssetId(101, 202))!;
    Assert(reference.IsValid && reference.Id == new Keire.AssetId(101, 202),
           "Prefab objects must preserve stable prefab identity.");
}

static void InspectorAttributeContract()
{
    var range = new Keire.RangeAttribute(-2.0, 8.0);
    var minimum = new Keire.MinAttribute(0.0);
    var maximum = new Keire.MaxAttribute(100.0);
    var step = new Keire.InspectorStepAttribute(0.25);
    var multiline = new Keire.MultilineAttribute(6);
    var displayName = new Keire.InspectorNameAttribute("Movement Speed");
    Assert(range.Minimum == -2.0 && range.Maximum == 8.0 && minimum.Minimum == 0.0 &&
               maximum.Maximum == 100.0 && step.Step == 0.25 && multiline.Lines == 6 &&
               displayName.Name == "Movement Speed",
           "Inspector attributes must preserve validated authoring metadata exactly.");

    AssertThrows<ArgumentOutOfRangeException>(() => _ = new Keire.RangeAttribute(1.0, 1.0),
                                              "Inspector sliders require an increasing range.");
    AssertThrows<ArgumentOutOfRangeException>(() => _ = new Keire.MinAttribute(double.NaN),
                                              "Inspector minimums must reject non-finite values.");
    AssertThrows<ArgumentOutOfRangeException>(() => _ = new Keire.InspectorStepAttribute(0.0),
                                              "Inspector drag steps must be positive.");
    AssertThrows<ArgumentOutOfRangeException>(() => _ = new Keire.MultilineAttribute(33),
                                              "Inspector multiline fields must remain bounded.");
    AssertThrows<ArgumentException>(() => _ = new Keire.InspectorNameAttribute(" "),
                                    "Inspector labels must contain visible text.");
}

static void ScriptableObjectAuthoringContract()
{
    string exported = Keire.ManagedAssetMetadata.Export();
    using System.Text.Json.JsonDocument document = System.Text.Json.JsonDocument.Parse(exported);
    string fullName = typeof(ScriptableObjectAuthoringProbe).FullName!;
    System.Text.Json.JsonElement descriptor = document.RootElement.GetProperty("types").EnumerateArray()
        .Single(type => type.GetProperty("fullName").GetString() == fullName);
    Assert(descriptor.GetProperty("menuPath").GetString() == "Gameplay/Authoring Probe",
           "CreateAssetMenu must publish the ScriptableObject into the editor Create menu.");

    System.Text.Json.JsonElement property = descriptor.GetProperty("properties").EnumerateArray().Single();
    string stableFieldId = property.GetProperty("stableFieldId").GetString()!;
    Assert(Guid.TryParse(stableFieldId, out Guid parsedFieldId) && parsedFieldId != Guid.Empty,
           "Public ScriptableObject fields must receive deterministic implicit stable IDs.");
    using System.Text.Json.JsonDocument second = System.Text.Json.JsonDocument.Parse(Keire.ManagedAssetMetadata.Export());
    string repeatedFieldId = second.RootElement.GetProperty("types").EnumerateArray()
        .Single(type => type.GetProperty("fullName").GetString() == fullName)
        .GetProperty("properties").EnumerateArray().Single().GetProperty("stableFieldId").GetString()!;
    Assert(stableFieldId == repeatedFieldId,
           "Implicit ScriptableObject field IDs must remain deterministic across discovery passes.");

    var target = Keire.ScriptableObject.CreateInstance<ScriptableObjectAuthoringProbe>();
    string hydration = System.Text.Json.JsonSerializer.Serialize(new
    {
        schemaVersion = 1,
        managedTypeId = "73616e64-626f-4078-8000-00000000c001",
        fields = new[] { new { stableId = stableFieldId, name = "Value", value = 42 } },
    });
    target.RuntimeHydrateManagedData(hydration);
    Assert(target.Value == 42, "Implicit stable field IDs must hydrate the persistent ScriptableObject instance.");

    string dictionaryName = typeof(DictionaryAuthoringProbe).FullName!;
    System.Text.Json.JsonElement dictionaryDescriptor = document.RootElement.GetProperty("types").EnumerateArray()
        .Single(type => type.GetProperty("fullName").GetString() == dictionaryName);
    System.Text.Json.JsonElement dictionaryProperty = dictionaryDescriptor.GetProperty("properties").EnumerateArray()
        .Single();
    Assert(dictionaryProperty.GetProperty("kind").GetInt32() == 15 &&
               dictionaryProperty.GetProperty("children").GetArrayLength() == 2 &&
               dictionaryProperty.GetProperty("children")[0].GetProperty("name").GetString() == "Key" &&
               dictionaryProperty.GetProperty("children")[1].GetProperty("name").GetString() == "Value",
           "Managed asset metadata must describe exact dictionaries with deterministic Key and Value children.");

    string dictionaryFieldId = dictionaryProperty.GetProperty("stableFieldId").GetString()!;
    var dictionaryTarget = Keire.ScriptableObject.CreateInstance<DictionaryAuthoringProbe>();
    string dictionaryHydration = System.Text.Json.JsonSerializer.Serialize(new
    {
        schemaVersion = 1,
        managedTypeId = "73616e64-626f-4078-8000-00000000c002",
        fields = new[]
        {
            new
            {
                stableId = dictionaryFieldId,
                name = "Values",
                value = new[] { new { key = "first", value = new[] { new[] { 4, 5, 6 } } } },
            },
        },
    });
    dictionaryTarget.RuntimeHydrateManagedData(dictionaryHydration);
    Assert(dictionaryTarget.Values["first"][0].SequenceEqual([4, 5, 6]),
           "Managed-data hydration must restore recursively nested dictionary/list/array values.");

    string graphTypeName = typeof(ManagedGraphAuthoringProbe).FullName!;
    System.Text.Json.JsonElement graphDescriptor = document.RootElement.GetProperty("types").EnumerateArray()
        .Single(type => type.GetProperty("fullName").GetString() == graphTypeName);
    System.Text.Json.JsonElement graphProperty = graphDescriptor.GetProperty("properties").EnumerateArray()
        .Single(property => property.GetProperty("name").GetString() == nameof(ManagedGraphAuthoringProbe.Root));
    const string graphRuntimeTypeId = "73616e64-626f-4078-8000-00000000d101";
    Assert(graphProperty.GetProperty("referenceGraph").GetBoolean() &&
               graphProperty.GetProperty("referenceTypeChoices").EnumerateArray()
                   .Any(value => value.GetString() == graphRuntimeTypeId) &&
               graphDescriptor.GetProperty("referenceTypes").EnumerateArray()
                   .Any(type => type.GetProperty("stableTypeId").GetString() == graphRuntimeTypeId),
           "ScriptableObject metadata must expose registered concrete choices for SerializeReference fields.");
    ManagedSerializationV3Tests.ValidatePersistentManagedData(graphDescriptor);

    string behaviourName = typeof(ManagedSerializationV3Probe).FullName!;
    System.Text.Json.JsonElement behaviourDescriptor = document.RootElement.GetProperty("behaviours").EnumerateArray()
        .Single(type => type.GetProperty("fullName").GetString() == behaviourName);
    System.Text.Json.JsonElement[] behaviourProperties =
        behaviourDescriptor.GetProperty("properties").EnumerateArray().ToArray();
    System.Text.Json.JsonElement behaviourGraph = behaviourProperties.Single(property =>
        property.GetProperty("name").GetString() == nameof(ManagedSerializationV3Probe.Root));
    System.Text.Json.JsonElement behaviourDictionary = behaviourProperties.Single(property =>
        property.GetProperty("name").GetString() == nameof(ManagedSerializationV3Probe.Nested));
    Assert(behaviourGraph.GetProperty("referenceGraph").GetBoolean() &&
               behaviourDescriptor.GetProperty("referenceTypes").EnumerateArray()
                   .Any(type => type.GetProperty("stableTypeId").GetString() == graphRuntimeTypeId),
           "Behaviour metadata must publish the same registered graph choices to the native Inspector.");
    Assert(!behaviourDictionary.GetProperty("referenceGraph").GetBoolean() &&
               behaviourDictionary.GetProperty("kind").GetInt32() == 15 &&
               behaviourDictionary.GetProperty("children").GetArrayLength() == 2,
           "Behaviour metadata must publish exact Dictionary shapes to the native Inspector.");
    string eventBehaviourName = typeof(EventMetadataProbe).FullName!;
    Assert(!document.RootElement.GetProperty("behaviours").EnumerateArray()
                .Any(type => type.GetProperty("fullName").GetString() == eventBehaviourName),
           "Built-in event listener collections must remain owned by the event Inspector instead of being " +
           "rediscovered as generic nested Behaviour graphs.");
}

static void ManagedMetadataAllowlistContract()
{
    string assemblyName = typeof(FeatureGraphLibrary).Assembly.GetName().Name ??
                          throw new InvalidOperationException("Managed metadata test assembly has no stable name.");
    string[] allowedTypes =
    [
        typeof(FeatureGraphLibrary).FullName!, typeof(FeatureGraphAdd).FullName!,
        typeof(FeatureGraphMultiply).FullName!
    ];
    string request = System.Text.Json.JsonSerializer.Serialize(new
    {
        schemaVersion = 1,
        assemblies = new[] { new { name = assemblyName, types = allowedTypes } },
    });
    string missingTypeRequest = System.Text.Json.JsonSerializer.Serialize(new
    {
        schemaVersion = 1,
        assemblies = new[] { new { name = assemblyName, types = new[] { "Game.MissingFeatureGraphLibrary" } } },
    });

    try
    {
        string expected = Keire.ManagedAssetMetadata.Export(request);
        using System.Text.Json.JsonDocument document = System.Text.Json.JsonDocument.Parse(expected);
        System.Text.Json.JsonElement[] publishedTypes =
            document.RootElement.GetProperty("types").EnumerateArray().ToArray();
        Assert(publishedTypes.Length == 1 &&
                   publishedTypes[0].GetProperty("fullName").GetString() == typeof(FeatureGraphLibrary).FullName,
               "Explicit managed metadata allowlists must exclude ambient ScriptableObject types.");
        System.Text.Json.JsonElement operations = publishedTypes[0].GetProperty("properties").EnumerateArray()
            .Single(property => property.GetProperty("name").GetString() == nameof(FeatureGraphLibrary.Operations));
        string[] choices = operations.GetProperty("children")[0].GetProperty("referenceTypeChoices")
            .EnumerateArray().Select(value => value.GetString()!).ToArray();
        Assert(operations.GetProperty("referenceGraph").GetBoolean() && choices.Length == 2 &&
                   choices.Contains("73616e64-626f-4078-8000-00000000c111", StringComparer.Ordinal) &&
                   choices.Contains("73616e64-626f-4078-8000-00000000c112", StringComparer.Ordinal),
               "Feature Graph libraries must retain registered polymorphic choices in Edit and Play catalogs.");

        string[] concurrent = new string[16];
        Parallel.For(0, concurrent.Length,
                     index => concurrent[index] = Keire.ManagedAssetMetadata.Export(request));
        Assert(concurrent.All(value => value == expected),
               "Concurrent discovery from one committed allowlist must publish deterministic bytes.");

        Task<bool>[] mixedAttempts = Enumerable.Range(0, 16).Select(index => Task.Run(() =>
        {
            if ((index & 1) == 0)
                return Keire.ManagedAssetMetadata.Export(request) == expected;
            try
            {
                _ = Keire.ManagedAssetMetadata.Export(missingTypeRequest);
                return false;
            }
            catch (InvalidOperationException)
            {
                return true;
            }
        })).ToArray();
        Task.WaitAll(mixedAttempts);
        Assert(mixedAttempts.All(attempt => attempt.Result) &&
                   Keire.ManagedAssetMetadata.Export(request) == expected,
               "Repeated failed discovery must not poison a successfully loaded metadata allowlist.");
    }
    finally
    {
        _ = Keire.ManagedAssetMetadata.Export();
    }
}

static unsafe void RuntimeFoundationContract()
{
    NativeFoundationFixture.Install();
    try
    {
        Assert(System.Runtime.InteropServices.Marshal.SizeOf<Keire.NativeScreenState>() == 28,
               "Managed screen state must preserve the native 28-byte ABI layout.");
        Assert(Keire.Application.ProductName == "Nightglass" && Keire.Application.Version == "1.2.3" &&
                   Keire.Application.Identifier == "games.keire.nightglass" && Keire.Application.IsEditor,
               "Application metadata must flow through the native runtime foundation.");
        Assert(Path.IsPathFullyQualified(Keire.Application.PersistentDataPath),
               "Application.PersistentDataPath must be absolute.");

        Keire.Time.TimeScale = 0.35f;
        Keire.Time.Paused = true;
        Assert(MathF.Abs(Keire.Time.TimeScale - 0.35f) < 0.0001f && Keire.Time.Paused,
               "Managed time scale and pause state must round-trip through native services.");
        AssertThrows<ArgumentOutOfRangeException>(() => Keire.Time.TimeScale = float.NaN,
                                                  "Time scale must reject non-finite values before native dispatch.");

        Keire.Resolution resolution = Keire.Screen.CurrentResolution;
        Assert(resolution == new Keire.Resolution(1920, 1080, 3840, 2160, 2.0f) && Keire.Screen.Focused &&
                   Keire.Screen.VSyncEnabled,
               "Screen state must preserve logical and pixel extents, scale, focus, and presentation mode.");
        Assert(Keire.Screen.TrySetResolution(2560, 1440, Keire.FullscreenMode.BorderlessFullscreen) &&
                   NativeFoundationFixture.LastWidth == 2560 && NativeFoundationFixture.LastHeight == 1440 &&
                   NativeFoundationFixture.LastMode == Keire.FullscreenMode.BorderlessFullscreen,
               "Screen changes must reach native services without narrowing dimensions.");

        Keire.Application.Quit(17);
        Assert(NativeFoundationFixture.ExitCode == 17,
               "Application.Quit must preserve the requested process exit code.");
    }
    finally
    {
        NativeFoundationFixture.Uninstall();
    }
}

static void PlayerPreferencesPersistenceContract()
{
    string directory = Path.Combine(Path.GetTempPath(), $"keire-player-preferences-{Guid.NewGuid():N}");
    try
    {
        Keire.PlayerPreferences.ResetForTests(directory);
        Keire.PlayerPreferences.SetString("profile.name", "Astra");
        Keire.PlayerPreferences.SetInt("video.quality", 4);
        Keire.PlayerPreferences.SetFloat("audio.master", 0.625f);
        Keire.PlayerPreferences.SetBool("accessibility.subtitles", true);
        Keire.PlayerPreferences.Save();

        Keire.PlayerPreferences.ResetForTests(directory);
        Assert(Keire.PlayerPreferences.GetString("profile.name") == "Astra" &&
                   Keire.PlayerPreferences.GetInt("video.quality") == 4 &&
                   MathF.Abs(Keire.PlayerPreferences.GetFloat("audio.master") - 0.625f) < 0.0001f &&
                   Keire.PlayerPreferences.GetBool("accessibility.subtitles"),
               "Typed player preferences must survive a complete managed cache reset.");
        Assert(Keire.PlayerPreferences.GetInt("profile.name", 91) == 91,
               "Reading a preference through the wrong type must return the caller's default.");
        Assert(Keire.PlayerPreferences.DeleteKey("video.quality") &&
                   !Keire.PlayerPreferences.HasKey("video.quality"),
               "Preference deletion must update the in-memory transaction.");
        Keire.PlayerPreferences.Save();
        Assert(!Directory.EnumerateFiles(directory, "*.tmp-*", SearchOption.TopDirectoryOnly).Any(),
               "Atomic preference saves must not leave temporary files behind.");
    }
    finally
    {
        Keire.PlayerPreferences.ResetForTests();
        if (Directory.Exists(directory))
            Directory.Delete(directory, recursive: true);
    }
}

static void PlayerPreferencesValidationContract()
{
    string directory = Path.Combine(Path.GetTempPath(), $"keire-player-preferences-invalid-{Guid.NewGuid():N}");
    try
    {
        Keire.PlayerPreferences.ResetForTests(directory);
        AssertThrows<ArgumentException>(() => Keire.PlayerPreferences.SetString("", "value"),
                                        "Empty preference keys must be rejected.");
        AssertThrows<ArgumentOutOfRangeException>(() => Keire.PlayerPreferences.SetFloat("invalid", float.NaN),
                                                  "Non-finite preference floats must be rejected.");
        Directory.CreateDirectory(directory);
        File.WriteAllText(Path.Combine(directory, "player-preferences.json"), "{not-json");
        Keire.PlayerPreferences.ResetForTests(directory);
        AssertThrows<InvalidDataException>(() => Keire.PlayerPreferences.HasKey("profile.name"),
                                           "Corrupt preference files must fail explicitly instead of resetting data.");
        File.WriteAllText(Path.Combine(directory, "player-preferences.json"),
                          "{\"version\":1,\"values\":{\"broken\":{\"kind\":2,\"value\":\"NaN\"}}}");
        Keire.PlayerPreferences.ResetForTests(directory);
        AssertThrows<InvalidDataException>(() => Keire.PlayerPreferences.HasKey("broken"),
                                           "Malformed typed preference values must be rejected transactionally.");
    }
    finally
    {
        Keire.PlayerPreferences.ResetForTests();
        if (Directory.Exists(directory))
            Directory.Delete(directory, recursive: true);
    }
}

static void AnimatorFootGroundingContract()
{
    System.Reflection.MethodInfo? method = typeof(Keire.Animator).GetMethod(
        nameof(Keire.Animator.SetFootGroundingWeight),
        [typeof(float)]);
    Assert(method is not null && method.ReturnType == typeof(void),
           "Animator must expose a model-agnostic runtime foot-grounding weight.");
}

static unsafe void ManagedPhysicsQueryContract()
{
    var invalidContext = new Keire.Entity(1, new Keire.EntityId(2, 3));
    AssertThrows<ArgumentException>(
        () => Keire.Physics.TryRaycast(invalidContext, new Keire.Vector3(float.NaN, 0.0f, 0.0f),
                                       new Keire.Vector3(0.0f, -1.0f, 0.0f), out _),
        "Raycasts must reject non-finite origins before calling native code.");
    AssertThrows<ArgumentException>(
        () => Keire.Physics.TryRaycast(invalidContext, default,
                                       new Keire.Vector3(0.0f, float.PositiveInfinity, 0.0f),
                                       out _),
        "Raycasts must reject non-finite directions before calling native code.");
    AssertThrows<ArgumentOutOfRangeException>(
        () => Keire.Physics.TryRaycast(invalidContext, default, new Keire.Vector3(0.0f, -1.0f, 0.0f), out _,
                                       float.NaN),
        "Raycasts must reject non-finite maximum distances before calling native code.");

    Assert(System.Runtime.InteropServices.Marshal.SizeOf<Keire.NativeRaycastHit>() == 48 &&
               System.Runtime.InteropServices.Marshal.SizeOf<Keire.NativeEntityId>() == 16,
           "Managed physics query structs must preserve their native ABI layouts.");
    NativePhysicsFixture.Install();
    try
    {
        var context = new Keire.Entity(17, new Keire.EntityId(23, 29));
        var ignored = new Keire.Entity(17, new Keire.EntityId(31, 37));
        Assert(Keire.Physics.TryCapsuleCast(context, new Keire.Vector3(1.0f, 2.0f, 3.0f),
                                            new Keire.Quaternion(0.0f, 0.0f, 0.0f, 2.0f), 0.5f, 1.8f,
                                            new Keire.Vector3(0.0f, 0.0f, 4.0f), out Keire.RaycastHit capsuleHit,
                                            0x40u, true, ignored),
               "A valid capsule cast must reach the native runtime.");
        Assert(capsuleHit.Entity == new Keire.Entity(17, new Keire.EntityId(41, 43)) &&
                   capsuleHit.Distance == 2.5f && NativePhysicsFixture.CapsuleCalls == 1 &&
                   NativePhysicsFixture.Rotation == Keire.Quaternion.Identity && NativePhysicsFixture.IncludeTriggers,
               "Capsule casts must normalize rotation and preserve the native hit and trigger policy.");

        IReadOnlyList<Keire.Entity> overlaps = Keire.Physics.OverlapSphere(
            context, new Keire.Vector3(8.0f, 9.0f, 10.0f), 3.0f, 0x80u, false, ignored);
        Assert(overlaps.Count == 2 && overlaps[0] == new Keire.Entity(17, new Keire.EntityId(47, 53)) &&
                   overlaps[1] == new Keire.Entity(17, new Keire.EntityId(59, 61)) &&
                   NativePhysicsFixture.OverlapCalls == 2 && !NativePhysicsFixture.IncludeTriggers,
               "Sphere overlaps must perform one bounded count/copy transaction and preserve world identity.");

        int calls = NativePhysicsFixture.CapsuleCalls + NativePhysicsFixture.OverlapCalls;
        AssertThrows<ArgumentOutOfRangeException>(
            () => Keire.Physics.TryCapsuleCast(context, default, Keire.Quaternion.Identity, 1.0f, 1.5f,
                                                new Keire.Vector3(0.0f, 1.0f, 0.0f), out _),
            "Capsule height must contain the complete diameter.");
        AssertThrows<ArgumentException>(
            () => Keire.Physics.TryCapsuleCast(context, default, Keire.Quaternion.Identity, 0.5f, 1.0f, default,
                                                out _),
            "Capsule displacement cannot be zero.");
        AssertThrows<ArgumentException>(
            () => Keire.Physics.OverlapSphere(context, default, 1.0f, ignoredEntity: new Keire.Entity(
                                                  19, new Keire.EntityId(31, 37))),
            "Ignored overlap entities must belong to the query world.");
        Assert(calls == NativePhysicsFixture.CapsuleCalls + NativePhysicsFixture.OverlapCalls,
               "Invalid managed shape queries must fail before native dispatch.");
    }
    finally
    {
        NativePhysicsFixture.Uninstall();
    }
}

static unsafe void ManagedInputDeviceContract()
{
    Assert(System.Runtime.InteropServices.Marshal.SizeOf<Keire.NativeInputDevice>() == 8 &&
               System.Runtime.InteropServices.Marshal.SizeOf<Keire.NativeInputRebindSnapshot>() == 32,
           "Managed input structs must preserve their native ABI layouts.");
    NativeInputFixture.Install();
    try
    {
        IReadOnlyList<Keire.InputDevice> devices = Keire.Input.Devices;
        Assert(devices.Count == 2 && devices[0] == new Keire.InputDevice(
                   1, Keire.InputDeviceType.Keyboard, "Keyboard", true, true) &&
                   devices[1] == new Keire.InputDevice(7, Keire.InputDeviceType.Gamepad, "Pro Controller", true, true),
               "Managed input must preserve deterministic device metadata and UTF-8 names.");
        Assert(Keire.Input.ControlScheme == "Gamepad" && Keire.Input.TrySetControlScheme("Gamepad") &&
                   NativeInputFixture.SetSchemeCalls == 1 && Keire.Input.ClearControlSchemeLock(),
               "Managed control schemes must preserve native automatic and locked state changes.");

        Assert(Keire.Input.TrySetGamepadRumble(7, 0.25f, 0.75f, 1.5f) && NativeInputFixture.RumbleCalls == 1 &&
                   NativeInputFixture.LowFrequency == 0.25f && NativeInputFixture.HighFrequency == 0.75f &&
                   NativeInputFixture.DurationSeconds == 1.5f,
               "Managed rumble must preserve normalized motor strengths and bounded duration.");

        var options = new Keire.InputRebindOptions(0.65f, 8.0, Keire.InputDeviceMask.Gamepad);
        Keire.InputRebindOperation operation = Keire.Input.BeginInteractiveRebind(new Keire.AssetId(71, 73), options);
        Keire.InputRebindSnapshot snapshot = operation.Snapshot;
        Assert(operation.IsValid && NativeInputFixture.BeginRebindCalls == 1 &&
                   snapshot.Binding == new Keire.AssetId(71, 73) &&
                   snapshot.Status == Keire.InputRebindStatus.Candidate &&
                   snapshot.CandidatePath == "<Gamepad>/buttonSouth" && snapshot.RemainingSeconds == 3.5 &&
                   snapshot.ConflictCount == 2 && operation.Apply(Keire.InputRebindResolution.KeepBoth) &&
                   NativeInputFixture.ResolveCalls == 1 &&
                   NativeInputFixture.Resolution == Keire.InputRebindResolution.KeepBoth,
               "Managed interactive rebinding must preserve candidates conflicts timing and resolution.");

        Assert(Keire.Input.SaveBindingOverrides("Player_1") && Keire.Input.LoadBindingOverrides("Player_1") == 3 &&
                   Keire.Input.ClearBindingOverrides() && NativeInputFixture.PersistenceCalls == 3,
               "Managed binding override profiles must preserve native save load and clear transactions.");

        int nativeCalls = NativeInputFixture.RumbleCalls + NativeInputFixture.BeginRebindCalls;
        AssertThrows<ArgumentOutOfRangeException>(() => Keire.Input.TrySetGamepadRumble(7, -0.1f, 0.5f, 1.0f),
                                                  "Rumble strengths must be normalized before native dispatch.");
        AssertThrows<ArgumentOutOfRangeException>(
            () => Keire.Input.BeginInteractiveRebind(new Keire.AssetId(71, 73),
                                                     new Keire.InputRebindOptions(0.5f, 0.0,
                                                                                  Keire.InputDeviceMask.Gamepad)),
            "Interactive rebind timeouts must be positive and bounded.");
        AssertThrows<ArgumentException>(() => Keire.Input.SaveBindingOverrides("../escape"),
                                        "Binding profiles must reject traversal before native dispatch.");
        Assert(nativeCalls == NativeInputFixture.RumbleCalls + NativeInputFixture.BeginRebindCalls,
               "Invalid managed input operations must not reach native code.");
    }
    finally
    {
        NativeInputFixture.Uninstall();
    }
}

static unsafe void ManagedInputActionContract()
{
    Assert(System.Runtime.InteropServices.Marshal.SizeOf<Keire.NativeInputActionSnapshot>() == 24 &&
               System.Runtime.InteropServices.Marshal.SizeOf<Keire.NativeInputControlSnapshot>() == 24,
           "Managed action and control snapshots must preserve their native ABI layouts.");
    NativeInputFixture.Install();
    Keire.NativeRuntime.InstallManagedAssetsForTests(8101, 4, 2, null);
    try
    {
        var asset = Keire.Asset.FromId<Keire.InputActionAsset>(new Keire.AssetId(31, 37))!;
        using Keire.InputActionContext context = asset.CreateContext();
        Keire.InputActionMap map = context.FindActionMap("Player")!;
        Keire.InputAction action = map.FindAction("Interact")!;
        int performed = 0;
        Action<Keire.InputAction.CallbackContext> onPerformed = callback =>
        {
            Assert(callback.ReadValue<bool>() && callback.Phase == Keire.InputActionPhase.Performed,
                   "Action callback contexts must preserve values and phases.");
            ++performed;
        };
        action.performed += onPerformed;
        map.Enable();
        Assert(NativeInputFixture.LastContextOperation == 2 &&
                   NativeInputFixture.LastContextTarget == new Keire.AssetId(41, 43),
               "Map wrappers must enable their stable map ID instead of relying on implicit context state.");
        Keire.InputActionRuntime.DispatchEvents();
        Keire.InputActionRuntime.DispatchEvents();
        Assert(action.Enabled && action.IsPressed && action.WasPressedThisFrame && action.WasPerformedThisFrame &&
                   performed == 1,
               "Managed actions must expose values, edges, and at-most-once transition callbacks per frame.");
        Assert(action.BeginInteractiveRebind(new Keire.AssetId(71, 73)).IsValid,
                "Interactive rebinding must retain the action's explicit context.");
        action.performed -= onPerformed;
        int snapshotsBeforeUnregisteredDispatch = NativeInputFixture.ActionSnapshotCalls;
        Keire.InputActionRuntime.DispatchEvents();
        Assert(NativeInputFixture.ActionSnapshotCalls == snapshotsBeforeUnregisteredDispatch,
               "Removing an action's final callback must unregister its per-frame native snapshot polling.");

        Keire.Keyboard keyboard = Keire.Input.Keyboard.Current!;
        Keire.ButtonControl key = keyboard.wKey;
        Assert(key.IsPressed && key.WasPressed && key.WasPressedThisFrame && !key.WasReleasedThisFrame,
               "Direct keyboard polling must expose held and frame-edge state.");
        Assert(keyboard.eKey.Path == "<Keyboard>/e" && keyboard.f12Key.Path == "<Keyboard>/f12",
               "Unity-style keyboard properties must cover ordinary and function keys.");
        Assert(Keire.Input.Gamepad.Current?.DeviceId == 7,
                "Current direct devices must follow the native player's paired device selection.");
        Assert(Keire.Input.Mouse.Current?.WheelUp.Path == "<Mouse>/wheelUp" &&
                   Keire.Input.Mouse.Current?.WheelDown.Path == "<Mouse>/wheelDown" &&
                   Keire.Input.Gamepad.Current?.LeftStickButton.Path == "<Gamepad>/leftStickPress" &&
                   Keire.Input.Gamepad.Current?.RightStickButton.Path == "<Gamepad>/rightStickPress",
               "Direct input must expose wheel pulses and both gamepad stick buttons through canonical paths.");
    }
    finally
    {
        NativeInputFixture.Uninstall();
        _ = Keire.NativeRuntime.ResetManagedAssets(8101);
    }
}

static unsafe void NativeUiDocumentBridgeContract()
{
    NativeUiDocumentFixture.Install();
    var entity = new Keire.Entity(17, new Keire.EntityId(23, 29));
    var document = new Keire.UI.UIDocument(entity);
    try
    {
        Keire.UI.RuntimeVisualElement root = document.RootVisualElement ??
            throw new InvalidOperationException("The live UI Document root was not resolved.");
        Keire.UI.RuntimeVisualElement named = document.Q("launch") ??
            throw new InvalidOperationException("The named source-backed element was not resolved.");
        Keire.UI.RuntimeVisualElement stable = document.Q(NativeUiDocumentFixture.ButtonId) ??
            throw new InvalidOperationException("The stable-ID source-backed element was not resolved.");

        Assert(root.Type == Keire.UI.RuntimeVisualElementType.Panel && named.Type == Keire.UI.RuntimeVisualElementType.Button,
               "Document queries must preserve the native retained element types.");
        Assert(named.StableId == NativeUiDocumentFixture.ButtonId && stable.StableId == named.StableId,
               "Name and stable-ID queries must resolve the same authored element.");
        Assert(named.Text == "Launch" && MathF.Abs(named.Value - 25.0f) < 0.0001f,
               "Document elements must read source-backed text and values.");
        named.Text = "Continue";
        named.Value = 72.5f;
        named.Checked = true;
        named.Interactable = false;
        named.Enabled = false;
        Assert(NativeUiDocumentFixture.Text == "Continue" &&
                   MathF.Abs(NativeUiDocumentFixture.Value - 72.5f) < 0.0001f &&
                   NativeUiDocumentFixture.Checked && !NativeUiDocumentFixture.Interactable &&
                   !NativeUiDocumentFixture.Enabled,
               "Document element mutations must reach the live native retained tree.");
        named.Enabled = true;
        named.Interactable = true;
        named.Focus();
        Assert(named.HasFocus, "Document element focus must round-trip through the native presentation.");
        Assert(named.ClickedThisFrame && !named.ClickedThisFrame,
               "Document click events must be delivered exactly once.");
        Assert(named.ChangedThisFrame && !named.ChangedThisFrame,
               "Document change events must be delivered exactly once.");

        NativeUiDocumentFixture.FailReload();
        Assert(named.IsAlive && named.Text == "Continue",
               "A failed UI Document reload must preserve the last-good element generation.");
        NativeUiDocumentFixture.CommitReload();
        Assert(!named.IsAlive, "A successful UI Document reload must invalidate stale element handles.");
        AssertThrows<InvalidOperationException>(() => _ = named.Text,
            "A stale UI Document element must fail safely instead of reading a replacement generation.");
        Assert(document.Q("launch")?.IsAlive == true,
               "A successful reload must publish a fresh queryable generation.");
        NativeUiDocumentFixture.Destroy();
        Assert(document.RootVisualElement is null,
               "A destroyed UI Document must stop exposing a native retained root.");
    }
    finally
    {
        NativeUiDocumentFixture.Uninstall();
    }
}

static unsafe void ManagedRenderingContract()
{
    NativeRenderingFixture.Install();
    var entity = new Keire.Entity(17, new Keire.EntityId(23, 29));
    try
    {
        var camera = new Keire.Camera(entity);
        Assert(MathF.Abs(camera.VerticalFieldOfView - 68.0f) < 0.0001f && camera.Primary,
               "Camera handles must read native lens and selection state.");
        camera.VerticalFieldOfView = 91.0f;
        camera.Projection = Keire.CameraProjection.Orthographic;
        Assert(NativeRenderingFixture.ScalarComponent == Keire.NativeRenderingComponent.Camera &&
                   NativeRenderingFixture.ScalarProperty == Keire.NativeRenderingScalarProperty.VerticalFieldOfView &&
                   MathF.Abs(NativeRenderingFixture.ScalarValue - 91.0f) < 0.0001f &&
                   NativeRenderingFixture.IntegerValue == (int)Keire.CameraProjection.Orthographic,
               "Camera writes must preserve the native component, property, and value.");

        var renderer = new Keire.MeshRenderer(entity);
        Assert(renderer.Materials.Count == 2 && renderer.Materials[1].Id == NativeRenderingFixture.SecondMaterial,
               "Mesh Renderer material arrays must round-trip through the bounded native ABI.");
        renderer.AlwaysVisible = true;
        Assert(renderer.AlwaysVisible &&
                   NativeRenderingFixture.FlagProperty == Keire.NativeRenderingFlagProperty.AlwaysVisible &&
                   NativeRenderingFixture.FlagValue,
               "Mesh Renderer must expose the authored frustum-culling override through the managed ABI.");
        renderer.Materials = [Keire.Asset.FromId<Keire.Material>(NativeRenderingFixture.ReplacementMaterial)!];
        renderer.PropertyBlock.SetFloat("Roughness", 0.37f);
        renderer.PropertyBlock.SetTexture(
            "Albedo", Keire.Asset.FromId<Keire.Texture>(NativeRenderingFixture.ReplacementTexture));
        Keire.DynamicMaterial instance = renderer.GetMaterialInstance(1);
        instance.SetColor("Emission", new Keire.Color(0.2f, 0.4f, 0.8f, 1.0f));
        var collection = Keire.Asset.FromId<Keire.MaterialParameterCollection>(new Keire.AssetId(301, 401))!;
        Keire.MaterialParameterCollectionInstance globals = Keire.GlobalMaterialParameters.Open(collection);
        Assert(globals.IsReady, "Material Parameter Collections must expose their asynchronous readiness.");
        globals.SetFloat("Rain", 0.85f);
        Assert(NativeRenderingFixture.MaterialCount == 1 &&
                   NativeRenderingFixture.FirstMaterial == NativeRenderingFixture.ReplacementMaterial &&
                   MathF.Abs(NativeRenderingFixture.MaterialFloat - 0.37f) < 0.0001f &&
                   NativeRenderingFixture.MaterialTexture == NativeRenderingFixture.ReplacementTexture &&
                   NativeRenderingFixture.InstanceSlot == 1 &&
                   NativeRenderingFixture.GlobalCollection == collection.Id &&
                   MathF.Abs(NativeRenderingFixture.GlobalFloat - 0.85f) < 0.0001f,
               "Material slots, dynamic instances, and global parameters must reach native renderer state.");
        AssertThrows<ArgumentOutOfRangeException>(() => renderer.PropertyBlock.SetFloat("Invalid", float.NaN),
                                                  "Material property blocks must reject non-finite values early.");

        var spot = new Keire.SpotLight(entity);
        spot.OuterAngle = 46.0f;
        spot.CookieOffset = new Keire.Vector2(0.25f, 0.5f);
        Assert(NativeRenderingFixture.ScalarComponent == Keire.NativeRenderingComponent.SpotLight &&
                   NativeRenderingFixture.ScalarProperty == Keire.NativeRenderingScalarProperty.OuterAngle &&
                   MathF.Abs(NativeRenderingFixture.ScalarValue - 46.0f) < 0.0001f &&
                   NativeRenderingFixture.VectorValue == new Keire.Vector2(0.25f, 0.5f),
               "Typed light handles must preserve cone and cookie controls.");
    }
    finally
    {
        NativeRenderingFixture.Uninstall();
    }
}

static unsafe void ManagedWorldContract()
{
    NativeWorldFixture.Install();
    try
    {
        Assert(System.Runtime.InteropServices.Marshal.SizeOf<Keire.NativeSceneLoadStatus>() == 32 &&
                   System.Runtime.InteropServices.Marshal.SizeOf<Keire.NativeSceneHandle>() == 24 &&
                   System.Runtime.InteropServices.Marshal.SizeOf<Keire.NativeEntityHandle>() == 24 &&
                   System.Runtime.InteropServices.Marshal.SizeOf<Keire.NativeRenderEnvironment>() == 72,
               "Managed world structs must preserve their native ABI layouts.");
        Keire.Scene activeScene = Keire.SceneManager.ActiveScene!;
        Assert(activeScene.Asset.Id == NativeWorldFixture.CurrentScene &&
                   activeScene.Id == NativeWorldFixture.CurrentHandle &&
                   Keire.SceneManager.LoadedScenes is [{ Asset: var loaded, Id: var loadedHandle }] &&
                   loaded.Id == NativeWorldFixture.CurrentScene && loadedHandle == NativeWorldFixture.CurrentHandle,
               "Scene Manager must expose the native active and loaded scene state.");

        Keire.SceneLoadOperation load = Keire.SceneManager.LoadSceneAsync(
            Keire.Asset.FromId<Keire.SceneAsset>(NativeWorldFixture.ReplacementScene)!);
        Assert(load.State == Keire.SceneLoadState.Loading && MathF.Abs(load.Progress - 0.5f) < 0.0001f &&
                   load.KeepWaiting,
               "Scene load operations must expose progress and remain coroutine-compatible while loading.");
        NativeWorldFixture.CompleteLoad();
        Assert(load.Succeeded && load.IsDone && !load.KeepWaiting &&
                    load.Scene!.Asset.Id == NativeWorldFixture.ReplacementScene &&
                    load.Scene.Id == NativeWorldFixture.ReplacementHandle,
               "A committed native scene transition must publish a successful terminal operation.");

        Keire.RenderEnvironmentSettings environment = Keire.RenderSettings.Current;
        Assert(MathF.Abs(environment.Exposure - 1.25f) < 0.0001f && environment.SkyVisible,
               "Render settings must read the current native environment transaction.");
        Keire.RenderSettings.Current = environment with
        {
            Exposure = 1.8f,
            AmbientIntensity = 0.45f,
            DirectionalShadowResolution = 4096
        };
        Assert(NativeWorldFixture.RenderSetCount == 1 &&
                   MathF.Abs(NativeWorldFixture.Environment.Exposure - 1.8f) < 0.0001f &&
                   NativeWorldFixture.Environment.DirectionalShadowResolution == 4096,
               "Render settings must commit complete validated environment values atomically.");
        AssertThrows<ArgumentOutOfRangeException>(
            () => Keire.RenderSettings.Current = environment with { Exposure = float.NaN },
            "Render settings must reject non-finite values before native dispatch.");
        Assert(NativeWorldFixture.RenderSetCount == 1,
               "Rejected render settings must leave native state unchanged.");

        var player = new Keire.Entity(99, new Keire.EntityId(1, 2));
        Assert(player.Tags.SequenceEqual(["Player", "Faction.Friendly"]) && player.HasTag("Player"),
               "Entity tags must preserve bounded native ordering and exact matching.");
        Assert(player.AddTag("Objective.Carrier") && player.RemoveTag("Faction.Friendly"),
               "Validated entity tag mutations must reach the active native world.");
        player.ClearTags();
        Assert(NativeWorldFixture.TagMutationCount == 3,
               "Add, remove, and clear tag mutations must each dispatch exactly once.");
        AssertThrows<ArgumentException>(() => player.AddTag("invalid tag"),
                                        "Managed tags must reject invalid identifiers before native dispatch.");

        Assert(Keire.SceneManager.FindByName("PlayerSpawn") == player &&
                   Keire.SceneManager.FindAllWithTag("Enemy").Count == 2 &&
                   Keire.SceneManager.FindAllWithComponent<Keire.CharacterController>() is [{ } controller] &&
                   controller == player,
                "Scene queries must preserve stable world/entity handles across name, tag, and component indexes.");
        Assert(Keire.SceneManager.FindAllWithTag("Enemy", Keire.SceneQuery.Loaded).Count == 2 &&
                   NativeWorldFixture.LastQueryScope == Keire.SceneQueryScope.Loaded,
               "Scene queries must forward explicit loaded-scene scopes.");
        Assert(Keire.SceneManager.FindAllByName("PlayerSpawn", Keire.SceneQuery.In(activeScene))
                       .Single() == player &&
                   NativeWorldFixture.LastQueryHandle == NativeWorldFixture.CurrentHandle,
               "Specific-scene queries must preserve the opaque stable handle.");
        Assert(Keire.SceneManager.SetActiveScene(activeScene) &&
                   Keire.SceneManager.UnloadScene(activeScene) &&
                   Keire.SceneManager.Preserve(player),
               "Activation, unloading, and persistent-object requests must reach the native runtime.");
        AssertThrows<ArgumentOutOfRangeException>(() => Keire.SceneManager.FindAllWithTag("Enemy", 0),
                                                  "Scene queries must reject unbounded or empty result limits.");
    }
    finally
    {
        NativeWorldFixture.Uninstall();
    }
}

static void ManagedJobExecutionContract()
{
    int invocations = 0;
    var succeeded = new Keire.ManagedJobState(
        context =>
        {
            Assert(!context.IsCancellationRequested, "A normal managed job must receive a live cancellation token.");
            ++invocations;
        });
    succeeded.SetId(41);
    Assert(succeeded.Invoke(0, 0) == 0 && invocations == 1,
           "The managed job work phase must execute its delegate exactly once.");
    Assert(succeeded.Status == Keire.JobStatus.Running, "The managed job must report Running during execution.");
    Assert(succeeded.Invoke(1, 0) == 0, "The managed job success phase must complete.");
    succeeded.Completion.GetAwaiter().GetResult();
    Assert(succeeded.Status == Keire.JobStatus.Succeeded && succeeded.Completion.IsCompletedSuccessfully,
           "A successful managed delegate must publish a successful completion task.");

    var failed = new Keire.ManagedJobState(_ => throw new InvalidOperationException("expected managed failure"));
    failed.SetId(42);
    Assert(failed.Invoke(0, 0) != 0, "A throwing managed delegate must fail its native work phase.");
    Assert(failed.Invoke(2, 0) == 0 && failed.Status == Keire.JobStatus.Failed,
           "The native failure phase must publish Failed.");
    AssertThrows<InvalidOperationException>(() => failed.Completion.GetAwaiter().GetResult(),
                                            "The original managed exception must flow through Completion.");

    bool observedCancellation = false;
    var cancelled = new Keire.ManagedJobState(context => observedCancellation = context.IsCancellationRequested);
    cancelled.SetId(43);
    Assert(cancelled.Invoke(0, 1) == 0 && observedCancellation,
           "A stop-requested managed job must observe cancellation before its delegate runs.");
    Assert(cancelled.Invoke(3, 0) == 0 && cancelled.Status == Keire.JobStatus.Cancelled,
           "The native cancellation phase must publish Cancelled.");
    AssertThrows<TaskCanceledException>(() => cancelled.Completion.GetAwaiter().GetResult(),
                                        "A cancelled managed job must publish a cancelled completion task.");

    CancellationToken propagatedToken = default;
    var scopeCancelled = new Keire.ManagedJobState(context => propagatedToken = context.CancellationToken);
    scopeCancelled.SetId(44);
    Assert(scopeCancelled.Invoke(0, 0) == 0 && !propagatedToken.IsCancellationRequested,
           "A running managed job must begin with a live token.");
    Assert(scopeCancelled.Invoke(4, 0) == 0 && propagatedToken.IsCancellationRequested,
           "Native scope cancellation must propagate to the running managed cancellation token.");
    Assert(scopeCancelled.Invoke(3, 0) == 0, "The scope-cancelled job must publish its terminal state.");
}

static void ManagedJobCancellationExceptionContract()
{
    NativeJobFixture.Install();
    try
    {
        CancellationToken managedToken = default;
        Keire.Job managedCancelled = Keire.Jobs.Submit(context => managedToken = context.CancellationToken);
        Assert(managedCancelled.Id == NativeJobFixture.LastSubmittedId && NativeJobFixture.Invoke(0, 0) == 0,
               "The cancellation fixture must submit and enter its running phase through the interop callback.");
        using (managedToken.Register(() => throw new InvalidOperationException("expected cancellation failure")))
        {
            AssertThrows<AggregateException>(() => managedCancelled.Cancel(),
                "Managed cancellation callback failures must remain observable to the caller.");
        }
        Assert(managedToken.IsCancellationRequested && NativeJobFixture.CancelCount == 1 &&
                   NativeJobFixture.LastCancelledId == managedCancelled.Id &&
                   !NativeJobFixture.CurrentState!.IsInteropHandleReleased,
               "Managed cancellation must still request native cancellation when token callbacks throw.");
        Assert(NativeJobFixture.Invoke(3, 0) == 0 && managedCancelled.Status == Keire.JobStatus.Cancelled,
               "A callback failure during cancellation must not replace native terminal delivery.");
        AssertThrows<TaskCanceledException>(() => managedCancelled.Completion.GetAwaiter().GetResult(),
                                            "The cancelled job must retain its native terminal result.");
        managedCancelled.Cancel();
        Assert(NativeJobFixture.CancelCount == 1,
               "Cancelling a terminal managed job must not issue another native cancellation request.");

        CancellationToken nativeToken = default;
        Keire.Job nativeCancelled = Keire.Jobs.Submit(context => nativeToken = context.CancellationToken);
        Assert(NativeJobFixture.Invoke(0, 0) == 0,
               "The native cancellation fixture must enter its running phase through the interop callback.");
        using (nativeToken.Register(() => throw new InvalidOperationException("expected native cancellation failure")))
        {
            Assert(NativeJobFixture.Invoke(4, 0) == 0 && nativeToken.IsCancellationRequested,
                   "Native cancellation must contain token callback failures at the interop boundary.");
        }
        Assert(!NativeJobFixture.CurrentState!.IsInteropHandleReleased,
               "A nonterminal native cancellation callback must retain the managed interop handle.");
        Assert(NativeJobFixture.Invoke(3, 0) == 0 &&
                   NativeJobFixture.CurrentState.IsInteropHandleReleased &&
                   nativeCancelled.Status == Keire.JobStatus.Cancelled,
               "Only native terminal delivery may release the managed interop handle.");
        NativeJobFixture.CurrentState.ReleaseHandle();
    }
    finally
    {
        NativeJobFixture.Uninstall();
    }
}

static void ManagedJobDependencyContract()
{
    var activeState = new Keire.ManagedJobState(_ => { });
    activeState.SetId(51);
    var succeededState = new Keire.ManagedJobState(_ => { });
    succeededState.SetId(52);
    Assert(succeededState.Invoke(0, 0) == 0 && succeededState.Invoke(1, 0) == 0,
           "The completed dependency fixture must succeed.");

    ulong[] dependencies = Keire.Jobs.CollectDependencyIds(
        new[] { new Keire.Job(activeState), new Keire.Job(succeededState) }, out bool cancelled);
    Assert(!cancelled && dependencies.Length == 1 && dependencies[0] == 51,
           "Succeeded dependencies must be omitted because their ordering constraint is already satisfied.");
    var failedState = new Keire.ManagedJobState(_ => throw new InvalidOperationException("expected dependency failure"));
    failedState.SetId(53);
    Assert(failedState.Invoke(0, 0) != 0 && failedState.Invoke(2, 0) == 0,
           "The failed dependency fixture must publish a terminal failure.");
    dependencies = Keire.Jobs.CollectDependencyIds(new[] { new Keire.Job(failedState) }, out cancelled);
    Assert(cancelled && dependencies.Length == 0,
           "A reclaimed failed dependency must still cancel dependent work without requiring a native record.");
    AssertThrows<ArgumentException>(() =>
        Keire.Jobs.CollectDependencyIds(new Keire.Job[] { null! }, out _),
                                    "Invalid dependency handles must be rejected before native submission.");
}

static void BehaviourLifecycleContract()
{
    var behaviour = new DetachedManagedContractProbe();
    Assert(behaviour.Enabled, "Detached behaviours must begin enabled.");
    behaviour.Enabled = false;
    Assert(!behaviour.Enabled, "Detached Behaviour.Enabled state must remain coherent before native attachment.");

    System.Reflection.MethodInfo? animatorIk = typeof(Keire.Behaviour).GetMethod(
        "RuntimeAnimatorIk", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Public);
    Assert(animatorIk is not null && animatorIk.GetParameters() is [{ ParameterType: var parameterType }] &&
           parameterType == typeof(float), "The native runtime must retain the Animator IK callback entry point.");

    var dependency = new Keire.RequireComponentAttribute(typeof(Keire.CharacterController));
    Keire.ComponentTypeId expected = Keire.ComponentType.Of<Keire.CharacterController>();
    Assert(dependency.High == expected.High && dependency.Low == expected.Low,
        "RequireComponent metadata must expose the dependency's stable native component ID.");

    var reloadProbe = new ReloadLifecycleProbe();
    reloadProbe.RuntimeBeforeReload();
    Assert(reloadProbe.BeforeReloadCount == 1 && reloadProbe.AfterReloadCount == 0,
           "Preparing a managed reload must invoke only the before-reload callback.");
    reloadProbe.RuntimeResumeAfterFailedReload();
    Assert(reloadProbe.BeforeReloadCount == 1 && reloadProbe.AfterReloadCount == 1,
           "A failed managed reload must pair cleanup with the after-reload callback on the retained instance.");
    reloadProbe.RuntimeDestroy();
}

static void ManagedStateMathContract()
{
    var source = new ManagedStateMathProbe
    {
        Position = new Keire.Vector3(1.25f, -2.5f, 3.75f),
        Aim = new Keire.Vector2(-0.5f, 0.75f),
        Rotation = Keire.Quaternion.Euler(12.0f, 34.0f, 56.0f),
    };

    string state = Keire.ManagedStateSerializer.Capture(source, string.Empty, false);
    Assert(!state.Contains("Normalized", StringComparison.Ordinal) &&
               !state.Contains("Length", StringComparison.Ordinal),
           "Managed state must not traverse computed math properties.");

    var restored = new ManagedStateMathProbe();
    string warnings = Keire.ManagedStateSerializer.Restore(restored, state, false);
    Assert(string.IsNullOrEmpty(warnings), "Stable managed math fields must restore without migration warnings.");
    Assert(restored.Position == source.Position && restored.Aim == source.Aim && restored.Rotation == source.Rotation,
           "Managed vector and quaternion fields must round-trip exactly.");
}

static void CoroutineContract()
{
    var scheduler = new Keire.CoroutineScheduler();
    var events = new List<string>();
    int disposed = 0;
    Exception? failure = null;
    scheduler.UnhandledException = exception => failure = exception;

    System.Collections.IEnumerator Routine()
    {
        try
        {
            events.Add("start");
            yield return null;
            events.Add("update");
            yield return new Keire.WaitForFixedUpdate();
            events.Add("fixed");
            yield return new Keire.WaitForEndOfFrame();
            events.Add("late");
        }
        finally
        {
            ++disposed;
        }
    }

    Keire.Coroutine coroutine = scheduler.Start(Routine());
    Assert(coroutine.IsRunning && events.SequenceEqual(["start"]),
           "Starting a coroutine must advance it to its first yield.");
    scheduler.Pump(Keire.CoroutinePhase.FixedUpdate, 0.02f, 0.02f);
    Assert(events.Count == 1, "A null yield must wait for the next Update phase.");
    scheduler.Pump(Keire.CoroutinePhase.Update, 0.1f, 0.2f);
    Assert(events.SequenceEqual(["start", "update"]), "The next Update must resume a null yield.");
    scheduler.Pump(Keire.CoroutinePhase.Update, 0.1f, 0.2f);
    Assert(events.Count == 2, "WaitForFixedUpdate must not resume during Update.");
    scheduler.Pump(Keire.CoroutinePhase.FixedUpdate, 0.02f, 0.02f);
    Assert(events.SequenceEqual(["start", "update", "fixed"]), "WaitForFixedUpdate must resume during FixedUpdate.");
    scheduler.Pump(Keire.CoroutinePhase.LateUpdate, 0.0f, 0.0f);
    Assert(events.SequenceEqual(["start", "update", "fixed", "late"]) && !coroutine.IsRunning && disposed == 1,
           "WaitForEndOfFrame must resume during LateUpdate and completed iterators must be disposed.");

    Keire.Coroutine stopped = scheduler.Start(Routine());
    Assert(stopped.Stop() && !stopped.IsRunning && disposed == 2,
           "Stopping a coroutine must dispose its iterator exactly once.");

    System.Collections.IEnumerator FailingRoutine()
    {
        try
        {
            throw new InvalidOperationException("expected coroutine failure");
#pragma warning disable CS0162
            yield break;
#pragma warning restore CS0162
        }
        finally
        {
            ++disposed;
        }
    }

    Keire.Coroutine failed = scheduler.Start(FailingRoutine());
    Assert(!failed.IsRunning && failure is InvalidOperationException && disposed == 3 && scheduler.Count == 0,
           "A coroutine that fails during its first move must be removed, disposed, and reported.");
}

static void GameplayHandleContract()
{
    System.Reflection.PropertyInfo? position =
        typeof(Keire.Transform).GetProperty(nameof(Keire.Transform.Position));
    System.Reflection.PropertyInfo? rotation =
        typeof(Keire.Transform).GetProperty(nameof(Keire.Transform.Rotation));
    Assert(position is { CanRead: true, CanWrite: true } && rotation is { CanRead: true, CanWrite: true },
           "World transform properties must remain writable for gameplay scripts.");

    System.Reflection.MethodInfo? addForce =
        typeof(Keire.RigidBody).GetMethod(nameof(Keire.RigidBody.AddForce));
    Assert(addForce is not null && typeof(Keire.Component).IsAssignableFrom(typeof(Keire.RigidBody)),
           "RigidBody must expose the strongly typed component gameplay API.");
    Assert(Enum.GetValues<Keire.ForceMode>().Length == 4,
           "Rigid body force modes must retain force, acceleration, impulse, and velocity-change semantics.");
    var invalidEntity = new Keire.Entity(1, new Keire.EntityId(2, 3));
    var invalidTransform = new Keire.Transform(invalidEntity);
    AssertThrows<ArgumentException>(() => invalidTransform.Position = new(float.NaN, 0.0f, 0.0f),
                                    "World transform setters must reject non-finite values before crossing native code.");
    AssertThrows<ArgumentException>(() => invalidTransform.Rotation = default,
                                    "World rotation setters must reject zero quaternions before crossing native code.");
    var invalidBody = new Keire.RigidBody(invalidEntity);
    AssertThrows<ArgumentOutOfRangeException>(
        () => invalidBody.Motion = (Keire.RigidBodyMotion)byte.MaxValue,
        "Rigid body motion setters must reject undefined modes before crossing native code.");
}

static void EntityLayerContract()
{
    System.Reflection.PropertyInfo? layer = typeof(Keire.Entity).GetProperty(nameof(Keire.Entity.Layer));
    Assert(layer is not null && layer.PropertyType == typeof(uint) && layer.CanRead && layer.CanWrite,
        "Entity.Layer must remain a readable and writable unsigned layer index.");
}

static void CharacterControllerStableContract()
{
    Keire.ComponentTypeId id = Keire.ComponentType.Of<Keire.CharacterController>();
    Assert(id.High == 0x4b45495245434841UL, "Character Controller stable ID high lane changed.");
    Assert(id.Low == 0x5241435445520001UL, "Character Controller stable ID low lane changed.");
}

int failed = 0;
foreach ((string name, Action run) in tests)
{
    try
    {
        run();
        Console.WriteLine($"PASS {name}");
    }
    catch (Exception exception)
    {
        ++failed;
        Console.Error.WriteLine($"FAIL {name}: {exception}");
    }
}

return failed == 0 ? 0 : 1;

static void VfxRangesNormalizeAndValidate()
{
    var scalar = new Keire.VfxRange<float>(5.0f, -2.0f);
    scalar.Deconstruct(out float scalarMinimum, out float scalarMaximum);
    Assert(scalarMinimum == -2.0f && scalarMaximum == 5.0f, "Scalar VFX ranges must canonicalize reversed endpoints.");
    Assert(scalar == new Keire.VfxRange<float>(-2.0f, 5.0f), "Canonical VFX ranges must retain value equality.");

    var integer = new Keire.VfxRange<long>(long.MaxValue, long.MinValue);
    Assert(integer.Minimum == long.MinValue && integer.Maximum == long.MaxValue,
           "Signed integer VFX ranges must preserve their full domain.");
    var unsigned = new Keire.VfxRange<ulong>(ulong.MaxValue, 0UL);
    Assert(unsigned.Minimum == 0UL && unsigned.Maximum == ulong.MaxValue,
           "Unsigned integer VFX ranges must preserve their full domain.");

    var vector2 = new Keire.VfxRange<Keire.Vector2>(new(4.0f, -3.0f), new(-2.0f, 8.0f));
    Assert(vector2.Minimum == new Keire.Vector2(-2.0f, -3.0f) && vector2.Maximum == new Keire.Vector2(4.0f, 8.0f),
           "Vector2 VFX ranges must canonicalize each component independently.");
    var vector3 = new Keire.VfxRange<Keire.Vector3>(new(4.0f, -3.0f, 9.0f), new(-2.0f, 8.0f, 1.0f));
    Assert(vector3.Minimum == new Keire.Vector3(-2.0f, -3.0f, 1.0f) &&
               vector3.Maximum == new Keire.Vector3(4.0f, 8.0f, 9.0f),
           "Vector3 VFX ranges must canonicalize each component independently.");
    var vector4 = new Keire.VfxRange<Keire.Vector4>(new(4.0f, -3.0f, 9.0f, 0.8f), new(-2.0f, 8.0f, 1.0f, 0.2f));
    Assert(vector4.Minimum == new Keire.Vector4(-2.0f, -3.0f, 1.0f, 0.2f) &&
               vector4.Maximum == new Keire.Vector4(4.0f, 8.0f, 9.0f, 0.8f),
           "Vector4 VFX ranges must canonicalize each component independently.");
    var color = new Keire.VfxRange<Keire.Color>(new(0.9f, 0.1f, 0.8f, 1.0f), new(0.2f, 0.7f, 0.3f, 0.4f));
    Assert(color.Minimum == new Keire.Color(0.2f, 0.1f, 0.3f, 0.4f) &&
               color.Maximum == new Keire.Color(0.9f, 0.7f, 0.8f, 1.0f),
           "Color VFX ranges must canonicalize each channel independently.");

    AssertThrows<ArgumentOutOfRangeException>(() => _ = new Keire.VfxRange<float>(float.NaN, 1.0f),
                                              "Non-finite scalar VFX range endpoints must be rejected.");
    AssertThrows<ArgumentOutOfRangeException>(
        () => _ = new Keire.VfxRange<Keire.Vector3>(new(float.PositiveInfinity, 0.0f, 0.0f), default),
        "Non-finite vector VFX range endpoints must be rejected.");
    AssertThrows<NotSupportedException>(() => _ = new Keire.VfxRange<int>(1, 2),
                                        "Unsupported VFX range element types must be rejected.");
}

static void VfxRangeSettersExposeEverySupportedType()
{
    Type[] supportedTypes = {
        typeof(float),         typeof(long),          typeof(ulong),       typeof(Keire.Vector2),
        typeof(Keire.Vector3), typeof(Keire.Vector4), typeof(Keire.Color),
    };
    foreach (Type elementType in supportedTypes)
    {
        Type rangeType = typeof(Keire.VfxRange<>).MakeGenericType(elementType);
        var handleSetter =
            typeof(Keire.VfxEmitter)
                .GetMethod("SetParameter",
                           System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Instance,
                           binder: null, types: new[] { typeof(Keire.AssetId), rangeType }, modifiers: null);
        Assert(handleSetter?.ReturnType == typeof(bool),
               $"VfxEmitter must expose a Boolean {rangeType.Name} setter.");

        var staticSetter = typeof(Keire.Vfx).GetMethod(
            "SetParameter", System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Static, binder: null,
            types: new[] { typeof(Keire.Entity), typeof(Keire.AssetId), rangeType }, modifiers: null);
        Assert(staticSetter?.ReturnType == typeof(bool), $"Vfx must expose a Boolean {rangeType.Name} setter.");
    }
}

static unsafe void RuntimeAssetHandleContract()
{
    NativeAssetFixture.Install();
    try
    {
        Keire.Material reference = Keire.Asset.FromId<Keire.Material>(new Keire.AssetId(101, 201))!;
        using var handle = Keire.Assets.LoadRuntime(reference, Keire.AssetLoadPriority.High);
        Assert(NativeAssetFixture.Generation == 7001 && NativeAssetFixture.Asset == reference.Id &&
                   NativeAssetFixture.Type == new Keire.AssetId(0x4b454952454d4154, 0x455249414c000001) &&
                   NativeAssetFixture.Priority == Keire.AssetLoadPriority.High,
               "Runtime asset loads must preserve generation, asset type, identity, and priority.");
        Assert(handle.State == Keire.AssetLoadState.Queued && handle.UsingFallback && handle.Revision == 0 &&
                   handle.KeepWaiting,
               "Queued runtime asset handles must expose fallback state and remain coroutine-waitable.");

        NativeAssetFixture.Publish(Keire.AssetLoadState.Ready, usingFallback: false, revision: 4);
        handle.RequireReady();
        Assert(handle.IsReady && handle.IsDone && !handle.KeepWaiting && handle.Revision == 4 &&
                   handle.Diagnostic.IsEmpty,
               "Ready runtime asset handles must expose their resident revision without a native object.");
        Assert(handle.WaitUntilReadyAsync().IsCompletedSuccessfully,
               "Already-ready runtime asset handles must complete asynchronous waits synchronously.");

        NativeAssetFixture.Publish(Keire.AssetLoadState.Reloading, usingFallback: false, revision: 4);
        Assert(handle.IsReady && handle.IsDone && !handle.KeepWaiting && handle.Revision == 4,
               "Reloading runtime asset handles must keep their last-good revision usable.");

        NativeAssetFixture.Publish(Keire.AssetLoadState.Failed, usingFallback: true, revision: 4,
                                   operation: "decode", message: "material payload is invalid");
        try
        {
            handle.RequireReady();
            throw new InvalidOperationException("A failed runtime asset handle completed successfully.");
        }
        catch (Keire.AssetLoadException exception)
        {
            Assert(exception.Asset == reference.Id && exception.Diagnostic.Operation == "decode" &&
                       exception.Message.Contains("material payload is invalid", StringComparison.Ordinal),
                   "Runtime asset failures must retain typed identity and structured native diagnostics.");
        }

        AssertThrows<NotSupportedException>(
            () => Keire.Assets.LoadRuntime(Keire.Asset.FromId<UnregisteredAssetProbe>(new Keire.AssetId(1, 2))!),
            "Unregistered runtime asset marker types must be rejected before native dispatch.");
        AssertThrows<NotSupportedException>(
            () => Keire.Assets.LoadRuntime(
                Keire.Asset.FromId<ManagedRuntimeAssetProbe>(new Keire.AssetId(1, 3))!),
            "Managed data assets must use their managed loading pipeline.");

        handle.Dispose();
        handle.Dispose();
        Assert(NativeAssetFixture.ReleaseCount == 1,
               "Runtime asset handle disposal must release its native residency lease exactly once.");
        AssertThrows<ObjectDisposedException>(() => _ = handle.State,
                                              "Disposed runtime asset handles must reject further state access.");
    }
    finally
    {
        NativeAssetFixture.Uninstall();
    }
}

static void Assert(bool condition, string message)
{
    if (!condition)
        throw new InvalidOperationException(message);
}

static void AssertThrows<TException>(Action action, string message)
    where TException : Exception
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

file static unsafe class NativeJobFixture
{
    private static IntPtr s_state;
    private static IntPtr s_callback;
    private static ulong s_nextId;

    internal static int CancelCount { get; private set; }
    internal static ulong LastCancelledId { get; private set; }
    internal static ulong LastSubmittedId { get; private set; }
    internal static Keire.ManagedJobState? CurrentState { get; private set; }

    internal static void Install()
    {
        s_state = IntPtr.Zero;
        s_callback = IntPtr.Zero;
        s_nextId = 44;
        CancelCount = 0;
        LastCancelledId = 0;
        LastSubmittedId = 0;
        CurrentState = null;
        Keire.NativeRuntime.SubmitManagedJobIcall = &SubmitManagedJob;
        Keire.NativeRuntime.CancelManagedJobIcall = &CancelManagedJob;
    }

    internal static void Uninstall()
    {
        CurrentState?.ReleaseHandle();
        CurrentState = null;
        s_state = IntPtr.Zero;
        s_callback = IntPtr.Zero;
        Keire.NativeRuntime.SubmitManagedJobIcall = null;
        Keire.NativeRuntime.CancelManagedJobIcall = null;
    }

    internal static byte Invoke(byte phase, byte stopRequested)
    {
        if (s_callback == IntPtr.Zero || s_state == IntPtr.Zero)
            throw new InvalidOperationException("No managed job callback has been submitted.");
        var callback = (delegate* unmanaged<IntPtr, byte, byte, byte>)s_callback;
        return callback(s_state, phase, stopRequested);
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static ulong SubmitManagedJob(ulong* dependencies, int dependencyCount, byte priority, byte jobClass,
                                          Keire.NativeString name, IntPtr state, IntPtr callback)
    {
        s_state = state;
        s_callback = callback;
        CurrentState = System.Runtime.InteropServices.GCHandle.FromIntPtr(state).Target as Keire.ManagedJobState;
        LastSubmittedId = ++s_nextId;
        return LastSubmittedId;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static void CancelManagedJob(ulong id)
    {
        ++CancelCount;
        LastCancelledId = id;
    }
}

file static unsafe class NativeAssetFixture
{
    private const ulong Handle = 41;
    private static Keire.NativeRuntimeAssetStatus s_status;
    private static string s_operation = string.Empty;
    private static string s_message = string.Empty;

    internal static ulong Generation { get; private set; }
    internal static Keire.AssetId Asset { get; private set; }
    internal static Keire.AssetId Type { get; private set; }
    internal static Keire.AssetLoadPriority Priority { get; private set; }
    internal static int ReleaseCount { get; private set; }

    internal static void Install()
    {
        Generation = 0;
        Asset = default;
        Type = default;
        Priority = default;
        ReleaseCount = 0;
        Publish(Keire.AssetLoadState.Queued, usingFallback: true, revision: 0);
        Keire.NativeRuntime.InstallManagedAssetsForTests(7001, maximumLoadedAssets: 1,
                                                        maximumInFlightLoads: 1, provider: null);
        Keire.NativeAssets.BeginRuntimeAssetLoadIcall = &BeginRuntimeAssetLoad;
        Keire.NativeAssets.GetRuntimeAssetStatusIcall = &GetRuntimeAssetStatus;
        Keire.NativeAssets.GetRuntimeAssetDiagnosticIcall = &GetRuntimeAssetDiagnostic;
        Keire.NativeAssets.ReleaseRuntimeAssetIcall = &ReleaseRuntimeAsset;
    }

    internal static void Uninstall()
    {
        Keire.NativeAssets.BeginRuntimeAssetLoadIcall = null;
        Keire.NativeAssets.GetRuntimeAssetStatusIcall = null;
        Keire.NativeAssets.GetRuntimeAssetDiagnosticIcall = null;
        Keire.NativeAssets.ReleaseRuntimeAssetIcall = null;
        _ = Keire.NativeRuntime.ResetManagedAssets(7001);
    }

    internal static void Publish(Keire.AssetLoadState state, bool usingFallback, ulong revision,
                                 string operation = "", string message = "")
    {
        s_status.Revision = revision;
        s_status.State = (byte)state;
        s_status.UsingFallbackValue = usingFallback ? (byte)1 : (byte)0;
        s_operation = operation;
        s_message = message;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static ulong BeginRuntimeAssetLoad(ulong generation, ulong assetHigh, ulong assetLow, ulong typeHigh,
                                               ulong typeLow, byte priority)
    {
        Generation = generation;
        Asset = new Keire.AssetId(assetHigh, assetLow);
        Type = new Keire.AssetId(typeHigh, typeLow);
        Priority = (Keire.AssetLoadPriority)priority;
        return Handle;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetRuntimeAssetStatus(ulong handle, Keire.NativeRuntimeAssetStatus* destination)
    {
        if (handle != Handle || destination == null)
            return 0;
        *destination = s_status;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int GetRuntimeAssetDiagnostic(ulong handle, byte field, byte* destination, int capacity)
    {
        if (handle != Handle || field > 1 || capacity < 0)
            return -1;
        byte[] bytes = System.Text.Encoding.UTF8.GetBytes(field == 0 ? s_operation : s_message);
        if (destination == null || capacity == 0)
            return bytes.Length;
        if (capacity < bytes.Length)
            return -1;
        for (int index = 0; index < bytes.Length; ++index)
            destination[index] = bytes[index];
        return bytes.Length;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte ReleaseRuntimeAsset(ulong handle)
    {
        if (handle != Handle)
            return 0;
        ++ReleaseCount;
        return 1;
    }
}

file static unsafe class NativeWorldFixture
{
    internal static readonly Keire.AssetId CurrentScene = new(301, 401);
    internal static readonly Keire.AssetId ReplacementScene = new(302, 402);
    internal const ulong CurrentHandle = 11;
    internal const ulong ReplacementHandle = 22;
    private static byte s_loadState;

    internal static Keire.NativeRenderEnvironment Environment { get; private set; }
    internal static int RenderSetCount { get; private set; }
    internal static int TagMutationCount { get; private set; }
    internal static Keire.SceneQueryScope LastQueryScope { get; private set; }
    internal static ulong LastQueryHandle { get; private set; }

    internal static void Install()
    {
        s_loadState = (byte)Keire.SceneLoadState.Loading;
        Environment = new Keire.NativeRenderEnvironment
        {
            AmbientColor = new Keire.Color(0.2f, 0.25f, 0.3f, 1.0f),
            AmbientIntensity = 0.75f,
            Exposure = 1.25f,
            EnvironmentHigh = 501,
            EnvironmentLow = 601,
            EnvironmentDiffuseIntensity = 1.0f,
            EnvironmentSpecularIntensity = 1.0f,
            SkyVisibleValue = 1,
            DirectionalShadowDistance = 150.0f,
            DirectionalShadowCascadeCount = 4,
            DirectionalShadowResolution = 2048,
            DirectionalShadowSplitLambda = 0.65f
        };
        RenderSetCount = 0;
        TagMutationCount = 0;
        Keire.NativeWorld.BeginSceneLoadIcall = &BeginSceneLoad;
        Keire.NativeWorld.GetSceneLoadStatusIcall = &GetSceneLoadStatus;
        Keire.NativeWorld.GetSceneLoadDiagnosticIcall = &GetSceneLoadDiagnostic;
        Keire.NativeWorld.CancelSceneLoadIcall = &CancelSceneLoad;
        Keire.NativeWorld.UnloadSceneIcall = &UnloadScene;
        Keire.NativeWorld.SetActiveSceneIcall = &SetActiveScene;
        Keire.NativeWorld.MakeEntityPersistentIcall = &MakeEntityPersistent;
        Keire.NativeWorld.GetActiveSceneIcall = &GetActiveScene;
        Keire.NativeWorld.GetLoadedScenesIcall = &GetLoadedScenes;
        Keire.NativeWorld.GetEntityTagCountIcall = &GetEntityTagCount;
        Keire.NativeWorld.GetEntityTagIcall = &GetEntityTag;
        Keire.NativeWorld.AddEntityTagIcall = &AddEntityTag;
        Keire.NativeWorld.RemoveEntityTagIcall = &RemoveEntityTag;
        Keire.NativeWorld.ClearEntityTagsIcall = &ClearEntityTags;
        Keire.NativeWorld.QueryEntityNamesIcall = &QueryEntityNames;
        Keire.NativeWorld.QueryEntityTagsIcall = &QueryEntityTags;
        Keire.NativeWorld.QueryEntityComponentsIcall = &QueryEntityComponents;
        Keire.NativeWorld.GetRenderEnvironmentIcall = &GetRenderEnvironment;
        Keire.NativeWorld.SetRenderEnvironmentIcall = &SetRenderEnvironment;
    }

    internal static void CompleteLoad() => s_loadState = (byte)Keire.SceneLoadState.Ready;

    internal static void Uninstall()
    {
        Keire.NativeWorld.BeginSceneLoadIcall = null;
        Keire.NativeWorld.GetSceneLoadStatusIcall = null;
        Keire.NativeWorld.GetSceneLoadDiagnosticIcall = null;
        Keire.NativeWorld.CancelSceneLoadIcall = null;
        Keire.NativeWorld.UnloadSceneIcall = null;
        Keire.NativeWorld.SetActiveSceneIcall = null;
        Keire.NativeWorld.MakeEntityPersistentIcall = null;
        Keire.NativeWorld.GetActiveSceneIcall = null;
        Keire.NativeWorld.GetLoadedScenesIcall = null;
        Keire.NativeWorld.GetEntityTagCountIcall = null;
        Keire.NativeWorld.GetEntityTagIcall = null;
        Keire.NativeWorld.AddEntityTagIcall = null;
        Keire.NativeWorld.RemoveEntityTagIcall = null;
        Keire.NativeWorld.ClearEntityTagsIcall = null;
        Keire.NativeWorld.QueryEntityNamesIcall = null;
        Keire.NativeWorld.QueryEntityTagsIcall = null;
        Keire.NativeWorld.QueryEntityComponentsIcall = null;
        Keire.NativeWorld.GetRenderEnvironmentIcall = null;
        Keire.NativeWorld.SetRenderEnvironmentIcall = null;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static ulong BeginSceneLoad(ulong high, ulong low, byte mode) =>
        high == ReplacementScene.High && low == ReplacementScene.Low && mode == (byte)Keire.SceneLoadMode.Single
            ? 77UL
            : 0UL;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetSceneLoadStatus(ulong operation, Keire.NativeSceneLoadStatus* status)
    {
        if (operation != 77 || status == null)
            return 0;
        *status = new Keire.NativeSceneLoadStatus
        {
            SceneHigh = ReplacementScene.High,
            SceneLow = ReplacementScene.Low,
            Handle = s_loadState == (byte)Keire.SceneLoadState.Ready ? ReplacementHandle : 0,
            Progress = s_loadState == (byte)Keire.SceneLoadState.Ready ? 1.0f : 0.5f,
            Mode = (byte)Keire.SceneLoadMode.Single,
            State = s_loadState
        };
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int GetSceneLoadDiagnostic(ulong operation, byte* destination, int capacity)
    {
        byte[] bytes = System.Text.Encoding.UTF8.GetBytes(operation == 77 ? "activation failed" : string.Empty);
        if (destination == null || capacity == 0)
            return bytes.Length;
        if (capacity < bytes.Length)
            return -1;
        for (int index = 0; index < bytes.Length; ++index)
            destination[index] = bytes[index];
        return bytes.Length;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte CancelSceneLoad(ulong operation)
    {
        if (operation != 77 || s_loadState == (byte)Keire.SceneLoadState.Ready)
            return 0;
        s_loadState = (byte)Keire.SceneLoadState.Cancelled;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte UnloadScene(ulong handle) => handle == CurrentHandle ? (byte)1 : (byte)0;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetActiveScene(ulong handle) => handle == CurrentHandle ? (byte)1 : (byte)0;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte MakeEntityPersistent(ulong world, ulong high, ulong low) =>
        world == 99 && high == 1 && low == 2 ? (byte)1 : (byte)0;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetActiveScene(Keire.NativeSceneHandle* destination)
    {
        if (destination == null)
            return 0;
        *destination = new Keire.NativeSceneHandle
        {
            High = CurrentScene.High,
            Low = CurrentScene.Low,
            Handle = CurrentHandle
        };
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int GetLoadedScenes(Keire.NativeSceneHandle* destination, int capacity)
    {
        if (destination == null || capacity == 0)
            return 1;
        if (capacity < 1)
            return -1;
        destination[0] = new Keire.NativeSceneHandle
        {
            High = CurrentScene.High,
            Low = CurrentScene.Low,
            Handle = CurrentHandle
        };
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int GetEntityTagCount(ulong world, ulong high, ulong low) => world == 99 && high == 1 && low == 2 ? 2 : 0;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int GetEntityTag(ulong world, ulong high, ulong low, int index, byte* destination, int capacity) =>
        world == 99 && high == 1 && low == 2
            ? CopyText(index == 0 ? "Player" : index == 1 ? "Faction.Friendly" : null, destination, capacity)
            : -1;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte AddEntityTag(ulong world, ulong high, ulong low, Keire.NativeString tag)
    {
        ++TagMutationCount;
        return world == 99 && high == 1 && low == 2 ? (byte)1 : (byte)0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte RemoveEntityTag(ulong world, ulong high, ulong low, Keire.NativeString tag)
    {
        ++TagMutationCount;
        return world == 99 && high == 1 && low == 2 ? (byte)1 : (byte)0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte ClearEntityTags(ulong world, ulong high, ulong low)
    {
        ++TagMutationCount;
        return world == 99 && high == 1 && low == 2 ? (byte)1 : (byte)0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int QueryEntityNames(Keire.NativeString name, byte scope, ulong scene, int maximum,
                                        Keire.NativeEntityHandle* destination, int capacity)
    {
        (LastQueryScope, LastQueryHandle) = ((Keire.SceneQueryScope)scope, scene);
        return WriteEntities([new(99, 1, 2)], maximum, destination, capacity);
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int QueryEntityTags(Keire.NativeString tag, byte scope, ulong scene, int maximum,
                                       Keire.NativeEntityHandle* destination, int capacity)
    {
        (LastQueryScope, LastQueryHandle) = ((Keire.SceneQueryScope)scope, scene);
        return WriteEntities([new(99, 3, 4), new(99, 5, 6)], maximum, destination, capacity);
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int QueryEntityComponents(ulong high, ulong low, byte scope, ulong scene, int maximum,
                                             Keire.NativeEntityHandle* destination, int capacity)
    {
        (LastQueryScope, LastQueryHandle) = ((Keire.SceneQueryScope)scope, scene);
        return WriteEntities([new(99, 1, 2)], maximum, destination, capacity);
    }

    private static int WriteEntities(Keire.NativeEntityHandle[] values, int maximum,
                                     Keire.NativeEntityHandle* destination, int capacity)
    {
        int count = Math.Min(values.Length, maximum);
        if (destination == null || capacity == 0)
            return count;
        if (capacity < count)
            return -1;
        for (int index = 0; index < count; ++index)
            destination[index] = values[index];
        return count;
    }

    private static int CopyText(string? value, byte* destination, int capacity)
    {
        if (value == null || capacity < 0)
            return -1;
        byte[] bytes = System.Text.Encoding.UTF8.GetBytes(value);
        if (destination == null || capacity == 0)
            return bytes.Length;
        if (capacity < bytes.Length)
            return -1;
        for (int index = 0; index < bytes.Length; ++index)
            destination[index] = bytes[index];
        return bytes.Length;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetRenderEnvironment(Keire.NativeRenderEnvironment* destination)
    {
        if (destination == null)
            return 0;
        *destination = Environment;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetRenderEnvironment(Keire.NativeRenderEnvironment* value)
    {
        if (value == null)
            return 0;
        Environment = *value;
        ++RenderSetCount;
        return 1;
    }
}

file static unsafe class NativeFoundationFixture
{
    private static double s_timeScale;
    private static byte s_paused;
    private static string s_persistentDataPath = string.Empty;

    public static int ExitCode { get; private set; }
    public static uint LastWidth { get; private set; }
    public static uint LastHeight { get; private set; }
    public static Keire.FullscreenMode LastMode { get; private set; }

    public static void Install()
    {
        s_timeScale = 1.0;
        s_paused = 0;
        s_persistentDataPath = Path.GetFullPath(Path.Combine(Path.GetTempPath(), "keire-native-foundation"));
        ExitCode = 0;
        LastWidth = 0;
        LastHeight = 0;
        LastMode = Keire.FullscreenMode.Windowed;
        Keire.NativeFoundation.GetApplicationTextIcall = &GetApplicationText;
        Keire.NativeFoundation.IsEditorIcall = &IsEditor;
        Keire.NativeFoundation.RequestExitIcall = &RequestExit;
        Keire.NativeFoundation.GetTimeScaleIcall = &GetTimeScale;
        Keire.NativeFoundation.SetTimeScaleIcall = &SetTimeScale;
        Keire.NativeFoundation.IsTimePausedIcall = &IsTimePaused;
        Keire.NativeFoundation.SetTimePausedIcall = &SetTimePaused;
        Keire.NativeFoundation.GetScreenStateIcall = &GetScreenState;
        Keire.NativeFoundation.SetScreenIcall = &SetScreen;
    }

    public static void Uninstall()
    {
        Keire.NativeFoundation.GetApplicationTextIcall = null;
        Keire.NativeFoundation.IsEditorIcall = null;
        Keire.NativeFoundation.RequestExitIcall = null;
        Keire.NativeFoundation.GetTimeScaleIcall = null;
        Keire.NativeFoundation.SetTimeScaleIcall = null;
        Keire.NativeFoundation.IsTimePausedIcall = null;
        Keire.NativeFoundation.SetTimePausedIcall = null;
        Keire.NativeFoundation.GetScreenStateIcall = null;
        Keire.NativeFoundation.SetScreenIcall = null;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int GetApplicationText(byte field, byte* destination, int capacity)
    {
        string value = (Keire.ApplicationText)field switch
        {
            Keire.ApplicationText.ProductName => "Nightglass",
            Keire.ApplicationText.Version => "1.2.3",
            Keire.ApplicationText.Identifier => "games.keire.nightglass",
            Keire.ApplicationText.PersistentDataPath => s_persistentDataPath,
            _ => string.Empty
        };
        byte[] bytes = System.Text.Encoding.UTF8.GetBytes(value);
        if (destination == null || capacity == 0)
            return bytes.Length;
        if (capacity < bytes.Length)
            return -1;
        for (int index = 0; index < bytes.Length; ++index)
            destination[index] = bytes[index];
        return bytes.Length;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte IsEditor() => 1;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static void RequestExit(int exitCode) => ExitCode = exitCode;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static double GetTimeScale() => s_timeScale;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetTimeScale(double value)
    {
        if (!double.IsFinite(value) || value is < 0.0 or > 100.0)
            return 0;
        s_timeScale = value;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte IsTimePaused() => s_paused;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetTimePaused(byte value)
    {
        s_paused = value == 0 ? (byte)0 : (byte)1;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetScreenState(Keire.NativeScreenState* state)
    {
        if (state == null)
            return 0;
        *state = new Keire.NativeScreenState
        {
            LogicalWidth = 1920,
            LogicalHeight = 1080,
            PixelWidth = 3840,
            PixelHeight = 2160,
            DisplayScale = 2.0f,
            Mode = (byte)Keire.FullscreenMode.Windowed,
            FocusedValue = 1,
            VisibleValue = 1,
            MinimizedValue = 0,
            VSyncValue = 1
        };
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetScreen(uint width, uint height, byte mode)
    {
        LastWidth = width;
        LastHeight = height;
        LastMode = (Keire.FullscreenMode)mode;
        return 1;
    }
}

file static unsafe class NativeUiDocumentFixture
{
    private static ulong s_generation;
    private static bool s_alive;
    private static bool s_focused;
    private static bool s_clickPending;
    private static bool s_changePending;

    internal static readonly Keire.AssetId ButtonId = new(301, 401);
    internal static string Text { get; private set; } = string.Empty;
    internal static float Value { get; private set; }
    internal static bool Checked { get; private set; }
    internal static bool Interactable { get; private set; }
    internal static bool Enabled { get; private set; }

    internal static void Install()
    {
        s_generation = 41;
        s_alive = true;
        s_focused = false;
        s_clickPending = true;
        s_changePending = true;
        Text = "Launch";
        Value = 25.0f;
        Checked = false;
        Interactable = true;
        Enabled = true;
        Keire.NativeRuntimeUi.ResolveDocumentRootIcall = &ResolveRoot;
        Keire.NativeRuntimeUi.ResolveDocumentElementByIdIcall = &ResolveById;
        Keire.NativeRuntimeUi.ResolveDocumentElementByNameIcall = &ResolveByName;
        Keire.NativeRuntimeUi.DocumentElementAliveIcall = &Alive;
        Keire.NativeRuntimeUi.GetDocumentElementTextIcall = &GetText;
        Keire.NativeRuntimeUi.SetDocumentElementTextIcall = &SetText;
        Keire.NativeRuntimeUi.GetDocumentElementValueIcall = &GetValue;
        Keire.NativeRuntimeUi.SetDocumentElementValueIcall = &SetValue;
        Keire.NativeRuntimeUi.GetDocumentElementFlagIcall = &GetFlag;
        Keire.NativeRuntimeUi.SetDocumentElementFlagIcall = &SetFlag;
        Keire.NativeRuntimeUi.ConsumeDocumentElementEventIcall = &ConsumeEvent;
        Keire.NativeRuntimeUi.FocusDocumentElementIcall = &Focus;
    }

    internal static void Uninstall()
    {
        Keire.NativeRuntimeUi.ResolveDocumentRootIcall = null;
        Keire.NativeRuntimeUi.ResolveDocumentElementByIdIcall = null;
        Keire.NativeRuntimeUi.ResolveDocumentElementByNameIcall = null;
        Keire.NativeRuntimeUi.DocumentElementAliveIcall = null;
        Keire.NativeRuntimeUi.GetDocumentElementTextIcall = null;
        Keire.NativeRuntimeUi.SetDocumentElementTextIcall = null;
        Keire.NativeRuntimeUi.GetDocumentElementValueIcall = null;
        Keire.NativeRuntimeUi.SetDocumentElementValueIcall = null;
        Keire.NativeRuntimeUi.GetDocumentElementFlagIcall = null;
        Keire.NativeRuntimeUi.SetDocumentElementFlagIcall = null;
        Keire.NativeRuntimeUi.ConsumeDocumentElementEventIcall = null;
        Keire.NativeRuntimeUi.FocusDocumentElementIcall = null;
    }

    internal static void FailReload() { }
    internal static void CommitReload() { ++s_generation; Text = "Reloaded"; }
    internal static void Destroy() => s_alive = false;

    private static bool IsDocument(ulong high, ulong low) => high == 23 && low == 29 && s_alive;

    private static Keire.NativeUiDocumentElement Element(ulong element, Keire.AssetId stableId,
                                                         Keire.NativeUiDocumentElementType type) => new()
    {
        DocumentGeneration = s_generation,
        Element = element,
        StableIdHigh = stableId.High,
        StableIdLow = stableId.Low,
        Type = type
    };

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte ResolveRoot(ulong high, ulong low, Keire.NativeUiDocumentElement* result)
    {
        if (!IsDocument(high, low) || result == null)
            return 0;
        *result = Element(100, new Keire.AssetId(300, 400), Keire.NativeUiDocumentElementType.Panel);
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte ResolveById(ulong high, ulong low, ulong stableHigh, ulong stableLow,
                                    Keire.NativeUiDocumentElement* result)
    {
        if (!IsDocument(high, low) || result == null || stableHigh != ButtonId.High || stableLow != ButtonId.Low)
            return 0;
        *result = Element(101, ButtonId, Keire.NativeUiDocumentElementType.Button);
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte ResolveByName(ulong high, ulong low, Keire.NativeString name,
                                      Keire.NativeUiDocumentElement* result)
    {
        if (!IsDocument(high, low) || result == null)
            return 0;
        IntPtr data = *(IntPtr*)&name;
        if (!string.Equals(System.Runtime.InteropServices.Marshal.PtrToStringAuto(data), "launch",
                           StringComparison.Ordinal))
            return 0;
        *result = Element(101, ButtonId, Keire.NativeUiDocumentElementType.Button);
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte Alive(ulong high, ulong low, ulong generation, ulong element) =>
        IsDocument(high, low) && generation == s_generation && element is 100 or 101 ? (byte)1 : (byte)0;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int GetText(ulong high, ulong low, ulong generation, ulong element, byte* destination, int capacity)
    {
        if (AliveValue(high, low, generation, element) == 0)
            return -1;
        byte[] bytes = System.Text.Encoding.UTF8.GetBytes(Text);
        if (destination == null || capacity == 0)
            return bytes.Length;
        if (capacity < bytes.Length)
            return -1;
        for (int index = 0; index < bytes.Length; ++index)
            destination[index] = bytes[index];
        return bytes.Length;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetText(ulong high, ulong low, ulong generation, ulong element, Keire.NativeString value)
    {
        if (AliveValue(high, low, generation, element) == 0)
            return 0;
        IntPtr data = *(IntPtr*)&value;
        Text = System.Runtime.InteropServices.Marshal.PtrToStringAuto(data) ?? string.Empty;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetValue(ulong high, ulong low, ulong generation, ulong element, float* value)
    {
        if (AliveValue(high, low, generation, element) == 0 || value == null)
            return 0;
        *value = Value;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetValue(ulong high, ulong low, ulong generation, ulong element, float value)
    {
        if (AliveValue(high, low, generation, element) == 0)
            return 0;
        Value = value;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetFlag(ulong high, ulong low, ulong generation, ulong element, byte property, byte* value)
    {
        if (AliveValue(high, low, generation, element) == 0 || value == null)
            return 0;
        *value = (Keire.NativeUiDocumentFlag)property switch
        {
            Keire.NativeUiDocumentFlag.Interactable => Interactable ? (byte)1 : (byte)0,
            Keire.NativeUiDocumentFlag.Checked => Checked ? (byte)1 : (byte)0,
            Keire.NativeUiDocumentFlag.Focused => s_focused ? (byte)1 : (byte)0,
            _ => Enabled ? (byte)1 : (byte)0
        };
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetFlag(ulong high, ulong low, ulong generation, ulong element, byte property, byte value)
    {
        if (AliveValue(high, low, generation, element) == 0)
            return 0;
        bool enabled = value != 0;
        switch ((Keire.NativeUiDocumentFlag)property)
        {
        case Keire.NativeUiDocumentFlag.Interactable: Interactable = enabled; break;
        case Keire.NativeUiDocumentFlag.Checked: Checked = enabled; break;
        case Keire.NativeUiDocumentFlag.Focused: s_focused = enabled; break;
        case Keire.NativeUiDocumentFlag.Enabled: Enabled = enabled; break;
        }
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte ConsumeEvent(ulong high, ulong low, ulong generation, ulong element, byte type)
    {
        if (AliveValue(high, low, generation, element) == 0)
            return 0;
        if ((Keire.NativeUiEventType)type == Keire.NativeUiEventType.Click && s_clickPending)
        {
            s_clickPending = false;
            return 1;
        }
        if ((Keire.NativeUiEventType)type == Keire.NativeUiEventType.ValueChanged && s_changePending)
        {
            s_changePending = false;
            return 1;
        }
        return 0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte Focus(ulong high, ulong low, ulong generation, ulong element)
    {
        if (AliveValue(high, low, generation, element) == 0)
            return 0;
        s_focused = true;
        return 1;
    }

    private static byte AliveValue(ulong high, ulong low, ulong generation, ulong element) =>
        IsDocument(high, low) && generation == s_generation && element is 100 or 101 ? (byte)1 : (byte)0;
}

file static unsafe class NativeRenderingFixture
{
    internal static readonly Keire.AssetId FirstSourceMaterial = new(101, 201);
    internal static readonly Keire.AssetId SecondMaterial = new(102, 202);
    internal static readonly Keire.AssetId ReplacementMaterial = new(103, 203);
    internal static readonly Keire.AssetId ReplacementTexture = new(104, 204);
    internal static Keire.NativeRenderingComponent ScalarComponent;
    internal static Keire.NativeRenderingScalarProperty ScalarProperty;
    internal static float ScalarValue;
    internal static int IntegerValue;
    internal static Keire.Vector2 VectorValue;
    internal static int MaterialCount;
    internal static Keire.AssetId FirstMaterial;
    internal static float MaterialFloat;
    internal static Keire.AssetId MaterialTexture;
    internal static uint InstanceSlot;
    internal static Keire.AssetId GlobalCollection;
    internal static float GlobalFloat;
    internal static Keire.NativeRenderingFlagProperty FlagProperty;
    internal static bool FlagValue;

    internal static void Install()
    {
        ScalarComponent = default;
        ScalarProperty = default;
        ScalarValue = default;
        IntegerValue = default;
        VectorValue = default;
        MaterialCount = default;
        FirstMaterial = default;
        MaterialFloat = default;
        MaterialTexture = default;
        InstanceSlot = default;
        GlobalCollection = default;
        GlobalFloat = default;
        FlagProperty = default;
        FlagValue = default;
        Keire.NativeRuntimeRendering.GetScalarIcall = &GetScalar;
        Keire.NativeRuntimeRendering.SetScalarIcall = &SetScalar;
        Keire.NativeRuntimeRendering.GetIntegerIcall = &GetInteger;
        Keire.NativeRuntimeRendering.SetIntegerIcall = &SetInteger;
        Keire.NativeRuntimeRendering.GetFlagIcall = &GetFlag;
        Keire.NativeRuntimeRendering.SetFlagIcall = &SetFlag;
        Keire.NativeRuntimeRendering.GetVectorIcall = &GetVector;
        Keire.NativeRuntimeRendering.SetVectorIcall = &SetVector;
        Keire.NativeRuntimeRendering.GetColorIcall = &GetColor;
        Keire.NativeRuntimeRendering.SetColorIcall = &SetColor;
        Keire.NativeRuntimeRendering.GetAssetIcall = &GetAsset;
        Keire.NativeRuntimeRendering.SetAssetIcall = &SetAsset;
        Keire.NativeRuntimeRendering.GetMaterialsIcall = &GetMaterials;
        Keire.NativeRuntimeRendering.SetMaterialsIcall = &SetMaterials;
        Keire.NativeRuntimeRendering.SetMaterialFloatIcall = &SetMaterialFloat;
        Keire.NativeRuntimeRendering.SetMaterialVector2Icall = &SetMaterialVector2;
        Keire.NativeRuntimeRendering.SetMaterialVector3Icall = &SetMaterialVector3;
        Keire.NativeRuntimeRendering.SetMaterialVector4Icall = &SetMaterialVector4;
        Keire.NativeRuntimeRendering.SetMaterialColorIcall = &SetMaterialColor;
        Keire.NativeRuntimeRendering.SetMaterialTextureIcall = &SetMaterialTexture;
        Keire.NativeRuntimeRendering.ResetMaterialPropertyIcall = &ResetMaterialProperty;
        Keire.NativeRuntimeRendering.ClearMaterialPropertiesIcall = &ClearMaterialProperties;
        Keire.NativeRuntimeRendering.SetMaterialInstanceColorIcall = &SetMaterialInstanceColor;
        Keire.NativeRuntimeRendering.MaterialParameterCollectionReadyIcall = &MaterialParameterCollectionReady;
        Keire.NativeRuntimeRendering.SetMaterialParameterFloatIcall = &SetMaterialParameterFloat;
    }

    internal static void Uninstall()
    {
        Keire.NativeRuntimeRendering.GetScalarIcall = null;
        Keire.NativeRuntimeRendering.SetScalarIcall = null;
        Keire.NativeRuntimeRendering.GetIntegerIcall = null;
        Keire.NativeRuntimeRendering.SetIntegerIcall = null;
        Keire.NativeRuntimeRendering.GetFlagIcall = null;
        Keire.NativeRuntimeRendering.SetFlagIcall = null;
        Keire.NativeRuntimeRendering.GetVectorIcall = null;
        Keire.NativeRuntimeRendering.SetVectorIcall = null;
        Keire.NativeRuntimeRendering.GetColorIcall = null;
        Keire.NativeRuntimeRendering.SetColorIcall = null;
        Keire.NativeRuntimeRendering.GetAssetIcall = null;
        Keire.NativeRuntimeRendering.SetAssetIcall = null;
        Keire.NativeRuntimeRendering.GetMaterialsIcall = null;
        Keire.NativeRuntimeRendering.SetMaterialsIcall = null;
        Keire.NativeRuntimeRendering.SetMaterialFloatIcall = null;
        Keire.NativeRuntimeRendering.SetMaterialVector2Icall = null;
        Keire.NativeRuntimeRendering.SetMaterialVector3Icall = null;
        Keire.NativeRuntimeRendering.SetMaterialVector4Icall = null;
        Keire.NativeRuntimeRendering.SetMaterialColorIcall = null;
        Keire.NativeRuntimeRendering.SetMaterialTextureIcall = null;
        Keire.NativeRuntimeRendering.ResetMaterialPropertyIcall = null;
        Keire.NativeRuntimeRendering.ClearMaterialPropertiesIcall = null;
        Keire.NativeRuntimeRendering.SetMaterialInstanceColorIcall = null;
        Keire.NativeRuntimeRendering.MaterialParameterCollectionReadyIcall = null;
        Keire.NativeRuntimeRendering.SetMaterialParameterFloatIcall = null;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetScalar(ulong high, ulong low, byte component, byte property, float* value)
    {
        if (high != 23 || low != 29 || value == null)
            return 0;
        *value = (Keire.NativeRenderingComponent)component == Keire.NativeRenderingComponent.Camera &&
                 (Keire.NativeRenderingScalarProperty)property ==
                 Keire.NativeRenderingScalarProperty.VerticalFieldOfView
            ? 68.0f
            : 12.0f;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetScalar(ulong high, ulong low, byte component, byte property, float value)
    {
        ScalarComponent = (Keire.NativeRenderingComponent)component;
        ScalarProperty = (Keire.NativeRenderingScalarProperty)property;
        ScalarValue = value;
        return high == 23 && low == 29 ? (byte)1 : (byte)0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetInteger(ulong high, ulong low, byte component, byte property, int* value)
    {
        if (high != 23 || low != 29 || value == null)
            return 0;
        *value = 0;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetInteger(ulong high, ulong low, byte component, byte property, int value)
    {
        IntegerValue = value;
        return high == 23 && low == 29 ? (byte)1 : (byte)0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetFlag(ulong high, ulong low, byte component, byte property, byte* value)
    {
        if (high != 23 || low != 29 || value == null)
            return 0;
        *value = 1;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetFlag(ulong high, ulong low, byte component, byte property, byte value)
    {
        FlagProperty = (Keire.NativeRenderingFlagProperty)property;
        FlagValue = value != 0;
        return high == 23 && low == 29 ? (byte)1 : (byte)0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetVector(ulong high, ulong low, byte component, byte property, Keire.Vector2* value)
    {
        if (high != 23 || low != 29 || value == null)
            return 0;
        *value = new Keire.Vector2(1.0f, 1.0f);
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetVector(ulong high, ulong low, byte component, byte property, Keire.Vector2 value)
    {
        VectorValue = value;
        return high == 23 && low == 29 ? (byte)1 : (byte)0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetColor(ulong high, ulong low, byte component, byte property, Keire.Color* value)
    {
        if (high != 23 || low != 29 || value == null)
            return 0;
        *value = Keire.Color.White;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetColor(ulong high, ulong low, byte component, byte property, Keire.Color value) =>
        high == 23 && low == 29 ? (byte)1 : (byte)0;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetAsset(ulong high, ulong low, byte component, byte property, Keire.AssetId* value)
    {
        if (high != 23 || low != 29 || value == null)
            return 0;
        *value = new Keire.AssetId(105, 205);
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetAsset(ulong high, ulong low, byte component, byte property, Keire.AssetId value) =>
        high == 23 && low == 29 ? (byte)1 : (byte)0;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int GetMaterials(ulong high, ulong low, Keire.AssetId* destination, int capacity)
    {
        if (high != 23 || low != 29)
            return -1;
        if (destination == null || capacity == 0)
            return 2;
        if (capacity < 2)
            return -1;
        destination[0] = FirstSourceMaterial;
        destination[1] = SecondMaterial;
        return 2;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetMaterials(ulong high, ulong low, Keire.AssetId* materials, int count)
    {
        MaterialCount = count;
        FirstMaterial = count > 0 ? materials[0] : default;
        return high == 23 && low == 29 ? (byte)1 : (byte)0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetMaterialFloat(ulong high, ulong low, Keire.NativeString name, float value)
    {
        MaterialFloat = value;
        return high == 23 && low == 29 ? (byte)1 : (byte)0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetMaterialVector2(ulong high, ulong low, Keire.NativeString name, Keire.Vector2 value) =>
        high == 23 && low == 29 ? (byte)1 : (byte)0;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetMaterialVector3(ulong high, ulong low, Keire.NativeString name, Keire.Vector3 value) =>
        high == 23 && low == 29 ? (byte)1 : (byte)0;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetMaterialVector4(ulong high, ulong low, Keire.NativeString name, Keire.Vector4 value) =>
        high == 23 && low == 29 ? (byte)1 : (byte)0;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetMaterialColor(ulong high, ulong low, Keire.NativeString name, Keire.Color value) =>
        high == 23 && low == 29 ? (byte)1 : (byte)0;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetMaterialTexture(ulong high, ulong low, Keire.NativeString name, Keire.AssetId value)
    {
        MaterialTexture = value;
        return high == 23 && low == 29 ? (byte)1 : (byte)0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte ResetMaterialProperty(ulong high, ulong low, Keire.NativeString name) =>
        high == 23 && low == 29 ? (byte)1 : (byte)0;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte ClearMaterialProperties(ulong high, ulong low) =>
        high == 23 && low == 29 ? (byte)1 : (byte)0;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetMaterialInstanceColor(ulong high, ulong low, uint slot, Keire.NativeString name,
                                                 Keire.Color value)
    {
        InstanceSlot = slot;
        return high == 23 && low == 29 ? (byte)1 : (byte)0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte MaterialParameterCollectionReady(ulong high, ulong low)
    {
        GlobalCollection = new Keire.AssetId(high, low);
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetMaterialParameterFloat(ulong high, ulong low, Keire.NativeString name, float value)
    {
        GlobalCollection = new Keire.AssetId(high, low);
        GlobalFloat = value;
        return 1;
    }
}

file sealed class DetachedManagedContractProbe : Keire.Behaviour;

[Keire.StableComponentId("73616e64-626f-4078-8000-00000000e001")]
file sealed class EventMetadataProbe : Keire.Behaviour
{
    public Keire.KeireEvent Changed = new();
}

[Keire.StableAssetTypeId("d3762027-3016-4ec9-b315-67d654f46443")]
file sealed class ManagedRuntimeAssetProbe : Keire.ScriptableObject;

[Keire.StableAssetTypeId("73616e64-626f-4078-8000-00000000c001")]
[Keire.CreateAssetMenu("Gameplay/Authoring Probe", "AuthoringProbe")]
file sealed class ScriptableObjectAuthoringProbe : Keire.ScriptableObject
{
    public int Value = 0;
}

internal interface IFeatureGraphOperation;

[Keire.SerializableType]
[Keire.StableSerializedTypeId("73616e64-626f-4078-8000-00000000c111")]
internal sealed class FeatureGraphAdd : IFeatureGraphOperation
{
    public float Offset = 1.0f;
}

[Keire.SerializableType]
[Keire.StableSerializedTypeId("73616e64-626f-4078-8000-00000000c112")]
internal sealed class FeatureGraphMultiply : IFeatureGraphOperation
{
    public float Factor = 2.0f;
}

[Keire.StableAssetTypeId("73616e64-626f-4078-8000-00000000c110")]
[Keire.CreateAssetMenu("Feature Gallery/Feature Graph Library", "FeatureGraphLibrary")]
internal sealed class FeatureGraphLibrary : Keire.ScriptableObject
{
    [Keire.SerializeReference]
    [Keire.StableFieldId("73616e64-626f-4078-8000-00000000c113")]
    public List<IFeatureGraphOperation> Operations = [];
}

[Keire.StableAssetTypeId("73616e64-626f-4078-8000-00000000c002")]
file sealed class DictionaryAuthoringProbe : Keire.ScriptableObject
{
    public Dictionary<string, List<int[]>> Values = [];
}

file sealed class UnregisteredAssetProbe : Keire.Asset;

[Keire.StableComponentId("d3762027-3016-4ec9-b315-67d654f46442")]
file sealed class ReloadLifecycleProbe : Keire.Behaviour
{
    public int BeforeReloadCount { get; private set; }
    public int AfterReloadCount { get; private set; }

    protected override void OnBeforeReload() => ++BeforeReloadCount;
    protected override void OnAfterReload() => ++AfterReloadCount;
}

file sealed class ManagedStateMathProbe : Keire.Behaviour
{
    [Keire.StableFieldId("73616e64-626f-4078-8000-00000000a001")]
    public Keire.Vector3 Position;

    [Keire.StableFieldId("73616e64-626f-4078-8000-00000000a002")]
    public Keire.Vector2 Aim;

    [Keire.StableFieldId("73616e64-626f-4078-8000-00000000a003")]
    public Keire.Quaternion Rotation;
}

[Serializable]
file sealed class DirectReferenceValue
{
    public Keire.Entity? Entity;
    public List<Keire.Prefab> Prefabs = [];
}

[Keire.StableComponentId("d3762027-3016-4ec9-b315-67d654f46452")]
file sealed class DirectReferenceStateProbe : Keire.Behaviour
{
    [Keire.StableFieldId("73616e64-626f-4078-8000-00000000b001")]
    public Keire.Entity? Target;

    [Keire.SerializeField]
    [Keire.StableFieldId("73616e64-626f-4078-8000-00000000b002")]
    private Keire.AudioSource? _audioSource;

    [Keire.StableFieldId("73616e64-626f-4078-8000-00000000b003")]
    public DirectReferenceValue References = new();

    private string NotSerializedByUnityRules = "hidden";

    public void SetReferences(Keire.Entity entity, Keire.AudioSource audioSource, Keire.Prefab prefab)
    {
        Target = entity;
        _audioSource = audioSource;
        References.Entity = entity;
        References.Prefabs.Add(prefab);
        _ = NotSerializedByUnityRules;
    }
}
