internal static unsafe class NativeLogFixture
{
    internal static int WriteCount;
    internal static int DebugLineCount;

    internal static void Install()
    {
        WriteCount = 0;
        DebugLineCount = 0;
        Keire.NativeRuntime.WriteLogIcall = &WriteLog;
        Keire.NativeRuntime.DrawDebugLineIcall = &DrawDebugLine;
    }

    internal static void Uninstall()
    {
        Keire.NativeRuntime.WriteLogIcall = null;
        Keire.NativeRuntime.DrawDebugLineIcall = null;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static void WriteLog(byte level, Keire.NativeString message) => ++WriteCount;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static void DrawDebugLine(Keire.Vector3 start, Keire.Vector3 end, Keire.Color color, float duration) =>
        ++DebugLineCount;
}
