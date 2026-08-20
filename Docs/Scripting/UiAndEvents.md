# UI And Events From C#

Kéire supports scene-authored runtime UI, managed control bindings, text updates, persistent Inspector events,
runtime listeners, keyboard/gamepad navigation, UTF-8 input, scrolling, and cooperative cursor ownership.

## Scene UI References

Use `Entity` for general UI objects such as panels and labels:

```csharp
[SerializeField, StableFieldId("4abb4e35-ad42-4bc0-8208-22bc7d7fc078")]
private Entity _panel;

[SerializeField, StableFieldId("fcb61c69-1ec5-4d90-9f7a-ab06503dddae")]
private Entity _ammoLabel;
```

Use typed managed handles for scene-authored controls:

```csharp
[SerializeField, StableFieldId("3ea684dc-669f-4407-a358-8ed110de569b")]
private UiButton? _resumeButton;

[SerializeField, StableFieldId("71a9094c-d1c5-4e7c-8bfe-ab0726129b78")]
private UiSlider? _sensitivity;

[SerializeField, StableFieldId("eb542341-d318-4931-ae6a-2f1c95608fe3")]
private UiInputField? _profileName;
```

`UiButton`, `UiSlider`, `UiToggle`, `UiInputField`, and `UiScrollView` are managed reference types and may be `null`.
Their `IsValid` properties also verify that the entity still has the corresponding native component.

This differs from `AssetReference<T>`, which is a value type and uses only `.IsValid`.

## Button Events

Bind and unbind symmetrically:

```csharp
private UiButton? _subscribedButton;

protected override void OnEnable()
{
    BindButton();
}

protected override void OnDisable()
{
    UnbindButton();
}

protected override void OnBeforeReload()
{
    UnbindButton();
}

protected override void OnAfterReload()
{
    BindButton();
}

private void BindButton()
{
    if (ReferenceEquals(_subscribedButton, _resumeButton))
        return;

    UnbindButton();
    _subscribedButton = _resumeButton;
    if (_subscribedButton is not null)
        _subscribedButton.Clicked += HandleResumeClicked;
}

private void UnbindButton()
{
    if (_subscribedButton is not null)
        _subscribedButton.Clicked -= HandleResumeClicked;
    _subscribedButton = null;
}
```

Native clicks are dispatched before managed script `Update`, so a button event and input polling in that frame observe
a consistent order.

For a wrapper obtained at runtime:

```csharp
UiButton? button = RuntimeUi.GetButton(buttonEntity);
```

or:

```csharp
UiButton? button = UiButton.FromEntity(buttonEntity);
```

Both return `null` when the entity lacks a UI Button component.

## Sliders, Toggles, Input Fields, And Scroll Views

Typed controls read and write the live scene component used by both editor Play Mode and packaged players:

```csharp
protected override void Update()
{
    if (_sensitivity is not null && _sensitivity.ChangedThisFrame)
        PlayerPreferences.SetFloat("input.lookSensitivity", _sensitivity.Value);

    if (_profileName is not null && _profileName.SubmittedThisFrame)
        PlayerPreferences.SetString("profile.name", _profileName.Text);
}
```

`UiSlider` exposes `Minimum`, `Maximum`, `Value`, `Interactable`, and `ChangedThisFrame`. `UiToggle` exposes `IsOn`,
`Interactable`, and `ChangedThisFrame`. `UiInputField` exposes UTF-8 `Text`, focus, change/submit/cancel events, and
interactability. `UiScrollView` exposes `Offset`, `ContentSize`, interactability, and `ScrolledThisFrame`.

Call `Focus()` to move runtime focus to a control. Tab/Shift+Tab, arrow keys, Enter/Escape, mouse wheels, and gamepad
D-pad/accept/cancel are routed through the retained UI tree. Input-field length limits are measured in UTF-8 bytes so
the native and managed contracts remain identical.

The Inspector exposes accessibility label, hint, semantic role, and explicit navigation order through the
`UiAccessibilityComponent`. Navigation order is stable; equal or automatic values retain scene order.

## Polling UI

Polling remains available:

```csharp
protected override void Update()
{
    if (RuntimeUi.WasClicked(_resumeButtonEntity))
        ResumeGame();
}
```

`WasClicked` consumes the pending native click. Prefer either an event binding or polling for a particular button;
mixing both creates competing consumers.

## Updating Text

```csharp
bool updated = RuntimeUi.SetText(_ammoLabel, $"{rounds} / {reserve}");
if (!updated)
    Debug.Warn("Ammo label is unavailable.");
```

A native-bound `RuntimeCanvas` provides equivalent helpers:

```csharp
RuntimeCanvas canvas = new(canvasEntity);
canvas.SetText(_ammoLabel, $"{rounds} / {reserve}");
```

The call returns `false` when the entity is invalid or does not expose compatible UI text state.

## Inspector Events

Declare persistent events:

```csharp
[SerializeField, StableFieldId("f912e5a1-dd50-45e7-8998-ae78135a1256")]
private KeireEvent _opened = new();

[SerializeField, StableFieldId("4199ba71-0afc-4340-a863-2a8f3e0bff56")]
private KeireEvent<float> _progressChanged = new();
```

The Inspector stores each persistent listener's:

- enabled state;
- target entity;
- managed component stable ID;
- callback method.

Persistent callbacks may be public or non-public, must return `void`, and must accept compatible arguments in the same
order:

```csharp
private void HandleOpened()
{
    Debug.Log("Menu opened.");
}

private void HandleProgress(float value)
{
    RuntimeUi.SetText(_progressLabel, $"{value:P0}");
}
```

Invoke events:

```csharp
_opened.Invoke();
_progressChanged.Invoke(0.75f);
```

Generic `KeireEvent` variants support one through four arguments. Persistent listeners run before runtime listeners.
Renaming a target component type is safe when its `StableComponentId` remains unchanged. Renaming a callback method
requires updating its persistent listener selection.

## Runtime Event Listeners

```csharp
protected override void OnEnable()
{
    _opened.AddListener(HandleOpened);
}

protected override void OnDisable()
{
    _opened.RemoveListener(HandleOpened);
}

protected override void OnBeforeReload()
{
    _opened.RemoveListener(HandleOpened);
}

protected override void OnAfterReload()
{
    _opened.AddListener(HandleOpened);
}
```

`RemoveAllListeners` removes runtime listeners only; persistent Inspector listeners remain serialized.

Avoid anonymous lambdas when you need to unsubscribe unless the delegate is stored:

```csharp
private Action? _listener;

protected override void OnEnable()
{
    _listener = () => Debug.Log("Opened");
    _opened.AddListener(_listener);
}

protected override void OnDisable()
{
    if (_listener is not null)
        _opened.RemoveListener(_listener);
    _listener = null;
}
```

Named methods are simpler for most lifecycle-bound subscriptions.

## Cursor Ownership

Menus should request a visible cursor rather than directly fighting gameplay capture:

```csharp
private IDisposable? _cursorVisibility;

private void ApplyVisibility(bool visible)
{
    if (visible)
        _cursorVisibility ??= Cursor.RequestVisible();
    else
        ReleaseCursorVisibility();
}

private void ReleaseCursorVisibility()
{
    _cursorVisibility?.Dispose();
    _cursorVisibility = null;
}
```

Visible requests take priority over active capture requests. When the final menu request is disposed, gameplay capture
resumes automatically if its owner still holds a `RequestCapture` token.

Release cursor requests in `OnDisable` and `OnBeforeReload`.

## Complete Menu Controller

```csharp
using Keire;

namespace MyGame;

[StableComponentId("39559aac-e6d9-4387-9445-3b7718e83f71")]
public sealed class PauseMenu : Behaviour
{
    [SerializeField, StableFieldId("92fd3515-cdbb-48bc-946f-7554e53b5575")]
    private Entity _panel;

    [SerializeField, StableFieldId("f1152698-2941-4a72-84da-a3e3ecf47d28")]
    private UiButton? _resumeButton;

    [SerializeField, StableFieldId("c90f0583-8c4f-418d-a159-4b014b1efeed")]
    private AssetReference<AudioClip> _toggleSound;

    [SerializeField, StableFieldId("98f8428b-f8e9-4621-b280-f2a3cd5fe153")]
    private KeireEvent _opened = new();

    [SerializeField, StableFieldId("241f55cb-04c8-4246-9289-6c54f61663ca")]
    private KeireEvent _closed = new();

    private UiButton? _subscribedButton;
    private IDisposable? _cursorVisibility;
    private bool _eventsBound;

    protected override void OnEnable()
    {
        BindRuntimeRelationships();
        ApplyVisibility(_panel.Id.IsValid && _panel.Active);
    }

    protected override void Update()
    {
        if (Input.Pressed("TogglePause"))
            Toggle();
    }

    protected override void OnDisable()
    {
        UnbindRuntimeRelationships();
        ApplyVisibility(false);
    }

    protected override void OnBeforeReload()
    {
        UnbindRuntimeRelationships();
        ApplyVisibility(false);
    }

    protected override void OnAfterReload()
    {
        BindRuntimeRelationships();
        ApplyVisibility(_panel.Id.IsValid && _panel.Active);
    }

    private void Toggle()
    {
        if (!_panel.IsValid)
            return;

        bool open = !_panel.Active;
        _panel.Active = open;

        if (open)
            _opened.Invoke();
        else
            _closed.Invoke();

        if (_toggleSound.IsValid)
        {
            Audio.Play(Entity, _toggleSound, new AudioPlaybackOptions
            {
                Bus = "UI",
                Spatial = false
            });
        }
    }

    private void BindRuntimeRelationships()
    {
        if (!ReferenceEquals(_subscribedButton, _resumeButton))
        {
            UnbindButton();
            _subscribedButton = _resumeButton;
            if (_subscribedButton is not null)
                _subscribedButton.Clicked += Toggle;
        }

        if (_eventsBound)
            return;
        _opened.AddListener(HandleOpened);
        _closed.AddListener(HandleClosed);
        _eventsBound = true;
    }

    private void UnbindRuntimeRelationships()
    {
        UnbindButton();
        if (!_eventsBound)
            return;
        _opened.RemoveListener(HandleOpened);
        _closed.RemoveListener(HandleClosed);
        _eventsBound = false;
    }

    private void UnbindButton()
    {
        if (_subscribedButton is not null)
            _subscribedButton.Clicked -= Toggle;
        _subscribedButton = null;
    }

    private void HandleOpened() => ApplyVisibility(true);
    private void HandleClosed() => ApplyVisibility(false);

    private void ApplyVisibility(bool visible)
    {
        if (visible)
            _cursorVisibility ??= Cursor.RequestVisible();
        else
        {
            _cursorVisibility?.Dispose();
            _cursorVisibility = null;
        }
    }
}
```

The shared `Toggle` method gives keyboard and button activation the same audio and event behavior.

## In-Memory Runtime UI Layout

`RuntimeCanvas` also contains an in-memory UI tree:

```csharp
RuntimeCanvas canvas = new();
canvas.ReferenceResolution = new Vector2(1920.0f, 1080.0f);
canvas.MatchWidthOrHeight = 0.5f;

UiPanel panel = new()
{
    Name = "PausePanel",
    AnchorMinimum = new Vector2(0.5f, 0.5f),
    AnchorMaximum = new Vector2(0.5f, 0.5f),
    Size = new Vector2(480.0f, 320.0f)
};

canvas.Root.Add(panel);
canvas.Layout(new Vector2(2560.0f, 1440.0f));
UiRect result = panel.LayoutRect;
```

`UiPanel`, `UiText`, `UiImage`, and unbound `UiButton` values support hierarchy and layout calculation. Cycles are
rejected. This model is useful for runtime layout data; scene-authored UI rendering and native interaction remain
entity/component operations through `RuntimeUi` and native-bound control handles.
