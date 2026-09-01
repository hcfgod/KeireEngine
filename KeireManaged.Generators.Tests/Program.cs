using Keire;

if (IGeneratedNativeProbeNative.ServiceId != Guid.Parse("73616e64-626f-4078-8000-00000000f701"))
    throw new InvalidOperationException("The generated native service stub lost its stable service ID.");

NativeServiceRuntime.Install(3,
    static (service, method, arguments) =>
    {
        if (service != Guid.Parse("73616e64-626f-4078-8000-00000000f701") ||
            method != Guid.Parse("73616e64-626f-4078-8000-00000000f702") || arguments.Length != 2)
        {
            return new NativeInvocationResult(default,
                new NativeCallError("KEIRE-TEST", "The generated stub supplied invalid descriptors."));
        }
        return new NativeInvocationResult(NativeAbiValue.From(
            checked((int)(long)arguments[0].BoxedValue! + (int)(long)arguments[1].BoxedValue!)), null);
    });
try
{
    if (IGeneratedNativeProbeNative.Add(2, 3) != 5)
        throw new InvalidOperationException("The generated native service stub did not marshal its bounded values.");
}
finally
{
    NativeServiceRuntime.Remove(3);
}

Console.WriteLine("PASS incremental native binding generator emits typed bounded stubs");

[NativeServiceContract("73616e64-626f-4078-8000-00000000f701")]
internal interface IGeneratedNativeProbe
{
    [NativeMethod("73616e64-626f-4078-8000-00000000f702")]
    int Add(int left, int right);
}
