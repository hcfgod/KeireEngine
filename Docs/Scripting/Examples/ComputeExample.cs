using Keire;

namespace ScriptingExamples;

public static class ComputeExample
{
    public static byte[] DispatchAndReadback(ComputeBackend backend, ulong programKey)
    {
        using ComputeDevice device = new(backend);
        using ComputeBuffer output = device.CreateBuffer(16);
        using ComputePipeline pipeline = device.CreatePipeline(programKey);
        output.Upload(new byte[16]);
        using ComputeSubmission dispatch = device.Dispatch(
            pipeline, new[] { new ComputeBufferBinding(0, output, Writable: true) }, x: 1);
        dispatch.Wait();
        using ComputeSubmission readback = output.RequestReadback();
        return readback.GetReadback();
    }
}
