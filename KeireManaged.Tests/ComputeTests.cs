using System.Runtime.InteropServices;
using Keire;

internal static unsafe class ComputeTests
{
    private static ulong _next;
    private static int _calls;

    internal static void Run()
    {
        Throws<ArgumentOutOfRangeException>(() => new ComputeDevice((ComputeBackend)255));
        Throws<InvalidOperationException>(() => new ComputeDevice(ComputeBackend.D3D12));
        NativeCompute.CommandIcall = &Command;
        try
        {
            using var device = new ComputeDevice(ComputeBackend.D3D12);
            using var other = new ComputeDevice(ComputeBackend.D3D12);
            using var buffer = device.CreateBuffer(32);
            using var pipeline = device.CreatePipeline(1);
            int before = _calls;
            Throws<ArgumentOutOfRangeException>(() => device.CreateBuffer(0));
            Throws<ArgumentOutOfRangeException>(() => buffer.Upload(new byte[33]));
            Throws<ArgumentException>(() => other.Dispatch(pipeline, [], 1));
            Task.Run(() => Throws<InvalidOperationException>(() => buffer.Upload(new byte[1])))
                .GetAwaiter().GetResult();
            if (_calls != before)
                throw new Exception("Rejected compute operations reached the native bridge.");

            buffer.Dispose();
            before = _calls;
            buffer.Dispose();
            Throws<ObjectDisposedException>(() => buffer.Upload(new byte[1]));
            if (_calls != before)
                throw new Exception("Disposed buffers reached the native bridge.");
            device.Dispose();
            before = _calls;
            pipeline.Dispose();
            device.Dispose();
            Throws<ObjectDisposedException>(() => device.CreateBuffer(16));
            if (_calls != before)
                throw new Exception("Retained resources called a disposed device.");
        }
        finally
        {
            NativeCompute.CommandIcall = null;
        }
    }

    [UnmanagedCallersOnly]
    private static byte Command(ulong device, byte command, ulong a, ulong b, byte* bytes, uint size, ulong* result)
    {
        ++_calls;
        *result = ++_next;
        return 1;
    }

    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T) { return; }
        throw new Exception($"Expected {typeof(T).Name}.");
    }
}
