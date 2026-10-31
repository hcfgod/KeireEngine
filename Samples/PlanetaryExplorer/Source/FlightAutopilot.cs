using System;
using Keire;

namespace PlanetaryExplorer;

internal sealed class FlightAutopilot
{
    private Vector3d _start;
    private Vector3d _up;
    private double _elapsed;

    public void Begin(Vector3d position)
    {
        _start = position;
        var planet = PlanetaryFlightGeometry.Nearest(position.X, position.Y, position.Z);
        _up = new Vector3d(planet.UpX, planet.UpY, planet.UpZ);
        _elapsed = 0.0;
    }

    public Vector3d Step(double delta, out bool complete)
    {
        _elapsed = Math.Min(150.0, _elapsed + delta);
        double fraction = _elapsed < 60.0 ? Smooth(_elapsed / 60.0)
            : _elapsed < 80.0 ? 1.0
            : 1.0 - Smooth((_elapsed - 80.0) / 60.0);
        complete = _elapsed >= 150.0;
        double distance = 150000.0 * fraction;
        return new Vector3d(_start.X + _up.X * distance,
            _start.Y + _up.Y * distance, _start.Z + _up.Z * distance);
    }

    private static double Smooth(double value)
    {
        value = Math.Clamp(value, 0.0, 1.0);
        return value * value * (3.0 - 2.0 * value);
    }
}
