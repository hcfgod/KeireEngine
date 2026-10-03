namespace Keire;

/// <summary>A currently supporting contact. Omit swinging, lost, and unconfirmed contacts.</summary>
public readonly record struct LimbSupportPoint(LimbId Limb, Vector3 Position);

public enum LimbSupportBalanceStatus
{
    InsufficientSupportArea,
    OutsideSafetyMargin,
    Supported
}

/// <summary>Geometric support assessment; it does not model friction, forces, or dynamic balance.</summary>
public readonly record struct LimbSupportBalanceResult(
    LimbSupportBalanceStatus Status, int HullVertexCount, double MinimumEdgeClearance, float SafetyMargin)
{
    public bool IsSupported => Status == LimbSupportBalanceStatus.Supported;
}

/// <summary>Pure support-polygon assessment for arbitrary limb layouts in a caller-defined support plane.</summary>
public static class LimbSupportBalance
{
    public const int MaximumContacts = 64;

    /// <summary>
    /// Projects contacts and the caller's balance point along upDirection onto the support plane.
    /// The balance point may be an authored approximation or an actual center of mass. Plane normal
    /// and upDirection need not be normalized, but must point into the same hemisphere and must not
    /// be nearly perpendicular. All positions and distances use the same coordinate space and units.
    /// At least three noncollinear points are necessary; two point-feet have no support area.
    /// MinimumEdgeClearance is the minimum inward signed distance to any hull edge's infinite line,
    /// not Euclidean distance to the polygon when outside. Degenerate support returns zero clearance.
    /// The evaluation allocates no managed memory and never modifies contacts.
    /// </summary>
    public static LimbSupportBalanceResult Evaluate(ReadOnlySpan<LimbSupportPoint> contacts,
        Vector3 bodyPosition, Vector3 planeOrigin, Vector3 planeNormal, Vector3 upDirection,
        float safetyMargin = 0)
    {
        if (contacts.Length > MaximumContacts)
            throw new ArgumentOutOfRangeException(nameof(contacts), "At most 64 contacts are supported.");
        ValidateFinite(bodyPosition, nameof(bodyPosition));
        ValidateFinite(planeOrigin, nameof(planeOrigin));
        ValidateFinite(planeNormal, nameof(planeNormal));
        ValidateFinite(upDirection, nameof(upDirection));
        if (!float.IsFinite(safetyMargin) || safetyMargin < 0)
            throw new ArgumentOutOfRangeException(nameof(safetyMargin));
        var normal = Double3.Normalize(planeNormal, nameof(planeNormal));
        var up = Double3.Normalize(upDirection, nameof(upDirection));
        double alignment = Double3.Dot(normal, up);
        if (alignment < .0001)
            throw new ArgumentException("Up must point away from the plane and not run along it.", nameof(upDirection));
        var reference = Math.Abs(normal.X) < .8 ? new Double3(1, 0, 0) : new Double3(0, 1, 0);
        var axisX = Double3.Unit(Double3.Cross(reference, normal));
        var axisY = Double3.Cross(normal, axisX);
        Span<Point> points = stackalloc Point[MaximumContacts];
        for (int index = 0; index < contacts.Length; ++index)
        {
            ValidateFinite(contacts[index].Position, nameof(contacts));
            if (contacts[index].Limb.Value == 0)
                throw new ArgumentException("Each contact needs a nonzero limb identifier.", nameof(contacts));
            for (int prior = 0; prior < index; ++prior)
                if (contacts[prior].Limb == contacts[index].Limb)
                    throw new ArgumentException("Limb identifiers must be unique.", nameof(contacts));
            points[index] = Project(contacts[index].Position, planeOrigin, normal, up, alignment, axisX, axisY);
        }
        // A bounded insertion sort keeps ordering and storage deterministic without a comparer allocation.
        for (int index = 1; index < contacts.Length; ++index)
        {
            Point value = points[index];
            int destination = index;
            while (destination > 0 && Before(value, points[destination - 1]))
            {
                points[destination] = points[destination - 1];
                --destination;
            }
            points[destination] = value;
        }
        int uniqueCount = 0;
        for (int index = 0; index < contacts.Length; ++index)
            if (uniqueCount == 0 || points[index] != points[uniqueCount - 1])
                points[uniqueCount++] = points[index];
        if (uniqueCount < 3)
            return new(LimbSupportBalanceStatus.InsufficientSupportArea, uniqueCount, 0, safetyMargin);
        Span<Point> hull = stackalloc Point[MaximumContacts * 2];
        int count = 0;
        for (int index = 0; index < uniqueCount; ++index)
        {
            while (count >= 2 && Cross(hull[count - 2], hull[count - 1], points[index]) <= 0) --count;
            hull[count++] = points[index];
        }
        int upperStart = count + 1;
        for (int index = uniqueCount - 2; index >= 0; --index)
        {
            while (count >= upperStart && Cross(hull[count - 2], hull[count - 1], points[index]) <= 0) --count;
            hull[count++] = points[index];
        }
        --count; // Last vertex repeats the first.
        if (count < 3)
            return new(LimbSupportBalanceStatus.InsufficientSupportArea, count, 0, safetyMargin);
        Point body = Project(bodyPosition, planeOrigin, normal, up, alignment, axisX, axisY);
        double clearance = double.PositiveInfinity;
        for (int index = 0; index < count; ++index)
        {
            Point start = hull[index], end = hull[(index + 1) % count];
            double x = end.X - start.X, y = end.Y - start.Y;
            clearance = Math.Min(clearance, Cross(start, end, body) / Math.Sqrt(x * x + y * y));
        }
        return new(clearance >= safetyMargin ? LimbSupportBalanceStatus.Supported :
            LimbSupportBalanceStatus.OutsideSafetyMargin, count, clearance, safetyMargin);
    }

    private static Point Project(Vector3 position, Vector3 origin, Double3 normal, Double3 up,
        double alignment, Double3 axisX, Double3 axisY)
    {
        var relative = new Double3((double)position.X - origin.X, (double)position.Y - origin.Y,
            (double)position.Z - origin.Z);
        double distance = Double3.Dot(relative, normal) / alignment;
        var projected = new Double3(relative.X - up.X * distance, relative.Y - up.Y * distance,
            relative.Z - up.Z * distance);
        return new(Double3.Dot(projected, axisX), Double3.Dot(projected, axisY));
    }

    private static bool Before(Point left, Point right) => left.X < right.X || (left.X == right.X && left.Y < right.Y);
    private static double Cross(Point origin, Point a, Point b) =>
        (a.X - origin.X) * (b.Y - origin.Y) - (a.Y - origin.Y) * (b.X - origin.X);
    private static void ValidateFinite(Vector3 value, string name)
    {
        if (!float.IsFinite(value.X) || !float.IsFinite(value.Y) || !float.IsFinite(value.Z))
            throw new ArgumentOutOfRangeException(name, "Positions and directions must be finite.");
    }
    private readonly record struct Point(double X, double Y);
    private readonly record struct Double3(double X, double Y, double Z)
    {
        internal static double Dot(Double3 a, Double3 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;
        internal static Double3 Cross(Double3 a, Double3 b) =>
            new(a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X);
        internal static Double3 Unit(Double3 value)
        {
            double length = Math.Sqrt(Dot(value, value));
            return new(value.X / length, value.Y / length, value.Z / length);
        }
        internal static Double3 Normalize(Vector3 value, string name)
        {
            var converted = new Double3(value.X, value.Y, value.Z);
            if (Dot(converted, converted) == 0) throw new ArgumentException("Direction must be nonzero.", name);
            return Unit(converted);
        }
    }
}
