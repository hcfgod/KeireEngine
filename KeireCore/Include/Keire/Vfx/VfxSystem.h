#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Math/Curves.h"
#include "Keire/Math/Math.h"
#include "Keire/Ref.h"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Keire
{
    /// Controls whether simulated particle positions are relative to the emitter or fixed in world coordinates.
    ///
    /// On both CPU and GPU, Local particles follow later SetTransform calls. World particles keep the world position
    /// at which they were spawned; moving the emitter only changes the origin used by subsequent spawns.
    enum class VfxSimulationSpace : std::uint8_t
    {
        Local,
        World
    };

    /// Spawn-volume algorithms supported by the modular runtime.
    enum class VfxShape : std::uint8_t
    {
        Point,
        Box,
        Sphere,
        Cone,
        Mesh,
        Volume
    };

    /// Collision source requested by an effect. Unsupported backend requests select a documented fallback and
    /// publish a VfxRuntimeDiagnostic.
    enum class VfxCollisionMode : std::uint8_t
    {
        None,
        Cpu,
        GpuDepth,
        ScenePhysics
    };

    /// Render primitive produced by the effect.
    enum class VfxRendererType : std::uint8_t
    {
        Sprite,
        Mesh
    };

    /// Simulation path requested from VfxWorld.
    enum class VfxBackend : std::uint8_t
    {
        Cpu,
        Gpu
    };

    /// Selects the compatibility module stack or the schema-4 Context/Block graph as the authoritative program.
    enum class VfxExecutionSource : std::uint8_t
    {
        LegacyModules,
        Graph
    };

    /// Selects whether a schema-4 Graph enforces native capability errors or preserves historical module behavior with
    /// explicit warnings. Conversion and schema 1-3 migration select MigratedLegacyModules automatically.
    enum class VfxCompatibilityMode : std::uint8_t
    {
        NativeSchema4,
        MigratedLegacyModules
    };

    /// Continuous emission in particles per second.
    struct VfxEmissionRateModule
    {
        float ParticlesPerSecond = 10.0F;

        [[nodiscard]] bool operator==(const VfxEmissionRateModule&) const = default;
    };

    /// A deterministic timed burst. Cycles repeat Count emissions at Interval seconds from Time.
    struct VfxBurstModule
    {
        float Time = 0.0F;
        std::uint32_t Count = 10;
        std::uint32_t Cycles = 1;
        float Interval = 0.1F;

        [[nodiscard]] bool operator==(const VfxBurstModule&) const = default;
    };

    /// Selects and configures the particle spawn volume. Mesh and Volume require VfxWorldSpecification::ShapeSample.
    struct VfxShapeModule
    {
        VfxShape Shape = VfxShape::Point;
        Vector3 BoxHalfExtent{0.5F, 0.5F, 0.5F};
        float Radius = 0.5F;
        float ConeAngleDegrees = 25.0F;
        float ConeLength = 1.0F;
        AssetId Mesh;
        AssetId Volume;

        [[nodiscard]] bool operator==(const VfxShapeModule&) const = default;
    };

    /// Random ranges sampled once when each particle is created.
    struct VfxInitializeModule
    {
        float LifetimeMinimum = 1.0F;
        float LifetimeMaximum = 1.0F;
        Vector3 VelocityMinimum;
        Vector3 VelocityMaximum;
        Vector3 RotationMinimum;
        Vector3 RotationMaximum;

        [[nodiscard]] bool operator==(const VfxInitializeModule&) const = default;
    };

    /// Constant acceleration plus a multiple of engine gravity.
    struct VfxForceModule
    {
        Vector3 Force;
        float GravityMultiplier = 0.0F;

        [[nodiscard]] bool operator==(const VfxForceModule&) const = default;
    };

    /// Particle size evaluated against normalized age.
    struct VfxSizeOverLifetimeModule
    {
        Curve1D Size = Curve1D::Constant(1.0F);

        [[nodiscard]] bool operator==(const VfxSizeOverLifetimeModule&) const = default;
    };

    /// Particle tint evaluated against normalized age.
    struct VfxColorOverLifetimeModule
    {
        ColorGradient Color = ColorGradient::Constant(Keire::Color{});

        [[nodiscard]] bool operator==(const VfxColorOverLifetimeModule&) const = default;
    };

    /// Collision response. KillOnCollision releases the particle instead of reflecting its velocity.
    struct VfxCollisionModule
    {
        VfxCollisionMode Mode = VfxCollisionMode::None;
        float Restitution = 0.5F;
        bool KillOnCollision = false;

        [[nodiscard]] bool operator==(const VfxCollisionModule&) const = default;
    };

    /// Output selection and its optional sprite or mesh asset.
    struct VfxRendererModule
    {
        VfxRendererType Type = VfxRendererType::Sprite;
        AssetId Sprite;
        AssetId Mesh;

        [[nodiscard]] bool operator==(const VfxRendererModule&) const = default;
    };

    using VfxModulePayload =
        std::variant<VfxEmissionRateModule, VfxBurstModule, VfxShapeModule, VfxInitializeModule, VfxForceModule,
                     VfxSizeOverLifetimeModule, VfxColorOverLifetimeModule, VfxCollisionModule, VfxRendererModule>;

    /// Stable compatibility payload. LegacyModules executes enabled entries directly. Graph Blocks reference payloads
    /// for structural data, while each Block's enabled state, typed inputs, and Context-stack position are
    /// authoritative.
    struct VfxModuleDefinition
    {
        AssetId Id;
        bool Enabled = true;
        VfxModulePayload Payload;

        [[nodiscard]] bool operator==(const VfxModuleDefinition&) const = default;
    };

    /// Execution stages used by graph nodes and the compiled particle program.
    enum class VfxContextType : std::uint8_t
    {
        Spawn,
        Initialize,
        Update,
        Output,
        Event
    };

    inline constexpr std::uint32_t CurrentVfxSchemaVersion = 4;

    template <typename T> struct VfxRange
    {
        T Minimum{};
        T Maximum{};

        [[nodiscard]] bool operator==(const VfxRange&) const = default;
    };

    using VfxScalarRange = VfxRange<float>;
    using VfxIntegerRange = VfxRange<std::int64_t>;
    using VfxUnsignedIntegerRange = VfxRange<std::uint64_t>;
    using VfxVector2Range = VfxRange<Vector2>;
    using VfxVector3Range = VfxRange<Vector3>;
    using VfxVector4Range = VfxRange<Vector4>;
    using VfxColorRange = VfxRange<Color>;

    /// Stable ASCII identifier for a catalog entry. Display labels are intentionally stored separately.
    struct VfxNodeTypeId
    {
        std::string Value;

        [[nodiscard]] bool Empty() const noexcept { return Value.empty(); }
        [[nodiscard]] std::string_view View() const noexcept { return Value; }
        [[nodiscard]] bool operator==(const VfxNodeTypeId&) const = default;
    };

    /// Data types shared by graph pins and blackboard defaults. Existing schema-3 values retain their numeric IDs.
    enum class VfxValueType : std::uint8_t
    {
        Boolean,
        Integer,
        Scalar,
        Vector2,
        Vector3,
        Color,
        Texture,
        Mesh,
        Asset,
        ParticleStream,
        UnsignedInteger,
        Vector4,
        Quaternion,
        Matrix,
        Curve,
        Gradient,
        ScalarRange,
        IntegerRange,
        UnsignedIntegerRange,
        Vector2Range,
        Vector3Range,
        Vector4Range,
        ColorRange,
        Texture2DArray,
        Texture3D,
        TextureCube,
        Buffer,
        PointCache,
        SignedDistanceField
    };

    /// Returns whether a value has the packed two-uint4 representation consumed by the current GPU expression
    /// interpreter. Resource handles and structured authoring values require dedicated lowering paths.
    [[nodiscard]] constexpr bool IsVfxGpuExpressionValueType(const VfxValueType type) noexcept
    {
        switch (type)
        {
        case VfxValueType::Boolean:
        case VfxValueType::Integer:
        case VfxValueType::UnsignedInteger:
        case VfxValueType::Scalar:
        case VfxValueType::Vector2:
        case VfxValueType::Vector3:
        case VfxValueType::Vector4:
        case VfxValueType::Color:
        case VfxValueType::ScalarRange:
        case VfxValueType::IntegerRange:
        case VfxValueType::UnsignedIntegerRange:
        case VfxValueType::Vector2Range:
        case VfxValueType::Vector3Range:
        case VfxValueType::Vector4Range:
        case VfxValueType::ColorRange:
            return true;
        case VfxValueType::Texture:
        case VfxValueType::Mesh:
        case VfxValueType::Asset:
        case VfxValueType::ParticleStream:
        case VfxValueType::Quaternion:
        case VfxValueType::Matrix:
        case VfxValueType::Curve:
        case VfxValueType::Gradient:
        case VfxValueType::Texture2DArray:
        case VfxValueType::Texture3D:
        case VfxValueType::TextureCube:
        case VfxValueType::Buffer:
        case VfxValueType::PointCache:
        case VfxValueType::SignedDistanceField:
            return false;
        }
        return false;
    }

    using VfxParameterValue =
        std::variant<bool, std::int64_t, float, Vector2, Vector3, Color, AssetId, std::uint64_t, Vector4, Quaternion,
                     Matrix4, Curve1D, ColorGradient, VfxScalarRange, VfxIntegerRange, VfxUnsignedIntegerRange,
                     VfxVector2Range, VfxVector3Range, VfxVector4Range, VfxColorRange>;

    using VfxGraphPropertyValue = std::variant<bool, std::int64_t, std::uint64_t, float, std::string, Vector2, Vector3,
                                               Vector4, Quaternion, Color, Matrix4, AssetId>;

    struct VfxGraphProperty
    {
        std::string Name;
        VfxGraphPropertyValue Value = false;

        [[nodiscard]] bool operator==(const VfxGraphProperty&) const = default;
    };

    /// Runtime override keyed by the parameter's stable identity. Names are intentionally not part of the binding.
    struct VfxParameterOverride
    {
        AssetId Parameter;
        VfxParameterValue Value = 0.0F;

        [[nodiscard]] bool operator==(const VfxParameterOverride&) const = default;
    };

    enum class VfxGraphNodeKind : std::uint8_t
    {
        Context,
        Module,
        Parameter,
        CustomHlsl,
        Operator,
        Attribute,
        Subgraph
    };

    enum class VfxEvaluationDomain : std::uint8_t
    {
        CompileTimeConstant,
        PerEffect,
        PerFrame,
        PerSpawn,
        PerParticleUpdate,
        PerOutputEvent
    };

    enum class VfxRandomScope : std::uint8_t
    {
        PerParticle,
        PerVfxComponent,
        PerParticleStrip
    };

    enum class VfxNodeSupportTier : std::uint8_t
    {
        Supported,
        GpuRequired,
        KeireEquivalent,
        Disabled
    };

    enum class VfxNodeBackendTier : std::uint8_t
    {
        CpuOnly,
        CpuAndGpu,
        GpuRequired
    };

    enum class VfxNodeClass : std::uint8_t
    {
        Operator,
        Parameter,
        Constant,
        Attribute,
        Subgraph,
        Block,
        Context,
        Output
    };

    enum class VfxNodeTypeBehavior : std::uint8_t
    {
        Fixed,
        Uniform,
        Unified,
        Cascaded
    };

    /// Append-only opcode set. Underlying values are encoded into canonical compiled IR.
    enum class VfxValueOpcode : std::uint8_t
    {
        Constant,
        Range,
        Random,
        RandomRange,
        Remap,
        Add,
        Subtract,
        Multiply,
        Divide,
        Minimum,
        Maximum,
        Clamp,
        Saturate,
        Absolute,
        Compare,
        BooleanAnd,
        BooleanOr,
        BooleanNot,
        Select,
        Combine,
        Split,
        Dot,
        Cross,
        Normalize,
        Length,
        Distance,
        Time,
        DeltaTime,
        Age,
        Lifetime,
        ParticleId,
        SpawnIndex,
        ToFloat,
        ToInteger,
        ToUnsignedInteger,
        Sine,
        Cosine,
        Tangent,
        ArcSine,
        ArcCosine,
        ArcTangent,
        Atan2,
        Power,
        SquareRoot,
        Exponential,
        Logarithm,
        LogarithmBase2,
        LogarithmBase10,
        Ceiling,
        Floor,
        Round,
        Fractional,
        Lerp,
        Smoothstep,
        Step,
        Negate,
        Sign
    };

    enum class VfxComparisonCondition : std::uint8_t
    {
        Less,
        LessOrEqual,
        Equal,
        NotEqual,
        GreaterOrEqual,
        Greater
    };

    /// A typed graph endpoint. Input=false identifies an output pin.
    struct VfxGraphPin
    {
        AssetId Id;
        std::string Name;
        VfxValueType Type = VfxValueType::Scalar;
        bool Input = true;
        /// Stable compiler-owned meaning for module pins. Display names may change without breaking the binding.
        std::string Semantic;
        /// Typed fallback used by Custom HLSL inputs when no cable is connected.
        std::optional<VfxParameterValue> DefaultValue;

        [[nodiscard]] bool operator==(const VfxGraphPin&) const = default;
    };

    /// Ordered executable element owned by a schema-4 Context node.
    struct VfxGraphBlock
    {
        AssetId Id;
        VfxNodeTypeId TypeId;
        std::string Type;
        bool Enabled = true;
        std::vector<VfxGraphPin> Pins;
        std::vector<VfxGraphProperty> Properties;
        std::uint32_t DefinitionVersion = 1;
        AssetId Reference;

        [[nodiscard]] bool operator==(const VfxGraphBlock&) const = default;
    };

    /// Persisted Context or free-floating value node. Reference identifies a Blackboard parameter or legacy payload.
    struct VfxGraphNode
    {
        AssetId Id;
        std::string Type;
        VfxContextType Context = VfxContextType::Update;
        Vector2 EditorPosition;
        std::vector<VfxGraphPin> Pins;
        std::string CustomHlsl;
        VfxGraphNodeKind Kind = VfxGraphNodeKind::Context;
        AssetId Reference;
        VfxNodeTypeId TypeId;
        std::uint32_t DefinitionVersion = 1;
        std::vector<VfxGraphProperty> Properties;
        /// Resolved polymorphic signature and adaptive pin order are persisted to make reload and undo deterministic.
        std::vector<VfxValueType> ResolvedSignature;
        std::vector<AssetId> DynamicPinOrder;
        /// Only Context nodes own Blocks. Vector order is executable order.
        std::vector<VfxGraphBlock> Blocks;

        [[nodiscard]] bool operator==(const VfxGraphNode&) const = default;
    };

    struct VfxGraphEndpoint
    {
        AssetId Node;
        AssetId Block;
        AssetId Pin;

        [[nodiscard]] bool operator==(const VfxGraphEndpoint&) const = default;
    };

    /// A validated, typed output-to-input connection between graph nodes.
    struct VfxGraphConnection
    {
        AssetId Id;
        AssetId OutputNode;
        AssetId OutputPin;
        AssetId InputNode;
        AssetId InputPin;
        AssetId OutputBlock;
        AssetId InputBlock;

        [[nodiscard]] VfxGraphEndpoint OutputEndpoint() const noexcept { return {OutputNode, OutputBlock, OutputPin}; }
        [[nodiscard]] VfxGraphEndpoint InputEndpoint() const noexcept { return {InputNode, InputBlock, InputPin}; }

        [[nodiscard]] bool operator==(const VfxGraphConnection&) const = default;
    };

    struct VfxNodePinDescriptor
    {
        std::string Name;
        std::string Semantic;
        VfxValueType Type = VfxValueType::Scalar;
        bool Input = true;
        std::optional<VfxParameterValue> DefaultValue;
        std::vector<VfxValueType> AcceptedTypes;
    };

    struct VfxNodeSettingDescriptor
    {
        std::string Name;
        VfxGraphPropertyValue DefaultValue = false;
    };

    /// Single source of truth shared by compiler, editor palette, validation, tests, and generated documentation.
    struct VfxNodeDescriptor
    {
        VfxNodeTypeId TypeId;
        std::string Label;
        std::string Category;
        std::vector<std::string> Synonyms;
        VfxNodeClass Class = VfxNodeClass::Operator;
        VfxNodeTypeBehavior TypeBehavior = VfxNodeTypeBehavior::Fixed;
        VfxNodeSupportTier SupportTier = VfxNodeSupportTier::Disabled;
        std::string DisabledReason;
        std::vector<VfxContextType> ValidContexts;
        std::vector<VfxNodePinDescriptor> Pins;
        std::vector<VfxNodeSettingDescriptor> Settings;
        std::optional<VfxValueOpcode> Lowering;
        std::uint32_t DefinitionVersion = 1;
        VfxNodeBackendTier BackendTier = VfxNodeBackendTier::CpuOnly;
    };

    /// An authoring container for graph nodes and links. Multiple systems do not currently create multiple emitters.
    struct VfxGraphSystem
    {
        AssetId Id;
        std::string Name;
        std::vector<VfxGraphNode> Nodes;
        std::vector<VfxGraphConnection> Connections;

        [[nodiscard]] bool operator==(const VfxGraphSystem&) const = default;
    };

    /// Persisted blackboard default. Graph bindings and runtime overrides use Id, never the mutable display name.
    struct VfxBlackboardParameter
    {
        AssetId Id;
        std::string Name;
        VfxValueType Type = VfxValueType::Scalar;
        VfxParameterValue DefaultValue = 0.0F;
        bool Exposed = true;

        [[nodiscard]] bool operator==(const VfxBlackboardParameter&) const = default;
    };

    /// Complete serialized definition of a .keirevfx asset.
    ///
    /// Schemas 1-3 are migrated in memory. Explicit publication writes schema 4.
    struct VfxEffectDefinition
    {
        std::uint32_t SchemaVersion = CurrentVfxSchemaVersion;
        AssetId EmitterId;
        std::string Name = "VFX Effect";
        bool Loop = false;
        float Duration = 1.0F;
        VfxSimulationSpace Space = VfxSimulationSpace::World;
        std::uint32_t Seed = 1;
        std::uint32_t Capacity = 1024;
        std::vector<VfxModuleDefinition> Modules;
        std::vector<VfxGraphSystem> Systems;
        std::vector<VfxBlackboardParameter> Blackboard;
        VfxExecutionSource ExecutionSource = VfxExecutionSource::LegacyModules;
        VfxCompatibilityMode CompatibilityMode = VfxCompatibilityMode::NativeSchema4;

        [[nodiscard]] bool operator==(const VfxEffectDefinition&) const = default;
    };

    enum class VfxCompileDiagnosticSeverity : std::uint8_t
    {
        Information,
        Warning,
        Error
    };

    struct VfxCompileDiagnostic
    {
        VfxCompileDiagnosticSeverity Severity = VfxCompileDiagnosticSeverity::Error;
        AssetId Node;
        std::string Message;
    };

    enum class VfxModuleProperty : std::uint8_t
    {
        None,
        EmissionParticlesPerSecond,
        BurstTime,
        BurstCount,
        BurstCycles,
        BurstInterval,
        ShapeBoxHalfExtent,
        ShapeRadius,
        ShapeConeAngleDegrees,
        ShapeConeLength,
        ShapeMesh,
        ShapeVolume,
        InitializeLifetimeMinimum,
        InitializeLifetimeMaximum,
        InitializeVelocityMinimum,
        InitializeVelocityMaximum,
        InitializeRotationMinimum,
        InitializeRotationMaximum,
        ForceVector,
        ForceGravityMultiplier,
        SizeConstant,
        ColorConstant,
        CollisionRestitution,
        CollisionKillOnCollision,
        RendererSprite,
        RendererMesh
    };

    struct VfxCompiledParameter
    {
        AssetId Parameter;
        VfxValueType Type = VfxValueType::Scalar;
        VfxParameterValue DefaultValue = 0.0F;
        std::uint32_t Slot = 0;
        bool Exposed = true;
    };

    struct VfxCompiledBinding
    {
        AssetId Node;
        AssetId Module;
        VfxModuleProperty Property = VfxModuleProperty::None;
        VfxValueType Type = VfxValueType::Scalar;
        std::uint32_t ParameterSlot = 0;
        std::uint32_t ValueRegister = ~std::uint32_t{0};
        std::optional<VfxParameterValue> LiteralValue;
    };

    enum class VfxCompiledValueSourceKind : std::uint8_t
    {
        Literal,
        Parameter,
        Register
    };

    struct VfxCompiledValueSource
    {
        VfxCompiledValueSourceKind Kind = VfxCompiledValueSourceKind::Literal;
        VfxValueType Type = VfxValueType::Scalar;
        std::uint32_t Index = 0;
        VfxParameterValue Literal = 0.0F;
    };

    struct VfxCompiledValueInstruction
    {
        AssetId Node;
        VfxValueOpcode Opcode = VfxValueOpcode::Constant;
        VfxValueType Type = VfxValueType::Scalar;
        VfxContextType Context = VfxContextType::Update;
        VfxEvaluationDomain Domain = VfxEvaluationDomain::CompileTimeConstant;
        std::uint32_t OutputRegister = 0;
        /// Zero-based output pin selected from the descriptor. Multi-output Operators compile one live result at a
        /// time, preserving dead-output elimination and stable pin identity.
        std::uint32_t OutputIndex = 0;
        std::vector<VfxCompiledValueSource> Inputs;
        std::uint32_t ChannelSalt = 0;
        VfxRandomScope RandomScope = VfxRandomScope::PerParticle;
        bool ConstantRandom = false;
        bool IndependentRandomChannels = true;
        bool InclusiveMaximum = false;
        bool ClampRemap = false;
        VfxComparisonCondition Comparison = VfxComparisonCondition::Less;
    };

    /// Shader-consumable value storage. Primary contains a scalar/vector value or a Range minimum. Secondary contains
    /// the Range maximum and is zero for non-Range values. Floating-point components are stored as IEEE-754 bits;
    /// signed and unsigned 64-bit integers occupy the first two words of their lane.
    struct alignas(16) VfxGpuValue
    {
        std::array<std::uint32_t, 4> Primary{};
        std::array<std::uint32_t, 4> Secondary{};

        [[nodiscard]] bool operator==(const VfxGpuValue&) const noexcept = default;
    };

    enum class VfxGpuValueSourceKind : std::uint32_t
    {
        Literal,
        Parameter,
        Register
    };

    /// Packed source record consumed as one uint4 by the GPU expression interpreter. Index selects the program's
    /// Constants table, the compiled Blackboard parameter slot, or the logical GPU register file according to Kind.
    struct alignas(16) VfxGpuValueSource
    {
        std::uint32_t Kind = static_cast<std::uint32_t>(VfxGpuValueSourceKind::Literal);
        std::uint32_t Type = static_cast<std::uint32_t>(VfxValueType::Scalar);
        std::uint32_t Index = 0;
        std::uint32_t Reserved = 0;

        [[nodiscard]] bool operator==(const VfxGpuValueSource&) const noexcept = default;
    };

    enum class VfxGpuValueInstructionFlag : std::uint32_t
    {
        ConstantRandom = 1U << 0U,
        IndependentRandomChannels = 1U << 1U,
        InclusiveMaximum = 1U << 2U,
        ClampRemap = 1U << 3U
    };

    /// Four uint4 records forming the stable shader ABI for one expression instruction.
    ///
    /// Header: Opcode, Type, Context, Domain.
    /// Output: logical register, selected output, first flattened source, source count.
    /// Settings: channel salt, Random scope, VfxGpuValueInstructionFlag bits, Compare condition.
    /// NodeIdentity: low/high words of AssetId::High followed by low/high words of AssetId::Low.
    struct alignas(16) VfxGpuValueInstruction
    {
        std::array<std::uint32_t, 4> Header{};
        std::array<std::uint32_t, 4> Output{};
        std::array<std::uint32_t, 4> Settings{};
        std::array<std::uint32_t, 4> NodeIdentity{};

        [[nodiscard]] bool operator==(const VfxGpuValueInstruction&) const noexcept = default;
    };

    /// Bounded packed expression program prepared by GPU compilation. The renderer uploads these tables without
    /// reinterpretation. Parameters remain indexed separately so live overrides can transactionally repack them.
    struct VfxCompiledGpuValueProgram
    {
        static constexpr std::size_t MaximumInstructions = 64;
        static constexpr std::size_t MaximumRegisters = 64;
        static constexpr std::size_t MaximumSources = MaximumInstructions * 4;
        static constexpr std::size_t MaximumConstants = MaximumSources;

        std::array<std::uint32_t, 4> SystemIdentity{};
        std::vector<VfxGpuValueInstruction> Instructions;
        std::vector<VfxGpuValueSource> Sources;
        std::vector<VfxGpuValue> Constants;
        std::uint32_t RegisterCount = 0;

        [[nodiscard]] bool operator==(const VfxCompiledGpuValueProgram&) const noexcept = default;
    };

    static_assert(sizeof(VfxGpuValue) == 32);
    static_assert(sizeof(VfxGpuValueSource) == 16);
    static_assert(sizeof(VfxGpuValueInstruction) == 64);

    struct VfxCompiledModule
    {
        AssetId Node;
        AssetId Module;
        VfxContextType Context = VfxContextType::Update;
        std::uint32_t ModuleIndex = 0;
    };

    enum class VfxCustomTarget : std::uint8_t
    {
        Position,
        Velocity,
        Rotation,
        Tint,
        Size
    };

    enum class VfxCustomOperation : std::uint8_t
    {
        Assign,
        Add,
        Multiply
    };

    /// One verified instruction produced from the bounded Portable Custom HLSL syntax.
    struct VfxCompiledCustomInstruction
    {
        AssetId Node;
        VfxContextType Context = VfxContextType::Update;
        VfxCustomTarget Target = VfxCustomTarget::Velocity;
        VfxCustomOperation Operation = VfxCustomOperation::Add;
        VfxValueType OperandType = VfxValueType::Scalar;
        /// UINT32_MAX selects Literal; otherwise this indexes VfxCompiledProgram::Parameters.
        std::uint32_t ParameterSlot = ~std::uint32_t{0};
        /// UINT32_MAX selects ParameterSlot or Literal; otherwise this indexes the expression register file.
        std::uint32_t ValueRegister = ~std::uint32_t{0};
        VfxParameterValue Literal = 0.0F;
        bool ScaleByDeltaTime = false;
    };

    enum class VfxCompiledOperationKind : std::uint8_t
    {
        Module,
        CustomHlsl
    };

    /// One operation in Context Block order. Node is the stable Block ID; Index selects Modules or CustomInstructions.
    struct VfxCompiledOperation
    {
        AssetId Node;
        VfxContextType Context = VfxContextType::Update;
        VfxCompiledOperationKind Kind = VfxCompiledOperationKind::Module;
        std::uint32_t Index = 0;
    };

    /// Deterministic executable program lowered from the compatibility stack or a schema-4 Context/Block graph.
    struct VfxCompiledProgram
    {
        std::uint64_t Hash = 0;
        std::uint64_t StateLayoutHash = 0;
        VfxBackend Backend = VfxBackend::Cpu;
        std::vector<std::byte> CanonicalIr;
        std::vector<VfxCompiledParameter> Parameters;
        std::vector<VfxCompiledModule> Modules;
        std::vector<VfxCompiledBinding> Bindings;
        std::vector<VfxCompiledValueInstruction> ValueInstructions;
        std::uint32_t ValueRegisterCount = 0;
        VfxCompiledGpuValueProgram GpuValueProgram;
        std::vector<VfxCompiledCustomInstruction> CustomInstructions;
        std::vector<VfxCompiledOperation> Operations;
        std::vector<VfxCompileDiagnostic> Diagnostics;
        bool Valid = false;
    };

    /// Validates persisted authoring data while permitting a temporarily incomplete executable graph.
    /// For Graph execution, canonical node shapes, references, cable ownership, direction, type, stage order, and
    /// acyclic topology remain strict. Throws std::invalid_argument when the draft is structurally invalid.
    /// Runtime compilation and asset publication must use ValidateVfxEffect instead.
    KEIRE_API void ValidateVfxEffectAuthoring(const VfxEffectDefinition& definition);
    /// Validates authoring data plus the complete, publishable executable graph contract.
    KEIRE_API void ValidateVfxEffect(const VfxEffectDefinition& definition);
    /// Returns asset-valued module and blackboard dependencies in deterministic order.
    [[nodiscard]] KEIRE_API std::vector<AssetId> VfxEffectDependencies(const VfxEffectDefinition& definition);
    /// Lowers an effect into the deterministic program consumed by CPU and GPU simulation.
    [[nodiscard]] KEIRE_API VfxCompiledProgram CompileVfxEffect(const VfxEffectDefinition& definition,
                                                                VfxBackend backend);
    /// Returns the immutable compiler-owned node catalog used by authoring and lowering.
    [[nodiscard]] KEIRE_API std::span<const VfxNodeDescriptor> VfxNodeCatalog();
    [[nodiscard]] KEIRE_API const VfxNodeDescriptor* FindVfxNodeDescriptor(std::string_view typeId);
    [[nodiscard]] KEIRE_API bool VfxValueMatchesType(VfxValueType type, const VfxParameterValue& value) noexcept;
    [[nodiscard]] KEIRE_API bool IsFiniteVfxValue(const VfxParameterValue& value) noexcept;
    [[nodiscard]] KEIRE_API VfxParameterValue DefaultVfxValue(VfxValueType type);
    /// Creates an Operator node from a supported catalog descriptor with canonical stable pins and settings.
    [[nodiscard]] KEIRE_API VfxGraphNode CreateVfxGraphOperatorNode(std::string_view typeId,
                                                                    Vector2 editorPosition = {});
    /// Creates an executable module node with generated stable identity and canonical typed pins.
    [[nodiscard]] KEIRE_API VfxGraphNode CreateVfxGraphModuleNode(const VfxModuleDefinition& module,
                                                                  Vector2 editorPosition = {});
    /// Creates a schema-4 Block for an ordered Context stack. The block references, but does not duplicate, the
    /// module payload and owns canonical data-input pins only; its vector position in the Context is execution order.
    [[nodiscard]] KEIRE_API VfxGraphBlock CreateVfxGraphBlock(const VfxModuleDefinition& module);
    /// Creates an ordered Portable Custom HLSL Block. Callers may append supported typed input pins before insertion.
    [[nodiscard]] KEIRE_API VfxGraphBlock CreateVfxGraphPortableHlslBlock(std::string source);
    /// Migrates any supported historical definition to the current schema without changing existing stable IDs.
    [[nodiscard]] KEIRE_API VfxEffectDefinition MigrateVfxEffectToSchema4(const VfxEffectDefinition& definition);
    /// Converts a legacy module stack to an executable graph without changing module stable IDs.
    [[nodiscard]] KEIRE_API VfxEffectDefinition ConvertVfxEffectToGraph(const VfxEffectDefinition& definition);

    /// Immutable runtime asset that retains a validated VfxEffectDefinition.
    class KEIRE_API VfxEffectAsset final : public Asset
    {
      public:
        explicit VfxEffectAsset(VfxEffectDefinition definition);

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245564658ULL, 0x4546464543540001ULL));
        }

        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const VfxEffectDefinition& Definition() const noexcept { return m_Definition; }

        [[nodiscard]] static VfxEffectDefinition DefaultDefinition();
        [[nodiscard]] static Ref<VfxEffectAsset> Default();
        [[nodiscard]] static Ref<VfxEffectAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const VfxEffectDefinition& definition);

      private:
        VfxEffectDefinition m_Definition;
    };

    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateVfxEffectAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateVfxEffectAssetDecoder();

    /// Generation-bearing native handle owned by one VfxWorld.
    ///
    /// Handles become stale after Stop, Clear, natural completion, or slot reuse. They must not be used with another
    /// world.
    class KEIRE_API VfxHandle final
    {
      public:
        constexpr VfxHandle() noexcept = default;

        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return m_Index != InvalidIndex && m_Generation != 0;
        }
        [[nodiscard]] constexpr std::uint32_t Index() const noexcept { return m_Index; }
        [[nodiscard]] constexpr std::uint32_t Generation() const noexcept { return m_Generation; }
        [[nodiscard]] constexpr auto operator<=>(const VfxHandle&) const noexcept = default;

      private:
        static constexpr std::uint32_t InvalidIndex = ~std::uint32_t{0};
        friend class VfxWorld;
        constexpr VfxHandle(const std::uint32_t index, const std::uint32_t generation) noexcept
            : m_Index(index), m_Generation(generation)
        {
        }

        std::uint32_t m_Index = InvalidIndex;
        std::uint32_t m_Generation = 0;
    };

    /// Hit returned by a VfxWorldSpecification::CollisionQuery callback.
    struct VfxCollisionHit
    {
        Vector3 Position;
        Vector3 Normal{0.0F, 1.0F, 0.0F};
    };

    /// Runtime capability and data-quality flags reported per active effect.
    enum class VfxRuntimeDiagnostic : std::uint32_t
    {
        None = 0,
        GpuDepthFellBackToCpu = 1U << 0U,
        ScenePhysicsSelectedCpu = 1U << 1U,
        CollisionQueryUnavailable = 1U << 2U,
        ShapeAssetSamplerUnavailable = 1U << 3U,
        SimulationValueInvalid = 1U << 4U,
        ParameterOverrideRejected = 1U << 5U
    };

    [[nodiscard]] constexpr VfxRuntimeDiagnostic operator|(const VfxRuntimeDiagnostic left,
                                                           const VfxRuntimeDiagnostic right) noexcept
    {
        return static_cast<VfxRuntimeDiagnostic>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }

    constexpr VfxRuntimeDiagnostic& operator|=(VfxRuntimeDiagnostic& left, const VfxRuntimeDiagnostic right) noexcept
    {
        left = left | right;
        return left;
    }

    [[nodiscard]] constexpr bool HasVfxDiagnostic(const VfxRuntimeDiagnostic value,
                                                  const VfxRuntimeDiagnostic flag) noexcept
    {
        return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
    }

    /// Fixed budgets, backend selection, and optional host callbacks for one VfxWorld.
    ///
    /// A world is normally scene- or application-owned and shared by many emitters. Callers must serialize access;
    /// VfxWorld is not a cross-thread synchronization primitive.
    struct VfxWorldSpecification
    {
        std::uint32_t MaximumEffects = 256;
        std::uint32_t MaximumParticles = 65'536;
        VfxBackend Backend = VfxBackend::Cpu;
        std::function<std::optional<VfxCollisionHit>(Vector3 start, Vector3 end)> CollisionQuery;
        std::function<std::optional<Vector3>(AssetId shapeAsset, std::uint32_t randomValue)> ShapeSample;
    };

    /// Parameters used to create one effect instance.
    struct VfxActivation
    {
        /// Retained for the full handle lifetime.
        Ref<const VfxEffectAsset> Effect;
        /// Nonzero content revision used by Reload and render synchronization.
        std::uint64_t Revision = 1;
        /// Initial emitter world position and rotation. Scale is intentionally not part of the VFX contract.
        Vector3 Position;
        Quaternion Rotation;
        /// Combined with VfxEffectDefinition::Seed using XOR for deterministic per-emitter variation.
        std::uint32_t SeedOffset = 0;
        /// Optional exposed Blackboard values keyed by stable parameter ID.
        std::vector<VfxParameterOverride> ParameterOverrides;
    };

    /// Bounded world counters. Dropped values are cumulative for the world lifetime.
    struct VfxWorldStatistics
    {
        std::uint32_t ActiveEffects = 0;
        std::uint32_t ActiveParticles = 0;
        std::uint64_t DroppedEffects = 0;
        std::uint64_t DroppedParticles = 0;
    };

    struct VfxDebugEffect
    {
        VfxHandle Handle;
        AssetId EmitterId;
        std::uint64_t Revision = 0;
        float ElapsedSeconds = 0.0F;
        std::uint32_t ActiveParticles = 0;
        std::uint64_t DroppedParticles = 0;
        bool Emitting = false;
        VfxRuntimeDiagnostic Diagnostics = VfxRuntimeDiagnostic::None;
    };

    struct VfxDebugParticle
    {
        VfxHandle Effect;
        Vector3 Position;
        Vector3 Velocity;
        Vector3 Rotation;
        Color Tint;
        float Size = 1.0F;
        float NormalizedAge = 0.0F;
        VfxRendererType Renderer = VfxRendererType::Sprite;
    };

    struct VfxRenderParticle
    {
        VfxHandle Effect;
        Vector3 Position;
        Vector3 Rotation;
        float Size = 1.0F;
        Color Tint;
        VfxRendererType Renderer = VfxRendererType::Sprite;
        AssetId Sprite;
        AssetId Mesh;

        [[nodiscard]] bool operator==(const VfxRenderParticle&) const noexcept = default;
    };

    struct VfxRenderPacketCopyResult
    {
        std::size_t Written = 0;
        std::size_t Dropped = 0;
    };

    /// Immutable renderer-facing work descriptor for one live GPU effect generation.
    ///
    /// The renderer keys persistent particles by Handle. SimulationRevision changes request a handle-local particle
    /// retirement without advancing the VfxRenderSnapshot world reset revision.
    enum class VfxGpuParticleOperationKind : std::uint8_t
    {
        Shape,
        Initialize,
        Force,
        Size,
        Color,
        Collision,
        Renderer,
        CustomHlsl
    };

    struct VfxGpuCustomInstruction
    {
        VfxContextType Context = VfxContextType::Update;
        VfxCustomTarget Target = VfxCustomTarget::Velocity;
        VfxCustomOperation Operation = VfxCustomOperation::Add;
        bool ScaleByDeltaTime = false;
        VfxValueType OperandType = VfxValueType::Scalar;
        Vector4 Operand;
        /// UINT32_MAX selects Operand; otherwise the shader reads the evaluated expression register.
        std::uint32_t ValueRegister = ~std::uint32_t{0};

        [[nodiscard]] bool operator==(const VfxGpuCustomInstruction&) const noexcept = default;
    };

    struct VfxGpuParticleOperation
    {
        VfxContextType Context = VfxContextType::Update;
        VfxGpuParticleOperationKind Kind = VfxGpuParticleOperationKind::CustomHlsl;
        /// Selects CustomInstructions when Kind is CustomHlsl; zero for built-in module operations.
        std::uint32_t Index = 0;

        [[nodiscard]] bool operator==(const VfxGpuParticleOperation&) const noexcept = default;
    };

    /// Immutable execution tables retained by a render snapshot. Static program tables, live Blackboard values, and
    /// ordered particle operations share one lifetime so render threads never borrow storage from the simulation
    /// world. The vectors are compiler-bounded by byte-addressable backend limits, not legacy cbuffer array sizes.
    struct VfxGpuExecutionPayload
    {
        VfxCompiledGpuValueProgram ValueProgram;
        std::vector<VfxGpuValue> Parameters;
        std::vector<VfxGpuCustomInstruction> CustomInstructions;
        std::vector<VfxGpuParticleOperation> ParticleOperations;

        [[nodiscard]] bool operator==(const VfxGpuExecutionPayload&) const noexcept = default;
    };

    struct VfxGpuEmitter
    {
        using ParticleOperationKind = VfxGpuParticleOperationKind;
        using CustomInstruction = VfxGpuCustomInstruction;
        using ParticleOperation = VfxGpuParticleOperation;

        VfxHandle Handle;
        std::uint64_t Revision = 0;
        /// Cumulative requested spawn count used to preserve spawn work across skipped render snapshots.
        /// Multiple skipped simulation steps currently execute with the latest published timing values.
        std::uint64_t SpawnSequence = 0;
        Vector3 Position;
        Quaternion Rotation;
        Vector3 ShapeExtent;
        Vector3 VelocityMinimum;
        float LifetimeMinimum = 1.0F;
        Vector3 VelocityMaximum;
        float LifetimeMaximum = 1.0F;
        Vector3 Acceleration;
        float ShapeRadius = 0.0F;
        Color ColorStart;
        Color ColorEnd;
        float SizeStart = 1.0F;
        float SizeEnd = 1.0F;
        std::uint32_t Seed = 1;
        VfxShape Shape = VfxShape::Point;
        VfxSimulationSpace Space = VfxSimulationSpace::World;
        VfxRendererType Renderer = VfxRendererType::Sprite;
        /// Advances when an incompatible reload must discard this handle's prior GPU simulation state.
        std::uint64_t SimulationRevision = 1;
        /// Deprecated compatibility mirror. GPU execution consumes Execution->CustomInstructions without this bound.
        static constexpr std::size_t MaximumCustomInstructions = 8;
        std::array<CustomInstruction, MaximumCustomInstructions> CustomInstructions;
        std::uint32_t CustomInstructionCount = 0;
        /// Deprecated compatibility mirror. GPU execution consumes Execution->ParticleOperations without this bound.
        static constexpr std::size_t MaximumParticleOperations = 15;
        std::array<ParticleOperation, MaximumParticleOperations> ParticleOperations;
        std::uint32_t ParticleOperationCount = 0;
        /// Scaled simulation delta resolved for this handle by the world update that produced the snapshot.
        float SimulationDeltaSeconds = 0.0F;
        /// Legacy module values required by the generic GPU kernel but kept outside its fixed historical payload.
        Vector3 RotationMinimum;
        Vector3 RotationMaximum;
        float ConeAngleDegrees = 25.0F;
        float ConeLength = 1.0F;
        /// Shared, snapshot-safe packed program, live Blackboard values, and dynamic operation tables. GPU emitters
        /// captured from a live world always provide this payload.
        std::shared_ptr<const VfxGpuExecutionPayload> Execution;
        /// Effect-local time and deterministic simulation step consumed by Time, Delta Time, and Random Operators.
        float EffectTime = 0.0F;
        std::uint64_t SimulationStep = 0;
    };

    /// Immutable render handoff for either CPU particle packets or GPU emitter descriptors.
    ///
    /// The snapshot owns its storage and can safely outlive the VfxWorld frame that produced it.
    class KEIRE_API VfxRenderSnapshot final
    {
      public:
        static constexpr std::size_t MaximumParticles = 65'536;

        VfxRenderSnapshot() = default;
        VfxRenderSnapshot(const VfxRenderSnapshot&) = default;
        VfxRenderSnapshot(VfxRenderSnapshot&&) noexcept = default;
        VfxRenderSnapshot& operator=(const VfxRenderSnapshot&) = default;
        VfxRenderSnapshot& operator=(VfxRenderSnapshot&&) noexcept = default;

        [[nodiscard]] std::uint64_t Revision() const noexcept { return m_Revision; }
        [[nodiscard]] std::uint64_t WorldId() const noexcept { return m_WorldId; }
        [[nodiscard]] std::uint64_t ResetRevision() const noexcept { return m_ResetRevision; }
        /// Advances only when VfxWorld::Update accepts a positive delta. Renderers consume each step at most once.
        [[nodiscard]] std::uint64_t SimulationStepRevision() const noexcept { return m_SimulationStepRevision; }
        [[nodiscard]] std::uint32_t ParticleCapacity() const noexcept { return m_ParticleCapacity; }
        [[nodiscard]] float DeltaSeconds() const noexcept { return m_DeltaSeconds; }
        [[nodiscard]] std::span<const VfxRenderParticle> Particles() const noexcept { return m_Particles; }
        [[nodiscard]] std::span<const VfxGpuEmitter> GpuEmitters() const noexcept { return m_GpuEmitters; }
        [[nodiscard]] std::size_t DroppedParticles() const noexcept { return m_DroppedParticles; }

      private:
        friend class VfxWorld;
        std::uint64_t m_Revision = 0;
        std::uint64_t m_WorldId = 0;
        std::uint64_t m_ResetRevision = 0;
        std::uint64_t m_SimulationStepRevision = 0;
        std::uint32_t m_ParticleCapacity = 0;
        float m_DeltaSeconds = 0.0F;
        std::vector<VfxRenderParticle> m_Particles;
        std::vector<VfxGpuEmitter> m_GpuEmitters;
        std::size_t m_DroppedParticles = 0;
    };

    /// Bounded diagnostic snapshot intended for tools, statistics, and tests.
    struct VfxDebugSnapshot
    {
        static constexpr std::size_t MaximumEffects = 256;
        static constexpr std::size_t MaximumParticles = 2048;

        std::uint64_t Revision = 0;
        VfxWorldStatistics Statistics;
        std::array<VfxDebugEffect, MaximumEffects> Effects;
        std::array<VfxDebugParticle, MaximumParticles> Particles;
        std::size_t EffectCount = 0;
        std::size_t ParticleCount = 0;
        std::size_t DroppedEffectSamples = 0;
        std::size_t DroppedParticleSamples = 0;
    };

    /// Bounded particle-effect simulation owner.
    ///
    /// VfxWorld owns handle slots, particle storage, deterministic simulation state, reload state, and render
    /// snapshots. Construct one shared world per scene or equivalent lifetime, not one world per emitter.
    class KEIRE_API VfxWorld final : public RefCounted
    {
      public:
        explicit VfxWorld(VfxWorldSpecification specification = {});
        ~VfxWorld() override;

        VfxWorld(const VfxWorld&) = delete;
        VfxWorld& operator=(const VfxWorld&) = delete;

        /// Activates an effect and retains activation.Effect. Returns an empty handle when the effect budget is full;
        /// malformed activations throw std::invalid_argument.
        [[nodiscard]] VfxHandle Activate(const VfxActivation& activation);
        /// Returns true only while handle still identifies its original active generation in this world.
        [[nodiscard]] bool IsAlive(VfxHandle handle) const noexcept;
        /// Stops a live handle and releases only its particles. GPU retirement is generation-qualified, so unrelated
        /// handles in the same world keep their particles and spawn progress. Stale handles are ignored.
        void Stop(VfxHandle handle);
        /// Updates the emitter origin. On both backends, Local particles follow the rigid position/rotation change;
        /// existing World particles remain where they spawned. Scale is not part of this contract.
        void SetTransform(VfxHandle handle, Vector3 position, Quaternion rotation);
        /// Sets a per-effect 0..8 simulation multiplier. Zero pauses this handle.
        void SetSimulationSpeed(VfxHandle handle, float speed);
        /// Replaces all overrides transactionally. Unknown, hidden, or type-mismatched parameters are rejected.
        void SetParameterOverrides(VfxHandle handle, std::span<const VfxParameterOverride> overrides);
        /// Sets one exposed parameter without disturbing other overrides.
        void SetParameter(VfxHandle handle, AssetId parameter, VfxParameterValue value);
        /// Returns one parameter to its asset default. Missing overrides are a no-op.
        void ResetParameter(VfxHandle handle, AssetId parameter);
        /// Replaces the retained effect only when revision is newer. Matching EmitterId values preserve compatible
        /// simulation state; a changed EmitterId restarts only this handle and preserves unrelated world effects.
        [[nodiscard]] bool Reload(VfxHandle handle, Ref<const VfxEffectAsset> effect, std::uint64_t revision);
        /// Advances all active effects. deltaSeconds must be finite and in the range 0..10 seconds.
        void Update(float deltaSeconds);
        /// Returns current bounded world counters.
        [[nodiscard]] VfxWorldStatistics Statistics() const noexcept;
        /// Copies CPU render packets into caller storage and reports packets omitted by the destination bound.
        [[nodiscard]] VfxRenderPacketCopyResult
        CopyRenderPackets(std::span<VfxRenderParticle> destination) const noexcept;
        /// Produces an owning CPU-particle or GPU-emitter render snapshot.
        [[nodiscard]] VfxRenderSnapshot
        CaptureRenderSnapshot(std::size_t maximumParticles = VfxRenderSnapshot::MaximumParticles) const;
        /// Produces a bounded owning tool/debug snapshot without throwing.
        [[nodiscard]] VfxDebugSnapshot CaptureDebugSnapshot() const noexcept;
        /// Performs a world-wide reset, stopping every effect and invalidating every handle. On GPU this advances the
        /// world reset revision; use Stop for handle-local retirement. Safe to call repeatedly.
        void Clear() noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire

template <> struct std::hash<Keire::VfxHandle>
{
    std::size_t operator()(const Keire::VfxHandle value) const noexcept
    {
        return std::hash<std::uint32_t>{}(value.Index()) ^ (std::hash<std::uint32_t>{}(value.Generation()) << 1U);
    }
};
