using System.Runtime.InteropServices;
using Keire;

internal static unsafe class LimbIkRigTests
{
    private static int _calls;
    private static string _goal = "";
    private static string _bones = "";
    private static byte _result = 1;
    private static Vector3 _target;
    private static byte _space;
    private static float _weight;

    internal static void Run()
    {
        var two = NativeRuntime.SetAnimatorTwoBoneIkIcall;
        var fabrik = NativeRuntime.SetAnimatorFabrikIkIcall;
        var clear = NativeRuntime.ClearAnimatorIkIcall;
        NativeRuntime.SetAnimatorTwoBoneIkIcall = &TwoBone;
        NativeRuntime.SetAnimatorFabrikIkIcall = &Fabrik;
        NativeRuntime.ClearAnimatorIkIcall = &Clear;
        _calls = 0;
        _result = 1;
        try
        {
            var animator = new Animator(new Entity(1, new(2, 3)));
            var mutable = new[] { "Root", "Middle", "Tip" };
            var definition = new LimbIkDefinition(new(1), "Front left", mutable, LimbIkSolver.TwoBone);
            mutable[0] = "Changed";
            var definitions = new[] { definition, new LimbIkDefinition(new(2), "Front right", new[] { "A", "B", "C", "D" }) };
            var rig = new LimbIkRig(animator, "Spider", definitions);
            definitions[0] = definitions[1];
            Check(rig.Limbs[0].Id == new LimbId(1) && definition.Bones[0] == "Root", "Binding aliases caller collections.");
            foreach (var space in new[] { AnimatorIkSpace.Model, AnimatorIkSpace.World, AnimatorIkSpace.PresentationWorld })
            {
                rig.SetTarget(new(1), new(new(1, 2, 3), Vector3.Forward, .5f, space));
                Check(_goal == "Spider/1" && _bones == "Root|Middle|Tip", "Two-bone binding lost names/ID.");
                Check(_target == new Vector3(1, 2, 3) && _space == (byte)space && _weight == .5f, "Target settings changed.");
                rig.SetTarget(new(2), new(new(4, 5, 6), default, 0, space));
                Check(_goal == "Spider/2" && _bones == "A|B|C|D" && _weight == 0, "FABRIK binding changed.");
            }
            Check(_calls == 6, "Each target must submit once.");
            Reject(() => rig.SetTarget(new(3), default));
            Reject(() => rig.SetTarget(new(1), new(new(float.NaN, 0, 0), default)));
            Reject(() => rig.SetTarget(new(1), new(default, default, 2)));
            Reject(() => rig.SetTarget(new(1), new(default, default, 1, (AnimatorIkSpace)255)));
            Reject(() => rig.SetTarget(new(1), new(default, new(float.NaN, 0, 0))));
            Reject(() => new LimbIkRig(animator, "Rig", new[] { definition, definition }));
            Reject(() => new LimbIkRig(animator, new string('x', 256), new[] { definition }));
            Reject(() => new LimbIkDefinition(default, "Leg", new[] { "A", "B" }));
            Reject(() => new LimbIkDefinition(new(3), "Leg", new[] { "A", "A" }));
            Reject(() => new LimbIkDefinition(new(3), "Leg", new[] { "A", "B" }, LimbIkSolver.TwoBone));
            Reject(() => new LimbIkDefinition(new(3), "Leg", new[] { "A", "B" }, (LimbIkSolver)255));
            Reject(() => new LimbIkDefinition(new(3), "Leg", new[] { "A", "B" }, maximumIterations: 0));
            Reject(() => new LimbIkDefinition(new(3), "Leg", new[] { "A", "B" }, tolerance: float.NaN));
            rig.ClearAll();
            Check(_calls == 8 && _goal == "Spider/2", "ClearAll must touch only registered limb goals.");
            _result = 0;
            Check(!rig.Clear(new(1)), "Missing goal removal must return false.");
            int before = _calls;
            try { rig.SetTarget(new(1), new(default, Vector3.Up)); throw new Exception("Expected native rejection."); }
            catch (InvalidOperationException) { }
            Check(_calls == before + 1, "Native rejection retried submission.");
        }
        finally
        {
            NativeRuntime.SetAnimatorTwoBoneIkIcall = two;
            NativeRuntime.SetAnimatorFabrikIkIcall = fabrik;
            NativeRuntime.ClearAnimatorIkIcall = clear;
        }
    }

    private static void Reject(Action action)
    {
        int before = _calls;
        try { action(); throw new Exception("Expected argument rejection."); }
        catch (ArgumentException) { }
        Check(_calls == before, "Invalid managed inputs reached native mutation.");
    }
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }
    private static string Read(NativeString value) => Marshal.PtrToStringAuto(*(IntPtr*)&value) ?? "";
    [UnmanagedCallersOnly]
    private static byte TwoBone(ulong world, ulong high, ulong low, NativeString goal, NativeString root,
                                NativeString middle, NativeString end, Vector3 target, Vector3 pole, float weight, byte space)
    {
        ++_calls;
        _goal = Read(goal);
        _bones = Read(root) + "|" + Read(middle) + "|" + Read(end);
        _target = target; _weight = weight; _space = space;
        return _result;
    }
    [UnmanagedCallersOnly]
    private static byte Fabrik(ulong world, ulong high, ulong low, NativeString goal, NativeString bones,
                               Vector3 target, float weight, uint iterations, float tolerance, byte space)
    {
        ++_calls;
        _goal = Read(goal);
        _bones = Read(bones).Replace('\u001f', '|');
        _target = target; _weight = weight; _space = space;
        return _result;
    }
    [UnmanagedCallersOnly]
    private static byte Clear(ulong world, ulong high, ulong low, NativeString goal)
    {
        ++_calls; _goal = Read(goal); return _result;
    }
}
