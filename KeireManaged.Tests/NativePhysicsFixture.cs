internal static unsafe class NativePhysicsFixture
{
    internal static int CapsuleCalls;
    internal static int OverlapCalls;
    internal static Keire.Quaternion Rotation;
    internal static bool IncludeTriggers;

    internal static void Install()
    {
        CapsuleCalls = 0;
        OverlapCalls = 0;
        Rotation = default;
        IncludeTriggers = false;
        Keire.NativeRuntime.CapsuleCastIcall = &CapsuleCast;
        Keire.NativeRuntime.OverlapSphereIcall = &OverlapSphere;
    }

    internal static void Uninstall()
    {
        Keire.NativeRuntime.CapsuleCastIcall = null;
        Keire.NativeRuntime.OverlapSphereIcall = null;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static byte CapsuleCast(ulong world, ulong contextHigh, ulong contextLow, Keire.Vector3 origin,
                                    Keire.Quaternion rotation, float radius, float height, Keire.Vector3 displacement,
                                    uint mask, byte includeTriggers, ulong ignoredHigh, ulong ignoredLow,
                                    Keire.NativeRaycastHit* destination)
    {
        ++CapsuleCalls;
        Rotation = rotation;
        IncludeTriggers = includeTriggers != 0;
        if (world != 17 || contextHigh != 23 || contextLow != 29 || radius != 0.5f || height != 1.8f ||
            displacement != new Keire.Vector3(0.0f, 0.0f, 4.0f) || mask != 0x40u || ignoredHigh != 31 ||
            ignoredLow != 37 || destination == null)
        {
            return 0;
        }
        destination->EntityHigh = 41;
        destination->EntityLow = 43;
        destination->Point = new Keire.Vector3(1.0f, 2.0f, 5.5f);
        destination->Normal = new Keire.Vector3(0.0f, 1.0f, 0.0f);
        destination->Distance = 2.5f;
        return 1;
    }

    [System.Runtime.InteropServices.UnmanagedCallersOnly]
    private static int OverlapSphere(ulong world, ulong contextHigh, ulong contextLow, Keire.Vector3 center,
                                     float radius, uint mask, byte includeTriggers, ulong ignoredHigh,
                                     ulong ignoredLow, Keire.NativeEntityId* destination, int capacity)
    {
        ++OverlapCalls;
        IncludeTriggers = includeTriggers != 0;
        if (world != 17 || contextHigh != 23 || contextLow != 29 || center != new Keire.Vector3(8.0f, 9.0f, 10.0f) ||
            radius != 3.0f || mask != 0x80u || ignoredHigh != 31 || ignoredLow != 37)
        {
            return -1;
        }
        if (destination == null || capacity == 0)
            return 2;
        if (capacity < 2)
            return -1;
        destination[0] = new Keire.NativeEntityId { High = 47, Low = 53 };
        destination[1] = new Keire.NativeEntityId { High = 59, Low = 61 };
        return 2;
    }
}
