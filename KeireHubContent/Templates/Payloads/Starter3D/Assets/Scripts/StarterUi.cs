using Keire;
using Keire.UI;

namespace Game;

[UxmlElement("StarterStatus")]
public sealed class StarterStatus : VisualElement
{
    [UxmlAttribute("value")]
    public float Value { get; set; }
}

[StableComponentId("b1b2d001-1000-4000-8000-000000000001")]
public sealed class StarterUiController : Behaviour
{
    private UIDocument? _document;
    private RuntimeVisualElement? _continue;

    protected override void OnEnable()
    {
        UxmlElementRegistry.Register<StarterStatus>();
        Resolve();
    }

    protected override void OnDisable() => _continue = null;
    protected override void OnBeforeReload() => _continue = null;
    protected override void OnAfterReload() => OnEnable();

    protected override void Update()
    {
        if (_continue is not { IsAlive: true })
            Resolve();
        if (_continue?.ClickedThisFrame != true)
            return;

        _continue.Text = "Ready";
        _continue.Interactable = false;
        Debug.Log("Starter UI Document button clicked.");
    }

    private void Resolve()
    {
        _document ??= Entity.GetComponent<UIDocument>();
        _continue = _document?.Q("continue");
    }
}

[StableComponentId("b1b2d001-1000-4000-8000-000000000002")]
public sealed class StarterWorldUiController : Behaviour
{
    private UIDocument? _document;
    private RuntimeVisualElement? _action;

    protected override void OnEnable() => Resolve();
    protected override void OnDisable() => _action = null;
    protected override void OnBeforeReload() => _action = null;
    protected override void OnAfterReload() => Resolve();

    protected override void Update()
    {
        if (_action is not { IsAlive: true })
            Resolve();
        if (_action?.ClickedThisFrame == true)
            _action.Text = _action.Text == "Activate" ? "Active" : "Activate";
    }

    private void Resolve()
    {
        _document ??= Entity.GetComponent<UIDocument>();
        _action = _document?.Q("world-action");
    }
}
