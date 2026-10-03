namespace Keire;

/// <summary>A constant-scale support's world pose. Invalid/default rotations are rejected by transport.</summary>
public readonly record struct LimbSupportPose(Vector3 Position, Quaternion Rotation);

/// <summary>
/// Per-simulation-tick discontinuity bounds, independent of leg reach. Angular comparison permits one part per
/// million relative rounding error for float quaternion inputs; a zero angular bound allows no rotation.
/// </summary>
public readonly record struct LimbSupportContinuity(float MaximumTranslation, float MaximumRotationDegrees)
{
    public static LimbSupportContinuity Default => new(2, 45);
}

/// <summary>Physics-independent rigid support motion, suitable for feet, hands, or other contact anchors.</summary>
public static class LimbSupportMotion
{
    public static bool TryTransportAnchor(LimbSupportPose previous, LimbSupportPose current, Vector3 localAnchor,
                                          LimbSupportContinuity limits, out Vector3 worldAnchor)
    {
        Validate(limits);
        worldAnchor = default;
        if (!Valid(previous) || !Valid(current) || !Finite(localAnchor))
            return false;
        var a = previous.Rotation.Normalized;
        var b = current.Rotation.Normalized;
        var priorAnchor = previous.Position + a * localAnchor;
        var nextAnchor = current.Position + b * localAnchor;
        // The relative vector retains tiny rotations that disappear when a float dot rounds to one.
        // q and -q describe the same rotation; atan2 with the absolute scalar takes the shortest arc.
        double x = -(double)b.W * a.X + (double)b.X * a.W - (double)b.Y * a.Z + (double)b.Z * a.Y;
        double y = -(double)b.W * a.Y + (double)b.X * a.Z + (double)b.Y * a.W - (double)b.Z * a.X;
        double z = -(double)b.W * a.Z - (double)b.X * a.Y + (double)b.Y * a.X + (double)b.Z * a.W;
        double w = (double)a.X * b.X + (double)a.Y * b.Y + (double)a.Z * b.Z + (double)a.W * b.W;
        double angle = 2 * Math.Atan2(Math.Sqrt(x * x + y * y + z * z), Math.Abs(w)) * 180 / Math.PI;
        double maxSquared = (double)limits.MaximumTranslation * limits.MaximumTranslation;
        if (!Finite(priorAnchor) || !Finite(nextAnchor) ||
            DistanceSquared(previous.Position, current.Position) > maxSquared ||
            DistanceSquared(priorAnchor, nextAnchor) > maxSquared ||
            angle > limits.MaximumRotationDegrees * 1.000001) // Relative float-quaternion rounding; zero remains exact.
            return false;
        worldAnchor = nextAnchor;
        return true;
    }

    internal static bool Valid(LimbSupportPose pose)
    {
        var q = pose.Rotation;
        double length = (double)q.X * q.X + (double)q.Y * q.Y + (double)q.Z * q.Z + (double)q.W * q.W;
        return Finite(pose.Position) && double.IsFinite(length) && Math.Abs(length - 1) <= 0.001;
    }

    internal static bool Finite(Vector3 point) => float.IsFinite(point.X) && float.IsFinite(point.Y) && float.IsFinite(point.Z);

    private static double DistanceSquared(Vector3 a, Vector3 b)
    {
        double x = (double)a.X - b.X, y = (double)a.Y - b.Y, z = (double)a.Z - b.Z;
        return x * x + y * y + z * z;
    }

    internal static void Validate(LimbSupportContinuity limits)
    {
        if (!float.IsFinite(limits.MaximumTranslation) || limits.MaximumTranslation < 0 ||
            !float.IsFinite(limits.MaximumRotationDegrees) || limits.MaximumRotationDegrees < 0 ||
            limits.MaximumRotationDegrees > 180)
            throw new ArgumentOutOfRangeException(nameof(limits), "Support motion bounds must be finite and nonnegative; rotation cannot exceed 180 degrees.");
    }
}

public enum LimbContactEventKind : byte
{
    Planted,
    Lifted,
    SupportLost,
    Recovered
}

public enum LimbSupportLoss : byte
{
    None,
    MissingOrChangedSupport,
    DiscontinuousMotion,
    Unreachable
}

public readonly record struct LimbContactEvent(LimbId Limb, LimbContactEventKind Kind, EntityId Support,
                                               Vector3 Position, Vector3 Normal, LimbSupportLoss Reason);

/// <summary>
/// Per-limb planting state. Call only from the owning simulation thread. Physics queries and reach admission are
/// supplied by the caller; no raycast, gait, or two-foot assumption is embedded here. State commits before events.
/// Event handlers may inspect the tracker but cannot mutate it recursively. Handler exceptions propagate while
/// retaining committed state; the tracker accepts subsequent calls after dispatch unwinds.
/// </summary>
public sealed class LimbContactTracker
{
    private LimbSupportPose _previous;
    private Vector3 _localAnchor;
    private Vector3 _localNormal;
    private bool _recovering;
    private bool _dispatching;
    public LimbId Limb { get; }
    public bool IsPlanted { get; private set; }
    public EntityId Support { get; private set; }
    public Vector3 Position { get; private set; }
    public Vector3 Normal { get; private set; }
    public event Action<LimbContactEvent>? Changed;

    public LimbContactTracker(LimbId limb)
    {
        if (!limb.IsValid)
            throw new ArgumentException("A contact requires a nonzero limb ID.", nameof(limb));
        Limb = limb;
    }

    public void Plant(EntityId support, LimbSupportPose pose, Vector3 worldPosition, Vector3 worldNormal)
    {
        RequireMutationAllowed();
        if (IsPlanted)
            throw new InvalidOperationException("Lift or release the existing contact before planting another.");
        if (!support.IsValid || !LimbSupportMotion.Valid(pose) || !LimbSupportMotion.Finite(worldPosition) ||
            !LimbSupportMotion.Finite(worldNormal) || !float.IsFinite(worldNormal.LengthSquared) || worldNormal.LengthSquared < 0.000001f)
            throw new ArgumentException("Planting requires a valid support, rigid pose, finite point, and nonzero normal.");
        var rotation = pose.Rotation.Normalized;
        var inverse = new Quaternion(-rotation.X, -rotation.Y, -rotation.Z, rotation.W);
        var local = inverse * (worldPosition - pose.Position);
        if (!LimbSupportMotion.Finite(local))
            throw new ArgumentException("Contact coordinates overflow the support's local space.");
        _localAnchor = local;
        _localNormal = inverse * worldNormal.Normalized;
        _previous = pose;
        Position = worldPosition;
        Normal = worldNormal.Normalized;
        Support = support;
        IsPlanted = true;
        var kind = _recovering ? LimbContactEventKind.Recovered : LimbContactEventKind.Planted;
        _recovering = false;
        Notify(new(Limb, kind, Support, Position, Normal, LimbSupportLoss.None));
    }

    public bool UpdateSupport(EntityId support, LimbSupportPose pose, LimbSupportContinuity limits)
    {
        RequireMutationAllowed();
        LimbSupportMotion.Validate(limits);
        if (!IsPlanted)
            return false;
        if (!support.IsValid || support != Support)
            return LoseSupport(LimbSupportLoss.MissingOrChangedSupport);
        if (!LimbSupportMotion.TryTransportAnchor(_previous, pose, _localAnchor, limits, out var anchor))
            return LoseSupport(LimbSupportLoss.DiscontinuousMotion);
        Position = anchor;
        Normal = (pose.Rotation.Normalized * _localNormal).Normalized;
        _previous = pose;
        return true;
    }

    public bool LoseSupport(LimbSupportLoss reason)
    {
        RequireMutationAllowed();
        if (!Enum.IsDefined(reason) || reason == LimbSupportLoss.None)
            throw new ArgumentOutOfRangeException(nameof(reason));
        Release(LimbContactEventKind.SupportLost, reason);
        return false;
    }

    public void Lift()
    {
        RequireMutationAllowed();
        Release(LimbContactEventKind.Lifted, LimbSupportLoss.None);
    }

    private void Release(LimbContactEventKind kind, LimbSupportLoss reason)
    {
        if (!IsPlanted)
            return;
        var previousSupport = Support;
        IsPlanted = false;
        Support = default;
        _previous = default;
        _localAnchor = _localNormal = default;
        _recovering = kind == LimbContactEventKind.SupportLost;
        Notify(new(Limb, kind, previousSupport, Position, Normal, reason));
    }

    private void RequireMutationAllowed()
    {
        if (_dispatching)
            throw new InvalidOperationException("Contact state cannot be changed recursively from a contact event.");
    }

    private void Notify(LimbContactEvent value)
    {
        _dispatching = true;
        try { Changed?.Invoke(value); }
        finally { _dispatching = false; }
    }
}
