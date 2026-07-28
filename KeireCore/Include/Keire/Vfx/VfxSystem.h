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
#include <variant>
#include <vector>

namespace Keire
{
    enum class VfxSimulationSpace : std::uint8_t
    {
        Local,
        World
    };

    enum class VfxShape : std::uint8_t
    {
        Point,
        Box,
        Sphere,
        Cone,
        Mesh,
        Volume
    };

    enum class VfxCollisionMode : std::uint8_t
    {
        None,
        Cpu,
        GpuDepth,
        ScenePhysics
    };

    enum class VfxRendererType : std::uint8_t
    {
        Sprite,
        Mesh
    };

    struct VfxEmissionRateModule
    {
        float ParticlesPerSecond = 10.0F;

        [[nodiscard]] bool operator==(const VfxEmissionRateModule&) const = default;
    };

    struct VfxBurstModule
    {
        float Time = 0.0F;
        std::uint32_t Count = 10;
        std::uint32_t Cycles = 1;
        float Interval = 0.1F;

        [[nodiscard]] bool operator==(const VfxBurstModule&) const = default;
    };

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

    struct VfxForceModule
    {
        Vector3 Force;
        float GravityMultiplier = 0.0F;

        [[nodiscard]] bool operator==(const VfxForceModule&) const = default;
    };

    struct VfxSizeOverLifetimeModule
    {
        Curve1D Size = Curve1D::Constant(1.0F);

        [[nodiscard]] bool operator==(const VfxSizeOverLifetimeModule&) const = default;
    };

    struct VfxColorOverLifetimeModule
    {
        ColorGradient Color = ColorGradient::Constant(Keire::Color{});

        [[nodiscard]] bool operator==(const VfxColorOverLifetimeModule&) const = default;
    };

    struct VfxCollisionModule
    {
        VfxCollisionMode Mode = VfxCollisionMode::None;
        float Restitution = 0.5F;
        bool KillOnCollision = false;

        [[nodiscard]] bool operator==(const VfxCollisionModule&) const = default;
    };

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

    struct VfxModuleDefinition
    {
        AssetId Id;
        bool Enabled = true;
        VfxModulePayload Payload;

        [[nodiscard]] bool operator==(const VfxModuleDefinition&) const = default;
    };

    struct VfxEffectDefinition
    {
        std::uint32_t SchemaVersion = 1;
        AssetId EmitterId;
        std::string Name = "VFX Effect";
        bool Loop = false;
        float Duration = 1.0F;
        VfxSimulationSpace Space = VfxSimulationSpace::World;
        std::uint32_t Seed = 1;
        std::uint32_t Capacity = 1024;
        std::vector<VfxModuleDefinition> Modules;

        [[nodiscard]] bool operator==(const VfxEffectDefinition&) const = default;
    };

    KEIRE_API void ValidateVfxEffect(const VfxEffectDefinition& definition);
    [[nodiscard]] KEIRE_API std::vector<AssetId> VfxEffectDependencies(const VfxEffectDefinition& definition);

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

    struct VfxCollisionHit
    {
        Vector3 Position;
        Vector3 Normal{0.0F, 1.0F, 0.0F};
    };

    enum class VfxRuntimeDiagnostic : std::uint32_t
    {
        None = 0,
        GpuDepthFellBackToCpu = 1U << 0U,
        ScenePhysicsSelectedCpu = 1U << 1U,
        CollisionQueryUnavailable = 1U << 2U,
        ShapeAssetSamplerUnavailable = 1U << 3U,
        SimulationValueInvalid = 1U << 4U
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

    struct VfxWorldSpecification
    {
        std::uint32_t MaximumEffects = 256;
        std::uint32_t MaximumParticles = 65'536;
        std::function<std::optional<VfxCollisionHit>(Vector3 start, Vector3 end)> CollisionQuery;
        std::function<std::optional<Vector3>(AssetId shapeAsset, std::uint32_t randomValue)> ShapeSample;
    };

    struct VfxActivation
    {
        Ref<const VfxEffectAsset> Effect;
        std::uint64_t Revision = 1;
        Vector3 Position;
        Quaternion Rotation;
        std::uint32_t SeedOffset = 0;
    };

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
        [[nodiscard]] std::span<const VfxRenderParticle> Particles() const noexcept { return m_Particles; }
        [[nodiscard]] std::size_t DroppedParticles() const noexcept { return m_DroppedParticles; }

      private:
        friend class VfxWorld;
        std::uint64_t m_Revision = 0;
        std::vector<VfxRenderParticle> m_Particles;
        std::size_t m_DroppedParticles = 0;
    };

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

    class KEIRE_API VfxWorld final : public RefCounted
    {
      public:
        explicit VfxWorld(VfxWorldSpecification specification = {});
        ~VfxWorld() override;

        VfxWorld(const VfxWorld&) = delete;
        VfxWorld& operator=(const VfxWorld&) = delete;

        [[nodiscard]] VfxHandle Activate(const VfxActivation& activation);
        [[nodiscard]] bool IsAlive(VfxHandle handle) const noexcept;
        void Stop(VfxHandle handle);
        void SetTransform(VfxHandle handle, Vector3 position, Quaternion rotation);
        void SetSimulationSpeed(VfxHandle handle, float speed);
        [[nodiscard]] bool Reload(VfxHandle handle, Ref<const VfxEffectAsset> effect, std::uint64_t revision);
        void Update(float deltaSeconds);
        [[nodiscard]] VfxWorldStatistics Statistics() const noexcept;
        [[nodiscard]] VfxRenderPacketCopyResult
        CopyRenderPackets(std::span<VfxRenderParticle> destination) const noexcept;
        [[nodiscard]] VfxRenderSnapshot
        CaptureRenderSnapshot(std::size_t maximumParticles = VfxRenderSnapshot::MaximumParticles) const;
        [[nodiscard]] VfxDebugSnapshot CaptureDebugSnapshot() const noexcept;
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
