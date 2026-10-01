using System.Runtime.InteropServices;
using Keire;

internal static unsafe class AnimatorIkTests
{
    private static int _calls;
    private static Vector3 _target;
    private static byte _result;
    private static (ulong World, ulong High, ulong Low) _entity;
    private static Vector3 _pole;
    private static float _weight;
    private static byte _space;
    private static uint _iterations;
    private static float _tolerance;
    private static string _goal = string.Empty;
    private static string[] _bones = Array.Empty<string>();

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
            void Chain(IReadOnlyList<string>? bones = null, uint iterations = 12, float tolerance = 0.001f,
                       Vector3 target = default, float weight = 1, AnimatorIkSpace space = AnimatorIkSpace.World) =>
                AnimatorApi.SetFabrikIK(entity, "Hand", bones ?? new[] { "Root", "End" }, target,
                                       weight, iterations, tolerance, space);

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
                foreach (var vector in new[] { new Vector3(invalid, 0, 0), new Vector3(0, invalid, 0),
                                               new Vector3(0, 0, invalid) })
                {
                    Reject(() => Two(target: vector), "target");
                    Reject(() => Two(pole: vector), "pole");
                    Reject(() => Chain(target: vector), "target");
                }
                Reject(() => Chain(weight: invalid), "weight");
                Reject(() => Two(weight: invalid), "weight");
                Reject(() => Chain(tolerance: invalid), "tolerance");
            }
            Reject(() => Two(weight: -0.01f), "weight");
            Reject(() => Two(weight: 1.01f), "weight");
            Reject(() => Two(space: (AnimatorIkSpace)255), "space");
            Reject(() => Chain(weight: -0.01f), "weight");
            Reject(() => Chain(weight: 1.01f), "weight");
            Reject(() => Chain(space: (AnimatorIkSpace)255), "space");
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
            foreach (var space in new[] { AnimatorIkSpace.Model, AnimatorIkSpace.World, AnimatorIkSpace.PresentationWorld })
            {
                var target = new Vector3(-7.5f, 2.25f, 11);
                var pole = new Vector3(3, -4, 5);
                Two(target: target, pole: pole, weight: 0.375f, space: space);
                Check(_entity == (123UL, 4UL, 5UL), "TwoBone must preserve the owning entity identity.");
                Check(_target == target && _pole == pole && _weight == 0.375f && _space == (byte)space,
                      "TwoBone must preserve target, pole, blend weight and coordinate space.");
                Chain(target: target, weight: 0.625f, iterations: 57, tolerance: 0.0025f, space: space);
                Check(_entity == (123UL, 4UL, 5UL), "FABRIK must preserve the owning entity identity.");
                Check(_target == target && _weight == 0.625f && _space == (byte)space &&
                      _iterations == 57 && _tolerance == 0.0025f,
                      "FABRIK must preserve its target, blend weight, coordinate space and convergence settings.");
            }
            var unicodeGoal = "Main gauche \u00e9\U0001f590";
            var unicodeBones = new[] { "\u9aa8\u76c6", "\u00c9paule", "Main\U0001f590" };
            AnimatorApi.SetTwoBoneIK(entity, unicodeGoal, unicodeBones[0], unicodeBones[1], unicodeBones[2],
                                    default, default);
            Check(_goal == unicodeGoal && _bones.SequenceEqual(unicodeBones),
                  "TwoBone must preserve Unicode goal and bone names across the native boundary.");
            var longChain = Enumerable.Range(0, 256).Select(i => $"Bone {i} \u00e9").ToArray();
            AnimatorApi.SetFabrikIK(entity, unicodeGoal, longChain, default);
            Check(_goal == unicodeGoal && _bones.SequenceEqual(longChain),
                  "FABRIK must preserve every bone name and its order in a maximum-length chain.");
            Check(AnimatorApi.ClearIK(entity, unicodeGoal), "Unicode goals must be removable.");
            Check(_goal == unicodeGoal && _entity == (123UL, 4UL, 5UL),
                  "Goal removal must preserve the name and owning entity identity.");
            Check(AnimatorApi.ClearIK(entity, "Hand"), "Native removal success must be returned.");
            _result = 0;
            Check(!AnimatorApi.ClearIK(entity, "Hand"), "An absent goal must return false.");
            foreach (Action submit in new Action[] { () => Two(), () => Chain() })
            {
                int before = _calls;
                try
                {
                    submit();
                    throw new Exception("Native rejection must remain an InvalidOperationException.");
                }
                catch (InvalidOperationException) { }
                Check(_calls == before + 1, "Native rejection must not retry or duplicate a goal submission.");
            }
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
        _entity = (world, high, low);
        _target = target;
        _goal = Read(goal);
        _bones = new[] { Read(root), Read(middle), Read(end) };
        _pole = pole;
        _weight = weight;
        _space = space;
        return _result;
    }

    [UnmanagedCallersOnly]
    private static byte Fabrik(ulong world, ulong high, ulong low, NativeString goal, NativeString bones,
                               Vector3 target, float weight, uint iterations, float tolerance, byte space)
    {
        ++_calls;
        _entity = (world, high, low);
        _target = target;
        _weight = weight;
        _space = space;
        _goal = Read(goal);
        _bones = Read(bones).Split('\u001f');
        _iterations = iterations;
        _tolerance = tolerance;
        return _result;
    }

    [UnmanagedCallersOnly]
    private static byte Clear(ulong world, ulong high, ulong low, NativeString goal)
    {
        ++_calls;
        _entity = (world, high, low);
        _goal = Read(goal);
        return _result;
    }

    private static string Read(NativeString value) => Marshal.PtrToStringAuto(*(IntPtr*)&value) ?? string.Empty;
}
