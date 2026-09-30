internal static unsafe class NativeFoundationFixture
{
    private static double s_timeScale;
    private static byte s_paused;
    private static byte s_presentMode;
    private static string s_persistentDataPath = string.Empty;

    public static int ExitCode { get; private set; }
    public static uint LastWidth { get; private set; }
    public static uint LastHeight { get; private set; }
    public static Keire.FullscreenMode LastMode { get; private set; }

    public static void Install()
    {
        s_timeScale = 1.0;
        s_paused = 0;
        s_presentMode = 0;
        s_persistentDataPath = Path.GetFullPath(Path.Combine(Path.GetTempPath(), "keire-native-foundation"));
        ExitCode = 0;
        LastWidth = 0;
        LastHeight = 0;
        LastMode = Keire.FullscreenMode.Windowed;
        Keire.NativeFoundation.GetApplicationTextIcall = &GetApplicationText;
        Keire.NativeFoundation.IsEditorIcall = &IsEditor;
        Keire.NativeFoundation.RequestExitIcall = &RequestExit;
        Keire.NativeFoundation.GetTimeScaleIcall = &GetTimeScale;
        Keire.NativeFoundation.SetTimeScaleIcall = &SetTimeScale;
        Keire.NativeFoundation.IsTimePausedIcall = &IsTimePaused;
        Keire.NativeFoundation.SetTimePausedIcall = &SetTimePaused;
        Keire.NativeFoundation.GetScreenStateIcall = &GetScreenState;
        Keire.NativeFoundation.SetScreenIcall = &SetScreen;
        Keire.NativeFoundation.GetPresentationStateIcall = &GetPresentationState;
        Keire.NativeFoundation.SetPresentModeIcall = &SetPresentMode;
    }

    public static void Uninstall()
    {
        Keire.NativeFoundation.GetApplicationTextIcall = null;
        Keire.NativeFoundation.IsEditorIcall = null;
        Keire.NativeFoundation.RequestExitIcall = null;
        Keire.NativeFoundation.GetTimeScaleIcall = null;
        Keire.NativeFoundation.SetTimeScaleIcall = null;
        Keire.NativeFoundation.IsTimePausedIcall = null;
        Keire.NativeFoundation.SetTimePausedIcall = null;
        Keire.NativeFoundation.GetScreenStateIcall = null;
        Keire.NativeFoundation.SetScreenIcall = null;
        Keire.NativeFoundation.GetPresentationStateIcall = null;
        Keire.NativeFoundation.SetPresentModeIcall = null;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int GetApplicationText(byte field, byte* destination, int capacity)
    {
        string value = (Keire.ApplicationText)field switch
        {
            Keire.ApplicationText.ProductName => "Nightglass",
            Keire.ApplicationText.Version => "1.2.3",
            Keire.ApplicationText.Identifier => "games.keire.nightglass",
            Keire.ApplicationText.PersistentDataPath => s_persistentDataPath,
            _ => string.Empty
        };
        byte[] bytes = System.Text.Encoding.UTF8.GetBytes(value);
        if (destination == null || capacity == 0)
            return bytes.Length;
        if (capacity < bytes.Length)
            return -1;
        for (int index = 0; index < bytes.Length; ++index)
            destination[index] = bytes[index];
        return bytes.Length;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte IsEditor() => 1;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static void RequestExit(int exitCode) => ExitCode = exitCode;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static double GetTimeScale() => s_timeScale;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetTimeScale(double value)
    {
        if (!double.IsFinite(value) || value is < 0.0 or > 100.0)
            return 0;
        s_timeScale = value;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte IsTimePaused() => s_paused;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetTimePaused(byte value)
    {
        s_paused = value == 0 ? (byte)0 : (byte)1;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte GetScreenState(Keire.NativeScreenState* state)
    {
        if (state == null)
            return 0;
        *state = new Keire.NativeScreenState
        {
            LogicalWidth = 1920,
            LogicalHeight = 1080,
            PixelWidth = 3840,
            PixelHeight = 2160,
            DisplayScale = 2.0f,
            Mode = (byte)Keire.FullscreenMode.Windowed,
            FocusedValue = 1,
            VisibleValue = 1,
            MinimizedValue = 0,
            VSyncValue = 1
        };
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static ushort GetPresentationState() => (ushort)(0x0700 | s_presentMode);

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetPresentMode(byte mode)
    {
        if (mode > 2)
            return 0;
        s_presentMode = mode;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte SetScreen(uint width, uint height, byte mode)
    {
        LastWidth = width;
        LastHeight = height;
        LastMode = (Keire.FullscreenMode)mode;
        return 1;
    }
}
