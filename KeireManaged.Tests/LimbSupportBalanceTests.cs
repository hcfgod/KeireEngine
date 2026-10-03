using Keire;

internal static class LimbSupportBalanceTests
{
    internal static void Run()
    {
        static LimbSupportBalanceResult Evaluate(LimbSupportPoint[] points, Vector3 body = default, float margin = 0) =>
            LimbSupportBalance.Evaluate(points, body, Vector3.Zero, Vector3.Up, Vector3.Up, margin);
        static LimbSupportPoint Contact(uint id, float x, float z) => new(new LimbId(id), new(x, 0, z));
        var rectangle = new[] { Contact(1, -1, -1), Contact(2, 1, -1), Contact(3, 1, 1), Contact(4, -1, 1) };
        Check(Evaluate(rectangle).IsSupported, "Four contacts surround the balance point.");
        Near(Evaluate(rectangle).MinimumEdgeClearance, 1, "Unit rectangle clearance.");
        Check(Evaluate(rectangle, new(0, 20, 0), 1).IsSupported, "Height projects away and the exact margin is inclusive.");
        Check(!Evaluate(rectangle, default, 1.001f).IsSupported, "Margin cannot exceed available clearance.");
        Check(Evaluate(rectangle, new(1, 0, 0)).IsSupported, "Boundary is admitted with zero margin.");
        Check(!Evaluate(rectangle, new(1, 0, 0), .001f).IsSupported, "Positive margin excludes boundary.");
        Check(Evaluate(rectangle, new(2, 0, 0)).MinimumEdgeClearance < 0, "Outside projection reports negative clearance.");
        Check(Evaluate(Array.Empty<LimbSupportPoint>()).Status == LimbSupportBalanceStatus.InsufficientSupportArea,
            "No support is not stable.");
        Check(Evaluate(rectangle[..1]).HullVertexCount == 1, "One point has no area.");
        Check(Evaluate(rectangle[..2]).Status == LimbSupportBalanceStatus.InsufficientSupportArea,
            "Two point-feet cannot invent sole area.");
        var lost = rectangle[..3];
        Check(!Evaluate(lost, new(-.8f, 0, .8f)).IsSupported, "Losing one support excludes its corner.");
        Check(Evaluate(new[] { Contact(1, -1, 0), Contact(2, 0, 0), Contact(3, 1, 0), Contact(4, 2, 0) }).
            Status == LimbSupportBalanceStatus.InsufficientSupportArea, "Collinear contacts have no polygon.");
        Check(Evaluate(new[] { Contact(1, 0, 0), Contact(2, 0, 0), Contact(3, 0, 0) }).HullVertexCount == 1,
            "Coincident positions are deduplicated while limb identity remains distinct.");
        foreach (int count in new[] { 4, 6, 8, 64 })
        {
            var ring = new LimbSupportPoint[count];
            for (int i = 0; i < count; ++i)
            {
                float angle = 2 * MathF.PI * i / count;
                ring[i] = Contact((uint)i + 1, MathF.Cos(angle), MathF.Sin(angle));
            }
            var result = Evaluate(ring, default, .6f);
            Check(result.IsSupported && result.HullVertexCount == count, "Arbitrary limb count forms its actual hull.");
            Near(result.MinimumEdgeClearance, Math.Cos(Math.PI / count), "Regular polygon analytical clearance.");
            Array.Reverse(ring);
            Near(Evaluate(ring).MinimumEdgeClearance, result.MinimumEdgeClearance, "Input order cannot change support.");
            foreach (var rotation in new[] { Quaternion.Euler(25, 40, 15), Quaternion.Euler(90, 0, 0), Quaternion.Euler(0, 0, 180) })
            {
                var translated = new Vector3(4, 7, -2);
                var rotated = ring.Select(point => point with { Position = translated + rotation * point.Position }).ToArray();
                var up = rotation * Vector3.Up;
                var rotatedResult = LimbSupportBalance.Evaluate(rotated, translated + up * 2, translated, up, up, .6f);
                Check(rotatedResult.IsSupported, "Global coordinate rotation preserves support including inverted world up.");
                Near(rotatedResult.MinimumEdgeClearance, result.MinimumEdgeClearance, "Rigid transforms preserve clearance.");
            }
        }
        var slope = rectangle.Select(point => point with { Position = new(point.Position.X, point.Position.X, point.Position.Z) }).ToArray();
        Check(LimbSupportBalance.Evaluate(slope, new(0, 10, 0), Vector3.Zero, new(-1, 1, 0), Vector3.Up, .9f).IsSupported,
            "Slope projects along gravity, not along its normal.");
        Check(!LimbSupportBalance.Evaluate(slope, new(1.5f, 10, 0), Vector3.Zero, new(-1, 1, 0), Vector3.Up).IsSupported,
            "Gravity projection outside slope is rejected.");
        Near(LimbSupportBalance.Evaluate(rectangle, default, default, new(0, 100, 0), new(0, .01f, 0)).MinimumEdgeClearance,
            1, "Directions need not arrive normalized.");
        Reject(() => Evaluate(new[] { Contact(1, 0, 0), Contact(1, 1, 1) }), "Duplicate IDs rejected.");
        Reject(() => Evaluate(new[] { Contact(0, 0, 0) }), "Zero IDs rejected.");
        Reject(() => Evaluate(new LimbSupportPoint[65]), "Contact bound enforced.");
        foreach (float invalid in new[] { float.NaN, float.PositiveInfinity, float.NegativeInfinity })
        {
            Reject(() => Evaluate(rectangle, new(invalid, 0, 0)), "Invalid body rejected.");
            Reject(() => Evaluate(new[] { Contact(1, invalid, 0) }), "Invalid contact rejected.");
            Reject(() => Evaluate(rectangle, default, invalid), "Invalid margin rejected.");
            Reject(() => LimbSupportBalance.Evaluate(rectangle, default, new(0, invalid, 0), Vector3.Up, Vector3.Up),
                "Invalid plane origin rejected.");
            Reject(() => LimbSupportBalance.Evaluate(rectangle, default, default, new(0, invalid, 0), Vector3.Up),
                "Invalid plane normal rejected.");
            Reject(() => LimbSupportBalance.Evaluate(rectangle, default, default, Vector3.Up, new(0, invalid, 0)),
                "Invalid up rejected.");
        }
        Reject(() => Evaluate(rectangle, default, -1), "Negative margin rejected.");
        Reject(() => LimbSupportBalance.Evaluate(rectangle, default, default, default, Vector3.Up), "Zero normal rejected.");
        Reject(() => LimbSupportBalance.Evaluate(rectangle, default, default, Vector3.Up, default), "Zero up rejected.");
        Reject(() => LimbSupportBalance.Evaluate(rectangle, default, default, Vector3.Up, new(1, 0, 0)), "Parallel projection rejected.");
        Reject(() => LimbSupportBalance.Evaluate(rectangle, default, default, Vector3.Up, -Vector3.Up), "Opposed plane/up rejected.");
        var copy = rectangle.ToArray();
        Evaluate(rectangle);
        Check(rectangle.SequenceEqual(copy), "Caller contacts are not mutated.");
        // Warmup isolates the actual synchronous evaluation from JIT and caller allocations.
        for (int i = 0; i < 100; ++i) Evaluate(rectangle);
        long before = GC.GetAllocatedBytesForCurrentThread();
        for (int i = 0; i < 100; ++i) Evaluate(rectangle);
        Check(GC.GetAllocatedBytesForCurrentThread() == before, "Steady-state evaluation allocates no managed memory.");
    }

    private static void Check(bool value, string message)
    {
        if (!value) throw new InvalidOperationException(message);
    }
    private static void Near(double actual, double expected, string message) => Check(Math.Abs(actual - expected) < .00001, message);
    private static void Reject(Action action, string message)
    {
        try { action(); }
        catch (ArgumentException) { return; }
        throw new InvalidOperationException(message);
    }
}
