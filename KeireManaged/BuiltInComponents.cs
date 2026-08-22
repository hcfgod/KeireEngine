namespace Keire;

public enum ColliderShape : byte
{
    Box,
    Sphere,
    Capsule,
    ConvexMesh,
    TriangleMesh
}

[StableAssetTypeId("4b454952-4550-4859-534d-415445520001")]
public sealed class PhysicsMaterial : Asset;

[StableComponentId("4b454952-4543-4f4c-4c49-444552000001")]
public sealed class Collider : Component
{
    internal Collider(Entity entity) : base(entity) { }

    public ColliderShape Shape { get => (ColliderShape)GetBuiltinInteger("shape"); set => SetBuiltinInteger("shape", (long)value); }
    public Vector3 Center { get => GetBuiltinVector3("center"); set => SetBuiltinVector3("center", value); }
    public Vector3 HalfExtent { get => GetBuiltinVector3("halfExtent"); set => SetBuiltinVector3("halfExtent", value); }
    public float Radius { get => GetBuiltinScalar("radius"); set => SetBuiltinScalar("radius", value); }
    public float Height { get => GetBuiltinScalar("height"); set => SetBuiltinScalar("height", value); }
    public Mesh? CollisionMesh { get => GetBuiltinAsset<Mesh>("collisionMesh"); set => SetBuiltinAsset("collisionMesh", value); }
    public PhysicsMaterial? Material { get => GetBuiltinAsset<PhysicsMaterial>("physicsMaterial"); set => SetBuiltinAsset("physicsMaterial", value); }
    public uint CollisionMask { get => checked((uint)GetBuiltinInteger("mask")); set => SetBuiltinInteger("mask", value); }
    public bool IsTrigger { get => GetBuiltinBoolean("trigger"); set => SetBuiltinBoolean("trigger", value); }
}

public enum ReflectionProbeCaptureMode : byte { Baked, OnDemand }
public enum ReflectionProbeResolution : ushort { Size64 = 64, Size128 = 128, Size256 = 256, Size512 = 512 }

[StableComponentId("4b454952-4552-4546-4c50-524f42450001")]
public sealed class ReflectionProbe : Component
{
    internal ReflectionProbe(Entity entity) : base(entity) { }

    public ReflectionProbeCaptureMode CaptureMode { get => (ReflectionProbeCaptureMode)GetBuiltinInteger("captureMode"); set => SetBuiltinInteger("captureMode", (long)value); }
    public ReflectionProbeResolution Resolution { get => (ReflectionProbeResolution)GetBuiltinInteger("resolution"); set => SetBuiltinInteger("resolution", (long)value); }
    public Vector3 BoxExtents { get => GetBuiltinVector3("boxExtents"); set => SetBuiltinVector3("boxExtents", value); }
    public float BlendDistance { get => GetBuiltinScalar("blendDistance"); set => SetBuiltinScalar("blendDistance", value); }
    public int Importance { get => checked((int)GetBuiltinInteger("importance")); set => SetBuiltinInteger("importance", value); }
    public float Intensity { get => GetBuiltinScalar("intensity"); set => SetBuiltinScalar("intensity", value); }
    public bool BoxProjection { get => GetBuiltinBoolean("boxProjection"); set => SetBuiltinBoolean("boxProjection", value); }
    public bool IncludeSky { get => GetBuiltinBoolean("includeSky"); set => SetBuiltinBoolean("includeSky", value); }
}

[StableComponentId("4b454952-454c-5056-4f4c-554d45000001")]
public sealed class LightProbeVolume : Component
{
    internal LightProbeVolume(Entity entity) : base(entity) { }

    public Vector3 BoxExtents { get => GetBuiltinVector3("boxExtents"); set => SetBuiltinVector3("boxExtents", value); }
    public Vector3 Spacing { get => GetBuiltinVector3("spacing"); set => SetBuiltinVector3("spacing", value); }
    public int Priority { get => checked((int)GetBuiltinInteger("priority")); set => SetBuiltinInteger("priority", value); }
    public float NormalBias { get => GetBuiltinScalar("normalBias"); set => SetBuiltinScalar("normalBias", value); }
    public float ViewBias { get => GetBuiltinScalar("viewBias"); set => SetBuiltinScalar("viewBias", value); }
}

public abstract class Joint : Component
{
    internal Joint(Entity entity) : base(entity) { }

    public Entity? ConnectedEntity { get => GetBuiltinEntity("connectedEntity"); set => SetBuiltinEntity("connectedEntity", value); }
    public Vector3 LocalAnchor { get => GetBuiltinVector3("localAnchor"); set => SetBuiltinVector3("localAnchor", value); }
    public Vector3 ConnectedAnchor { get => GetBuiltinVector3("connectedAnchor"); set => SetBuiltinVector3("connectedAnchor", value); }
    public float BreakForce { get => GetBuiltinScalar("breakForce"); set => SetBuiltinScalar("breakForce", value); }
    public float BreakTorque { get => GetBuiltinScalar("breakTorque"); set => SetBuiltinScalar("breakTorque", value); }
    public bool EnableCollision { get => GetBuiltinBoolean("enableCollision"); set => SetBuiltinBoolean("enableCollision", value); }
}

[StableComponentId("4b454952-4546-4958-4544-4a4f494e5401")]
public sealed class FixedJoint : Joint
{
    internal FixedJoint(Entity entity) : base(entity) { }
}

[StableComponentId("4b454952-4548-494e-4745-4a4f494e5401")]
public sealed class HingeJoint : Joint
{
    internal HingeJoint(Entity entity) : base(entity) { }

    public Vector3 Axis { get => GetBuiltinVector3("axis"); set => SetBuiltinVector3("axis", value); }
    public bool LimitsEnabled { get => GetBuiltinBoolean("limitsEnabled"); set => SetBuiltinBoolean("limitsEnabled", value); }
    public float LowerLimit { get => GetBuiltinScalar("lowerLimit"); set => SetBuiltinScalar("lowerLimit", value); }
    public float UpperLimit { get => GetBuiltinScalar("upperLimit"); set => SetBuiltinScalar("upperLimit", value); }
    public bool MotorEnabled { get => GetBuiltinBoolean("motorEnabled"); set => SetBuiltinBoolean("motorEnabled", value); }
    public float MotorSpeed { get => GetBuiltinScalar("motorSpeed"); set => SetBuiltinScalar("motorSpeed", value); }
    public float MaximumMotorTorque { get => GetBuiltinScalar("maximumMotorTorque"); set => SetBuiltinScalar("maximumMotorTorque", value); }
}

[StableComponentId("4b454952-4544-4953-544a-4f494e540001")]
public sealed class DistanceJoint : Joint
{
    internal DistanceJoint(Entity entity) : base(entity) { }

    public float MinimumDistance { get => GetBuiltinScalar("minimumDistance"); set => SetBuiltinScalar("minimumDistance", value); }
    public float MaximumDistance { get => GetBuiltinScalar("maximumDistance"); set => SetBuiltinScalar("maximumDistance", value); }
}

[StableComponentId("4b454952-4553-5052-494e-474a4f494e01")]
public sealed class SpringJoint : Joint
{
    internal SpringJoint(Entity entity) : base(entity) { }

    public float RestLength { get => GetBuiltinScalar("restLength"); set => SetBuiltinScalar("restLength", value); }
    public float Stiffness { get => GetBuiltinScalar("stiffness"); set => SetBuiltinScalar("stiffness", value); }
    public float Damping { get => GetBuiltinScalar("damping"); set => SetBuiltinScalar("damping", value); }
}
