using Keire;

namespace KeireSandbox;

[StableComponentId("3f100613-b2cc-4dc0-a7b2-216428d75ae0")]
public sealed class UIController : Behaviour
{
    [SerializeField] private Entity uiPanel;

    protected override void Update()
    {
        if(Input.Pressed("ToggleUI"))
        {

        }
    }
}
