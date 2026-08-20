using System.Runtime.InteropServices;
using System.Text;

namespace Keire;

internal enum ApplicationText : byte
{
    ProductName,
    Version,
    Identifier,
    PersistentDataPath
}

[StructLayout(LayoutKind.Sequential)]
internal struct NativeScreenState
{
    internal uint LogicalWidth;
    internal uint LogicalHeight;
    internal uint PixelWidth;
    internal uint PixelHeight;
    internal float DisplayScale;
    internal byte Mode;
    internal byte FocusedValue;
    internal byte VisibleValue;
    internal byte MinimizedValue;
    internal byte VSyncValue;

    internal readonly bool Focused => FocusedValue != 0;
    internal readonly bool Visible => VisibleValue != 0;
    internal readonly bool Minimized => MinimizedValue != 0;
    internal readonly bool VSync => VSyncValue != 0;
}

internal static unsafe class NativeFoundation
{
#pragma warning disable CS0649
    internal static delegate* unmanaged<byte, byte*, int, int> GetApplicationTextIcall;
    internal static delegate* unmanaged<byte> IsEditorIcall;
    internal static delegate* unmanaged<int, void> RequestExitIcall;
    internal static delegate* unmanaged<double> GetTimeScaleIcall;
    internal static delegate* unmanaged<double, byte> SetTimeScaleIcall;
    internal static delegate* unmanaged<byte> IsTimePausedIcall;
    internal static delegate* unmanaged<byte, byte> SetTimePausedIcall;
    internal static delegate* unmanaged<NativeScreenState*, byte> GetScreenStateIcall;
    internal static delegate* unmanaged<uint, uint, byte, byte> SetScreenIcall;
#pragma warning restore CS0649

    internal static string ReadApplicationText(ApplicationText field)
    {
        if (GetApplicationTextIcall == null)
            throw Unbound();
        int length = GetApplicationTextIcall((byte)field, null, 0);
        if (length < 0 || length > 1024 * 1024)
            throw new InvalidOperationException("The native runtime returned an invalid application text length.");
        if (length == 0)
            return string.Empty;
        var bytes = new byte[length];
        fixed (byte* destination = bytes)
        {
            int copied = GetApplicationTextIcall((byte)field, destination, bytes.Length);
            if (copied != bytes.Length)
                throw new InvalidOperationException("The native application text changed while it was being read.");
        }
        return Encoding.UTF8.GetString(bytes);
    }

    internal static bool IsEditor
    {
        get
        {
            if (IsEditorIcall == null)
                throw Unbound();
            return IsEditorIcall() != 0;
        }
    }

    internal static void RequestExit(int exitCode)
    {
        if (RequestExitIcall == null)
            throw Unbound();
        RequestExitIcall(exitCode);
    }

    internal static double TimeScale
    {
        get
        {
            if (GetTimeScaleIcall == null)
                throw Unbound();
            return GetTimeScaleIcall();
        }
        set
        {
            if (SetTimeScaleIcall == null)
                throw Unbound();
            if (SetTimeScaleIcall(value) == 0)
                throw new InvalidOperationException("The native runtime rejected the time scale change.");
        }
    }

    internal static bool TimePaused
    {
        get
        {
            if (IsTimePausedIcall == null)
                throw Unbound();
            return IsTimePausedIcall() != 0;
        }
        set
        {
            if (SetTimePausedIcall == null)
                throw Unbound();
            if (SetTimePausedIcall(value ? (byte)1 : (byte)0) == 0)
                throw new InvalidOperationException("The native runtime rejected the pause state change.");
        }
    }

    internal static NativeScreenState ScreenState
    {
        get
        {
            if (GetScreenStateIcall == null)
                throw Unbound();
            NativeScreenState state = default;
            if (GetScreenStateIcall(&state) == 0)
                throw new InvalidOperationException("Screen state is unavailable in the current runtime context.");
            return state;
        }
    }

    internal static bool TrySetScreen(uint width, uint height, FullscreenMode mode)
    {
        if (SetScreenIcall == null)
            throw Unbound();
        return SetScreenIcall(width, height, (byte)mode) != 0;
    }

    private static InvalidOperationException Unbound() => new("Kéire managed runtime foundation is not attached.");
}
