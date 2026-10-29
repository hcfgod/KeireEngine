using Keire;
using System.Runtime.InteropServices;

internal static unsafe class PrecisePositionTests
{
    private static Vector3d _nativePosition;
    private static byte _space;
    private static bool _reject;
    private static int _setCalls;
    private static Vector3 _legacyPosition;

    internal static void Run()
    {
        Vector3d origin = new(1e15, -1e15, 6371000.0);
        Vector3d position = new(1e15 + 0.125, -1e15 - 0.25, 6371000.001);
        Vector3 relative = position.RelativeFloat(origin, 1e-9);
        if (relative.X != 0.125f || relative.Y != -0.25f || Math.Abs(relative.Z - 0.001) > 1e-9)
        {
            throw new InvalidOperationException("Subtracting the origin must preserve local detail.");
        }

        Reject<ArgumentOutOfRangeException>(() => new Vector3d(16777217, 0, 0).RelativeFloat(default, 0.5));
        if (new Vector3d(16777217, 0, 0).RelativeFloat(default, 1).X != 16777216f)
        {
            throw new InvalidOperationException("A conversion within the explicit error budget should succeed.");
        }

        Reject<ArgumentException>(() => position.RelativeFloat(origin, -1));
        Reject<ArgumentException>(() => position.RelativeFloat(origin, double.NaN));
        Reject<ArgumentException>(() => new Vector3d(double.PositiveInfinity, 0, 0).RelativeFloat(default, 0));
        Reject<ArgumentOutOfRangeException>(() => new Vector3d(double.MaxValue, 0, 0)
            .RelativeFloat(new Vector3d(-double.MaxValue, 0, 0), 0));
        Reject<ArgumentOutOfRangeException>(() => new Vector3d(1e40, 0, 0).RelativeFloat(default, 0));

        RoundTripState(position, origin);
        RoundTripTransformBridge(position, origin);
        RejectInvalidLegacyPositions();
        VerifyPreciseContacts();
        VerifyPreciseQueries();
    }

    private static int _queryStatus = 1;
    private static uint _queryKind;

    private static void VerifyPreciseQueries()
    {
        if (sizeof(NativePrecisePhysicsQuery) != 128 || sizeof(NativePrecisePhysicsHit) != 56 ||
            Marshal.OffsetOf<NativePrecisePhysicsQuery>(nameof(NativePrecisePhysicsQuery.Origin)).ToInt32() != 40 ||
            Marshal.OffsetOf<NativePrecisePhysicsHit>(nameof(NativePrecisePhysicsHit.Point)).ToInt32() != 16)
        {
            throw new InvalidOperationException("Precise physics ABI layout changed.");
        }
        var previous = NativeRuntime.PrecisePhysicsQueryV1Icall;
        NativeRuntime.PrecisePhysicsQueryV1Icall = &PreciseQuery;
        var context = new Entity(12, new EntityId(34, 56));
        Vector3d point = new(1e15 + 0.125, -1e15 + 0.25, 3);
        try
        {
            _queryStatus = 1;
            _queryKind = 0;
            if (!Physics.TryRaycastPrecise(context, point, new Vector3(0, -2, 0), out var ray) ||
                ray.Point != point || ray.Entity.World != 12 || ray.Entity.Id != new EntityId(78, 90))
            {
                throw new InvalidOperationException("Precise raycast lost position or world identity.");
            }
            _queryKind = 1;
            if (!Physics.TryCapsuleCastPrecise(context, point, new Quaternion(0, 0, 0, 1), 0.5f, 1,
                new Vector3(0, -2, 0), out var capsule) || capsule.Point != point)
            {
                throw new InvalidOperationException("Precise capsule cast lost its position.");
            }
            _queryKind = 2;
            var overlap = Physics.OverlapSpherePrecise(context, point, 1);
            if (overlap.Count != 1 || overlap[0].Id != new EntityId(78, 90) || overlap[0].World != 12)
            {
                throw new InvalidOperationException("Precise overlap lost entity identity.");
            }
            _queryKind = 0;
            _queryStatus = 0;
            if (Physics.TryRaycastPrecise(context, point, new Vector3(0, -1, 0), out _))
            {
                throw new InvalidOperationException("A precise query miss became a hit.");
            }
            _queryStatus = -1;
            Reject<InvalidOperationException>(() => Physics.TryRaycastPrecise(context, point, new Vector3(0, -1, 0), out _));
            Reject<ArgumentException>(() => Physics.TryRaycastPrecise(context, new Vector3d(double.NaN, 0, 0),
                new Vector3(0, -1, 0), out _));
            Reject<ArgumentException>(() => Physics.TryRaycastPrecise(context, point, new Vector3(float.MaxValue, 0, 0), out _));
            Reject<ArgumentException>(() => Physics.OverlapSpherePrecise(context, point, 1,
                ignoredEntity: new Entity(99, new EntityId(78, 90))));
            NativeRuntime.PrecisePhysicsQueryV1Icall = null;
            Reject<InvalidOperationException>(() => Physics.OverlapSpherePrecise(context, point, 1));
        }
        finally
        {
            NativeRuntime.PrecisePhysicsQueryV1Icall = previous;
        }
    }

    [UnmanagedCallersOnly]
    private static int PreciseQuery(NativePrecisePhysicsQuery* query, int querySize,
        NativePrecisePhysicsHit* hit, int hitSize, NativeEntityId* entities, int capacity)
    {
        if (query == null || querySize != 128 || hitSize != 56 || query->Kind != _queryKind ||
            query->World != 12 || query->ContextHigh != 34 || query->ContextLow != 56 ||
            query->Origin != new Vector3d(1e15 + 0.125, -1e15 + 0.25, 3))
        {
            return -1;
        }
        if (_queryStatus <= 0)
        {
            return _queryStatus;
        }
        if (query->Kind == 2)
        {
            if (entities != null && capacity >= 1)
            {
                entities[0] = new NativeEntityId { High = 78, Low = 90 };
            }
            return 1;
        }
        if (hit == null)
        {
            return -1;
        }
        *hit = new NativePrecisePhysicsHit
        {
            EntityHigh = 78, EntityLow = 90, Point = query->Origin,
            Normal = new Vector3(0, 1, 0), Distance = 2
        };
        return 1;
    }
    private static void VerifyPreciseContacts()
    {
        var behaviour = new ContactProbe();
        behaviour.RuntimeAttach(12, 34, 56);
        Vector3 point = new(1, 2, 3);
        Vector3d precise = new(1e15 + 0.125, -1e15 + 0.25, 3);
        for (byte trigger = 0; trigger < 2; ++trigger)
        {
            for (byte phase = 0; phase < 3; ++phase)
            {
                behaviour.RuntimePhysicsContactPrecise(phase, trigger, 78, 90, point, new Vector3(0, 1, 0), 4, precise);
                if (behaviour.Contact.PointPrecise != precise || behaviour.Contact.Point != point ||
                    behaviour.Contact.Trigger != (trigger != 0) || behaviour.Phase != phase)
                {
                    throw new InvalidOperationException("Contact callbacks must preserve precise and legacy points.");
                }
            }
        }
        behaviour.RuntimePhysicsContact(0, 0, 78, 90, point, new Vector3(0, 1, 0), 4);
        if (behaviour.Contact.PointPrecise != Vector3d.Widen(point))
        {
            throw new InvalidOperationException("Legacy contact entry points must widen their supplied point.");
        }
        var legacy = new CollisionContact(new Entity(12, new EntityId(78, 90)), point, default, 0, false);
        if (legacy.PointPrecise != Vector3d.Widen(point))
        {
            throw new InvalidOperationException("Legacy contact construction must initialize its precise point.");
        }
        behaviour.RuntimeDestroy();
    }

    [StableComponentId("293b782b-d39b-49d3-bc7f-65be0f54c357")]
    private sealed class ContactProbe : Behaviour
    {
        internal CollisionContact Contact;
        internal byte Phase;

        private void Capture(CollisionContact contact, byte phase)
        {
            Contact = contact;
            Phase = phase;
        }

        protected override void OnCollisionEnter(CollisionContact contact) => Capture(contact, 0);

        protected override void OnCollisionStay(CollisionContact contact) => Capture(contact, 1);

        protected override void OnCollisionExit(CollisionContact contact) => Capture(contact, 2);

        protected override void OnTriggerEnter(CollisionContact contact) => Capture(contact, 0);

        protected override void OnTriggerStay(CollisionContact contact) => Capture(contact, 1);

        protected override void OnTriggerExit(CollisionContact contact) => Capture(contact, 2);
    }

    private static void RejectInvalidLegacyPositions()
    {
        var previousLocal = NativeRuntime.GetLocalPositionIcall;
        var previousWorld = NativeRuntime.GetWorldPositionIcall;
        var previousPresentation = NativeRuntime.GetPresentationWorldPositionIcall;
        NativeRuntime.GetLocalPositionIcall = &GetLegacyPosition;
        NativeRuntime.GetWorldPositionIcall = &GetLegacyPosition;
        NativeRuntime.GetPresentationWorldPositionIcall = &GetLegacyPosition;
        var transform = new Transform(new Entity(12, new EntityId(34, 56)));
        try
        {
            _legacyPosition = new Vector3(1, 2, 3);
            if (transform.LocalPosition != _legacyPosition || transform.Position != _legacyPosition ||
                transform.PresentationPosition != _legacyPosition)
            {
                throw new InvalidOperationException("Finite legacy positions must remain compatible.");
            }
            foreach (Vector3 invalid in new[]
                     {
                         new Vector3(float.NaN, 0, 0), new Vector3(0, float.PositiveInfinity, 0),
                         new Vector3(0, 0, float.NegativeInfinity),
                     })
            {
                _legacyPosition = invalid;
                Reject<InvalidOperationException>(() => _ = transform.LocalPosition);
                Reject<InvalidOperationException>(() => _ = transform.Position);
                Reject<InvalidOperationException>(() => _ = transform.PresentationPosition);
            }
        }
        finally
        {
            NativeRuntime.GetLocalPositionIcall = previousLocal;
            NativeRuntime.GetWorldPositionIcall = previousWorld;
            NativeRuntime.GetPresentationWorldPositionIcall = previousPresentation;
        }
    }

    [UnmanagedCallersOnly]
    private static Vector3 GetLegacyPosition(ulong world, ulong high, ulong low) => _legacyPosition;

    private static void RoundTripTransformBridge(Vector3d position, Vector3d origin)
    {
        if (sizeof(Vector3d) != 24 || Marshal.SizeOf<Vector3d>() != 24)
        {
            throw new InvalidOperationException("Precise position ABI must contain three consecutive doubles.");
        }
        var previousGet = NativeRuntime.GetPrecisePositionV1Icall;
        var previousSet = NativeRuntime.SetPrecisePositionV1Icall;
        var previousRotation = NativeRuntime.GetWorldRotationIcall;
        NativeRuntime.GetPrecisePositionV1Icall = &GetPosition;
        NativeRuntime.SetPrecisePositionV1Icall = &SetPosition;
        NativeRuntime.GetWorldRotationIcall = &GetRotation;
        _reject = false;
        _setCalls = 0;
        var transform = new Transform(new Entity(12, new EntityId(34, 56)));
        try
        {
            transform.LocalPositionPrecise = position;
            if (_space != 0 || transform.LocalPositionPrecise != position)
            {
                throw new InvalidOperationException("Precise local position must preserve channels and owner identity.");
            }
            transform.PositionPrecise = origin;
            if (_space != 1 || transform.PositionPrecise != origin)
            {
                throw new InvalidOperationException("Precise world position must select the world-space bridge.");
            }

            transform.Translate(new Vector3(0.25f, -0.5f, 1.0f), true);
            if (transform.PositionPrecise != new Vector3d(origin.X + 0.25, origin.Y - 0.5, origin.Z + 1.0))
            {
                throw new InvalidOperationException("Translate must not narrow the absolute world position.");
            }
            // The fixture rotates 180 degrees about Y, so local +X moves along world -X.
            transform.Translate(new Vector3(0.25f, 0, 0));
            if (transform.PositionPrecise != new Vector3d(origin.X, origin.Y - 0.5, origin.Z + 1.0))
            {
                throw new InvalidOperationException("Local Translate must preserve rotation and precise position.");
            }
            transform.PositionPrecise = origin;

            int callsBeforeInvalidInput = _setCalls;
            Reject<ArgumentException>(() => transform.PositionPrecise = new Vector3d(double.NaN, 0, 0));
            Reject<ArgumentException>(() => transform.Translate(new Vector3(float.NaN, 0, 0)));
            if (_setCalls != callsBeforeInvalidInput)
            {
                throw new InvalidOperationException("Invalid coordinates must be rejected before calling native code.");
            }

            _reject = true;
            Reject<InvalidOperationException>(() => _ = transform.PositionPrecise);
            Reject<InvalidOperationException>(() => transform.PositionPrecise = position);
            if (_nativePosition != origin)
            {
                throw new InvalidOperationException("A rejected position update must preserve the prior coordinates.");
            }
            _reject = false;
            _nativePosition = new Vector3d(double.PositiveInfinity, 0, 0);
            Reject<InvalidOperationException>(() => _ = transform.PositionPrecise);

            NativeRuntime.GetPrecisePositionV1Icall = null;
            NativeRuntime.SetPrecisePositionV1Icall = null;
            Reject<NotSupportedException>(() => _ = transform.PositionPrecise);
            Reject<NotSupportedException>(() => transform.PositionPrecise = position);
        }
        finally
        {
            NativeRuntime.GetPrecisePositionV1Icall = previousGet;
            NativeRuntime.SetPrecisePositionV1Icall = previousSet;
            NativeRuntime.GetWorldRotationIcall = previousRotation;
            _reject = false;
        }
    }

    [UnmanagedCallersOnly]
    private static Quaternion GetRotation(ulong world, ulong high, ulong low) => new(0, 1, 0, 0);

    [UnmanagedCallersOnly]
    private static byte GetPosition(ulong world, ulong high, ulong low, byte worldSpace, Vector3d* value, uint size)
    {
        if (_reject || (world, high, low) != (12UL, 34UL, 56UL) || worldSpace != _space || size != 24 || value == null)
        {
            return 0;
        }
        double* channels = (double*)value;
        channels[0] = _nativePosition.X;
        channels[1] = _nativePosition.Y;
        channels[2] = _nativePosition.Z;
        return 1;
    }

    [UnmanagedCallersOnly]
    private static byte SetPosition(ulong world, ulong high, ulong low, byte worldSpace, Vector3d* value, uint size)
    {
        ++_setCalls;
        if (_reject || (world, high, low) != (12UL, 34UL, 56UL) || worldSpace > 1 || size != 24 || value == null)
        {
            return 0;
        }
        double* channels = (double*)value;
        _nativePosition = new Vector3d(channels[0], channels[1], channels[2]);
        _space = worldSpace;
        return 1;
    }

    private static void RoundTripState(Vector3d position, Vector3d origin)
    {
        var source = new PrecisePositionProbe
        {
            Position = position,
            Route = [origin, position],
            SharedRoute = [position, origin],
        };
        string state = ManagedStateSerializer.Capture(source, string.Empty, false);
        if (state.Contains("IsFinite", StringComparison.Ordinal))
        {
            throw new InvalidOperationException("Precise position state must contain coordinates only.");
        }

        var restored = new PrecisePositionProbe();
        string warnings = ManagedStateSerializer.Restore(restored, state, false);
        if (!string.IsNullOrEmpty(warnings) || restored.Position != position ||
            !restored.Route.SequenceEqual(source.Route) || !restored.SharedRoute.SequenceEqual(source.SharedRoute))
        {
            throw new InvalidOperationException(
                $"Precise state round-trip failed. Warnings: {warnings}; position: {restored.Position}; " +
                $"route: {string.Join(';', restored.Route)}; shared route: {string.Join(';', restored.SharedRoute)}");
        }
    }

    private static void Reject<TException>(Action action) where TException : Exception
    {
        try
        {
            action();
        }
        catch (TException)
        {
            return;
        }

        throw new InvalidOperationException($"Expected {typeof(TException).Name}.");
    }
}

[StableComponentId("c930b8be-e0bd-4db2-b918-5aa189d46c21")]
internal sealed class PrecisePositionProbe : Behaviour
{
    [StableFieldId("c930b8be-e0bd-4db2-b918-5aa189d46c22")]
    public Vector3d Position;

    [StableFieldId("c930b8be-e0bd-4db2-b918-5aa189d46c23")]
    public Vector3d[] Route = [];

    [SerializeReference]
    [StableFieldId("c930b8be-e0bd-4db2-b918-5aa189d46c24")]
    public Vector3d[] SharedRoute = [];
}
