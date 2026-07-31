# Audio From C#

Managed audio supports typed clip and mixer references, stateful Audio Source control, one-call playback options, and
playback status. Audio data and native voices remain engine-owned.

## Import And Scene Setup

Import an audio file through the Project panel. Kéire publishes an `AudioClip` asset and exposes its import metadata in
the Inspector.

For stateful scene-authored playback:

1. Add an **Audio Source** component to the entity.
2. Assign a clip, mixer/bus routing, gain, pitch, priority, looping, spatialization, and attenuation as needed.
3. Add an **Audio Listener** to the active listening entity.
4. Use `Entity.AudioSource` or `Audio.Play(Entity)` from the script.

For an individual playback request with explicit clip and options, use `Audio.Play(Entity, clip, options)`.

## Reference Audio Correctly

`AudioClip` is an asset marker. Store an `AssetReference<AudioClip>`:

```csharp
[SerializeField, StableFieldId("33b83319-6061-4056-b28c-71cce922d51d")]
private AssetReference<AudioClip> _openSound;
```

Do not use:

```csharp
[SerializeField] private AudioClip? _openSound;
```

`AssetReference<AudioClip>` is a value type. Check `IsValid`, not `null`:

```csharp
if (_openSound.IsValid)
    Audio.Play(Entity, _openSound);
```

Equivalent ID form:

```csharp
if (_openSound.IsValid)
    Audio.Play(Entity, _openSound.Id);
```

## Stateful Audio Source

Get the entity's handle:

```csharp
AudioSourceHandle source = Entity.AudioSource;
if (!source.IsValid)
{
    Debug.Warn($"{Entity.Name} needs an Audio Source.");
    return;
}
```

Configure and play:

```csharp
source.Clip = _openSound;
source.Volume = 0.8f;
source.Pitch = 1.1f;
source.Loop = false;
source.Spatial = true;

if (!source.Play())
    Debug.Warn("Audio Source playback was rejected.");
```

An `AudioSourceHandle` never creates a missing component. Its property getters and setters throw
`InvalidOperationException` when the entity does not have an Audio Source, so check `IsValid` or add the component
explicitly. `Volume` must be finite and between `0.0` and `16.0`; `Pitch` must be finite, greater than `0.01`, and at
most `8.0`. Invalid scalar assignments throw `ArgumentOutOfRangeException` before native state is changed.

Control playback:

```csharp
source.Pause();
if (!source.Seek(0.5f))
    Debug.Warn("Audio Source seek was rejected.");
source.Resume();
source.Stop();
```

Inspect it:

```csharp
AudioSourceStatus status = source.Status;
Debug.Log($"{status.State}: {status.Time:0.00}/{status.Duration:0.00}");

if (source.IsPlaying)
    UpdatePlaybackUi(source.Time, source.Duration);
```

`Pause` preserves the playhead. `Seek` and assigning `Time` require a finite, non-negative time. Native playback may
still reject a valid command when the source or voice cannot perform it; `Seek` returns `false`, while assigning `Time`
throws `InvalidOperationException`.

## Playback Patterns

Play the clip configured on an Audio Source:

```csharp
Audio.Play(Entity);
```

Play a specific clip with default options:

```csharp
Audio.Play(Entity, _openSound);
```

Play an untyped ID with a simple gain:

```csharp
Audio.Play(Entity, _openSound.Id, volume: 0.65f);
```

Provide complete options:

```csharp
Audio.Play(Entity, _openSound, new AudioPlaybackOptions
{
    Bus = "UI",
    Gain = 0.8f,
    Pitch = 1.0f,
    Priority = 192,
    Loop = false,
    Spatial = false,
    MinimumDistance = 1.0f,
    MaximumDistance = 20.0f
});
```

Check the return value when playback failure matters to the game.

## Audio Playback Options

| Property | Default | Valid contract |
| --- | --- | --- |
| `Bus` | `"SFX"` | Non-empty, at most 128 UTF-8 bytes |
| `Mixer` | Invalid reference | Optional typed `AssetReference<AudioMixer>` |
| `BusId` | Invalid ID | Optional stable mixer bus identity |
| `Gain` | `1.0` | Finite, `0.0` through `16.0` |
| `Pitch` | `1.0` | Finite, greater than `0.01` and at most `8.0` |
| `Priority` | `128` | `0` through `255` |
| `Loop` | `false` | Whether playback repeats |
| `Spatial` | `true` | Whether entity/listener spatialization applies |
| `MinimumDistance` | `1.0` | Finite and non-negative |
| `MaximumDistance` | `100.0` | Finite and greater than minimum distance |

Use a non-spatial request for UI and global feedback. Use a spatial request for world sounds attached to a meaningful
source entity.

## Mixer And Bus References

Declare a mixer:

```csharp
[SerializeField, StableFieldId("2c270719-41c4-4830-a5c9-bc0d0ca7aa94")]
private AssetReference<AudioMixer> _gameplayMixer;
```

Route with the typed mixer and, when available, a stable bus ID:

```csharp
AudioPlaybackOptions options = new()
{
    Mixer = _gameplayMixer,
    BusId = _gameplayBus,
    Bus = "SFX",
    Gain = 0.9f
};
```

When the mixer asset is available, `BusId` selects its compiled fader, mute, solo, and parent-bus routing even if the
bus was renamed. If the stable ID is empty or no longer exists, the runtime looks up `Bus`; if neither resolves, it
routes to that mixer's Master bus. Until a referenced mixer is loaded, playback uses the legacy bus-name path. Mixer
asset revisions replace the routing snapshot transactionally, so voices already playing observe a valid hot reload.
Runtime diagnostics and legacy string controls use the bus's currently resolved authored name after a rename.

## Animation-Driven Audio

Use animation events for footsteps and similar synchronized sounds:

```csharp
[SerializeField, StableFieldId("592be18a-428e-4a21-af36-b5d48724a82a")]
private AssetReference<AudioClip> _footstep;

[SerializeField, StableFieldId("bf3391e8-0099-4d9b-bb23-40aa39e342f0")]
[Range(0.0, 1.0)]
private float _footstepVolume = 0.7f;

protected override void OnAnimationEvent(AnimationEvent animationEvent)
{
    if (animationEvent.Name == "Footstep" && _footstep.IsValid)
        Audio.Play(Entity, _footstep.Id, _footstepVolume);
}
```

The Animator Controller and clip authoring determine when the event fires. Keep event names stable and case-consistent.

## UI Audio

Guard optional references:

```csharp
private void HandleButtonClicked()
{
    ToggleMenu();

    if (_openSound.IsValid)
    {
        Audio.Play(Entity, _openSound, new AudioPlaybackOptions
        {
            Bus = "UI",
            Spatial = false
        });
    }
}
```

If playback belongs to both keyboard and button activation, call it from the shared action method rather than only one
input handler.

## Common Errors

| Symptom | Cause | Fix |
| --- | --- | --- |
| Cannot convert `AudioClip` to `AssetId` | Field uses the marker object instead of an asset reference | Use `AssetReference<AudioClip>` and pass it or `.Id` |
| Cannot compare the clip with `null` | `AssetReference<T>` is a value type | Check `.IsValid` |
| `Play()` returns `false` | Source, voice, or runtime playback rejected the command | Validate the entity, component, clip, and runtime state |
| `Audio.Play` throws for the clip | The reference ID is invalid | Guard with `.IsValid` |
| Audio Source property throws | The handle is invalid or the value is outside its documented range | Check `source.IsValid` and validate the value |
| Options throw | Gain, pitch, priority, bus, or distance range violates the contract | Validate values before constructing the request |
| Audio is unexpectedly spatial | Default `Spatial` is `true` | Set `Spatial = false` for UI/global audio |

For audio asset formats, import metadata, mixers, and source authoring, see the **Runtime UI And Audio** section of the
[root README](../../README.md#runtime-ui-and-audio).
