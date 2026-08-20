using System.Numerics;
using Keire.Production.Weapons;

var tests = new (string Name, Action Run)[]
{
    ("Physical magazine and chamber", PhysicalMagazineAndChamber),
    ("Deterministic shot identity", DeterministicShotIdentity),
    ("Pickup transaction", PickupTransaction),
    ("Bounded ballistic lifecycle", BoundedBallisticLifecycle),
    ("Reload interruption returns reserved magazines", ReloadInterruptionReturnsReservedMagazine),
    ("Feedback pool acquisition is transactional", FeedbackPoolAcquisitionIsTransactional),
    ("Ballistic zero-time and invalid steps", BallisticZeroTimeAndInvalidSteps),
    ("VFX ranges normalize and validate", VfxRangesNormalizeAndValidate),
    ("VFX range setters expose every supported type", VfxRangeSettersExposeEverySupportedType),
    ("Character Controller uses the native stable component contract", CharacterControllerStableContract),
    ("Entity exposes the production layer contract", EntityLayerContract),
    ("Behaviour lifecycle contracts are synchronized", BehaviourLifecycleContract),
    ("Managed state ignores computed math properties", ManagedStateMathContract),
    ("Coroutines schedule phases and dispose deterministically", CoroutineContract),
    ("Transform and rigid body gameplay handles expose writable runtime state", GameplayHandleContract),
    ("Animator exposes transient foot-grounding control", AnimatorFootGroundingContract),
    ("Physics rejects non-finite raycasts before native dispatch", PhysicsRaycastValidationContract),
    ("Native UI button dispatch advances with the player clock", NativeUiButtonDispatchClockContract),
    ("Native runtime UI controls preserve values text focus and events", NativeRuntimeUiControlContract),
    ("Managed rendering handles preserve camera lights materials and shader overrides", ManagedRenderingContract),
    ("Managed scene loads and render settings preserve native world transactions", ManagedWorldContract),
    ("Managed jobs execute delegates and publish terminal states", ManagedJobExecutionContract),
    ("Managed jobs preserve terminal dependency semantics", ManagedJobDependencyContract),
    ("Application, time, and screen use the native foundation contract", RuntimeFoundationContract),
    ("Player preferences persist typed values atomically", PlayerPreferencesPersistenceContract),
    ("Player preferences reject invalid and corrupt data", PlayerPreferencesValidationContract),
};

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
        [typeof(Keire.Entity), typeof(float)]);
    Assert(method is not null && method.ReturnType == typeof(void),
           "Animator must expose a model-agnostic runtime foot-grounding weight.");
}

static void PhysicsRaycastValidationContract()
{
    AssertThrows<ArgumentException>(
        () => Keire.Physics.TryRaycast(default, new Keire.Vector3(float.NaN, 0.0f, 0.0f),
                                       new Keire.Vector3(0.0f, -1.0f, 0.0f), out _),
        "Raycasts must reject non-finite origins before calling native code.");
    AssertThrows<ArgumentException>(
        () => Keire.Physics.TryRaycast(default, default, new Keire.Vector3(0.0f, float.PositiveInfinity, 0.0f),
                                       out _),
        "Raycasts must reject non-finite directions before calling native code.");
    AssertThrows<ArgumentOutOfRangeException>(
        () => Keire.Physics.TryRaycast(default, default, new Keire.Vector3(0.0f, -1.0f, 0.0f), out _, float.NaN),
        "Raycasts must reject non-finite maximum distances before calling native code.");
}

static unsafe void NativeUiButtonDispatchClockContract()
{
    NativeUiDispatchFixture.Install();
    var button = new Keire.UiButton(new Keire.Entity(17, new Keire.EntityId(23, 29)));
    int clicks = 0;
    Action clicked = () => ++clicks;
    button.Clicked += clicked;
    try
    {
        NativeUiDispatchFixture.QueueClick();
        Keire.UiButton.DispatchNativeClicks();
        Assert(clicks == 1, "The first native click must be dispatched.");

        NativeUiDispatchFixture.QueueClick();
        Keire.UiButton.DispatchNativeClicks();
        Assert(clicks == 1, "A button dispatch must run at most once for the same player frame clock value.");

        NativeUiDispatchFixture.AdvanceFrame();
        Keire.UiButton.DispatchNativeClicks();
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
        Keire.CameraHandle camera = entity.Camera;
        Assert(MathF.Abs(camera.VerticalFieldOfView - 68.0f) < 0.0001f && camera.Primary,
               "Camera handles must read native lens and selection state.");
        camera.VerticalFieldOfView = 91.0f;
        camera.Projection = Keire.CameraProjection.Orthographic;
        Assert(NativeRenderingFixture.ScalarComponent == Keire.NativeRenderingComponent.Camera &&
                   NativeRenderingFixture.ScalarProperty == Keire.NativeRenderingScalarProperty.VerticalFieldOfView &&
                   MathF.Abs(NativeRenderingFixture.ScalarValue - 91.0f) < 0.0001f &&
                   NativeRenderingFixture.IntegerValue == (int)Keire.CameraProjection.Orthographic,
               "Camera writes must preserve the native component, property, and value.");

        Keire.MeshRendererHandle renderer = entity.MeshRenderer;
        Assert(renderer.Materials.Count == 2 && renderer.Materials[1].Id == NativeRenderingFixture.SecondMaterial,
               "Mesh Renderer material arrays must round-trip through the bounded native ABI.");
        renderer.Materials = [new Keire.AssetReference<Keire.Material>(NativeRenderingFixture.ReplacementMaterial)];
        renderer.PropertyBlock.SetFloat("Roughness", 0.37f);
        renderer.PropertyBlock.SetTexture(
            "Albedo", new Keire.AssetReference<Keire.Texture>(NativeRenderingFixture.ReplacementTexture));
        Assert(NativeRenderingFixture.MaterialCount == 1 &&
                   NativeRenderingFixture.FirstMaterial == NativeRenderingFixture.ReplacementMaterial &&
                   MathF.Abs(NativeRenderingFixture.MaterialFloat - 0.37f) < 0.0001f &&
                   NativeRenderingFixture.MaterialTexture == NativeRenderingFixture.ReplacementTexture,
               "Material slots and typed shader property overrides must reach native renderer state.");
        AssertThrows<ArgumentOutOfRangeException>(() => renderer.PropertyBlock.SetFloat("Invalid", float.NaN),
                                                  "Material property blocks must reject non-finite values early.");

        Keire.SpotLightHandle spot = entity.SpotLight;
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
        Assert(System.Runtime.InteropServices.Marshal.SizeOf<Keire.NativeSceneLoadStatus>() == 24 &&
                   System.Runtime.InteropServices.Marshal.SizeOf<Keire.NativeAssetId>() == 16 &&
                   System.Runtime.InteropServices.Marshal.SizeOf<Keire.NativeRenderEnvironment>() == 72,
               "Managed world structs must preserve their native ABI layouts.");
        Assert(Keire.SceneManager.ActiveScene.Asset == NativeWorldFixture.CurrentScene &&
                   Keire.SceneManager.LoadedScenes is [{ Asset: var loaded }] &&
                   loaded == NativeWorldFixture.CurrentScene,
               "Scene Manager must expose the native active and loaded scene state.");

        Keire.SceneLoadOperation load = Keire.SceneManager.LoadSceneAsync(
            new Keire.AssetReference<Keire.SceneAsset>(NativeWorldFixture.ReplacementScene));
        Assert(load.State == Keire.SceneLoadState.Loading && MathF.Abs(load.Progress - 0.5f) < 0.0001f &&
                   load.KeepWaiting,
               "Scene load operations must expose progress and remain coroutine-compatible while loading.");
        NativeWorldFixture.CompleteLoad();
        Assert(load.Succeeded && load.IsDone && !load.KeepWaiting &&
                   load.Scene.Asset == NativeWorldFixture.ReplacementScene,
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
        new[] { new Keire.JobHandle(activeState), new Keire.JobHandle(succeededState) }, out bool cancelled);
    Assert(!cancelled && dependencies.Length == 1 && dependencies[0] == 51,
           "Succeeded dependencies must be omitted because their ordering constraint is already satisfied.");
    var failedState = new Keire.ManagedJobState(_ => throw new InvalidOperationException("expected dependency failure"));
    failedState.SetId(53);
    Assert(failedState.Invoke(0, 0) != 0 && failedState.Invoke(2, 0) == 0,
           "The failed dependency fixture must publish a terminal failure.");
    dependencies = Keire.Jobs.CollectDependencyIds(new[] { new Keire.JobHandle(failedState) }, out cancelled);
    Assert(cancelled && dependencies.Length == 0,
           "A reclaimed failed dependency must still cancel dependent work without requiring a native record.");
    AssertThrows<ArgumentException>(() =>
        Keire.Jobs.CollectDependencyIds(new[] { default(Keire.JobHandle) }, out _),
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

    var dependency = new Keire.RequireComponentAttribute(typeof(Keire.CharacterControllerComponent));
    Keire.ComponentTypeId expected = Keire.ComponentType.Of<Keire.CharacterControllerComponent>();
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
        typeof(Keire.TransformHandle).GetProperty(nameof(Keire.TransformHandle.Position));
    System.Reflection.PropertyInfo? rotation =
        typeof(Keire.TransformHandle).GetProperty(nameof(Keire.TransformHandle.Rotation));
    Assert(position is { CanRead: true, CanWrite: true } && rotation is { CanRead: true, CanWrite: true },
           "World transform properties must remain writable for gameplay scripts.");

    System.Reflection.PropertyInfo? rigidBody = typeof(Keire.Entity).GetProperty(nameof(Keire.Entity.RigidBody));
    System.Reflection.MethodInfo? addForce =
        typeof(Keire.RigidBodyHandle).GetMethod(nameof(Keire.RigidBodyHandle.AddForce));
    Assert(rigidBody?.PropertyType == typeof(Keire.RigidBodyHandle) && addForce is not null,
           "Entity must expose the strongly typed rigid body gameplay API.");
    Assert(Enum.GetValues<Keire.ForceMode>().Length == 4,
           "Rigid body force modes must retain force, acceleration, impulse, and velocity-change semantics.");
    Keire.TransformHandle invalidTransform = default;
    AssertThrows<ArgumentException>(() => invalidTransform.Position = new(float.NaN, 0.0f, 0.0f),
                                    "World transform setters must reject non-finite values before crossing native code.");
    AssertThrows<ArgumentException>(() => invalidTransform.Rotation = default,
                                    "World rotation setters must reject zero quaternions before crossing native code.");
    Keire.RigidBodyHandle invalidBody = default;
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
    Keire.ComponentTypeId id = Keire.ComponentType.Of<Keire.CharacterControllerComponent>();
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
        Console.Error.WriteLine($"FAIL {name}: {exception.Message}");
    }
}

return failed == 0 ? 0 : 1;

static void PhysicalMagazineAndChamber()
{
    var weapon = new ProductionWeaponDefinition();
    var ammo = new ProductionAmmoDefinition();
    var magazineDefinition = new ProductionMagazineDefinition();
    var inventory = new PhysicalAmmunitionInventory();
    var sink = new RecordingSink();
    var inserted = new PhysicalMagazine(1, magazineDefinition, 30);
    var replacement = new PhysicalMagazine(2, magazineDefinition, 20);
    inventory.AddMagazine(replacement);
    var runtime = new ProductionWeaponRuntime(100, weapon, ammo, inventory, sink);
    runtime.SetInitialMagazine(inserted, chamberRound: true);
    Assert(runtime.Snapshot.ChamberedRounds == 1, "Closed-bolt setup must chamber one round.");
    Assert(runtime.Snapshot.MagazineRounds == 29, "Chambering must remove a round from the magazine.");

    runtime.Equip();
    Advance(runtime, 1.0f, 1);
    runtime.Tick(0.0f, new WeaponInputFrame(false, true, false, false, false, false, false), 10);
    Assert(sink.ShotCount == 1, "Semi-automatic press must emit exactly one shot.");
    Assert(runtime.Snapshot.ChamberedRounds == 0, "The fired chamber must be empty until cycling.");
    runtime.Tick(1.0f, default, 11);
    Assert(runtime.Snapshot.ChamberedRounds == 1, "Cycling must feed the next cartridge.");
    Assert(runtime.Snapshot.MagazineRounds == 28, "Cycling must consume one magazine cartridge.");
}

static void DeterministicShotIdentity()
{
    var weapon = new ProductionWeaponDefinition();
    var ammo = new ProductionAmmoDefinition();
    var magazineDefinition = new ProductionMagazineDefinition();
    var inventory = new PhysicalAmmunitionInventory();
    var sink = new RecordingSink();
    var runtime = new ProductionWeaponRuntime(0xabc, weapon, ammo, inventory, sink);
    runtime.SetInitialMagazine(new PhysicalMagazine(1, magazineDefinition, 3), chamberRound: true);
    runtime.Equip();
    Advance(runtime, 1.0f, 1);
    runtime.Tick(0.0f, new WeaponInputFrame(false, true, false, false, false, false, false), 7);
    ProductionShotId first = sink.LastShot.Id;
    Assert(first.WeaponInstance == 0xabc, "Shot identity must retain its weapon instance.");
    Assert(first.Sequence == 1, "The first shot sequence must be one.");
    Assert(first.Pellet == 0, "Single-projectile ammunition must use pellet zero.");
}

static void PickupTransaction()
{
    var inventory = new PhysicalAmmunitionInventory();
    var magazineDefinition = new ProductionMagazineDefinition();
    var magazine = new PhysicalMagazine(42, magazineDefinition, 12);
    var pickup = new WeaponPickupTransaction(
        new WeaponPickupContents("ammo.556", 18, magazine));
    Assert(pickup.TryCollect(inventory), "The first pickup collection must succeed.");
    Assert(!pickup.TryCollect(inventory), "A pickup transaction must be idempotent.");
    Assert(inventory.CountLooseRounds("ammo.556") == 18, "Loose ammunition must transfer.");
    Assert(inventory.CountNonEmptyMagazines("mag.stanag") == 1, "Magazine must transfer.");
}

static void BoundedBallisticLifecycle()
{
    var collision = new EmptyCollisionWorld();
    var impacts = new RecordingImpactSink();
    var world = new ProductionBallisticWorld(2, collision, impacts);
    var request = new WeaponShotRequest(
        new ProductionShotId(1, 1, 0),
        100.0f,
        0.004f,
        0.003f,
        0.0f,
        1.0f,
        0.01f,
        10.0f,
        10.0f,
        uint.MaxValue,
        1);
    Assert(world.Spawn(request, 9, Vector3.Zero, Vector3.UnitZ), "First projectile must spawn.");
    Assert(world.Spawn(request, 9, Vector3.Zero, Vector3.UnitZ), "Second projectile must spawn.");
    Assert(!world.Spawn(request, 9, Vector3.Zero, Vector3.UnitZ), "Capacity must be bounded.");
    Assert(world.DroppedProjectiles == 1, "Pool pressure must be observable.");
    world.Step(0.02f);
    Assert(world.ActiveCount == 0, "Expired projectiles must return to the pool.");
}

static void ReloadInterruptionReturnsReservedMagazine()
{
    var weapon = new ProductionWeaponDefinition();
    var ammo = new ProductionAmmoDefinition();
    var magazineDefinition = new ProductionMagazineDefinition();
    var inventory = new PhysicalAmmunitionInventory();
    var sink = new RecordingSink();
    var inserted = new PhysicalMagazine(1, magazineDefinition, 10);
    var replacement = new PhysicalMagazine(2, magazineDefinition, 20);
    inventory.AddMagazine(replacement);
    var runtime = new ProductionWeaponRuntime(100, weapon, ammo, inventory, sink);
    runtime.SetInitialMagazine(inserted, chamberRound: true);
    runtime.Equip();
    Advance(runtime, 1.0f, 1);

    runtime.Tick(0.0f, new WeaponInputFrame(false, false, false, false, true, false, false), 20);
    Advance(runtime, 0.8f, 21);
    Assert(runtime.Snapshot.State == WeaponRuntimeState.InsertingMagazine,
        "The reload must hold the replacement after removing the current magazine.");
    Assert(runtime.InsertedMagazine is null, "The old magazine must be removed before replacement insertion.");
    Assert(inventory.CountNonEmptyMagazines("mag.stanag") == 1,
        "Only the retained old magazine should be in inventory while the replacement is reserved.");

    runtime.Unequip();
    Assert(runtime.Snapshot.State == WeaponRuntimeState.Unequipping, "Unequip must interrupt the reload.");
    Assert(runtime.Snapshot.ReloadKind == WeaponReloadKind.None, "Interrupted reload metadata must be cleared.");
    Assert(inventory.CountNonEmptyMagazines("mag.stanag") == 2,
        "Unequip must return the reserved replacement magazine to inventory.");
    Assert(inventory.CountMagazineRounds("mag.stanag") == 29,
        "Reload interruption must preserve every round outside the chamber.");
}

static void FeedbackPoolAcquisitionIsTransactional()
{
    var item = new object();
    bool rejectAcquire = true;
    var pool = new WeaponFeedbackPool<object>(
        new[] { item },
        _ =>
        {
            if (rejectAcquire)
                throw new InvalidOperationException("activation failed");
        });

    AssertThrows<InvalidOperationException>(
        () => pool.TryAcquire(out _),
        "Activation callback failures must be observable.");
    Assert(pool.ActiveCount == 0, "A failed activation callback must release its reserved slot.");

    rejectAcquire = false;
    Assert(pool.TryAcquire(out WeaponFeedbackLease<object> first), "The rolled-back slot must remain reusable.");
    WeaponFeedbackLease<object> staleCopy = first;
    first.Dispose();
    Assert(pool.TryAcquire(out WeaponFeedbackLease<object> second), "A released slot must be reacquirable.");
    staleCopy.Dispose();
    Assert(pool.ActiveCount == 1, "A stale copied lease must not release a newer acquisition.");
    second.Dispose();
    Assert(pool.ActiveCount == 0, "The current lease must release its acquisition.");
}

static void BallisticZeroTimeAndInvalidSteps()
{
    var world = new ProductionBallisticWorld(1, new EmptyCollisionWorld(), new RecordingImpactSink());
    var request = new WeaponShotRequest(
        new ProductionShotId(1, 1, 0),
        100.0f,
        0.004f,
        0.003f,
        0.0f,
        1.0f,
        0.00005f,
        10.0f,
        10.0f,
        uint.MaxValue,
        1);
    Assert(world.Spawn(request, 9, Vector3.Zero, Vector3.UnitZ), "The test projectile must spawn.");
    world.Step(0.0f);
    Assert(world.ActiveCount == 1, "A zero-time step must not advance projectile lifetime.");
    AssertThrows<ArgumentOutOfRangeException>(
        () => world.Step(-0.01f),
        "Negative ballistic steps must be rejected.");
    AssertThrows<ArgumentOutOfRangeException>(
        () => world.Step(float.NaN),
        "Non-finite ballistic steps must be rejected.");
    world.Step(0.0001f);
    Assert(world.ActiveCount == 0, "A positive step must continue advancing projectile lifetime.");
}

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
            typeof(Keire.VfxEmitterHandle)
                .GetMethod("SetParameter",
                           System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Instance,
                           binder: null, types: new[] { typeof(Keire.AssetId), rangeType }, modifiers: null);
        Assert(handleSetter?.ReturnType == typeof(bool),
               $"VfxEmitterHandle must expose a Boolean {rangeType.Name} setter.");

        var staticSetter = typeof(Keire.Vfx).GetMethod(
            "SetParameter", System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Static, binder: null,
            types: new[] { typeof(Keire.Entity), typeof(Keire.AssetId), rangeType }, modifiers: null);
        Assert(staticSetter?.ReturnType == typeof(bool), $"Vfx must expose a Boolean {rangeType.Name} setter.");
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

static void Advance(ProductionWeaponRuntime runtime, float seconds, uint firstTick)
{
    const float step = 0.1f;
    int steps = (int)MathF.Ceiling(seconds / step);
    for (int index = 0; index < steps; ++index)
        runtime.Tick(step, default, unchecked(firstTick + (uint)index));
}

file static unsafe class NativeWorldFixture
{
    internal static readonly Keire.AssetId CurrentScene = new(301, 401);
    internal static readonly Keire.AssetId ReplacementScene = new(302, 402);
    private static byte s_loadState;

    internal static Keire.NativeRenderEnvironment Environment { get; private set; }
    internal static int RenderSetCount { get; private set; }

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
        Keire.NativeWorld.BeginSceneLoadIcall = &BeginSceneLoad;
        Keire.NativeWorld.GetSceneLoadStatusIcall = &GetSceneLoadStatus;
        Keire.NativeWorld.GetSceneLoadDiagnosticIcall = &GetSceneLoadDiagnostic;
        Keire.NativeWorld.CancelSceneLoadIcall = &CancelSceneLoad;
        Keire.NativeWorld.GetActiveSceneIcall = &GetActiveScene;
        Keire.NativeWorld.GetLoadedScenesIcall = &GetLoadedScenes;
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
        Keire.NativeWorld.GetActiveSceneIcall = null;
        Keire.NativeWorld.GetLoadedScenesIcall = null;
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
    private static byte GetActiveScene(Keire.NativeAssetId* destination)
    {
        if (destination == null)
            return 0;
        *destination = new Keire.NativeAssetId { High = CurrentScene.High, Low = CurrentScene.Low };
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int GetLoadedScenes(Keire.NativeAssetId* destination, int capacity)
    {
        if (destination == null || capacity == 0)
            return 1;
        if (capacity < 1)
            return -1;
        destination[0] = new Keire.NativeAssetId { High = CurrentScene.High, Low = CurrentScene.Low };
        return 1;
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
    private static byte SetFlag(ulong high, ulong low, byte component, byte property, byte value) =>
        high == 23 && low == 29 ? (byte)1 : (byte)0;

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
}

file sealed class RecordingSink : IWeaponRuntimeSink
{
    public int ShotCount { get; private set; }
    public WeaponShotRequest LastShot { get; private set; }

    public void OnStateChanged(in WeaponRuntimeSnapshot snapshot)
    {
    }

    public void OnShot(in WeaponShotRequest request)
    {
        ++ShotCount;
        LastShot = request;
    }

    public void OnDryFire()
    {
    }

    public void OnMagazineRemoved(PhysicalMagazine magazine)
    {
    }

    public void OnMagazineInserted(PhysicalMagazine magazine)
    {
    }

    public void OnShellInserted(int tubeRounds)
    {
    }
}

file sealed class EmptyCollisionWorld : IBallisticCollisionWorld
{
    public bool SweepSphere(in BallisticSweepRequest request, out BallisticSweepHit hit)
    {
        hit = default;
        return false;
    }
}

file sealed class DetachedManagedContractProbe : Keire.Behaviour;

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

file sealed class RecordingImpactSink : IBallisticImpactSink
{
    public WeaponDamageResponse ApplyDamage(in WeaponDamagePacket damage) => default;

    public void OnImpact(in WeaponDamagePacket damage, bool penetrated, bool ricocheted)
    {
    }

    public void OnTracer(in ProductionShotId shotId, Vector3 start, Vector3 end)
    {
    }
}
