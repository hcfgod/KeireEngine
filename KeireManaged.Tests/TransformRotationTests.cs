using System.Runtime.InteropServices;
using Keire;

internal static unsafe class TransformRotationTests
{
    private sealed class Probe : Behaviour
    {
        public void Turn(float angle) => transform.Rotate(Vector3.Up, angle);
    }

    private static Quaternion _rotation;
    private static int _reads;
    private static int _writes;
    private static bool _exists;
    private static (ulong, ulong, ulong) _owner;

    internal static void Run()
    {
        var get = NativeRuntime.GetWorldRotationIcall;
        var set = NativeRuntime.SetWorldRotationIcall;
        var exists = NativeRuntime.ComponentExistsIcall;
        NativeRuntime.GetWorldRotationIcall = &Get;
        NativeRuntime.SetWorldRotationIcall = &Set;
        NativeRuntime.ComponentExistsIcall = &Exists;
        try
        {
            _exists = true;
            var entity = new Entity(12, new EntityId(34, 56));
            var probe = new Probe { Entity = entity };
            Component component = probe;
            Transform target = entity.Transform;
            Check(ReferenceEquals(component.transform, target) && ReferenceEquals(probe.Transform, target),
                "Both aliases must resolve the same entity Transform.");
            _rotation = Quaternion.Identity;
            probe.Turn(90);
            Near(target.Forward, Vector3.Right, "Positive Y quarter turn must face +X.");
            Check(_owner == (12UL, 34UL, 56UL), "Rotation must retain world and entity identity.");

            foreach (float magnitude in new[] { 7f, float.MaxValue, float.Epsilon })
            {
                _rotation = Quaternion.Identity;
                target.Rotate(new Vector3(0, magnitude, 0), -90);
                Near(target.Forward, -Vector3.Right, "Finite nonunit axes must normalize without overflow/underflow.");
            }
            _rotation = Quaternion.Identity;
            target.Rotate(Vector3.Up, 450);
            Near(target.Forward, Vector3.Right, "Angles are degrees and wrap full revolutions.");
            target.Rotate(Vector3.Up, 0);
            Near(target.Forward, Vector3.Right, "Zero degrees preserves orientation.");
            target.Rotate(Vector3.Up, float.MaxValue);
            Check(float.IsFinite(_rotation.W), "Every finite angle must produce a finite rotation.");

            _rotation = Quaternion.Euler(90, 0);
            target.Rotate(Vector3.Up, 90);
            Near(target.Forward, Vector3.Right, "Default rotation composes around object-local axes.");
            _rotation = Quaternion.Euler(90, 0);
            target.Rotate(Vector3.Up, 90, worldSpace: true);
            Near(target.Forward, -Vector3.Up, "World rotation premultiplies the current orientation.");
            _rotation = Quaternion.Euler(90, 0);
            target.Rotate(Quaternion.Euler(0, 90), worldSpace: true);
            Near(target.Forward, -Vector3.Up, "Existing quaternion overload remains compatible.");

            int reads = _reads;
            int writes = _writes;
            Quaternion before = _rotation;
            Reject(() => target.Rotate(Vector3.Zero, 0), "axis");
            Reject(() => target.Rotate(new Vector3(float.NaN, 0, 0), 90), "axis");
            Reject(() => target.Rotate(new Vector3(0, float.PositiveInfinity, 0), 90), "axis");
            foreach (float angle in new[] { float.NaN, float.PositiveInfinity, float.NegativeInfinity })
                Reject(() => target.Rotate(Vector3.Up, angle), "angleDegrees");
            Check(_reads == reads && _writes == writes && _rotation == before,
                "Invalid arguments must reject before native reads or writes, preserving rotation.");
            _exists = false;
            try
            {
                _ = probe.transform;
                throw new Exception("A stale entity must not return a cached Transform through the alias.");
            }
            catch (InvalidOperationException) { }
        }
        finally
        {
            NativeRuntime.GetWorldRotationIcall = get;
            NativeRuntime.SetWorldRotationIcall = set;
            NativeRuntime.ComponentExistsIcall = exists;
        }
    }

    [UnmanagedCallersOnly]
    private static Quaternion Get(ulong world, ulong high, ulong low)
    {
        ++_reads;
        _owner = (world, high, low);
        return _rotation;
    }
    [UnmanagedCallersOnly]
    private static void Set(ulong world, ulong high, ulong low, Quaternion value)
    {
        ++_writes;
        _owner = (world, high, low);
        _rotation = value;
    }
    [UnmanagedCallersOnly]
    private static byte Exists(ulong world, ulong high, ulong low, ulong typeHigh, ulong typeLow) =>
        _exists ? (byte)1 : (byte)0;
    private static void Near(Vector3 actual, Vector3 expected, string message) =>
        Check((actual - expected).Length < 0.00001f, message);
    private static void Reject(Action action, string parameter)
    {
        try { action(); }
        catch (ArgumentException error) when (error.ParamName == parameter) { return; }
        throw new Exception("Expected argument rejection: " + parameter);
    }
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }
}
