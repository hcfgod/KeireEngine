using System;
using Keire;
using Keire.UI;

namespace SpaceFlight;

[StableComponentId("eb170000-2026-4000-8000-000000000001")]
public sealed class FlightDirector : Behaviour
{
    private const double SurfaceAltitude = 12.0;
    private const double CruiseAltitude = 150000.0;
    private const double AscentSeconds = 60.0;
    private const double HoldSeconds = 20.0;
    private const double DescentSeconds = 60.0;
    private const double LandingSeconds = 10.0;
    private double _elapsed;
    private bool _running;
    private bool _follow = true;
    private float _cameraBlend = 1.0f;
    private UIDocument? _document;
    private Camera? _camera;

    protected override void Start()
    {
        _document = Entity.GetComponent<UIDocument>();
        _camera = Entity.GetComponent<Camera>();
    }

    protected override void Update()
    {
        var keyboard = Keyboard.Current;
        if (_document?.Q("launch")?.ClickedThisFrame == true || keyboard?.enterKey.WasPressedThisFrame == true)
        {
            _elapsed = 0.0;
            _running = true;
        }
        if (_document?.Q("view")?.ClickedThisFrame == true || keyboard?.vKey.WasPressedThisFrame == true)
        {
            _follow = !_follow;
        }
        if (_document?.Q("pause")?.ClickedThisFrame == true || keyboard?.pKey.WasPressedThisFrame == true)
        {
            _running = !_running;
        }
        if (_document?.Q("return")?.ClickedThisFrame == true || keyboard?.rKey.WasPressedThisFrame == true)
        {
            // Mirror the ascent along the same smooth curve, preserving current altitude.
            if (_elapsed < AscentSeconds)
            {
                _elapsed = AscentSeconds + HoldSeconds + DescentSeconds * (1.0 - _elapsed / AscentSeconds);
            }
            else if (_elapsed < AscentSeconds + HoldSeconds)
            {
                _elapsed = AscentSeconds + HoldSeconds;
            }
            _running = true;
        }

        double delta = Math.Clamp(Time.DeltaTime, 0.0f, 0.1f);
        if (_running)
        {
            _elapsed = Math.Min(_elapsed + delta, 150.0);
            if (_elapsed >= 150.0)
            {
                _running = false;
            }
        }
        double altitude = SurfaceAltitude;
        string phase = "READY / LANDED";
        if (_elapsed > 0.0 && _elapsed < AscentSeconds)
        {
            altitude += (CruiseAltitude - SurfaceAltitude) * Smooth(_elapsed / AscentSeconds);
            phase = "ASCENT";
        }
        else if (_elapsed >= AscentSeconds && _elapsed < AscentSeconds + HoldSeconds)
        {
            altitude = CruiseAltitude;
            phase = "SPACE / COAST";
        }
        else if (_elapsed >= AscentSeconds + HoldSeconds && _elapsed < 140.0)
        {
            altitude += (CruiseAltitude - SurfaceAltitude) * (1.0 - Smooth((_elapsed - 80.0) / DescentSeconds));
            phase = "RETURN / DESCENT";
        }
        else if (_elapsed >= 140.0)
        {
            phase = _elapsed < 140.0 + LandingSeconds ? "LANDED / COMPLETE" : "COMPLETE";
        }
        if (Entity.Parent is { } ship)
        {
            ship.Transform.PositionPrecise = new Vector3d(0.0, altitude, 0.0);
        }
        float blendTarget = _follow ? 1.0f : 0.0f;
        _cameraBlend += (blendTarget - _cameraBlend) * (1.0f - MathF.Exp(-5.0f * (float)delta));
        Transform.LocalPosition = new Vector3(0.0f, 1.5f + 5.5f * _cameraBlend, 2.5f - 24.5f * _cameraBlend);
        Transform.LocalRotation = Quaternion.Euler(18.0f * _cameraBlend, 0.0f);
        if (_camera is not null)
        {
            // Increase depth precision during ascent while keeping the follow ship beyond the near plane.
            _camera.NearPlane = 0.2f + 9.8f * (float)Smooth((altitude - SurfaceAltitude) / 5000.0);
        }
        var status = _document?.Q("status");
        if (status is not null)
        {
            status.Text = $"{phase}  |  {altitude / 1000.0:F1} km  |  {_elapsed:F0} / 150 s";
        }
        var view = _document?.Q("view");
        if (view is not null)
        {
            view.Text = _follow ? "Cockpit view [V]" : "Follow view [V]";
        }
    }

    private static double Smooth(double value)
    {
        value = Math.Clamp(value, 0.0, 1.0);
        return value * value * (3.0 - 2.0 * value);
    }
}
