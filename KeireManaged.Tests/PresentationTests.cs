using System.Runtime.InteropServices;

internal static unsafe class PresentationTests
{
    private static ushort _state;
    private static int _calls;

    internal static void Run()
    {
        Keire.NativeFoundation.GetPresentationStateIcall = &GetState;
        Keire.NativeFoundation.SetPresentModeIcall = &SetMode;
        try
        {
            _state = 0x0700;
            _calls = 0;
            Assert(Keire.Screen.PresentationAvailable && Keire.Screen.VSyncEnabled, "FIFO must report synchronized.");
            foreach (Keire.PresentMode mode in Enum.GetValues<Keire.PresentMode>())
            {
                Assert(Keire.Screen.IsPresentModeSupported(mode), "Advertised capability missing.");
                Assert(Keire.Screen.TrySetPresentMode(mode) && Keire.Screen.PresentMode == mode, "Mode did not round trip.");
                Assert(Keire.Screen.VSyncEnabled == (mode != Keire.PresentMode.Immediate), "Actual synchronization incorrect.");
            }
            Keire.Screen.VSyncEnabled = true;
            Assert(Keire.Screen.PresentMode == Keire.PresentMode.VSync, "Enable must select FIFO.");
            Keire.Screen.VSyncEnabled = false;
            Assert(Keire.Screen.PresentMode == Keire.PresentMode.Immediate, "Disable must select Immediate.");
            _state = 0x0100;
            Assert(!Keire.Screen.IsPresentModeSupported(Keire.PresentMode.Mailbox), "Unsupported mode advertised.");
            Assert(!Keire.Screen.TrySetPresentMode(Keire.PresentMode.Mailbox) && Keire.Screen.PresentMode == Keire.PresentMode.VSync,
                "Unsupported switch must preserve current mode.");
            Throws<InvalidOperationException>(() => Keire.Screen.VSyncEnabled = false);
            int calls = _calls;
            Throws<ArgumentOutOfRangeException>(() => Keire.Screen.TrySetPresentMode((Keire.PresentMode)255));
            Throws<ArgumentOutOfRangeException>(() => Keire.Screen.IsPresentModeSupported((Keire.PresentMode)42));
            Assert(calls == _calls, "Invalid enums must be rejected before native mutation.");
            _state = 255;
            Assert(!Keire.Screen.PresentationAvailable && !Keire.Screen.VSyncEnabled, "Headless must not report synchronization.");
            Assert(!Keire.Screen.TrySetPresentMode(Keire.PresentMode.VSync), "Unavailable runtime accepted mutation.");
            Throws<InvalidOperationException>(() => { _ = Keire.Screen.PresentMode; });
        }
        finally
        {
            Keire.NativeFoundation.GetPresentationStateIcall = null;
            Keire.NativeFoundation.SetPresentModeIcall = null;
        }
        Throws<InvalidOperationException>(() => { _ = Keire.Screen.PresentationAvailable; });
        Throws<InvalidOperationException>(() => Keire.Screen.TrySetPresentMode(Keire.PresentMode.VSync));
    }

    [UnmanagedCallersOnly]
    private static ushort GetState() => _state;

    [UnmanagedCallersOnly]
    private static byte SetMode(byte mode)
    {
        ++_calls;
        if (mode > 2 || (_state & (1 << (8 + mode))) == 0)
            return 0;
        _state = (ushort)((_state & 0xff00) | mode);
        return 1;
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }

    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T) { return; }
        throw new InvalidOperationException($"Expected {typeof(T).Name}.");
    }
}
