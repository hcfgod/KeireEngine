namespace Keire;

public static class Application
{
    public static string ProductName => NativeFoundation.ReadApplicationText(ApplicationText.ProductName);
    public static string Version => NativeFoundation.ReadApplicationText(ApplicationText.Version);
    public static string Identifier => NativeFoundation.ReadApplicationText(ApplicationText.Identifier);
    public static string PersistentDataPath =>
        NativeFoundation.ReadApplicationText(ApplicationText.PersistentDataPath);
    public static bool IsEditor => NativeFoundation.IsEditor;

    public static void Quit(int exitCode = 0) => NativeFoundation.RequestExit(exitCode);
}

public static partial class Time
{
    public static float TimeScale
    {
        get => (float)NativeFoundation.TimeScale;
        set
        {
            if (!float.IsFinite(value) || value < 0.0f || value > 100.0f)
                throw new ArgumentOutOfRangeException(nameof(value), "Time scale must be finite and in the range 0..100.");
            NativeFoundation.TimeScale = value;
        }
    }

    public static bool Paused
    {
        get => NativeFoundation.TimePaused;
        set => NativeFoundation.TimePaused = value;
    }
}

public enum FullscreenMode : byte
{
    Windowed,
    BorderlessFullscreen
}

public readonly record struct Resolution(uint Width, uint Height, uint PixelWidth, uint PixelHeight,
                                         float DisplayScale);

public readonly record struct ScreenRect(float X, float Y, float Width, float Height);

/// <summary>Presentation choices reported by the active GPU surface. Mailbox synchronizes without a FIFO queue.</summary>
public enum PresentMode : byte
{
    VSync,
    Mailbox,
    Immediate
}

public static class Screen
{
    public static Resolution CurrentResolution
    {
        get
        {
            NativeScreenState state = NativeFoundation.ScreenState;
            return new Resolution(state.LogicalWidth, state.LogicalHeight, state.PixelWidth, state.PixelHeight,
                                  state.DisplayScale);
        }
    }

    public static uint Width => NativeFoundation.ScreenState.LogicalWidth;
    public static uint Height => NativeFoundation.ScreenState.LogicalHeight;
    public static float DisplayScale => NativeFoundation.ScreenState.DisplayScale;
    public static FullscreenMode Mode => (FullscreenMode)NativeFoundation.ScreenState.Mode;
    public static bool Fullscreen => Mode != FullscreenMode.Windowed;
    public static bool Focused => NativeFoundation.ScreenState.Focused;
    public static bool Visible => NativeFoundation.ScreenState.Visible;
    public static bool Minimized => NativeFoundation.ScreenState.Minimized;
    /// <summary>False when there is no active presentation surface (for example, a headless player).</summary>
    public static bool PresentationAvailable => (NativeFoundation.PresentationState & 255) <= 2;

    /// <summary>The actual active mode. Changes affect this window only and are not persisted to project settings.</summary>
    public static PresentMode PresentMode
    {
        get
        {
            byte mode = (byte)(NativeFoundation.PresentationState & 255);
            if (mode > 2)
                throw new InvalidOperationException("There is no active presentation surface.");
            return (PresentMode)mode;
        }
        set => SetPresentMode(value);
    }

    /// <summary>True for FIFO VSync and Mailbox. Enabling selects VSync; disabling selects Immediate.</summary>
    public static bool VSyncEnabled
    {
        get => PresentationAvailable && PresentMode != Keire.PresentMode.Immediate;
        set => SetPresentMode(value ? Keire.PresentMode.VSync : Keire.PresentMode.Immediate);
    }

    public static bool IsPresentModeSupported(PresentMode mode)
    {
        ValidatePresentMode(mode);
        return (NativeFoundation.PresentationState & (1 << (8 + (int)mode))) != 0;
    }

    /// <summary>Returns false if the mode cannot be applied. Unsupported requests preserve the current mode;
    /// a rendering failure can make presentation unavailable. Check PresentationAvailable and the runtime log.</summary>
    public static bool TrySetPresentMode(PresentMode mode)
    {
        ValidatePresentMode(mode);
        return NativeFoundation.TrySetPresentMode(mode);
    }

    public static void SetPresentMode(PresentMode mode)
    {
        if (!TrySetPresentMode(mode))
            throw new InvalidOperationException("The presentation mode could not be applied. Check Screen.IsPresentModeSupported, Screen.PresentationAvailable, and the runtime log for unsupported modes or rendering errors.");
    }

    private static void ValidatePresentMode(PresentMode mode)
    {
        if (!Enum.IsDefined(mode))
            throw new ArgumentOutOfRangeException(nameof(mode));
    }
    public static ScreenRect SafeArea
    {
        get
        {
            NativeScreenState state = NativeFoundation.ScreenState;
            return new ScreenRect(0.0f, 0.0f, state.LogicalWidth, state.LogicalHeight);
        }
    }

    public static bool TrySetResolution(uint width, uint height, FullscreenMode mode = FullscreenMode.Windowed)
    {
        ValidateResolution(width, height, mode);
        return NativeFoundation.TrySetScreen(width, height, mode);
    }

    public static void SetResolution(uint width, uint height, FullscreenMode mode = FullscreenMode.Windowed)
    {
        if (!TrySetResolution(width, height, mode))
            throw new InvalidOperationException("The runtime could not apply the requested screen configuration.");
    }

    private static void ValidateResolution(uint width, uint height, FullscreenMode mode)
    {
        if (width is < 64 or > 16384)
            throw new ArgumentOutOfRangeException(nameof(width), "Screen width must be in the range 64..16384.");
        if (height is < 64 or > 16384)
            throw new ArgumentOutOfRangeException(nameof(height), "Screen height must be in the range 64..16384.");
        if (!Enum.IsDefined(mode))
            throw new ArgumentOutOfRangeException(nameof(mode));
    }
}
