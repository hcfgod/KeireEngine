using System.Runtime.InteropServices;
using Keire;

internal static unsafe class BoundLimbBridgeTests
{
    private static int _calls;
    private static byte _accepted = 1;
    private static (ulong, ulong, ulong, uint) _identity;
    private static Vector3 _target;
    private static Vector3 _pole;
    private static float _weight;
    private static byte _space;
    private static byte _enabled;

    internal static void Run()
    {
        var set = NativeRuntime.SetAnimatorLimbIkIcall;
        var clear = NativeRuntime.ClearAnimatorLimbIkIcall;
        var get = NativeRuntime.GetAnimatorLimbResultIcall;
        NativeRuntime.SetAnimatorLimbIkIcall = &Set;
        NativeRuntime.ClearAnimatorLimbIkIcall = &Clear;
        NativeRuntime.GetAnimatorLimbResultIcall = &Get;
        _calls = 0;
        _accepted = 1;
        try
        {
            var animator = new Animator(new Entity(8, new(9, 10)));
            foreach (var space in new[] { AnimatorIkSpace.Model, AnimatorIkSpace.World, AnimatorIkSpace.PresentationWorld })
            {
                animator.SetLimbIK(new(7), new(1, 2, 3), new(4, 5, 6), .4f, space, false);
                Check(_identity == (8UL, 9UL, 10UL, 7U), "Bridge changed entity or stable limb ID.");
                Check(_target == new Vector3(1, 2, 3) && _pole == new Vector3(4, 5, 6) &&
                      _weight == .4f && _space == (byte)space && _enabled == 0, "Bridge changed limb target settings.");
            }
            int before = _calls;
            Reject(() => animator.SetLimbIK(default, default, default));
            Reject(() => animator.SetLimbIK(new(7), new(float.NaN, 0, 0), default));
            Reject(() => animator.SetLimbIK(new(7), default, new(float.PositiveInfinity, 0, 0)));
            Reject(() => animator.SetLimbIK(new(7), default, default, 2));
            Reject(() => animator.SetLimbIK(new(7), default, default, 1, (AnimatorIkSpace)255));
            Check(_calls == before, "Rejected inputs reached native code.");
            Check(animator.TryGetLimbIKResult(new(7), out var result), "Published result missing.");
            Check(result.Id == new LimbId(7) && result.Status == LimbIkSolveStatus.JointLimited && result.JointLimited &&
                  result.EndPosition == new Vector3(1, 2, 3) && result.PositionError == .25f && result.ReachError == 0,
                  "Bridge lost constrained result diagnostics.");
            Check(animator.ClearLimbIK(new(7)), "Accepted clear did not succeed.");
            _accepted = 0;
            Check(!animator.TryGetLimbIKResult(new(7), out result) && result == default, "Missing result retained stale values.");
            Check(!animator.ClearLimbIK(new(7)), "Missing target clear should return false.");
            try { animator.SetLimbIK(new(7), default, default); throw new Exception("Native rejection was ignored."); }
            catch (InvalidOperationException) { }
            NativeRuntime.SetAnimatorLimbIkIcall = null;
            try { animator.SetLimbIK(new(7), default, default); throw new Exception("Missing bridge was ignored."); }
            catch (NotSupportedException) { }
        }
        finally
        {
            NativeRuntime.SetAnimatorLimbIkIcall = set;
            NativeRuntime.ClearAnimatorLimbIkIcall = clear;
            NativeRuntime.GetAnimatorLimbResultIcall = get;
        }
    }

    private static void Reject(Action action)
    {
        try { action(); throw new Exception("Expected argument rejection."); }
        catch (ArgumentException) { }
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    [UnmanagedCallersOnly]
    private static byte Set(ulong world, ulong high, ulong low, uint id, Vector3 target, Vector3 pole,
                            float weight, byte space, byte enabled)
    {
        ++_calls;
        _identity = (world, high, low, id);
        _target = target; _pole = pole; _weight = weight; _space = space; _enabled = enabled;
        return _accepted;
    }

    [UnmanagedCallersOnly]
    private static byte Clear(ulong world, ulong high, ulong low, uint id) => _accepted;

    [UnmanagedCallersOnly]
    private static byte Get(ulong world, ulong high, ulong low, uint id, Vector3* endpoint,
                            float* positionError, float* reachError, byte* status, byte* limited)
    {
        *endpoint = new(1, 2, 3); *positionError = .25f; *reachError = 0;
        *status = (byte)LimbIkSolveStatus.JointLimited; *limited = 1;
        return _accepted;
    }
}
