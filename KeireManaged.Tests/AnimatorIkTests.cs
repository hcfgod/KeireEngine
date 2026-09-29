using System.Runtime.InteropServices;
using Keire;

internal static unsafe class AnimatorIkTests
{
    private static int _calls;
    private static Vector3 _target;
    private static byte _result;

    internal static void Run()
    {
        var previousTwoBone = NativeRuntime.SetAnimatorTwoBoneIkIcall;
        var previousFabrik = NativeRuntime.SetAnimatorFabrikIkIcall;
        var previousClear = NativeRuntime.ClearAnimatorIkIcall;
        NativeRuntime.SetAnimatorTwoBoneIkIcall = &TwoBone;
        NativeRuntime.SetAnimatorFabrikIkIcall = &Fabrik;
        NativeRuntime.ClearAnimatorIkIcall = &Clear;
        var entity = new Entity(123, new EntityId(4, 5));
        _calls = 0;
        _result = 1;
        try
        {
            void Two(string goal = "Hand", string root = "Root", Vector3 target = default,
                     Vector3 pole = default, float weight = 1, AnimatorIkSpace space = AnimatorIkSpace.World) =>
                AnimatorApi.SetTwoBoneIK(entity, goal, root, "Middle", "End", target, pole, weight, space);
            void Chain(IReadOnlyList<string>? bones = null, uint iterations = 12, float tolerance = 0.001f) =>
                AnimatorApi.SetFabrikIK(entity, "Hand", bones ?? new[] { "Root", "End" }, default,
                                       maximumIterations: iterations, tolerance: tolerance);

            foreach (string invalid in new[] { "", " ", "bad\0name", new string('é', 129) })
            {
                Reject(() => Two(goal: invalid), "goal");
                Reject(() => Two(root: invalid), "rootBone");
                Reject(() => Chain(new[] { "Root", invalid }), "bones");
                Reject(() => AnimatorApi.ClearIK(entity, invalid), "goal");
            }
            Reject(() => Two(goal: null!), "goal");
            Reject(() => AnimatorApi.SetFabrikIK(entity, "Hand", null!, default), "bones");
            Reject(() => Chain(Array.Empty<string>()), "bones");
            Reject(() => Chain(new[] { "Root" }), "bones");
            Reject(() => Chain(Enumerable.Repeat("Bone", 257).ToArray()), "bones");
            Reject(() => Chain(new[] { "Root", "End\u001fOther" }), "bones");
            foreach (float invalid in new[] { float.NaN, float.PositiveInfinity, float.NegativeInfinity })
            {
                Reject(() => Two(target: new Vector3(invalid, 0, 0)), "target");
                Reject(() => Two(pole: new Vector3(0, invalid, 0)), "pole");
                Reject(() => Two(weight: invalid), "weight");
                Reject(() => Chain(tolerance: invalid), "tolerance");
            }
            Reject(() => Two(weight: -0.01f), "weight");
            Reject(() => Two(weight: 1.01f), "weight");
            Reject(() => Two(space: (AnimatorIkSpace)255), "space");
            Reject(() => Chain(iterations: 0), "maximumIterations");
            Reject(() => Chain(iterations: 1025), "maximumIterations");
            Reject(() => Chain(tolerance: 0), "tolerance");
            Reject(() => Chain(tolerance: -1), "tolerance");

            Two(goal: new string('é', 128), weight: 0, space: AnimatorIkSpace.Model);
            for (int i = 0; i < 24; ++i)
            {
                var target = new Vector3(i, -i, i * 0.5f);
                Two(target: target);
                Check(_target == target, "Moving IK targets must reach the native bridge unchanged.");
            }
            Chain(Enumerable.Repeat("Bone", 256).ToArray(), iterations: 1024, tolerance: float.Epsilon);
            Check(_calls == 26, "Every accepted update must be submitted exactly once.");
            Check(AnimatorApi.ClearIK(entity, "Hand"), "Native removal success must be returned.");
            _result = 0;
            Check(!AnimatorApi.ClearIK(entity, "Hand"), "An absent goal must return false.");
            try
            {
                Two();
                throw new Exception("Native rejection must remain an InvalidOperationException.");
            }
            catch (InvalidOperationException) { }
        }
        finally
        {
            NativeRuntime.SetAnimatorTwoBoneIkIcall = previousTwoBone;
            NativeRuntime.SetAnimatorFabrikIkIcall = previousFabrik;
            NativeRuntime.ClearAnimatorIkIcall = previousClear;
        }
    }

    private static void Reject(Action action, string parameter)
    {
        int before = _calls;
        try
        {
            action();
            throw new Exception($"Expected an argument error for {parameter}.");
        }
        catch (ArgumentException error)
        {
            Check(error.ParamName == parameter, $"Expected parameter {parameter}, got {error.ParamName}.");
            Check(_calls == before, "Invalid arguments must not modify native IK goals.");
        }
    }

    private static void Check(bool condition, string message)
    {
        if (!condition)
            throw new Exception(message);
    }

    [UnmanagedCallersOnly]
    private static byte TwoBone(ulong world, ulong high, ulong low, NativeString goal, NativeString root,
                                NativeString middle, NativeString end, Vector3 target, Vector3 pole,
                                float weight, byte space)
    {
        ++_calls;
        _target = target;
        return _result;
    }

    [UnmanagedCallersOnly]
    private static byte Fabrik(ulong world, ulong high, ulong low, NativeString goal, NativeString bones,
                               Vector3 target, float weight, uint iterations, float tolerance, byte space)
    {
        ++_calls;
        return _result;
    }

    [UnmanagedCallersOnly]
    private static byte Clear(ulong world, ulong high, ulong low, NativeString goal)
    {
        ++_calls;
        return _result;
    }
}
