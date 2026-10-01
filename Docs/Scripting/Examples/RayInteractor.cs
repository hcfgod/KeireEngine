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
