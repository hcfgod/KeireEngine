using Keire;

internal static class LimbContactTests
{
    internal static void Run()
    {
        var limits = LimbSupportContinuity.Default;
        var identity = new LimbSupportPose(default, Quaternion.Identity);
        var support = new EntityId(1, 2);
        var tracker = new LimbContactTracker(new(7));
        var events = new List<LimbContactEvent>();
        tracker.Changed += value =>
        {
            Check(value.Limb == new LimbId(7), "Events must preserve stable limb identity.");
            Check(tracker.IsPlanted == (value.Kind is LimbContactEventKind.Planted or LimbContactEventKind.Recovered),
                  "Callbacks must observe committed state.");
            events.Add(value);
        };
        tracker.Plant(support, identity, new(1, 0, 0), Vector3.Up);
        Check(events.Count == 1 && events[0].Kind == LimbContactEventKind.Planted, "Plant event missing.");
        Reject(() => tracker.Plant(support, identity, default, Vector3.Up));
        Check(tracker.Position == new Vector3(1, 0, 0) && events.Count == 1, "Rejected replant changed state.");
        var moved = new LimbSupportPose(new(0, .2f, 0), Quaternion.Euler(0, 30));
        Check(tracker.UpdateSupport(support, moved, limits), "Ordinary rotation/translation should transport.");
        Near(tracker.Position, moved.Position + moved.Rotation * new Vector3(1, 0, 0));
        Check(events.Count == 1, "Stable contact updates must not emit repeated plant events.");
        Check(!tracker.UpdateSupport(support, new(default, Quaternion.Euler(0, 180)), limits), "Rotation teleport accepted.");
        Check(!tracker.IsPlanted && !tracker.Support.IsValid && events[^1].Kind == LimbContactEventKind.SupportLost,
              "A lost support must stop contributing immediately.");
        Check(events[^1].Support == support && events[^1].Reason == LimbSupportLoss.DiscontinuousMotion,
              "Loss event must retain the old support identity and reason.");
        tracker.UpdateSupport(default, default, limits);
        tracker.Lift();
        Check(events.Count == 2, "Repeated removal must be idempotent.");
        tracker.Plant(support, identity, default, Vector3.Up);
        Check(events[^1].Kind == LimbContactEventKind.Recovered, "Fresh admission after loss must report recovery.");
        tracker.Lift();
        Check(events[^1].Kind == LimbContactEventKind.Lifted, "Voluntary lift must not report support loss.");
        tracker.Plant(support, identity, default, Vector3.Up);
        Check(events[^1].Kind == LimbContactEventKind.Planted, "New gait plant must not inherit old recovery state.");
        Check(!tracker.UpdateSupport(new(2, 3), identity, limits), "Switching support must require fresh admission.");
        Check(events[^1].Reason == LimbSupportLoss.MissingOrChangedSupport, "Wrong support-loss reason.");

        foreach (int frequency in new[] { 30, 60, 120 })
        {
            var prior = identity;
            var local = new Vector3(.4f, 0, -.3f);
            for (int tick = 1; tick <= frequency * 20; ++tick)
            {
                float time = (float)tick / frequency;
                var next = new LimbSupportPose(new(MathF.Sin(time), MathF.Sin(time * .4f), 0),
                                               Quaternion.Euler(0, time * 15));
                Check(LimbSupportMotion.TryTransportAnchor(prior, next, local, limits, out var point),
                      "Continuous rise/descent/reversal/rotation was rejected.");
                Near(point, next.Position + next.Rotation * local);
                prior = next;
            }
        }
        Check(!LimbSupportMotion.TryTransportAnchor(identity, new(default, Quaternion.Euler(0, 1)),
                                                     new(1000, 0, 0), limits, out _),
              "Far-pivot anchor motion must obey the translation bound.");
        Check(LimbSupportMotion.TryTransportAnchor(identity, new(default, new(0, 0, 0, -1)), default, limits, out _),
              "Equivalent quaternion signs must not count as rotation.");
        Check(LimbSupportMotion.TryTransportAnchor(identity, new(default, Quaternion.Euler(0, 45)), default, limits, out _),
              "An authored boundary rotation must be admitted within float quaternion roundoff.");
        Check(!LimbSupportMotion.TryTransportAnchor(identity, new(default, Quaternion.Euler(0, 45.001f)), default, limits, out _),
              "Rotation meaningfully beyond the boundary must not be hidden by numerical allowance.");
        Check(!LimbSupportMotion.TryTransportAnchor(identity, new(default, Quaternion.Euler(0, .01f)), default,
              new(2, 0), out _), "Zero angular limit must reject a tiny nonzero pivot rotation.");
        Check(!LimbSupportMotion.TryTransportAnchor(identity, new(default, Quaternion.Euler(0, .02f)), default,
              new(2, .01f), out _), "Small angular bounds must not disappear through dot-product rounding.");
        Check(LimbSupportMotion.TryTransportAnchor(identity, new(default, Quaternion.Euler(0, .01f)), default,
              new(2, .01f), out _), "Small exact-boundary rotation must remain admissible.");
        Check(LimbSupportMotion.TryTransportAnchor(identity, new(default, new(0, 0, 0, -1)), default,
              new(0, 0), out _), "Zero limits still accept an unchanged equivalent pose.");
        var enormous = new LimbSupportPose(new(float.MaxValue, 0, 0), Quaternion.Identity);
        Check(!LimbSupportMotion.TryTransportAnchor(enormous, enormous, new(float.MaxValue, 0, 0),
              new(float.MaxValue, 180), out _), "Finite inputs whose transported anchors overflow must be rejected.");
        Check(!LimbSupportMotion.TryTransportAnchor(identity, identity, new(float.NaN, 0, 0), limits, out _),
              "Invalid local anchor cannot be transported.");
        var overflowPlant = new LimbContactTracker(new(10));
        Reject(() => overflowPlant.Plant(support, enormous, new(-float.MaxValue, 0, 0), Vector3.Up));
        Check(!overflowPlant.IsPlanted && !overflowPlant.Support.IsValid, "Overflowed plant cannot publish partial contact state.");
        foreach (var invalid in new[] { default(LimbSupportPose), new(default, new Quaternion(float.NaN, 0, 0, 1)),
                                        new(new Vector3(float.MaxValue, 0, 0), Quaternion.Identity) })
            Check(!LimbSupportMotion.TryTransportAnchor(identity, invalid, default, limits, out _), "Invalid support accepted.");
        foreach (var invalid in new[] { new LimbSupportContinuity(-1, 45), new(float.NaN, 45), new(2, 181) })
            Reject(() => LimbSupportMotion.TryTransportAnchor(identity, identity, default, invalid, out _));
        Reject(() => new LimbContactTracker(default));
        Reject(() => tracker.Plant(default, identity, default, Vector3.Up));
        Reject(() => tracker.Plant(support, default, default, Vector3.Up));
        Reject(() => tracker.Plant(support, identity, default, default));
        Reject(() => tracker.LoseSupport(LimbSupportLoss.None));
        Check(!tracker.IsPlanted, "Invalid inputs changed an unplanted tracker.");

        var throwing = new LimbContactTracker(new(8));
        throwing.Changed += _ => throw new ApplicationException("callback failure");
        try { throwing.Plant(support, identity, default, Vector3.Up); }
        catch (ApplicationException) { }
        Check(throwing.IsPlanted, "Callback failure must not roll back an already published contact.");
        try { throwing.Lift(); }
        catch (ApplicationException) { }
        Check(!throwing.IsPlanted, "A throwing event must not leave the mutation guard latched.");

        var recursive = new LimbContactTracker(new(9));
        int observed = 0;
        recursive.Changed += value =>
        {
            bool expectedPlanted = value.Kind is LimbContactEventKind.Planted or LimbContactEventKind.Recovered;
            Reject(() => recursive.Lift());
            Reject(() => recursive.LoseSupport(LimbSupportLoss.Unreachable));
            Reject(() => recursive.UpdateSupport(support, identity, limits));
            Reject(() => recursive.Plant(support, identity, default, Vector3.Up));
            Check(recursive.IsPlanted == expectedPlanted, "Rejected reentrant mutations changed published state.");
        };
        recursive.Changed += value =>
        {
            ++observed;
            Check(recursive.IsPlanted == (value.Kind is LimbContactEventKind.Planted or LimbContactEventKind.Recovered),
                "Later event subscribers must see the same committed state.");
        };
        recursive.Plant(support, identity, default, Vector3.Up);
        recursive.LoseSupport(LimbSupportLoss.Unreachable);
        recursive.Plant(support, identity, default, Vector3.Up);
        recursive.Lift();
        Check(observed == 4, "Nested contact changes must not emit extra events.");
    }

    private static void Near(Vector3 a, Vector3 b) => Check((a - b).Length < .0001f, "Rigid anchor mismatch.");
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }
    private static void Reject(Action action)
    {
        try { action(); }
        catch (ArgumentException) { return; }
        catch (InvalidOperationException) { return; }
        throw new Exception("Expected rejection.");
    }
}
