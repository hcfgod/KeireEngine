using System.Runtime.InteropServices;
using Keire;

internal static unsafe class FixedPresentationTests
{
    private static byte _enabled;
    private static bool _reject;
    private static (ulong World, ulong High, ulong Low) _owner;
    private sealed class IkProbe : Behaviour
    {
        public AnimationIkContext Last;
        protected override void OnAnimatorIk(AnimationIkContext context) => Last = context;
    }

    internal static void Run()
    {
        var get = NativeRuntime.GetFixedPresentationInterpolationIcall;
        var set = NativeRuntime.SetFixedPresentationInterpolationIcall;
        NativeRuntime.GetFixedPresentationInterpolationIcall = &Get;
        NativeRuntime.SetFixedPresentationInterpolationIcall = &Set;
        try
        {
            var transform = new Transform(new Entity(12, new EntityId(34, 56)));
            transform.FixedPresentationInterpolation = true;
            Check(transform.FixedPresentationInterpolation && _enabled == 1, "Opt-in must round trip as a native byte.");
            Check(_owner == (12UL, 34UL, 56UL), "Presentation setting must retain entity identity.");
            transform.FixedPresentationInterpolation = false;
            Check(!transform.FixedPresentationInterpolation, "Disabling must round trip.");
            _reject = true;
            try
            {
                transform.FixedPresentationInterpolation = true;
                throw new Exception("Rejected native mutation must not be reported as success.");
            }
            catch (InvalidOperationException) { }
            finally { _reject = false; }
            Check(!transform.FixedPresentationInterpolation, "Rejected opt-in leaves the existing flag intact.");
            var probe = new IkProbe();
            probe.RuntimeAnimatorIkEvaluation(.5f, .25f, 0);
            Check(probe.Last.LayerWeight == .5f && probe.Last.InterpolationAlpha == .25f && !probe.Last.IsFixedUpdate,
                "Presentation IK context must preserve weight and fractional alpha.");
            probe.RuntimeAnimatorIkEvaluation(1, 1, 1);
            Check(probe.Last.InterpolationAlpha == 1 && probe.Last.IsFixedUpdate, "Fixed IK evaluation must be explicit.");
            probe.RuntimeAnimatorIk(.75f);
            Check(probe.Last.LayerWeight == .75f && probe.Last.InterpolationAlpha == 1 && !probe.Last.IsFixedUpdate,
                "Legacy IK entrypoint and context constructor remain compatible.");
            NativeRuntime.GetFixedPresentationInterpolationIcall = null;
            try
            {
                _ = transform.FixedPresentationInterpolation;
                throw new Exception("Unbound getter must reject safely.");
            }
            catch (InvalidOperationException) { }
        }
        finally
        {
            NativeRuntime.GetFixedPresentationInterpolationIcall = get;
            NativeRuntime.SetFixedPresentationInterpolationIcall = set;
        }
    }

    [UnmanagedCallersOnly]
    private static byte Get(ulong world, ulong high, ulong low)
    {
        _owner = (world, high, low);
        return _enabled;
    }
    [UnmanagedCallersOnly]
    private static byte Set(ulong world, ulong high, ulong low, byte enabled)
    {
        if (_reject) return 0;
        _owner = (world, high, low);
        _enabled = enabled;
        return 1;
    }
    private static void Check(bool value, string message)
    {
        if (!value) throw new Exception(message);
    }
}
