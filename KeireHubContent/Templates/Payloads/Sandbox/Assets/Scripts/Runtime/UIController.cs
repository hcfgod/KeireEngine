using Keire;

namespace KeireSandbox;

[StableComponentId("3f100613-b2cc-4dc0-a7b2-216428d75ae0")]
public sealed class UIController : Behaviour
{
    private static int s_VisibleControllerCount;

    public static event Action<bool>? VisibilityChanged;
    public static bool IsAnyUiVisible => s_VisibleControllerCount > 0;

    [SerializeField] private Entity? uiPanel;
    [SerializeField] private KeireEvent uiOpened = new();
    [SerializeField] private KeireEvent uiClosed = new();

    [SerializeField] private AudioClip? uiAudioClip;

    private IDisposable? _cursorVisibility;
    private bool _eventsBound;
    private bool _reportedVisible;

    protected override void OnEnable()
    {
        BindUiEvents();
        ApplyUiVisibility(uiPanel is { IsValid: true, Active: true });
    }

    protected override void OnDisable()
    {
        UnbindUiEvents();
        ApplyUiVisibility(false);
    }

    protected override void OnBeforeReload()
    {
        UnbindUiEvents();
        ApplyUiVisibility(false);
    }

    protected override void OnAfterReload()
    {
        BindUiEvents();
        ApplyUiVisibility(uiPanel is { IsValid: true, Active: true });
    }

    protected override void Update()
    {
        if (Input.Pressed("ToggleUI"))
            ToggleUi();
    }

    private void ToggleUi()
    {
        if (uiPanel is not { IsValid: true })
            return;

        SetUiOpen(!uiPanel.Active);
    }

    private void SetUiOpen(bool open)
    {
        if (uiPanel is not { IsValid: true } || uiPanel.Active == open)
            return;
        uiPanel.Active = open;
        if (open)
            uiOpened.Invoke();
        else
            uiClosed.Invoke();
    }

    private void BindUiEvents()
    {
        if (_eventsBound)
            return;
        uiOpened.AddListener(HandleUiOpened);
        uiClosed.AddListener(HandleUiClosed);
        _eventsBound = true;
    }

    private void UnbindUiEvents()
    {
        if (!_eventsBound)
            return;
        uiOpened.RemoveListener(HandleUiOpened);
        uiClosed.RemoveListener(HandleUiClosed);
        _eventsBound = false;
    }

    private void HandleUiOpened() => ApplyUiVisibility(true);

    private void HandleUiClosed() => ApplyUiVisibility(false);

    private void ApplyUiVisibility(bool visible)
    {
        if (visible)
            _cursorVisibility ??= Cursor.RequestVisible();
        else
            ReleaseCursorVisibility();

        if (_reportedVisible == visible)
            return;

        bool wasAnyVisible = IsAnyUiVisible;
        _reportedVisible = visible;
        if (visible)
            ++s_VisibleControllerCount;
        else
            s_VisibleControllerCount = Math.Max(0, s_VisibleControllerCount - 1);

        bool isAnyVisible = IsAnyUiVisible;
        if (wasAnyVisible != isAnyVisible)
            VisibilityChanged?.Invoke(isAnyVisible);
    }

    private void ReleaseCursorVisibility()
    {
        _cursorVisibility?.Dispose();
        _cursorVisibility = null;
    }

}
