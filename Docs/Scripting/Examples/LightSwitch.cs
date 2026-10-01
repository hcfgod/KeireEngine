using Keire;

namespace MyGame;

[StableComponentId("31f48d51-3502-4452-abfe-6e9af83fd83a")]
public sealed class LightSwitch : Behaviour
{
    [SerializeField, StableFieldId("0bce4b90-da78-4c6d-969c-03807d71504c")]
    private Entity? _light = null;

    protected override void Update()
    {
        if (Input.Keyboard.Current?.lKey.WasPressedThisFrame == true && _light is { IsValid: true })
            _light.Active = !_light.Active;
    }
}
