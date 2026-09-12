namespace Keire;

public static class Debug
{
    public static void Log(object? message) => NativeRuntime.WriteLog(2, message?.ToString() ?? "null");
    public static void Warn(object? message) => NativeRuntime.WriteLog(3, message?.ToString() ?? "null");
    public static void LogWarning(object? message) => Warn(message);
    public static void Error(object? message) => NativeRuntime.WriteLog(4, message?.ToString() ?? "null");
    public static void LogError(object? message) => Error(message);

    public static void Log(string? format, params object?[]? args) =>
        NativeRuntime.WriteLog(2, FormatMessage(format, args));

    public static void Warn(string? format, params object?[]? args) =>
        NativeRuntime.WriteLog(3, FormatMessage(format, args));

    public static void LogWarning(string? format, params object?[]? args) =>
        NativeRuntime.WriteLog(3, FormatMessage(format, args));

    public static void Error(string? format, params object?[]? args) =>
        NativeRuntime.WriteLog(4, FormatMessage(format, args));

    public static void LogError(string? format, params object?[]? args) =>
        NativeRuntime.WriteLog(4, FormatMessage(format, args));

    public static void LogFormat(string format, params object?[] args) =>
        WriteFormatted(2, format, args);

    public static void LogWarningFormat(string format, params object?[] args) =>
        WriteFormatted(3, format, args);

    public static void LogErrorFormat(string format, params object?[] args) =>
        WriteFormatted(4, format, args);

    internal static void WriteFormatted(byte level, string format, object?[] args) =>
        NativeRuntime.WriteLog(level, string.Format(System.Globalization.CultureInfo.InvariantCulture, format, args));

    private static string FormatMessage(string? format, object?[]? args)
    {
        format ??= "null";
        args ??= new object?[] { null };
        if (args.Length == 0)
            return format;
        return format.Contains('{') || format.Contains('}')
            ? string.Format(System.Globalization.CultureInfo.InvariantCulture, format, args)
            : string.Join(" ", new[] { format }.Concat(args.Select(value => value?.ToString() ?? "null")));
    }

    private static string JoinValues(object? first, object? second, object?[]? rest) =>
        string.Join(" ", new[] { first, second }.Concat(rest ?? new object?[] { null })
            .Select(value => value?.ToString() ?? "null"));

    public static void Log(object? first, object? second, params object?[]? rest) =>
        NativeRuntime.WriteLog(2, JoinValues(first, second, rest));

    public static void Warn(object? first, object? second, params object?[]? rest) =>
        NativeRuntime.WriteLog(3, JoinValues(first, second, rest));

    public static void LogWarning(object? first, object? second, params object?[]? rest) =>
        NativeRuntime.WriteLog(3, JoinValues(first, second, rest));

    public static void Error(object? first, object? second, params object?[]? rest) =>
        NativeRuntime.WriteLog(4, JoinValues(first, second, rest));

    public static void LogError(object? first, object? second, params object?[]? rest) =>
        NativeRuntime.WriteLog(4, JoinValues(first, second, rest));

    public static void LogException(Exception exception) =>
        NativeRuntime.WriteLog(4, (exception ?? throw new ArgumentNullException(nameof(exception))).ToString());

    public static void Assert(bool condition, object? message = null)
    {
        if (!condition)
            NativeRuntime.WriteLog(4, $"Assertion failed: {message ?? "No message provided."}");
    }

    public static void DrawLine(Vector3 start, Vector3 end, Color color, float duration = 0.0f) =>
        NativeRuntime.DrawDebugLine(start, end, color, duration);
}

public static class Log
{
    public static void Trace(string message) => NativeRuntime.WriteLog(0, message);
    public static void Debug(string message) => NativeRuntime.WriteLog(1, message);
    public static void Info(string message) => NativeRuntime.WriteLog(2, message);
    public static void Warning(string message) => NativeRuntime.WriteLog(3, message);
    public static void Error(string message) => NativeRuntime.WriteLog(4, message);
    public static void Critical(string message) => NativeRuntime.WriteLog(5, message);

    public static void Trace(string format, params object?[] args) => Keire.Debug.WriteFormatted(0, format, args);
    public static void Debug(string format, params object?[] args) => Keire.Debug.WriteFormatted(1, format, args);
    public static void Info(string format, params object?[] args) => Keire.Debug.WriteFormatted(2, format, args);
    public static void Warning(string format, params object?[] args) => Keire.Debug.WriteFormatted(3, format, args);
    public static void Error(string format, params object?[] args) => Keire.Debug.WriteFormatted(4, format, args);
    public static void Critical(string format, params object?[] args) => Keire.Debug.WriteFormatted(5, format, args);
}
