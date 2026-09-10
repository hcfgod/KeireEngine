using Keire;

namespace KeireOutpost;

/// <summary>Replayable camera route with an independent, bounded exploration mode.</summary>
[StableComponentId("96312b07-df7f-46e4-9c38-a5e3b5f1e247")]
public sealed class OutpostTour : Behaviour
{
    private static readonly Vector3[] Positions =
    [
        new(25, 15, -32), new(7, 5, -22), new(-20, 6, -19),
        new(-22, 11, 18), new(2, 4, -1), new(-3, 4, 9), new(17, 5, -17), new(25, 15, -32)
    ];
    private static readonly Vector3[] Targets =
    [
        new(0, 2, 8), new(0, 2, 12), new(-15, 1.5f, -8),
        new(-10, 6, 23), new(10, 1.4f, 4), new(-4, 1, 18), new(11, 1, -6), new(0, 2, 8)
    ];
    [HotReloadState] private float _elapsed;
    [HotReloadState] private bool _paused;
    [HotReloadState] private bool _exploring;
    private float _yaw = -34;
    private float _pitch = 17;
    private bool _capture;
    private Entity? _explorer;
    private Entity? _gate;
    private Animator? _animator;
    private float _gateHeight = 1.8f;
    private bool _gateOpen;
    private bool _animationPreview;
    private float _animationTime;
    private DirectionalLight? _sun;
    private ShadowQuality _defaultShadows;
    private bool _vanguard;
    private string _motion = "";
    private float _jumpTime;
    private static readonly string[] PreviewMotions =
    [
        "Breathing Idle", "Walking", "Standard Run", "Walking Backward",
        "Left Strafe Walking", "Right Strafe Walking", "Left Strafe Run", "Right Strafe Run",
        "Crouch Idle", "Crouch Walk Forward", "Crouch Walk Back", "Crouch Walk Left",
        "Crouch Walk Right", "Crouch To Standing Idle", "Jump", "Falling Idle"
    ];

    protected override void Awake()
    {
        if (!Application.IsEditor) Screen.TrySetResolution(1920, 1080, FullscreenMode.Windowed);
    }

    protected override void Start()
    {
        _explorer = SceneManager.FindByName("Outpost explorer");
        _gate = SceneManager.FindByName("Signal gate");
        Entity? model = SceneManager.FindByName("Vanguard model");
        _vanguard = model is not null;
        _animator = (model ?? SceneManager.FindByName("Explorer model"))?.GetComponent<Animator>();
        _sun = SceneManager.FindByName("Dusk sunlight")?.GetComponent<DirectionalLight>();
        _defaultShadows = _sun?.Shadows ?? ShadowQuality.Soft;
        if (_explorer is null || _gate is null || _animator is null)
            Log.Warning("Outpost interactive scene references are unavailable.");
    }

    protected override void Update()
    {
        Keyboard? keyboard = Keyboard.Current;
        if (keyboard is not null)
        {
            if (!Application.IsEditor && keyboard[Key.Escape].WasPressedThisFrame) Application.Quit();
            if (!_exploring && keyboard[Key.Space].WasPressedThisFrame) _paused = !_paused;
            if (_exploring && keyboard[Key.Space].WasPressedThisFrame && _jumpTime <= 0) _jumpTime = 3;
            if (keyboard[Key.R].WasPressedThisFrame)
            {
                _elapsed = 0;
                _paused = false;
                _exploring = false;
                _capture = false;
                _animationPreview = false;
                _gateOpen = false;
                _jumpTime = 0;
                if (_sun is not null) _sun.Shadows = _defaultShadows;
                if (_explorer is not null) _explorer.Transform.LocalPosition = new Vector3(0, 0, -10);
            }
            if (keyboard[Key.Tab].WasPressedThisFrame)
            {
                _exploring = !_exploring;
                _paused = false;
                _capture = false;
                _animationPreview = false;
                _yaw = 0;
            }
            if (keyboard[Key.C].WasPressedThisFrame)
            {
                _capture = !_capture;
                _paused = false;
                _animationPreview = false;
            }
            if (_sun is not null && keyboard[Key.F8].WasPressedThisFrame)
                _sun.Shadows = _sun.Shadows == ShadowQuality.Disabled ? _defaultShadows : ShadowQuality.Disabled;
            if (keyboard[Key.F7].WasPressedThisFrame)
            {
                _animationPreview = !_animationPreview;
                _animationTime = 0;
                _paused = false;
                _capture = false;
            }
            ReadOnlySpan<Key> stationKeys = [Key.F1, Key.F2, Key.F3, Key.F4, Key.F5, Key.F6];
            ReadOnlySpan<int> stations = [0, 2, 3, 4, 5, 6];
            for (int i = 0; i < stationKeys.Length; ++i)
            {
                if (!keyboard[stationKeys[i]].WasPressedThisFrame) continue;
                _animationPreview = false;
                _exploring = false;
                _capture = false;
                _paused = true;
                _elapsed = stations[i] * 12;
                AimCamera(Positions[stations[i]], Targets[stations[i]]);
            }
            if (_exploring && !_capture && keyboard[Key.E].WasPressedThisFrame &&
                _explorer is not null && _explorer.Transform.LocalPosition.Z > 12) _gateOpen = !_gateOpen;
        }
        float delta = Math.Clamp(Time.DeltaTime, 0, 0.05f);
        _gateHeight += ((_gateOpen ? 5 : 1.8f) - _gateHeight) * Math.Clamp(delta * 4, 0, 1);
        if (_gate is not null) _gate.Transform.LocalPosition = new Vector3(0, _gateHeight, 19);
        if (_paused) return;
        if (_animationPreview && _explorer is not null)
        {
            _animationTime += delta;
            _explorer.Transform.LocalPosition = new Vector3(0, 0, -8);
            _explorer.Transform.LocalRotation = Quaternion.Euler(0, 180);
            SetMotion(_vanguard ? PreviewMotions[(int)(_animationTime / 4) % PreviewMotions.Length] : "Idle");
            AimCamera(new Vector3(4, 2.4f, -13), _explorer.Transform.LocalPosition + Vector3.Up);
            return;
        }
        if ((_exploring || _capture) && keyboard is not null)
        {
            float Axis(Key positive, Key negative) =>
                (keyboard[positive].IsPressed ? 1 : 0) - (keyboard[negative].IsPressed ? 1 : 0);
            _yaw += Axis(Key.RightArrow, Key.LeftArrow) * delta * 60;
            _pitch = Math.Clamp(_pitch + Axis(Key.DownArrow, Key.UpArrow) * delta * 40, -65, 65);
            float radians = _yaw * MathF.PI / 180;
            Vector3 forward = new(MathF.Sin(radians), 0, MathF.Cos(radians));
            Vector3 right = new(MathF.Cos(radians), 0, -MathF.Sin(radians));
            if (_exploring && !_capture && _explorer is not null)
            {
                float longitudinal = Axis(Key.W, Key.S);
                float lateral = Axis(Key.D, Key.A);
                bool crouching = keyboard[Key.LeftCtrl].IsPressed;
                bool running = !crouching && keyboard[Key.LeftShift].IsPressed;
                float speed = crouching ? 1.1f : running ? 4.5f : 1.8f;
                Vector3 movement = forward * longitudinal + right * lateral;
                if (movement.LengthSquared > 1) movement = movement.Normalized;
                Vector3 next = _explorer.Transform.LocalPosition + movement * delta * speed;
                // The tour's central lane is deliberately bounded and clear of buildings.
                float limit = _gateOpen ? 26 : 17;
                _explorer.Transform.LocalPosition = new Vector3(Math.Clamp(next.X, -2.6f, 2.6f), 0,
                    Math.Clamp(next.Z, -16, limit));
                _explorer.Transform.LocalRotation = Quaternion.Euler(0, _yaw);
                _jumpTime = Math.Max(0, _jumpTime - delta);
                if (_motion == "Jump" && _animator is not null && _animator.NormalizedTime >= .98f) _jumpTime = 0;
                string motion = crouching ? "Crouch Idle" : "Breathing Idle";
                if (MathF.Abs(lateral) > MathF.Abs(longitudinal))
                    motion = crouching ? (lateral > 0 ? "Crouch Walk Right" : "Crouch Walk Left") :
                        (lateral > 0 ? "Right Strafe " : "Left Strafe ") + (running ? "Run" : "Walking");
                else if (longitudinal != 0)
                    motion = crouching ? (longitudinal > 0 ? "Crouch Walk Forward" : "Crouch Walk Back") :
                        longitudinal < 0 ? "Walking Backward" : running ? "Standard Run" : "Walking";
                SetMotion(_jumpTime > 0 ? "Jump" : motion);
                Vector3 focus = _explorer.Transform.LocalPosition + Vector3.Up * 1.4f;
                Entity.Transform.LocalPosition = focus - forward * 5 + Vector3.Up * 2.2f;
                Entity.Transform.LocalRotation = Quaternion.Euler(24, _yaw);
                return;
            }
            Vector3 position = Entity.Transform.LocalPosition +
                (forward * Axis(Key.W, Key.S) + right * Axis(Key.D, Key.A) +
                 Vector3.Up * Axis(Key.E, Key.Q)) * delta * 8;
            Entity.Transform.LocalPosition = new Vector3(
                Math.Clamp(position.X, -40, 40), Math.Clamp(position.Y, 1.5f, 30), Math.Clamp(position.Z, -40, 45));
        }
        else
        {
            SetMotion("Breathing Idle");
            _elapsed = (_elapsed + delta) % 84;
            float route = _elapsed / 12;
            int segment = Math.Min((int)route, Positions.Length - 2);
            float blend = route - segment;
            blend = blend * blend * (3 - 2 * blend);
            // Each shot has a short dolly move. Cuts keep the camera out of buildings
            // and exhibits instead of flying through them between stations.
            Vector3 target = Targets[segment];
            Vector3 direction = (target - Positions[segment]).Normalized;
            Vector3 position = Positions[segment] + direction * (blend * 1.5f);
            AimCamera(position, target);
        }
        Entity.Transform.LocalRotation = Quaternion.Euler(_pitch, _yaw);
    }

    private void AimCamera(Vector3 position, Vector3 target)
    {
        Vector3 direction = target - position;
        _yaw = MathF.Atan2(direction.X, direction.Z) * 180 / MathF.PI;
        _pitch = -MathF.Atan2(direction.Y,
            MathF.Sqrt(direction.X * direction.X + direction.Z * direction.Z)) * 180 / MathF.PI;
        Entity.Transform.LocalPosition = position;
        Entity.Transform.LocalRotation = Quaternion.Euler(_pitch, _yaw);
    }

    private void SetMotion(string motion)
    {
        if (!_vanguard || _animator is null || _motion == motion) return;
        _animator.CrossFade(motion, .18f);
        _motion = motion;
    }
}
