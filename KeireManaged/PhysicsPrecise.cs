namespace Keire;

/// <summary>A physics hit whose point remains in double-precision world coordinates.</summary>
public readonly record struct RaycastHitPrecise(Entity Entity, Vector3d Point, Vector3 Normal, float Distance);

public static partial class Physics
{
    public static bool TryRaycastPrecise(Entity context, Vector3d origin, Vector3 direction,
        out RaycastHitPrecise hit, float maximumDistance = 1000.0f, uint mask = uint.MaxValue,
        bool includeTriggers = false, Entity? ignoredEntity = null)
    {
        ValidatePreciseQuery(context, origin, ignoredEntity);
        if (!IsFinite(direction) || !float.IsFinite(direction.LengthSquared) || direction.LengthSquared <= 0.0f)
        {
            throw new ArgumentException("Ray direction must have finite, non-zero length.", nameof(direction));
        }
        if (!float.IsFinite(maximumDistance) || maximumDistance <= 0.0f)
        {
            throw new ArgumentOutOfRangeException(nameof(maximumDistance));
        }
        var query = CreatePreciseQuery(context, origin, mask, includeTriggers, ignoredEntity);
        query.Kind = 0;
        query.Direction = direction.Normalized;
        query.MaximumDistance = maximumDistance;
        return NativeRuntime.TryPrecisePhysicsQuery(context, query, out hit);
    }

    public static bool TryCapsuleCastPrecise(Entity context, Vector3d origin, Quaternion rotation,
        float radius, float height, Vector3 displacement, out RaycastHitPrecise hit,
        uint mask = uint.MaxValue, bool includeTriggers = false, Entity? ignoredEntity = null)
    {
        ValidatePreciseQuery(context, origin, ignoredEntity);
        float rotationLengthSquared = rotation.X * rotation.X + rotation.Y * rotation.Y +
            rotation.Z * rotation.Z + rotation.W * rotation.W;
        if (!IsFinite(rotation) || !float.IsFinite(rotationLengthSquared) || rotationLengthSquared <= 0.000001f)
        {
            throw new ArgumentException("Rotation must have finite, non-zero length.", nameof(rotation));
        }
        if (!float.IsFinite(radius) || radius <= 0.0f)
        {
            throw new ArgumentOutOfRangeException(nameof(radius));
        }
        if (!float.IsFinite(height) || height < radius * 2.0f)
        {
            throw new ArgumentOutOfRangeException(nameof(height));
        }
        if (!IsFinite(displacement) || !float.IsFinite(displacement.LengthSquared) || displacement.LengthSquared <= 0.000001f)
        {
            throw new ArgumentException("Displacement must have finite, non-zero length.", nameof(displacement));
        }
        var query = CreatePreciseQuery(context, origin, mask, includeTriggers, ignoredEntity);
        query.Kind = 1;
        query.Rotation = rotation.Normalized;
        query.Radius = radius;
        query.Height = height;
        query.Displacement = displacement;
        return NativeRuntime.TryPrecisePhysicsQuery(context, query, out hit);
    }

    public static IReadOnlyList<Entity> OverlapSpherePrecise(Entity context, Vector3d center, float radius,
        uint mask = uint.MaxValue, bool includeTriggers = true, Entity? ignoredEntity = null)
    {
        ValidatePreciseQuery(context, center, ignoredEntity);
        if (!float.IsFinite(radius) || radius <= 0.0f)
        {
            throw new ArgumentOutOfRangeException(nameof(radius));
        }
        var query = CreatePreciseQuery(context, center, mask, includeTriggers, ignoredEntity);
        query.Kind = 2;
        query.Radius = radius;
        return NativeRuntime.PrecisePhysicsOverlap(context, query);
    }

    private static void ValidatePreciseQuery(Entity context, Vector3d position, Entity? ignoredEntity)
    {
        ArgumentNullException.ThrowIfNull(context);
        if (!double.IsFinite(position.X) || !double.IsFinite(position.Y) || !double.IsFinite(position.Z))
        {
            throw new ArgumentException("Physics positions must be finite.", nameof(position));
        }
        ValidateIgnoredEntityWorld(context, ignoredEntity);
    }

    private static NativePrecisePhysicsQuery CreatePreciseQuery(Entity context, Vector3d origin, uint mask,
        bool includeTriggers, Entity? ignoredEntity) => new()
    {
        World = context.World,
        ContextHigh = context.Id.High,
        ContextLow = context.Id.Low,
        IgnoredHigh = ignoredEntity?.Id.High ?? 0,
        IgnoredLow = ignoredEntity?.Id.Low ?? 0,
        Origin = origin,
        Mask = mask,
        IncludeTriggers = includeTriggers ? 1U : 0U
    };
}