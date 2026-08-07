using Keire;

namespace KeireSandbox;

[StableComponentId("73616e64-626f-4078-8000-000000000051")]
public sealed class SandboxWelcome : Behaviour
{
    protected override void Awake()
    {
        Debug.Log("Welcome to your Kéire Sandbox project.");
    }
}
