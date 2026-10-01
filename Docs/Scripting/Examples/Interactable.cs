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
