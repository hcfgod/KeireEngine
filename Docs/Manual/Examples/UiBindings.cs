using Keire;
using Keire.UI;

namespace KeireManualExamples;

[StableComponentId("fc48aece-1ea9-4d97-9357-983ca74b2f14")]
public sealed class HealthHudBinding : Behaviour
{
    private UIDocument? _document;

    protected override void OnEnable()
    {
        _document = GetComponent<UIDocument>();
        _document?.SetBindingValue("Player.Health", 75.0f);
    }

    public void SetHealth(float health)
    {
        _document?.SetBindingValue("Player.Health", health);
    }

    protected override void OnDisable()
    {
        _document?.ClearBindingSource();
        _document = null;
    }
}
