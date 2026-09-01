internal static class NativeServiceContractTests
{
    internal static void Run()
    {
        IReadOnlyList<Keire.NativeServiceDescriptor> catalog =
            Keire.NativeServiceCatalog.Discover([typeof(INativeServiceProbe)]);
        Keire.NativeServiceDescriptor service = catalog.Single();
        Assert(service.StableId == Guid.Parse("73616e64-626f-4078-8000-00000000f601") &&
                   service.AbiVersion == 2 && service.Methods.Count == 2 &&
                   service.Methods[0].StableId.CompareTo(service.Methods[1].StableId) < 0,
               "Native service catalogs must preserve stable ABI versions and sort methods by stable ID.");
        Keire.NativeMethodDescriptor bufferMethod = service.Methods.Single(method => method.Name == "Sum");
        Assert(bufferMethod.Parameters.Single().Kind == Keire.NativeValueKind.BoundedBuffer &&
                   bufferMethod.Parameters.Single().MaximumElements == 128,
               "Native service buffers must expose an explicit bounded value ABI.");

        AssertThrows<InvalidOperationException>(
            () => Keire.NativeServiceCatalog.Discover([typeof(IInvalidNativeServiceProbe)]),
            "Unsupported signatures must reject the complete native service catalog.");
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }

    private static void AssertThrows<TException>(Action action, string message) where TException : Exception
    {
        try
        {
            action();
        }
        catch (TException)
        {
            return;
        }
        throw new InvalidOperationException(message);
    }
}

[Keire.NativeServiceContract("73616e64-626f-4078-8000-00000000f601", 2)]
internal interface INativeServiceProbe
{
    [Keire.NativeMethod("73616e64-626f-4078-8000-00000000f603", 1,
                        Keire.NativeThreadAffinity.ManagedOwnerThread)]
    Keire.NativeCallResult<Keire.Vector3> Project(Keire.Vector3 value, Keire.AssetId asset);

    [Keire.NativeMethod("73616e64-626f-4078-8000-00000000f602")]
    long Sum([Keire.NativeBuffer(128)] ReadOnlySpan<int> values);
}

[Keire.NativeServiceContract("73616e64-626f-4078-8000-00000000f604")]
internal interface IInvalidNativeServiceProbe
{
    [Keire.NativeMethod("73616e64-626f-4078-8000-00000000f605")]
    DateTime Unsupported(DateTime value);
}
