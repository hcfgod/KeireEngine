using Keire;

internal static class LimbGaitSchedulerTests
{
    internal static void Run()
    {
        foreach (int count in new[] { 2, 4, 6, 8, 64 })
        {
            var ids = Enumerable.Range(1, count).Select(index => new LimbId((uint)index * 17)).ToArray();
            var first = ids.Where((_, index) => index % 2 == 0).ToArray();
            var second = ids.Where((_, index) => index % 2 != 0).ToArray();
            var groups = new[] { new LimbGaitGroup(first), new LimbGaitGroup(second) };
            var scheduler = new LimbGaitScheduler(ids, groups, Array.Empty<LimbGaitGroup>(), count / 2);
            var yes = Enumerable.Repeat(true, count).ToArray();
            var output = new LimbId[count];
            scheduler.Initialize(yes);
            for (int step = 0; step < 4; ++step)
            {
                int selected = scheduler.TryBeginStep(yes, yes, output);
                Check(selected == count / 2, "Alternating groups preserve minimum support.");
                Check(output.AsSpan(0, selected).SequenceEqual(step % 2 == 0 ? first : second), "Stable ID order and round robin preserved.");
                Check(scheduler.SupportingCount == count / 2 && scheduler.SwingingCount == count / 2, "Counts reflect committed batch.");
                Check(scheduler.TryBeginStep(yes, yes, output) == 0, "No overlapping group while existing feet swing.");
                foreach (var limb in output.AsSpan(0, selected)) scheduler.CompletePlant(limb);
            }
            scheduler.LoseContact(ids[0]);
            int recovery = scheduler.CopyRecoveryCandidates(output);
            Check(recovery == 1 && output[0] == ids[0], "Missing limb has explicit recovery priority without phantom support.");
            Check(scheduler.TryBeginStep(yes, yes, output) == 0, "Preferred full-contact gait waits for fresh recovery.");
            scheduler.CompletePlant(ids[0]);
            Check(scheduler.SupportingCount == count, "Confirmed recovery restores support.");
            scheduler.CompletePlant(ids[0]);
            Check(scheduler.SupportingCount == count, "Repeated confirmation cannot inflate count.");
            Check(scheduler.CopyRecoveryCandidates(output) == 0, "Recovered contact leaves the recovery list.");
        }
        LimbId[] four = { new(1), new(2), new(3), new(4) };
        bool[] all = { true, true, true, true };
        bool[] request = { true, false, false, false };
        var primary = new LimbGaitGroup(four[0], four[1]);
        var fallback = new LimbGaitGroup(four[0]);
        var gait = new LimbGaitScheduler(four, new[] { primary }, new[] { fallback }, 2,
            new[] { new LimbSupportRequirement(1, four[0], four[1]) });
        var selectedIds = new LimbId[4];
        gait.Initialize(all);
        Check(gait.TryBeginStep(request, all, selectedIds) == 1 && selectedIds[0] == four[0],
            "Region requirement vetoes full-side lift and permits safe fallback.");
        gait.LoseContact(four[0]);
        Check(gait.SwingingCount == 0 && gait.SupportingCount == 3, "Losing swing destination releases its reservation.");
        Check(gait.CopyRecoveryCandidates(selectedIds) == 1 && selectedIds[0] == four[0], "Failed swing goes to recovery.");
        gait.CompletePlant(four[0]);
        Check(gait.TryBeginStep(request, all, selectedIds, new[] { false, false }) == 0, "Caller polygon veto admits no group.");
        Check(gait.SupportingCount == 4, "Veto cannot mutate support.");
        Check(gait.TryBeginStep(new bool[4], all, selectedIds) == 0, "No requested movement keeps all contacts planted.");
        var unsafeLanding = new[] { false, true, true, true };
        Check(gait.TryBeginStep(request, unsafeLanding, selectedIds) == 0, "Unsafe destination is never admitted.");
        Array.Fill(selectedIds, new LimbId(999));
        Reject(() => gait.TryBeginStep(new bool[3], all, selectedIds), "Invalid request length.");
        Reject(() => gait.TryBeginStep(all, new bool[3], selectedIds), "Invalid landing length.");
        Reject(() => gait.TryBeginStep(all, all, new LimbId[1]), "Output too short even if fallback would fit.");
        Reject(() => gait.TryBeginStep(all, all, selectedIds, new bool[1]), "Admission must match group count.");
        Reject(() => gait.Initialize(new bool[3]), "Invalid reset leaves state intact.");
        Reject(() => gait.CompletePlant(new(900)), "Unknown plant rejected.");
        Reject(() => gait.LoseContact(new(900)), "Unknown loss rejected.");
        Reject(() => gait.CopyRecoveryCandidates(new LimbId[3]), "Recovery output must have full capacity.");
        Check(gait.SupportingCount == 4 && gait.SwingingCount == 0 && selectedIds.All(id => id.Value == 999),
            "Rejected operations preserve state and caller output atomically.");
        var mutable = new[] { four[0] };
        var immutableGroup = new LimbGaitGroup(mutable);
        mutable[0] = four[3];
        Check(immutableGroup.Limbs[0] == four[0], "Group snapshots caller configuration.");
        var mutableGroups = new[] { immutableGroup };
        var independent = new LimbGaitScheduler(four, mutableGroups, Array.Empty<LimbGaitGroup>(), 3);
        mutableGroups[0] = new(four[2]);
        independent.Initialize(all);
        Check(independent.TryBeginStep(all, all, selectedIds) == 1 && selectedIds[0] == four[0], "Scheduler snapshots group list.");
        independent.Initialize(all);
        Check(independent.SwingingCount == 0 && independent.SupportingCount == 4, "Explicit reset clears swing reservations.");
        Reject(() => new LimbGaitGroup(), "Empty group rejected.");
        Reject(() => new LimbGaitGroup(new LimbId(0)), "Zero ID rejected.");
        Reject(() => new LimbGaitGroup(four[0], four[0]), "Duplicate member rejected.");
        Reject(() => new LimbGaitScheduler(four, new[] { new LimbGaitGroup(new LimbId(100)) }, Array.Empty<LimbGaitGroup>(), 1),
            "Unknown configured limb rejected.");
        Reject(() => new LimbGaitScheduler(four, new[] { primary }, Array.Empty<LimbGaitGroup>(), 0), "Zero remaining support rejected.");
        Reject(() => new LimbGaitScheduler(four, new[] { primary }, Array.Empty<LimbGaitGroup>(), 5), "Excess support count rejected.");
        Reject(() => new LimbGaitScheduler(four, Enumerable.Repeat(primary, 65).ToArray(), Array.Empty<LimbGaitGroup>(), 1),
            "Group bound enforced.");
        var allLift = new LimbGaitScheduler(four, new[] { new LimbGaitGroup(four) }, Array.Empty<LimbGaitGroup>(), 1);
        allLift.Initialize(all);
        Check(allLift.TryBeginStep(all, all, selectedIds) == 0, "No group may voluntarily lift all supports.");
        // Warmed up stepping and confirmation use no delegates, allocations, or simulation-time accumulation.
        for (int index = 0; index < 100; ++index)
        {
            independent.TryBeginStep(all, all, selectedIds);
            independent.CompletePlant(four[0]);
        }
        long before = GC.GetAllocatedBytesForCurrentThread();
        for (int index = 0; index < 100; ++index)
        {
            independent.TryBeginStep(all, all, selectedIds);
            independent.CompletePlant(four[0]);
        }
        Check(GC.GetAllocatedBytesForCurrentThread() == before, "Steady-state scheduling allocates no managed memory.");
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
    private static void Reject(Action action, string message)
    {
        try { action(); }
        catch (ArgumentException) { return; }
        throw new InvalidOperationException(message);
    }
}
