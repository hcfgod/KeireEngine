# C# Scripting Cookbook

[Scripting home](README.md) · [Workflow map](WorkflowMap.md) · [Runtime reference](RuntimeReference.md)

These examples compile against the current managed API with nullable checking and warnings treated as errors.
They demonstrate engine calls and ownership; native execution requires the Editor or a packaged player with the
described content. Compilation does not establish that a scene, input map, controller, shader, or cooked asset exists.

## Use An Example

1. Copy the named `.cs` files from [Examples](Examples) into a runtime folder under your project's `Assets`.
2. Keep each public Behaviour or ScriptableObject in its matching filename. All examples use `ScriptingExamples`;
   change that namespace consistently if you prefer your project's namespace.
3. Each example already has distinct stable IDs. If creating a second, independent type from a copy, generate new
   component/asset and field UUIDs. Preserve IDs when editing or renaming the original persisted type/field.
4. Wait for a successful managed build, attach the Behaviour, and perform the recipe's Inspector setup.
5. Test Play Mode, disabling and re-enabling the owner, and script reload. Read the recipe's limitations before
   extending it into game logic.

The code blocks below are synchronized from the source files by the
[reference tool](ApiIndex.md#maintaining-the-member-reference). The
[example project](Examples/Keire.ScriptingExamples.csproj) is a compile harness, not a standalone game.

## Recipe Directory

| Recipe | What it teaches |
| --- | --- |
| [Fixed-step action movement](#fixed-step-action-movement) | Private action contexts, component requirements, reload-safe acquisition |
| [Ray interaction](#ray-interaction) | Direct input, nearest hit, hierarchy lookup, transient reload state |
| [Timed prefab spawning](#timed-prefab-spawning) | Prefab references, coroutine timing, delayed destruction |
| [Trigger feedback](#trigger-feedback) | Contact callbacks connected to audio, animation, and VFX |
| [Per-renderer damage flash](#per-renderer-damage-flash) | Typed material properties, scaled decay, selective cleanup |
| [Owned additive room](#owned-additive-room) | Scene operations, duplicate guards, cancellation, unload ownership |
| [Explicit audio residency](#explicit-audio-residency) | Asset readiness, failures, deterministic lease release |
| [Worker data and owner-thread publication](#worker-data-and-owner-thread-publication) | Snapshot capture, cancellation, job completion and profiling |
| [Source-backed health panel](#source-backed-health-panel) | Authored binding keys, consumptive input, live document queries |
| [Follow a presentation transform](#follow-a-presentation-transform) | Camera timing, optional targets, interpolated visual state |
| [Shared tuning data](#shared-tuning-data) | Persistent ScriptableObjects, typed asset dependencies |
| [Player settings and display options](#player-settings-and-display-options) | Defaults, explicit saving, supported display modes |

## Fixed-Step Action Movement

Source: [ActionMotor.cs](Examples/ActionMotor.cs).

Create an Input Action Asset with a `Player` map and a `Move` action returning Axis2D. Bind WASD or a stick. Attach
`ActionMotor` to a character root, assign the asset, and configure the required Character Controller. Add collision
geometry to the scene. Press movement controls during Play Mode; the character submits horizontal displacement each
fixed tick and clamps diagonal intent to unit length.

This example owns a context so disabling one motor does not disable another motor's actions through an asset's shared
context. It intentionally demonstrates horizontal motion only. Add gravity, jumping, and grounded-state handling for
a full controller; the engine does not supply gravity through this script. Do not also write the root position from a
second movement script. Bind the intended device/local player in the project input configuration.

<!-- example:ActionMotor.cs -->

```csharp
using Keire;

namespace ScriptingExamples;

[StableComponentId("d9918070-7683-4104-90eb-5a9bd8c5f001")]
[RequireComponent(typeof(CharacterController))]
public sealed class ActionMotor : Behaviour
{
    [SerializeField, StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f101")]
    private InputActionAsset? _actions = null;

    [SerializeField, StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f102"), Min(0.0)]
    private float _speed = 4.0f;

    private InputActionContext? _context;
    private InputAction? _move;
    private CharacterController? _motor;

    protected override void OnEnable() => Bind();
    protected override void OnDisable() => Release();
    protected override void OnBeforeReload() => Release();
    protected override void OnAfterReload()
    {
        if (Enabled && Entity.ActiveInHierarchy)
            Bind();
    }

    private void Bind()
    {
        Release();
        _motor = GetComponent<CharacterController>();
        if (_actions is not { IsValid: true })
            return;
        _context = _actions.CreateContext();
        _move = _context.FindAction("Player/Move");
        _move?.Enable();
        if (_move is null)
            Debug.Warn("ActionMotor needs a Player/Move Axis2D action.");
    }

    private void Release()
    {
        _context?.Dispose();
        _context = null;
        _move = null;
        _motor = null;
    }

    protected override void FixedUpdate()
    {
        if (_motor is not { IsValid: true })
            return;
        Vector2 input = _move?.ReadValue<Vector2>() ?? Vector2.Zero;
        Vector3 direction = Transform.Right * input.X + Transform.Forward * input.Y;
        if (direction.LengthSquared > 1.0f)
            direction = direction.Normalized;
        if (!_motor.Move(direction * (_speed * Time.FixedDeltaTime)))
            Debug.Warn("Character movement was rejected.");
    }
}
```

<!-- /example -->

For a jump, collect the press once in `Update`, retain a pending request, and consume it once in `FixedUpdate`.
Repeated fixed ticks see the same frame snapshot, so reading `WasPressedThisFrame` independently on every tick can
submit the same jump repeatedly. See [Input](GameplayServices.md#input) and
[Character Controller](GameplayServices.md#character-controller).

## Ray Interaction

Sources: [Interactable.cs](Examples/Interactable.cs), [RayInteractor.cs](Examples/RayInteractor.cs).

Attach `RayInteractor` to the player/view entity. Give the target a Collider and attach `Interactable` either to that
entity or one of its parents. Position it within three world units of the ray origin. Press **E** to log one use.
The hierarchy lookup lets a child collider delegate interaction to the owning object. `_uses` survives successful
script reload through `[HotReloadState]`; it is not saved as scene data.

<!-- example:Interactable.cs -->

```csharp
using Keire;

namespace ScriptingExamples;

[StableComponentId("d9918070-7683-4104-90eb-5a9bd8c5f002")]
public sealed class Interactable : Behaviour
{
    [HotReloadState]
    private int _uses;

    public void Interact(Entity actor)
    {
        ++_uses;
        Debug.Log($"{actor.Name} used {Entity.Name}; uses={_uses}.");
    }
}
```

<!-- /example -->

<!-- example:RayInteractor.cs -->

```csharp
using Keire;

namespace ScriptingExamples;

[StableComponentId("d9918070-7683-4104-90eb-5a9bd8c5f003")]
public sealed class RayInteractor : Behaviour
{
    protected override void Update()
    {
        if (Input.Keyboard.Current?.eKey.WasPressedThisFrame != true)
            return;
        if (Physics.TryRaycast(Entity, Transform.Position, Transform.Forward, out RaycastHit hit,
                maximumDistance: 3.0f, ignoredEntity: Entity) &&
            hit.Entity.GetComponentInParent<Interactable>() is { IsValid: true } target)
        {
            target.Interact(Entity);
            Debug.DrawLine(Transform.Position, hit.Point, Color.RedColor, 0.25f);
        }
    }
}
```

<!-- /example -->

`TryRaycast` normalizes the direction and returns only the nearest hit. This example ignores the caller's physics
entity; it does not automatically ignore every descendant body. Configure a layer mask when your rig uses separate
child bodies. Use a authored Input Action instead of direct keyboard polling when interactions need rebinding or
gamepad support. See [Physics Queries](GameplayServices.md#physics-queries).

## Timed Prefab Spawning

Source: [TimedSpawner.cs](Examples/TimedSpawner.cs).

Create a prefab, attach the spawner to a scene entity, and assign the prefab field. On enable it spawns three instances
one scaled second apart. Each instance schedules destruction five scaled seconds after spawning.

<!-- example:TimedSpawner.cs -->

```csharp
using System.Collections;
using Keire;

namespace ScriptingExamples;

[StableComponentId("d9918070-7683-4104-90eb-5a9bd8c5f004")]
public sealed class TimedSpawner : Behaviour
{
    [SerializeField, StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f104")]
    private Prefab? _prefab = null;

    protected override void OnEnable() => Begin();
    protected override void OnAfterReload()
    {
        if (Enabled && Entity.ActiveInHierarchy)
            Begin();
    }

    private void Begin()
    {
        StopAllCoroutines();
        if (_prefab is { IsValid: true })
            StartCoroutine(Spawn());
    }

    private IEnumerator Spawn()
    {
        for (int index = 0; index < 3; ++index)
        {
            if (_prefab is not { IsValid: true })
                yield break;
            Entity instance = _prefab.Instantiate(Transform.Position, Transform.Rotation);
            instance.Destroy(5.0f);
            yield return new WaitForSeconds(1.0f);
        }
    }
}
```

<!-- /example -->

Disable and reload stop the routine automatically. Existing spawned objects retain their own destruction requests;
they are not children owned by the coroutine. A successful reload starts a fresh batch while enabled. If a wave must
resume its previous count, persist an explicit counter and design the restart rule before adding `[HotReloadState]`.
Instantiation may run required `Awake`/`OnEnable` before returning, so assign authored dependencies on the prefab;
do not assume post-instantiation writes can initialize fields before those callbacks.

## Trigger Feedback

Source: [FeedbackTrigger.cs](Examples/FeedbackTrigger.cs).

Configure a trigger Collider and the physics bodies/layers needed for contacts. Attach this script to its entity.
Optionally add Audio Source, Animator, and VFX Emitter components. Assign an Audio Clip; assign an Animator Controller
with an `Activate` trigger; configure an emitter effect with a `Burst` event. Enter the volume in Play Mode to invoke
the available feedback components.

<!-- example:FeedbackTrigger.cs -->

```csharp
using Keire;

namespace ScriptingExamples;

[StableComponentId("d9918070-7683-4104-90eb-5a9bd8c5f005")]
public sealed class FeedbackTrigger : Behaviour
{
    [SerializeField, StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f105")]
    private AudioClip? _clip = null;

    protected override void OnTriggerEnter(CollisionContact contact)
    {
        if (!contact.Other.IsValid)
            return;
        if (_clip is { IsValid: true } && GetComponent<AudioSource>() is { IsValid: true } source)
            source.Play(_clip);
        if (GetComponent<Animator>() is { IsValid: true } animator)
            animator.SetTrigger("Activate");
        if (GetComponent<VfxEmitter>() is { IsValid: true } emitter)
            emitter.SendEvent("Burst", spawnCount: 8);
    }
}
```

<!-- /example -->

The script permits optional components but their configured names must be valid. Missing animator parameters can
throw and quarantine this Behaviour; optional component checks do not validate asset content. For gameplay pickups,
filter the other entity by tag or component and guard against repeated contacts before granting a reward. Audio and
VFX playback APIs can report failure with `false`; a production feedback system should decide whether to retry,
log, or deliberately skip unavailable presentation. See [Audio](Audio.md), [Animation](Animation.md), and
[VFX](GameplayServices.md#vfx).

## Per-Renderer Damage Flash

Source: [DamageFlash.cs](Examples/DamageFlash.cs).

Expose a float property named `Damage` in the object's shader/material graph and connect it to the visual effect.
Attach this script to the Mesh Renderer entity. Call `Flash()` from your hit/interaction logic; the value decays to
zero over roughly one third of a scaled second. During initial testing, invoke it from a temporary input callback.

<!-- example:DamageFlash.cs -->

```csharp
using Keire;

namespace ScriptingExamples;

[StableComponentId("d9918070-7683-4104-90eb-5a9bd8c5f006")]
public sealed class DamageFlash : Behaviour
{
    [HotReloadState]
    private float _amount;

    public void Flash() => _amount = 1.0f;

    protected override void Update()
    {
        if (GetComponent<MeshRenderer>() is not { IsValid: true } renderer)
            return;
        _amount = Math.Max(0.0f, _amount - Time.DeltaTime * 3.0f);
        renderer.PropertyBlock.SetFloat("Damage", _amount);
    }

    protected override void OnDisable() => Clear();
    protected override void OnBeforeReload() => Clear();

    private void Clear()
    {
        if (Entity.IsValid && GetComponent<MeshRenderer>() is { IsValid: true } renderer)
            renderer.PropertyBlock.Reset("Damage");
    }
}
```

<!-- /example -->

Only this script should own the renderer's `Damage` property. Cleanup resets that property rather than clearing
other systems' overrides. Unknown shader properties do not manufacture an effect: they can remain stored without
affecting the current shader. Use `GetMaterialInstance(slot)` when the effect belongs to a single slot, or a Material
Parameter Collection for global weather. See [Rendering and Materials](RenderingAndMaterials.md).

## Owned Additive Room

Source: [AdditiveRoom.cs](Examples/AdditiveRoom.cs).

Attach this to an entity in a scene that stays loaded. Assign a Scene Asset with cooked dependencies. Press **L** to
load it additively. Duplicate requests are ignored while a load is pending or the room is already loaded. Disabling
or reloading this controller releases its owned room, including a load that completed before the observing coroutine
resumed.

<!-- example:AdditiveRoom.cs -->

```csharp
using System.Collections;
using Keire;

namespace ScriptingExamples;

[StableComponentId("d9918070-7683-4104-90eb-5a9bd8c5f007")]
public sealed class AdditiveRoom : Behaviour
{
    [SerializeField, StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f107")]
    private SceneAsset? _room = null;

    private SceneLoadOperation? _operation;
    private Scene? _loaded;

    public void Load()
    {
        if (_operation is not null || _loaded is { IsLoaded: true } || _room is not { IsValid: true })
            return;
        _operation = SceneManager.LoadSceneAsync(_room, SceneLoadMode.Additive);
        StartCoroutine(Observe(_operation));
    }

    private IEnumerator Observe(SceneLoadOperation operation)
    {
        yield return operation;
        if (operation.Succeeded)
            _loaded = operation.Scene;
        else
            Debug.Warn($"Room load ended with {operation.State}: {operation.Error}");
        _operation = null;
    }

    protected override void Update()
    {
        if (Input.Keyboard.Current?.lKey.WasPressedThisFrame == true)
            Load();
    }

    protected override void OnDisable() => Release();
    protected override void OnBeforeReload() => Release();

    private void Release()
    {
        StopAllCoroutines();
        if (_operation is { Succeeded: true })
            _loaded = _operation.Scene;
        else
            _operation?.Cancel();
        _operation = null;
        if (_loaded is { IsLoaded: true } && !SceneManager.UnloadScene(_loaded))
            Debug.Warn("The owned room could not be unloaded.");
        _loaded = null;
    }
}
```

<!-- /example -->

This policy unloads the owned room before managed reload; it does not transfer room ownership to the replacement
instance. Press **L** again to reload it. Do not place the owner inside the room it unloads. Other scripts must not
activate or independently unload this owned room without coordinating ownership. If the active scene cannot be
unloaded, the API rejects the request and the example reports it; design a persistent scene director for more complex
policies. `Single` loading can destroy the requesting entity, so persistent directors are preferable when post-load
work must run after that transition. See [Scenes and Render Settings](ScenesAndRenderSettings.md).

## Explicit Audio Residency

Source: [ResidentAudio.cs](Examples/ResidentAudio.cs).

Attach to an Audio Source entity, assign a clip, and ensure a listener/output is configured. The script obtains an
explicit high-priority residency lease, yields until the load reaches a terminal/usable state, and then starts playback.
Disable or reload stops playback and releases the lease.

<!-- example:ResidentAudio.cs -->

```csharp
using System.Collections;
using Keire;

namespace ScriptingExamples;

[StableComponentId("d9918070-7683-4104-90eb-5a9bd8c5f008")]
[RequireComponent(typeof(AudioSource))]
public sealed class ResidentAudio : Behaviour
{
    [SerializeField, StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f108")]
    private AudioClip? _clip = null;

    private AssetLoadOperation<AudioClip>? _lease;

    protected override void OnEnable() => Begin();
    protected override void OnAfterReload()
    {
        if (Enabled && Entity.ActiveInHierarchy)
            Begin();
    }

    private void Begin()
    {
        Release();
        if (_clip is not { IsValid: true })
            return;
        _lease = Assets.LoadRuntime(_clip, AssetLoadPriority.High);
        StartCoroutine(PlayWhenReady(_lease));
    }

    private IEnumerator PlayWhenReady(AssetLoadOperation<AudioClip> lease)
    {
        yield return lease;
        if (lease.IsReady && GetComponent<AudioSource>() is { IsValid: true } source)
            source.Play(lease.Asset);
        else
            Debug.Warn($"Audio residency failed: {lease.Diagnostic.Message}");
    }

    protected override void OnDisable() => Release();
    protected override void OnBeforeReload() => Release();

    private void Release()
    {
        StopAllCoroutines();
        if (Entity.IsValid && GetComponent<AudioSource>() is { IsValid: true } source)
            source.Stop();
        _lease?.Dispose();
        _lease = null;
    }
}
```

<!-- /example -->

`IsValid` on the clip checks identity; `IsReady` on the lease checks runtime availability. `IsDone` alone would also
include failure/cancellation. Holding a serialized clip reference does not replace explicit residency ownership.
Keep the lease for as long as this policy needs it and dispose it at every ownership exit. The direct asset object
remains an identity/reference after disposal. See [Assets and ScriptableObjects](AssetsAndScriptableObjects.md).

## Worker Data And Owner-Thread Publication

Source: [PreparedPositions.cs](Examples/PreparedPositions.cs).

Attach to any entity in a running scene. The example captures its origin on the owner thread, creates an owned array,
computes points on a job worker, and draws the result on the owner thread once the job succeeds. The small workload is
for teaching; such a trivial calculation would ordinarily cost less inline than scheduling a job.

<!-- example:PreparedPositions.cs -->

```csharp
using System.Collections;
using Keire;

namespace ScriptingExamples;

[StableComponentId("d9918070-7683-4104-90eb-5a9bd8c5f009")]
public sealed class PreparedPositions : Behaviour
{
    private Job? _job;

    protected override void OnEnable() => Begin();
    protected override void OnAfterReload()
    {
        if (Enabled && Entity.ActiveInHierarchy)
            Begin();
    }

    private void Begin()
    {
        Release();
        Vector3 origin = Transform.Position;
        var points = new Vector3[64];
        _job = Jobs.Submit(context =>
        {
            for (int index = 0; index < points.Length; ++index)
            {
                context.CancellationToken.ThrowIfCancellationRequested();
                points[index] = origin + new Vector3(index * 0.5f, 0.0f, 0.0f);
            }
        }, new JobDescription { Name = "Prepare debug positions" });
        StartCoroutine(Publish(_job, points));
    }

    private IEnumerator Publish(Job job, Vector3[] points)
    {
        while (!job.Completion.IsCompleted)
            yield return null;
        if (job.Status == JobStatus.Succeeded && Entity.IsValid)
        {
            using (Profiler.Sample("PreparedPositions.Publish"))
            {
                for (int index = 1; index < points.Length; ++index)
                    Debug.DrawLine(points[index - 1], points[index], Color.RedColor, 2.0f);
            }
            Profiler.Counter("PreparedPositions.Count", points.Length);
        }
        else if (job.Completion.Exception is { } exception)
            Debug.LogException(exception);
        _job = null;
    }

    protected override void OnDisable() => Release();
    protected override void OnBeforeReload() => Release();

    private void Release()
    {
        StopAllCoroutines();
        _job?.Cancel();
        _job = null;
    }
}
```

<!-- /example -->

The worker captures a value and an array rather than accessing `Entity`, `Transform`, or UI. Only the worker writes
the array until job completion. Cancellation is cooperative, so a canceled job can take time to observe the token;
stopping the publication coroutine prevents an old owner from applying its eventual result. For await-based work,
begin inside a managed callback, use `LifetimeToken`, and recheck validity after awaiting. Avoid `.Wait()`/`.Result`
on the gameplay thread. See [Async, Reload, and Diagnostics](AsyncReloadAndDiagnostics.md).

## Source-Backed Health Panel

Source: [HealthPanel.cs](Examples/HealthPanel.cs).

Create a `.keireui` document in UI Builder. Add a ProgressBar with minimum `0`, maximum `100`, and a **OneWay**
binding from source path `Player.Health` to target property `value`. Add a button named `heal`. Assign the tree and
appropriate Panel Settings to a scene UIDocument and attach this Behaviour to the same entity. Call `Damage(25)`
from gameplay, then click Heal to restore health.

<!-- example:HealthPanel.cs -->

```csharp
using Keire;
using Keire.UI;

namespace ScriptingExamples;

[StableComponentId("d9918070-7683-4104-90eb-5a9bd8c5f010")]
[RequireComponent(typeof(UIDocument))]
public sealed class HealthPanel : Behaviour
{
    [HotReloadState]
    private float _health = 100.0f;

    public void Damage(float amount) => _health = Math.Clamp(_health - amount, 0.0f, 100.0f);

    protected override void Update()
    {
        if (GetComponent<UIDocument>() is not { IsValid: true } document)
            return;
        document.SetBindingValue("Player.Health", _health);
        if (document.Q("heal") is { IsAlive: true } heal && heal.ClickedThisFrame)
            _health = 100.0f;
    }

    protected override void OnDisable() => Clear();
    protected override void OnBeforeReload() => Clear();

    private void Clear()
    {
        if (Entity.IsValid && GetComponent<UIDocument>() is { IsValid: true } document)
            document.ClearBindingSource();
    }
}
```

<!-- /example -->

The dotted path is an explicit binding key, not an automatic reflection lookup. This recipe owns the document's binding
source; multiple scripts must coordinate before clearing a shared source. Querying the live button each frame avoids
retaining a stale tree generation after UI Builder reload. A query can return `null` before first presentation.
`ClickedThisFrame` consumes its pending event; have one input owner and fan out the gameplay action from there.
Managed `VisualElement` trees and source-backed `RuntimeVisualElement` handles are distinct surfaces; use the
[UI guide](UiAndEvents.md) for events, two-way fields, and custom controls.

## Follow A Presentation Transform

Source: [FollowTarget.cs](Examples/FollowTarget.cs).

Attach to the camera entity and drag a target entity into the field. Set the world-space offset. This example snaps
the camera position in `LateUpdate`; it does not rotate it or perform obstruction avoidance. The target's
`PresentationPosition` includes available interpolation instead of exposing stepped fixed simulation position.

<!-- example:FollowTarget.cs -->

```csharp
using Keire;

namespace ScriptingExamples;

[StableComponentId("d9918070-7683-4104-90eb-5a9bd8c5f011")]
public sealed class FollowTarget : Behaviour
{
    [SerializeField, StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f111")]
    private Entity? _target = null;

    [SerializeField, StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f112")]
    private Vector3 _offset = new(0.0f, 2.0f, -5.0f);

    protected override void LateUpdate()
    {
        if (_target is { IsValid: true })
            Transform.Position = _target.Transform.PresentationPosition + _offset;
    }
}
```

<!-- /example -->

For custom fixed-step transform motion, opt into `FixedPresentationInterpolation` on the target and reset its
interpolation history on a teleport. Gameplay physics uses `Position`; visual following can use
`PresentationPosition`. The offset here is in world axes, so it does not rotate with the target.
See [Transforms](EntitiesComponentsAndTransforms.md) and [Animation IK](Animation.md).

## Shared Tuning Data

Source: [MovementSettings.cs](Examples/MovementSettings.cs).

Compile this type, then use **Create > Examples > Movement Settings** in the Project panel. Set Speed and assign a
Footstep clip on the persistent asset. A Behaviour can declare a serialized `MovementSettings?` field and assign that
asset through the Inspector. Several characters can then use one authored tuning asset.

<!-- example:MovementSettings.cs -->

```csharp
using Keire;

namespace ScriptingExamples;

[StableAssetTypeId("d9918070-7683-4104-90eb-5a9bd8c5f012")]
[CreateAssetMenu("Examples/Movement Settings", "MovementSettings")]
public sealed class MovementSettings : ScriptableObject
{
    [StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f113"), Min(0.0)]
    public float Speed = 4.0f;

    [StableFieldId("d9918070-7683-4104-90eb-5a9bd8c5f114")]
    public AudioClip? Footstep;
}
```

<!-- /example -->

Persistent data may refer to project assets but not scene entities/components. Create a transient per-character copy
with `ScriptableObject.Instantiate(settings)` when runtime changes must be isolated, and destroy that copy when its
owner releases it. `ScriptableObject.CreateInstance<MovementSettings>()` creates defaults without creating a Project
asset. Stable asset type and field IDs preserve identity when the source is renamed. See
[Assets and ScriptableObjects](AssetsAndScriptableObjects.md) for loading, constructors, and dependency closure.

## Player Settings And Display Options

The following callback-body excerpt shows a settings screen's explicit Apply action:

The complete compile-checked helper is [SettingsExample.cs](Examples/SettingsExample.cs).

```csharp
float volume = PlayerPreferences.GetFloat("audio.master", 0.8f);
PlayerPreferences.SetFloat("audio.master", Math.Clamp(volume, 0.0f, 1.0f));
PlayerPreferences.SetBool("accessibility.subtitles", true);
PlayerPreferences.Save();

if (Screen.IsPresentModeSupported(PresentMode.VSync))
    Screen.TrySetPresentMode(PresentMode.VSync);
if (!Screen.TrySetResolution(1920, 1080, FullscreenMode.Windowed))
    Debug.Warn("Requested resolution is unavailable.");
```

Preferences store values; applying audio gain is a separate operation on your source or mixer bus. Save on explicit
commit, not every slider tick. Type-mismatched keys use the caller's default. Invalid data, persistence failures, and
unsupported display configurations need a visible failure policy. See [Gameplay Services](GameplayServices.md) for
contracts and [Audio](Audio.md) for applying bus settings.

## Verify And Extend The Examples

Run the [compilation command](ApiIndex.md#maintaining-the-member-reference) after changing examples. In-engine checks
should cover assigned and unassigned references, enable/disable cycles, successful/failed reload, scene teardown, and
the packaged player. A plain .NET process can compile these calls but has no attached native runtime for gameplay.

For advanced complete patterns, continue with [managed converters, services, and Editor extensions](ManagedExtensibility.md),
[animation and IK](Animation.md), [UI events and controls](UiAndEvents.md), and [GPU compute](Compute.md).
