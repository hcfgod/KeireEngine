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
