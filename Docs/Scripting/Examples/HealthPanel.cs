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
