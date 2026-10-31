using System;
using Keire;

namespace PlanetaryExplorer;

internal sealed class ShipCameraRig
{
    private float _blend = 1.0f;
    public bool Follow { get; private set; } = true;

    public void Toggle()
    {
        Follow = !Follow;
    }

    public void Update(Transform transform, Camera? camera, float delta, double altitude)
    {
        _blend += ((Follow ? 1.0f : 0.0f) - _blend) * (1.0f - MathF.Exp(-5.0f * delta));
        transform.LocalPosition = new Vector3(0.0f, 1.5f + 5.5f * _blend, 2.5f - 24.5f * _blend);
        transform.LocalRotation = Quaternion.Euler(18.0f * _blend, 0.0f);
        if (camera is not null)
        {
            // Preserve altitude-sensitive follow depth precision while blending toward the cockpit.
            float height = (float)Math.Clamp((altitude - 12.0) / 5000.0, 0.0, 1.0);
            float smoothHeight = height * height * (3.0f - 2.0f * height);
            camera.NearPlane = 0.2f + 9.8f * smoothHeight * _blend;
        }
    }
}
