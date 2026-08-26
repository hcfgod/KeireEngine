namespace Keire;

public abstract class InputControl
{
    protected InputControl(uint device, string path) => (DeviceId, Path) = (device, path);

    public uint DeviceId { get; }
    public string Path { get; }

    private protected NativeInputControlSnapshot Snapshot
    {
        get
        {
            if (!NativeInput.TryControlSnapshot(DeviceId, Path, out NativeInputControlSnapshot snapshot))
                throw new InvalidOperationException($"Input control '{Path}' is unavailable.");
            return snapshot;
        }
    }
}

public sealed class ButtonControl : InputControl
{
    internal ButtonControl(uint device, string path) : base(device, path) { }

    public bool IsPressed => Snapshot.X >= 0.5f;
    public bool WasPressedThisFrame => Snapshot.PressedValue != 0;
    public bool WasReleasedThisFrame => Snapshot.ReleasedValue != 0;
    public bool WasPressed => WasPressedThisFrame;
    public bool WasReleased => WasReleasedThisFrame;
    public float ReadValue() => Snapshot.X;
}

public sealed class AxisControl : InputControl
{
    internal AxisControl(uint device, string path) : base(device, path) { }
    public float ReadValue() => Snapshot.X;
}

public sealed class Vector2Control : InputControl
{
    internal Vector2Control(uint device, string path) : base(device, path) { }
    public Vector2 ReadValue()
    {
        NativeInputControlSnapshot snapshot = Snapshot;
        return new Vector2(snapshot.X, snapshot.Y);
    }
}

public enum Key
{
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
    Space, Enter, Escape, Tab, Backspace, UpArrow, DownArrow, LeftArrow, RightArrow,
    LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt, LeftMeta, RightMeta,
    Insert, Delete, Home, End, PageUp, PageDown, CapsLock, PrintScreen, ScrollLock, Pause,
    Minus, Equals, LeftBracket, RightBracket, Backslash, Semicolon, Quote, Backquote, Comma, Period, Slash,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12
}

public sealed class Keyboard
{
    private readonly Dictionary<Key, ButtonControl> _keys = new();

    private Keyboard(uint device) => DeviceId = device;

    public uint DeviceId { get; }
    public static Keyboard? Current => CreateCurrent();
    public ButtonControl this[Key key] => Get(key);

    public ButtonControl aKey => Get(Key.A);
    public ButtonControl bKey => Get(Key.B);
    public ButtonControl cKey => Get(Key.C);
    public ButtonControl dKey => Get(Key.D);
    public ButtonControl eKey => Get(Key.E);
    public ButtonControl fKey => Get(Key.F);
    public ButtonControl gKey => Get(Key.G);
    public ButtonControl hKey => Get(Key.H);
    public ButtonControl iKey => Get(Key.I);
    public ButtonControl jKey => Get(Key.J);
    public ButtonControl kKey => Get(Key.K);
    public ButtonControl lKey => Get(Key.L);
    public ButtonControl mKey => Get(Key.M);
    public ButtonControl nKey => Get(Key.N);
    public ButtonControl oKey => Get(Key.O);
    public ButtonControl pKey => Get(Key.P);
    public ButtonControl qKey => Get(Key.Q);
    public ButtonControl rKey => Get(Key.R);
    public ButtonControl sKey => Get(Key.S);
    public ButtonControl tKey => Get(Key.T);
    public ButtonControl uKey => Get(Key.U);
    public ButtonControl vKey => Get(Key.V);
    public ButtonControl wKey => Get(Key.W);
    public ButtonControl xKey => Get(Key.X);
    public ButtonControl yKey => Get(Key.Y);
    public ButtonControl zKey => Get(Key.Z);
    public ButtonControl digit0Key => Get(Key.Digit0);
    public ButtonControl digit1Key => Get(Key.Digit1);
    public ButtonControl digit2Key => Get(Key.Digit2);
    public ButtonControl digit3Key => Get(Key.Digit3);
    public ButtonControl digit4Key => Get(Key.Digit4);
    public ButtonControl digit5Key => Get(Key.Digit5);
    public ButtonControl digit6Key => Get(Key.Digit6);
    public ButtonControl digit7Key => Get(Key.Digit7);
    public ButtonControl digit8Key => Get(Key.Digit8);
    public ButtonControl digit9Key => Get(Key.Digit9);
    public ButtonControl spaceKey => Get(Key.Space);
    public ButtonControl enterKey => Get(Key.Enter);
    public ButtonControl escapeKey => Get(Key.Escape);
    public ButtonControl tabKey => Get(Key.Tab);
    public ButtonControl backspaceKey => Get(Key.Backspace);
    public ButtonControl upArrowKey => Get(Key.UpArrow);
    public ButtonControl downArrowKey => Get(Key.DownArrow);
    public ButtonControl leftArrowKey => Get(Key.LeftArrow);
    public ButtonControl rightArrowKey => Get(Key.RightArrow);
    public ButtonControl leftShiftKey => Get(Key.LeftShift);
    public ButtonControl rightShiftKey => Get(Key.RightShift);
    public ButtonControl leftCtrlKey => Get(Key.LeftCtrl);
    public ButtonControl rightCtrlKey => Get(Key.RightCtrl);
    public ButtonControl leftAltKey => Get(Key.LeftAlt);
    public ButtonControl rightAltKey => Get(Key.RightAlt);
    public ButtonControl leftMetaKey => Get(Key.LeftMeta);
    public ButtonControl rightMetaKey => Get(Key.RightMeta);
    public ButtonControl insertKey => Get(Key.Insert);
    public ButtonControl deleteKey => Get(Key.Delete);
    public ButtonControl homeKey => Get(Key.Home);
    public ButtonControl endKey => Get(Key.End);
    public ButtonControl pageUpKey => Get(Key.PageUp);
    public ButtonControl pageDownKey => Get(Key.PageDown);
    public ButtonControl capsLockKey => Get(Key.CapsLock);
    public ButtonControl printScreenKey => Get(Key.PrintScreen);
    public ButtonControl scrollLockKey => Get(Key.ScrollLock);
    public ButtonControl pauseKey => Get(Key.Pause);
    public ButtonControl minusKey => Get(Key.Minus);
    public ButtonControl equalsKey => Get(Key.Equals);
    public ButtonControl leftBracketKey => Get(Key.LeftBracket);
    public ButtonControl rightBracketKey => Get(Key.RightBracket);
    public ButtonControl backslashKey => Get(Key.Backslash);
    public ButtonControl semicolonKey => Get(Key.Semicolon);
    public ButtonControl quoteKey => Get(Key.Quote);
    public ButtonControl backquoteKey => Get(Key.Backquote);
    public ButtonControl commaKey => Get(Key.Comma);
    public ButtonControl periodKey => Get(Key.Period);
    public ButtonControl slashKey => Get(Key.Slash);
    public ButtonControl f1Key => Get(Key.F1);
    public ButtonControl f2Key => Get(Key.F2);
    public ButtonControl f3Key => Get(Key.F3);
    public ButtonControl f4Key => Get(Key.F4);
    public ButtonControl f5Key => Get(Key.F5);
    public ButtonControl f6Key => Get(Key.F6);
    public ButtonControl f7Key => Get(Key.F7);
    public ButtonControl f8Key => Get(Key.F8);
    public ButtonControl f9Key => Get(Key.F9);
    public ButtonControl f10Key => Get(Key.F10);
    public ButtonControl f11Key => Get(Key.F11);
    public ButtonControl f12Key => Get(Key.F12);

    // Pascal-case aliases preserve the engine's standard managed naming style.
    public ButtonControl WKey => wKey;
    public ButtonControl AKey => aKey;
    public ButtonControl SKey => sKey;
    public ButtonControl DKey => dKey;
    public ButtonControl SpaceKey => spaceKey;
    public ButtonControl EnterKey => enterKey;
    public ButtonControl EscapeKey => escapeKey;
    public ButtonControl LeftShiftKey => leftShiftKey;
    public ButtonControl LeftCtrlKey => leftCtrlKey;
    public ButtonControl UpArrowKey => upArrowKey;
    public ButtonControl DownArrowKey => downArrowKey;
    public ButtonControl LeftArrowKey => leftArrowKey;
    public ButtonControl RightArrowKey => rightArrowKey;

    private ButtonControl Get(Key key)
    {
        if (!_keys.TryGetValue(key, out ButtonControl? control))
            _keys.Add(key, control = new ButtonControl(DeviceId, $"<Keyboard>/{Path(key)}"));
        return control;
    }

    private static Keyboard? CreateCurrent()
    {
        uint device = NativeInput.CurrentDevice(InputDeviceType.Keyboard);
        return device != 0 ? new Keyboard(device) : null;
    }

    private static string Path(Key key)
    {
        if (key is >= Key.A and <= Key.Z)
            return ((char)('a' + ((int)key - (int)Key.A))).ToString();
        if (key is >= Key.Digit0 and <= Key.Digit9)
            return $"digit{(int)key - (int)Key.Digit0}";
        if (key is >= Key.F1 and <= Key.F12)
            return $"f{(int)key - (int)Key.F1 + 1}";
        string name = key.ToString();
        return char.ToLowerInvariant(name[0]) + name[1..];
    }
}

public sealed class Mouse
{
    private Mouse(uint device)
    {
        DeviceId = device;
        Position = new Vector2Control(device, "<Mouse>/position");
        Delta = new Vector2Control(device, "<Mouse>/delta");
        Scroll = new Vector2Control(device, "<Mouse>/scroll");
        LeftButton = new ButtonControl(device, "<Mouse>/leftButton");
        RightButton = new ButtonControl(device, "<Mouse>/rightButton");
        MiddleButton = new ButtonControl(device, "<Mouse>/middleButton");
        BackButton = new ButtonControl(device, "<Mouse>/backButton");
        ForwardButton = new ButtonControl(device, "<Mouse>/forwardButton");
        WheelUp = new ButtonControl(device, "<Mouse>/wheelUp");
        WheelDown = new ButtonControl(device, "<Mouse>/wheelDown");
    }

    public uint DeviceId { get; }
    public static Mouse? Current
    {
        get
        {
            uint device = NativeInput.CurrentDevice(InputDeviceType.Mouse);
            return device != 0 ? new Mouse(device) : null;
        }
    }
    public Vector2Control Position { get; }
    public Vector2Control Delta { get; }
    public Vector2Control Scroll { get; }
    public ButtonControl LeftButton { get; }
    public ButtonControl RightButton { get; }
    public ButtonControl MiddleButton { get; }
    public ButtonControl BackButton { get; }
    public ButtonControl ForwardButton { get; }
    public ButtonControl WheelUp { get; }
    public ButtonControl WheelDown { get; }
    public Vector2Control position => Position;
    public Vector2Control delta => Delta;
    public Vector2Control scroll => Scroll;
    public ButtonControl leftButton => LeftButton;
    public ButtonControl rightButton => RightButton;
}

public sealed class Gamepad
{
    private Gamepad(uint device)
    {
        DeviceId = device;
        LeftStick = new Vector2Control(device, "<Gamepad>/leftStick");
        RightStick = new Vector2Control(device, "<Gamepad>/rightStick");
        Dpad = new Vector2Control(device, "<Gamepad>/dpad");
        LeftTrigger = new AxisControl(device, "<Gamepad>/leftTrigger");
        RightTrigger = new AxisControl(device, "<Gamepad>/rightTrigger");
        ButtonSouth = Button("buttonSouth");
        ButtonEast = Button("buttonEast");
        ButtonWest = Button("buttonWest");
        ButtonNorth = Button("buttonNorth");
        LeftShoulder = Button("leftShoulder");
        RightShoulder = Button("rightShoulder");
        LeftStickButton = Button("leftStickPress");
        RightStickButton = Button("rightStickPress");
        StartButton = Button("start");
        SelectButton = Button("select");
    }

    public uint DeviceId { get; }
    public static Gamepad? Current => FromDevice(NativeInput.CurrentDevice(InputDeviceType.Gamepad));
    public static IReadOnlyList<Gamepad> All => Input.Devices
        .Where(device => device.Type == InputDeviceType.Gamepad && device.Connected)
        .Select(device => new Gamepad(device.Id)).ToArray();
    public Vector2Control LeftStick { get; }
    public Vector2Control RightStick { get; }
    public Vector2Control Dpad { get; }
    public AxisControl LeftTrigger { get; }
    public AxisControl RightTrigger { get; }
    public ButtonControl ButtonSouth { get; }
    public ButtonControl ButtonEast { get; }
    public ButtonControl ButtonWest { get; }
    public ButtonControl ButtonNorth { get; }
    public ButtonControl LeftShoulder { get; }
    public ButtonControl RightShoulder { get; }
    public ButtonControl LeftStickButton { get; }
    public ButtonControl RightStickButton { get; }
    public ButtonControl StartButton { get; }
    public ButtonControl SelectButton { get; }
    public Vector2Control leftStick => LeftStick;
    public Vector2Control rightStick => RightStick;
    public ButtonControl buttonSouth => ButtonSouth;
    public ButtonControl buttonEast => ButtonEast;
    public bool SetMotorSpeeds(float lowFrequency, float highFrequency, float durationSeconds = 0.25f) =>
        Input.TrySetGamepadRumble(DeviceId, lowFrequency, highFrequency, durationSeconds);

    private ButtonControl Button(string path) => new(DeviceId, $"<Gamepad>/{path}");
    private static Gamepad? FromDevice(uint device) => device != 0 ? new Gamepad(device) : null;
}

public static partial class Input
{
    public static class Keyboard
    {
        public static Keire.Keyboard? Current => Keire.Keyboard.Current;
    }

    public static class Mouse
    {
        public static Keire.Mouse? Current => Keire.Mouse.Current;
    }

    public static class Gamepad
    {
        public static Keire.Gamepad? Current => Keire.Gamepad.Current;
        public static IReadOnlyList<Keire.Gamepad> All => Keire.Gamepad.All;
    }
}
