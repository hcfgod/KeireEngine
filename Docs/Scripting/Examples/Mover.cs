using Keire;

namespace MyGame;

[StableComponentId("b94ebaa8-4a30-46b8-938a-0761f4589a22")]
public sealed class Mover : Behaviour
{
    [SerializeField, StableFieldId("fa1cbb4f-a81e-4c0a-af58-315401454e04")]
    [Range(0.0, 20.0)]
    private float _speed = 5.0f;

    protected override void Update()
    {
        Keyboard? keyboard = Input.Keyboard.Current;
        Vector2 input = new(keyboard?.dKey.IsPressed == true ? 1.0f : keyboard?.aKey.IsPressed == true ? -1.0f : 0.0f,
                            keyboard?.wKey.IsPressed == true ? 1.0f : keyboard?.sKey.IsPressed == true ? -1.0f : 0.0f);
        Vector3 direction = new(input.X, 0.0f, input.Y);
        if (direction.LengthSquared > 1.0f)
            direction = direction.Normalized;
        Transform.Position += direction * (_speed * Time.DeltaTime);
    }
}
