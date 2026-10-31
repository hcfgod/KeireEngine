using System;
using Keire;
using Keire.UI;

namespace PlanetaryExplorer;

[StableComponentId("517719fc-10d3-47c3-a6a7-8c4639b4cb5e")]
public sealed class ShipFlightController : Behaviour
{
    private enum FlightMode
    {
        Manual,
        Autopilot
    }

    private readonly FlightAutopilot _autopilot = new();
    private readonly ShipCameraRig _rig = new();
    private FlightMode _mode;
    private bool _paused;

    private UIDocument? _document;
    private Camera? _camera;

    protected override void Start()
    {
        _document = Entity.GetComponent<UIDocument>();
        _camera = Entity.GetComponent<Camera>();
    }

    protected override void Update()
    {
        if (Entity.Parent is not { } ship)
        {
            return;
        }
        var keyboard = Keyboard.Current;
        double delta = Math.Clamp(Time.DeltaTime, 0.0f, 0.1f);
        if (keyboard?.vKey.WasPressedThisFrame == true || Clicked("view"))
        {
            _rig.Toggle();
        }
        if (keyboard?.pKey.WasPressedThisFrame == true || Clicked("pause"))
        {
            _paused = !_paused;
        }
        if (keyboard?.enterKey.WasPressedThisFrame == true || Clicked("launch"))
        {
            _autopilot.Begin(ship.Transform.PositionPrecise);
            _mode = FlightMode.Autopilot;
            _paused = false;
        }
        if (keyboard?.rKey.WasPressedThisFrame == true || Clicked("return"))
        {
            _mode = FlightMode.Manual;
        }
        if (!_paused)
        {
            if (_mode == FlightMode.Autopilot)
            {
                ship.Transform.PositionPrecise = _autopilot.Step(delta, out bool complete);
                if (complete)
                {
                    _mode = FlightMode.Manual;
                }
            }
            else if (keyboard is not null)
            {
                MoveManual(ship, keyboard, delta);
            }
        }
        Vector3d position = ship.Transform.PositionPrecise;
        var planet = PlanetaryFlightGeometry.Nearest(position.X, position.Y, position.Z);
        double altitude = planet.Altitude;
        _rig.Update(Transform, _camera, (float)delta, altitude);
        var status = _document?.Q("status");
        if (status is not null)
        {
            status.Text = $"{(_paused ? "PAUSED" : _mode.ToString())} | {planet.Name} altitude {altitude / 1000.0:F1} km | Kinematic demo";
        }
        var view = _document?.Q("view");
        if (view is not null)
        {
            view.Text = _rig.Follow ? "Cockpit [V]" : "Follow [V]";
        }
    }

    private void MoveManual(Entity ship, Keyboard keyboard, double delta)
    {
        float yaw = Axis(keyboard.rightArrowKey.IsPressed, keyboard.leftArrowKey.IsPressed);
        float pitch = Axis(keyboard.downArrowKey.IsPressed, keyboard.upArrowKey.IsPressed);
        ship.Transform.Rotation = (ship.Transform.Rotation *
            Quaternion.Euler(pitch * 45.0f * (float)delta, yaw * 45.0f * (float)delta)).Normalized;
        Vector3 forward = ship.Transform.Forward;
        Vector3 right = ship.Transform.Right;
        double throttle = Axis(keyboard.wKey.IsPressed, keyboard.sKey.IsPressed);
        double strafe = Axis(keyboard.dKey.IsPressed, keyboard.aKey.IsPressed);
        double lift = Axis(keyboard.spaceKey.IsPressed, keyboard.qKey.IsPressed);
        double normalization = Math.Max(1.0, Math.Sqrt(throttle * throttle + strafe * strafe + lift * lift));
        double step = (keyboard.leftShiftKey.IsPressed ? 10000.0 : 1000.0) * delta / normalization;
        Vector3d p = ship.Transform.PositionPrecise;
        var localPlanet = PlanetaryFlightGeometry.Nearest(p.X, p.Y, p.Z);
        Vector3d next = new(p.X + (forward.X * throttle + right.X * strafe + localPlanet.UpX * lift) * step,
            p.Y + (forward.Y * throttle + right.Y * strafe + localPlanet.UpY * lift) * step,
            p.Z + (forward.Z * throttle + right.Z * strafe + localPlanet.UpZ * lift) * step);
        // Analytic safety boundaries only: this is not a contact/landing or rigid-body simulation.
        var boundary = PlanetaryFlightGeometry.Nearest(next.X, next.Y, next.Z);
        if (boundary.Altitude < 12.0)
        {
            double radius = boundary.Radius + 12.0;
            next = new Vector3d(boundary.CenterX + boundary.UpX * radius,
                boundary.CenterY + boundary.UpY * radius, boundary.UpZ * radius);
        }
        ship.Transform.PositionPrecise = next;
    }

    private bool Clicked(string name)
    {
        return _document?.Q(name)?.ClickedThisFrame == true;
    }

    private static float Axis(bool positive, bool negative)
    {
        return (positive ? 1.0f : 0.0f) - (negative ? 1.0f : 0.0f);
    }
}
