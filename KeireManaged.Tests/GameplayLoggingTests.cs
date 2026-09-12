using System.Globalization;
using System.Runtime.InteropServices;
using Keire;

internal static unsafe class GameplayLoggingTests
{
    private static readonly List<(byte Level, string Message)> Records = new();

    internal static void Run()
    {
        var previousCulture = CultureInfo.CurrentCulture;
        Records.Clear();
        NativeRuntime.WriteLogIcall = &WriteLog;
        try
        {
            CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("fr-FR");
            Action<string, object?[]>[] writers =
            {
                Log.Trace, Log.Debug, Log.Info, Log.Warning, Log.Error, Log.Critical,
                Debug.LogFormat, Debug.LogWarningFormat, Debug.LogErrorFormat
            };
            byte[] levels = { 0, 1, 2, 3, 4, 5, 2, 3, 4 };
            for (int index = 0; index < writers.Length; ++index)
            {
                writers[index]("Player {0}: health {1:F2}, enabled {2}, target [{3}]",
                               new object?[] { new Player("Ada"), 75.25f, true, null });
                Check(Records.Count == index + 1, "Each gameplay log must produce exactly one native record.");
                Check(Records[index] == (levels[index], "Player Ada: health 75.25, enabled True, target []"),
                      "Every log level must preserve objects, nulls and invariant numeric formatting.");
            }
            Debug.LogFormat("Literal {{braces}}", Array.Empty<object?>());
            Check(Records[^1].Message == "Literal {braces}", "Explicit formatting must process escaped braces.");
            Log.Info("Literal {braces}");
            Check(Records[^1].Message == "Literal {braces}", "Existing single-string logs must remain literal.");
            int before = Records.Count;
            try { Debug.LogFormat("Bad {1}", 0); throw new Exception("Missing format rejection."); }
            catch (FormatException) { }
            Check(Records.Count == before, "Malformed format strings must not emit a partial log record.");
            Debug.Assert(true, "Must not log");
            Check(Records.Count == before, "Passing assertions must not emit records.");
            Debug.Assert(false, "Missing player");
            Check(Records[^1] == ((byte)4, "Assertion failed: Missing player"), "Failed assertions must be errors.");
            var failure = new InvalidOperationException("Loading inventory failed", new Exception("Missing data"));
            Debug.LogException(failure);
            Check(Records[^1].Level == 4 && Records[^1].Message.Contains("Loading inventory failed") &&
                  Records[^1].Message.Contains("Missing data"), "Exception logs must preserve nested diagnostics.");
        }
        finally
        {
            NativeRuntime.WriteLogIcall = null;
            CultureInfo.CurrentCulture = previousCulture;
        }
    }

    private sealed record Player(string Name)
    {
        public override string ToString() => Name;
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    [UnmanagedCallersOnly]
    private static void WriteLog(byte level, NativeString message) =>
        Records.Add((level, Marshal.PtrToStringAuto(*(IntPtr*)&message) ?? string.Empty));
}
