using Keire;

namespace Keire.FirstPerson;

public readonly record struct FirstPersonLookSettings(float MouseSensitivity, float StickDegreesPerSecond,
    bool InvertY, float MinimumPitch, float MaximumPitch)
{
    public static FirstPersonLookSettings Default => new(0.12f, 150, false, -89, 89);
    public void Validate()
    {
        if (!float.IsFinite(MouseSensitivity) || MouseSensitivity < 0 || !float.IsFinite(StickDegreesPerSecond) ||
            StickDegreesPerSecond < 0 || !float.IsFinite(MinimumPitch) || !float.IsFinite(MaximumPitch) ||
            MinimumPitch < -89 || MaximumPitch > 89 || MinimumPitch > MaximumPitch)
            throw new ArgumentOutOfRangeException(nameof(FirstPersonLookSettings), "FPS look settings require finite sensitivity and ordered pitch limits within -89 to 89 degrees.");
    }
}

public readonly record struct FirstPersonLookStep(float YawDelta, float Pitch);

public static class FirstPersonLook
{
    public static FirstPersonLookStep Step(float pitch, Vector2 mouseDelta, Vector2 stick, float dt,
        FirstPersonLookSettings settings)
    {
        settings.Validate();
        if (!float.IsFinite(pitch) || !float.IsFinite(dt) || dt < 0 || !float.IsFinite(mouseDelta.X) ||
            !float.IsFinite(mouseDelta.Y) || !float.IsFinite(stick.X) || !float.IsFinite(stick.Y))
            throw new ArgumentOutOfRangeException(nameof(dt), "FPS look input and time must be finite.");
        if (stick.LengthSquared > 1)
            stick = stick.Normalized;
        Vector2 mouse = mouseDelta * settings.MouseSensitivity;
        Vector2 gamepad = stick * (settings.StickDegreesPerSecond * dt);
        // Mouse Y points down; stick Y points up. Positive engine pitch looks down.
        return new(mouse.X + gamepad.X, Math.Clamp(pitch + (mouse.Y - gamepad.Y) * (settings.InvertY ? -1 : 1),
            settings.MinimumPitch, settings.MaximumPitch));
    }
}
