var tests = new (string Name, Action Run)[]
{
    ("Prefab assets expose typed stable identity", PrefabAssetMarkerContract),
    ("Unity-shaped object API replaces public handles and marker components", UnityShapedObjectApiContract),
    ("Managed state v2 tags direct entity component and asset references", DirectReferenceStateContract),
    ("VFX ranges normalize and validate", VfxRangesNormalizeAndValidate),
    ("Inspector attributes validate production editing metadata", InspectorAttributeContract),
    ("ScriptableObject authoring discovers and hydrates Unity-style public fields", ScriptableObjectAuthoringContract),
    ("VFX range setters expose every supported type", VfxRangeSettersExposeEverySupportedType),
    ("Runtime asset operations preserve typed diagnostics and explicit leases", RuntimeAssetHandleContract),
    ("Character Controller uses the native stable component contract", CharacterControllerStableContract),
    ("Entity exposes the production layer contract", EntityLayerContract),
    ("Behaviour lifecycle contracts are synchronized", BehaviourLifecycleContract),
    ("Managed state ignores computed math properties", ManagedStateMathContract),
    ("Coroutines schedule phases and dispose deterministically", CoroutineContract),
    ("Transform and rigid body components expose writable runtime state", GameplayHandleContract),
    ("Animator exposes transient foot-grounding control", AnimatorFootGroundingContract),
    ("Managed physics shape queries validate and preserve native results", ManagedPhysicsQueryContract),
    ("Managed input devices rebinding persistence and rumble preserve native contracts", ManagedInputDeviceContract),
    ("Native UI button dispatch advances with the player clock", NativeUiButtonDispatchClockContract),
    ("Native runtime UI controls preserve values text focus and events", NativeRuntimeUiControlContract),
    ("Managed rendering objects preserve camera lights materials and shader overrides", ManagedRenderingContract),
    ("Managed scene loads and render settings preserve native world transactions", ManagedWorldContract),
    ("Managed jobs execute delegates and publish terminal states", ManagedJobExecutionContract),
    ("Managed jobs preserve terminal dependency semantics", ManagedJobDependencyContract),
    ("Application, time, and screen use the native foundation contract", RuntimeFoundationContract),
    ("Player preferences persist typed values atomically", PlayerPreferencesPersistenceContract),
    ("Player preferences reject invalid and corrupt data", PlayerPreferencesValidationContract),
};

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
    Assert(nativeComponents.Length == 31,
           $"Every one of the 31 native registry entries needs a concrete managed component; found {nativeComponents.Length}.");
    Assert(typeof(Keire.Canvas).GetProperty(nameof(Keire.Canvas.ReferenceResolution)) is { CanRead: true, CanWrite: true } &&
               typeof(Keire.RectTransform).GetProperty(nameof(Keire.RectTransform.AnchorMinimum)) is { CanRead: true, CanWrite: true } &&
               typeof(Keire.UiText).GetProperty(nameof(Keire.UiText.Text)) is { CanRead: true, CanWrite: true } &&
               typeof(Keire.UiAccessibility).GetProperty(nameof(Keire.UiAccessibility.Label)) is { CanRead: true, CanWrite: true },
           "Scene UI component objects must expose their native authoring properties directly.");

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
    Assert(state.Contains("\"Version\":2", StringComparison.Ordinal) ||
               state.Contains("\"version\":2", StringComparison.Ordinal),
           "Managed authoring state must use the v2 reference format.");
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

static unsafe void NativeUiButtonDispatchClockContract()
{
    NativeUiDispatchFixture.Install();
    var button = new Keire.RuntimeUiButton(new Keire.Entity(17, new Keire.EntityId(23, 29)));
    int clicks = 0;
    Action clicked = () => ++clicks;
    button.Clicked += clicked;
    try
    {
        NativeUiDispatchFixture.QueueClick();
        Keire.RuntimeUiButton.DispatchNativeClicks();
        Assert(clicks == 1, "The first native click must be dispatched.");

        NativeUiDispatchFixture.QueueClick();
        Keire.RuntimeUiButton.DispatchNativeClicks();
        Assert(clicks == 1, "A button dispatch must run at most once for the same player frame clock value.");

        NativeUiDispatchFixture.AdvanceFrame();
        Keire.RuntimeUiButton.DispatchNativeClicks();
        Assert(clicks == 2, "Advancing the player frame clock must make the next native click dispatchable.");
    }
    finally
    {
        button.Clicked -= clicked;
        NativeUiDispatchFixture.Uninstall();
    }
}

static unsafe void NativeRuntimeUiControlContract()
{
    NativeRuntimeUiFixture.Install();
    var entity = new Keire.Entity(17, new Keire.EntityId(23, 29));
    try
    {
        var slider = new Keire.UiSlider(entity);
        Assert(slider.Minimum == 0.0f && slider.Maximum == 100.0f && slider.Value == 25.0f,
               "Slider ranges and values must be read from the native scene component.");
        slider.Value = 72.5f;
        Assert(NativeRuntimeUiFixture.ScalarProperty == Keire.NativeUiScalarProperty.Value &&
                   MathF.Abs(NativeRuntimeUiFixture.ScalarValue - 72.5f) < 0.0001f,
               "Slider writes must preserve the native scalar property and value.");
        Assert(slider.ChangedThisFrame && !slider.ChangedThisFrame,
               "Typed UI events must be consumed exactly once.");

        var toggle = new Keire.UiToggle(entity);
        Assert(!toggle.IsOn && toggle.Interactable, "Toggle state and interactability must use native flags.");
        toggle.IsOn = true;
        Assert(NativeRuntimeUiFixture.FlagProperty == Keire.NativeUiFlagProperty.Checked &&
                   NativeRuntimeUiFixture.FlagValue,
               "Toggle writes must preserve the checked flag.");

        var input = new Keire.UiInputField(entity);
        Assert(input.Text == "Astra Ω", "Input text must round-trip through the UTF-8 native ABI.");
        input.Text = "Nightglass";
        input.Focus();
        Assert(NativeRuntimeUiFixture.InputTextSet && NativeRuntimeUiFixture.Focused,
               "Input text writes and focus requests must reach native services.");

        var scroll = new Keire.UiScrollView(entity);
        Assert(scroll.Offset == new Keire.Vector2(4.0f, 8.0f) &&
                   scroll.ContentSize == new Keire.Vector2(1920.0f, 1080.0f),
               "Scroll offset and content extents must preserve native vectors.");
        scroll.Offset = new Keire.Vector2(10.0f, 20.0f);
        Assert(NativeRuntimeUiFixture.VectorProperty == Keire.NativeUiVectorProperty.ScrollOffset &&
                   NativeRuntimeUiFixture.VectorValue == new Keire.Vector2(10.0f, 20.0f),
               "Scroll writes must preserve the native vector property and value.");
    }
    finally
    {
        NativeRuntimeUiFixture.Uninstall();
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

file static unsafe class NativeUiDispatchFixture
{
    private static double s_Elapsed = 1.0;
    private static int s_PendingClicks;

    public static void Install()
    {
        s_Elapsed = 1.0;
        s_PendingClicks = 0;
        Keire.NativeRuntime.ElapsedTimeIcall = &ElapsedTime;
        Keire.NativeRuntime.ConsumeUiClickIcall = &ConsumeUiClick;
    }

    public static void QueueClick() => ++s_PendingClicks;

    public static void AdvanceFrame() => s_Elapsed += 1.0;

    public static void Uninstall()
    {
        Keire.NativeRuntime.ElapsedTimeIcall = null;
        Keire.NativeRuntime.ConsumeUiClickIcall = null;
        s_PendingClicks = 0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static double ElapsedTime() => s_Elapsed;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte ConsumeUiClick(ulong world, ulong entityHigh, ulong entityLow)
    {
        if (world != 17 || entityHigh != 23 || entityLow != 29 || s_PendingClicks == 0)
            return 0;
        --s_PendingClicks;
        return 1;
    }
}

file static unsafe class NativeRuntimeUiFixture
{
    private static bool s_eventPending;

    public static Keire.NativeUiScalarProperty ScalarProperty { get; private set; }
    public static float ScalarValue { get; private set; }
    public static Keire.NativeUiFlagProperty FlagProperty { get; private set; }
    public static bool FlagValue { get; private set; }
    public static Keire.NativeUiVectorProperty VectorProperty { get; private set; }
    public static Keire.Vector2 VectorValue { get; private set; }
    public static bool InputTextSet { get; private set; }
    public static bool Focused { get; private set; }

    public static void Install()
    {
        ScalarProperty = default;
        ScalarValue = 0.0f;
        FlagProperty = default;
        FlagValue = false;
        VectorProperty = default;
        VectorValue = default;
        InputTextSet = false;
        Focused = false;
        s_eventPending = true;
        Keire.NativeRuntimeUi.GetScalarIcall = &GetScalar;
        Keire.NativeRuntimeUi.SetScalarIcall = &SetScalar;
        Keire.NativeRuntimeUi.GetFlagIcall = &GetFlag;
        Keire.NativeRuntimeUi.SetFlagIcall = &SetFlag;
        Keire.NativeRuntimeUi.GetVectorIcall = &GetVector;
        Keire.NativeRuntimeUi.SetVectorIcall = &SetVector;
        Keire.NativeRuntimeUi.GetInputTextIcall = &GetInputText;
        Keire.NativeRuntimeUi.SetInputTextIcall = &SetInputText;
        Keire.NativeRuntimeUi.ConsumeEventIcall = &ConsumeEvent;
        Keire.NativeRuntimeUi.FocusIcall = &Focus;
    }

    public static void Uninstall()
    {
        Keire.NativeRuntimeUi.GetScalarIcall = null;
        Keire.NativeRuntimeUi.SetScalarIcall = null;
        Keire.NativeRuntimeUi.GetFlagIcall = null;
        Keire.NativeRuntimeUi.SetFlagIcall = null;
        Keire.NativeRuntimeUi.GetVectorIcall = null;
        Keire.NativeRuntimeUi.SetVectorIcall = null;
        Keire.NativeRuntimeUi.GetInputTextIcall = null;
        Keire.NativeRuntimeUi.SetInputTextIcall = null;
        Keire.NativeRuntimeUi.ConsumeEventIcall = null;
        Keire.NativeRuntimeUi.FocusIcall = null;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetScalar(ulong high, ulong low, byte property, float* value)
    {
        if (high != 23 || low != 29 || value == null)
            return 0;
        *value = (Keire.NativeUiScalarProperty)property switch
        {
            Keire.NativeUiScalarProperty.Minimum => 0.0f,
            Keire.NativeUiScalarProperty.Maximum => 100.0f,
            _ => 25.0f
        };
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetScalar(ulong high, ulong low, byte property, float value)
    {
        ScalarProperty = (Keire.NativeUiScalarProperty)property;
        ScalarValue = value;
        return high == 23 && low == 29 ? (byte)1 : (byte)0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetFlag(ulong high, ulong low, byte property, byte* value)
    {
        if (high != 23 || low != 29 || value == null)
            return 0;
        *value = (Keire.NativeUiFlagProperty)property == Keire.NativeUiFlagProperty.Interactable ? (byte)1 : (byte)0;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetFlag(ulong high, ulong low, byte property, byte value)
    {
        FlagProperty = (Keire.NativeUiFlagProperty)property;
        FlagValue = value != 0;
        return high == 23 && low == 29 ? (byte)1 : (byte)0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetVector(ulong high, ulong low, byte property, Keire.Vector2* value)
    {
        if (high != 23 || low != 29 || value == null)
            return 0;
        *value = (Keire.NativeUiVectorProperty)property == Keire.NativeUiVectorProperty.ScrollOffset
            ? new Keire.Vector2(4.0f, 8.0f)
            : new Keire.Vector2(1920.0f, 1080.0f);
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetVector(ulong high, ulong low, byte property, Keire.Vector2 value)
    {
        VectorProperty = (Keire.NativeUiVectorProperty)property;
        VectorValue = value;
        return high == 23 && low == 29 ? (byte)1 : (byte)0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int GetInputText(ulong high, ulong low, byte* destination, int capacity)
    {
        if (high != 23 || low != 29)
            return -1;
        byte[] bytes = System.Text.Encoding.UTF8.GetBytes("Astra Ω");
        if (destination == null || capacity == 0)
            return bytes.Length;
        if (capacity < bytes.Length)
            return -1;
        for (int index = 0; index < bytes.Length; ++index)
            destination[index] = bytes[index];
        return bytes.Length;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetInputText(ulong high, ulong low, Keire.NativeString value)
    {
        InputTextSet = true;
        return high == 23 && low == 29 ? (byte)1 : (byte)0;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte ConsumeEvent(ulong high, ulong low, byte type)
    {
        if (high != 23 || low != 29 || type != (byte)Keire.NativeUiEventType.ValueChanged || !s_eventPending)
            return 0;
        s_eventPending = false;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte Focus(ulong high, ulong low)
    {
        Focused = high == 23 && low == 29;
        return Focused ? (byte)1 : (byte)0;
    }
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

[Keire.StableAssetTypeId("d3762027-3016-4ec9-b315-67d654f46443")]
file sealed class ManagedRuntimeAssetProbe : Keire.ScriptableObject;

[Keire.StableAssetTypeId("73616e64-626f-4078-8000-00000000c001")]
[Keire.CreateAssetMenu("Gameplay/Authoring Probe", "AuthoringProbe")]
file sealed class ScriptableObjectAuthoringProbe : Keire.ScriptableObject
{
    public int Value = 0;
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
