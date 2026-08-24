internal static unsafe class NativeLogFixture
{
    internal static int WriteCount;

    internal static void Install()
    {
        WriteCount = 0;
        Keire.NativeRuntime.WriteLogIcall = &WriteLog;
    }

    internal static void Uninstall() => Keire.NativeRuntime.WriteLogIcall = null;

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static void WriteLog(byte level, Keire.NativeString message) => ++WriteCount;
}
