using System.Collections.Concurrent;
using System.Diagnostics;

namespace Keire;

public static class Profiler
{
    private static readonly ConcurrentDictionary<string, ulong> MarkerIds = new(StringComparer.Ordinal);

    public static ProfileSample Sample(string name)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        return new ProfileSample(MarkerId(name), Stopwatch.GetTimestamp());
    }

    public static void Counter(string name, double value)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        if (double.IsFinite(value))
            NativeRuntime.SetProfileCounter(MarkerId(name), value);
    }

    private static ulong MarkerId(string name)
    {
        return MarkerIds.GetOrAdd(name, static value =>
        {
            const ulong offset = 14695981039346656037UL;
            const ulong prime = 1099511628211UL;
            ulong id = offset;
            foreach (char character in value)
            {
                id ^= character;
                id *= prime;
            }
            if (id == 0)
                id = 1;
            NativeRuntime.RegisterProfileName(id, value);
            return id;
        });
    }
}

public readonly struct ProfileSample : IDisposable
{
    private readonly ulong _id;
    private readonly long _start;

    internal ProfileSample(ulong id, long start)
    {
        _id = id;
        _start = start;
    }

    public void Dispose()
    {
        if (_id == 0)
            return;
        long end = Stopwatch.GetTimestamp();
        double scale = 1_000_000.0 / Stopwatch.Frequency;
        NativeRuntime.RecordProfileSpan(_id, _start * scale, (end - _start) * scale);
    }
}
