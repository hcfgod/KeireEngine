#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace Keire
{
    enum class RenderPath : std::uint8_t;
}

namespace Keire::RenderBackend
{
    struct FrameGraphResource final
    {
        std::uint32_t Value = std::numeric_limits<std::uint32_t>::max();

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return Value != std::numeric_limits<std::uint32_t>::max();
        }
        auto operator<=>(const FrameGraphResource&) const noexcept = default;
    };

    struct FrameGraphPass final
    {
        std::uint32_t Value = std::numeric_limits<std::uint32_t>::max();

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return Value != std::numeric_limits<std::uint32_t>::max();
        }
        auto operator<=>(const FrameGraphPass&) const noexcept = default;
    };

    enum class FrameGraphResourceKind : std::uint8_t
    {
        Texture,
        Buffer
    };

    enum class FrameGraphPassKind : std::uint8_t
    {
        Upload,
        Graphics,
        Compute,
        Transfer,
        Present
    };

    enum class FrameGraphResourceState : std::uint8_t
    {
        Undefined,
        External,
        CopyDestination,
        ShaderRead,
        ColorAttachment,
        StorageRead,
        StorageWrite,
        CopySource,
        Present,
        DepthStencilAttachment
    };

    enum class FrameGraphTextureFormat : std::uint8_t
    {
        Undefined,
        Rgba8Unorm,
        Rgba8Srgb,
        Rgba16Float,
        Rgba32Float,
        Rgba32Uint,
        Rg16Float,
        R32Float,
        D32Float
    };

    enum class FrameGraphResourceUsage : std::uint16_t
    {
        None = 0,
        Sampled = 1U << 0U,
        ColorAttachment = 1U << 1U,
        DepthStencilAttachment = 1U << 2U,
        Storage = 1U << 3U,
        TransferSource = 1U << 4U,
        TransferDestination = 1U << 5U,
        Present = 1U << 6U,
        IndirectArguments = 1U << 7U,
        UnfilteredRead = 1U << 8U
    };

    [[nodiscard]] constexpr FrameGraphResourceUsage operator|(const FrameGraphResourceUsage left,
                                                              const FrameGraphResourceUsage right) noexcept
    {
        return static_cast<FrameGraphResourceUsage>(static_cast<std::uint16_t>(left) |
                                                    static_cast<std::uint16_t>(right));
    }

    [[nodiscard]] constexpr bool HasFrameGraphResourceUsage(const FrameGraphResourceUsage usages,
                                                            const FrameGraphResourceUsage usage) noexcept
    {
        return (static_cast<std::uint16_t>(usages) & static_cast<std::uint16_t>(usage)) != 0U;
    }

    struct FrameGraphTextureDescription final
    {
        FrameGraphTextureFormat Format = FrameGraphTextureFormat::Undefined;
        FrameGraphResourceUsage Usage = FrameGraphResourceUsage::None;
        std::uint8_t SampleCount = 1;
        std::uint8_t WidthScaleNumerator = 1;
        std::uint8_t WidthScaleDenominator = 1;
        std::uint8_t HeightScaleNumerator = 1;
        std::uint8_t HeightScaleDenominator = 1;

        auto operator<=>(const FrameGraphTextureDescription&) const noexcept = default;
    };

    struct FrameGraphResourceDescription final
    {
        std::string Name;
        FrameGraphResourceKind Kind = FrameGraphResourceKind::Texture;
        bool Imported = false;
        std::uint64_t CompatibilityKey = 0;
        std::uint64_t SizeBytes = 0;
        /// Typed texture metadata is mandatory for production transient textures. Undefined preserves legacy tests.
        FrameGraphTextureDescription Texture;
    };

    struct FrameGraphPassDescription final
    {
        std::string Name;
        std::vector<FrameGraphResource> Reads;
        std::vector<FrameGraphResource> Writes;
        FrameGraphPassKind Kind = FrameGraphPassKind::Graphics;
    };

    struct FrameGraphResourceLifetime final
    {
        std::uint32_t FirstPass = 0;
        std::uint32_t LastPass = 0;
        bool Used = false;
    };

    struct CompiledFrameGraph final
    {
        struct Transition final
        {
            FrameGraphResource Resource;
            FrameGraphResourceState Before = FrameGraphResourceState::Undefined;
            FrameGraphResourceState After = FrameGraphResourceState::Undefined;
        };

        struct PassExecution final
        {
            FrameGraphPass Pass;
            std::vector<Transition> Transitions;
        };

        struct TransientAllocation final
        {
            FrameGraphResourceKind Kind = FrameGraphResourceKind::Texture;
            std::uint64_t CompatibilityKey = 0;
            std::uint64_t SizeBytes = 0;
            FrameGraphTextureDescription Texture;
        };

        std::vector<FrameGraphPass> Order;
        std::vector<PassExecution> Execution;
        std::vector<FrameGraphResourceLifetime> Lifetimes;
        std::vector<std::uint32_t> PhysicalResources;
        std::vector<TransientAllocation> TransientAllocations;
        std::vector<std::string> Diagnostics;
    };

    class FrameGraphExecutionContext
    {
      public:
        virtual ~FrameGraphExecutionContext() = default;
        virtual void Transition(const CompiledFrameGraph::Transition& transition) = 0;
        virtual void Execute(FrameGraphPass pass, const FrameGraphPassDescription& description) = 0;
    };

    class FrameGraph final
    {
      public:
        [[nodiscard]] FrameGraphResource AddResource(FrameGraphResourceDescription description);
        [[nodiscard]] FrameGraphPass AddPass(FrameGraphPassDescription description);
        [[nodiscard]] CompiledFrameGraph Compile() const;
        void Execute(const CompiledFrameGraph& compiled, FrameGraphExecutionContext& context) const;
        void Clear() noexcept;

        [[nodiscard]] std::span<const FrameGraphResourceDescription> Resources() const noexcept { return m_Resources; }
        [[nodiscard]] std::span<const FrameGraphPassDescription> Passes() const noexcept { return m_Passes; }

      private:
        std::vector<FrameGraphResourceDescription> m_Resources;
        std::vector<FrameGraphPassDescription> m_Passes;
    };

    struct StaticSceneFrameGraph final
    {
        RenderPath Path{};
        FrameGraph Graph;
        CompiledFrameGraph Compiled;
        FrameGraphResource HdrScene;
        FrameGraphResource SceneDepth;
        FrameGraphResource SampledDepth;
        FrameGraphResource GBufferBaseColorMetallic;
        FrameGraphResource GBufferNormalRoughness;
        FrameGraphResource GBufferMaterial;
        FrameGraphResource GBufferLighting;
        FrameGraphResource GBufferVelocity;
        FrameGraphResource DBufferBaseColor;
        FrameGraphResource DBufferNormal;
        FrameGraphResource DBufferMaterial;
        FrameGraphResource IrradynRadiance;
        FrameGraphResource GpuOcclusionDepth;
        FrameGraphResource GpuOcclusionPyramid;
        FrameGraphResource GpuOcclusionIndirectArguments;
        FrameGraphResource GpuVisibilityMasks;
        FrameGraphResource SpatialSelectionRecords;
        FrameGraphResource VfxDynamicCandidates;
        FrameGraphResource ForwardPlusLightTiles;
        FrameGraphPass ResourceUploads;
        FrameGraphPass DirectionalShadows;
        FrameGraphPass ForwardPlusCulling;
        FrameGraphPass GpuOcclusionDepthPass;
        FrameGraphPass GpuOcclusionPyramidPass;
        FrameGraphPass VfxSimulation;
        FrameGraphPass GpuOcclusionCullingPass;
        FrameGraphPass SpatialSelection;
        FrameGraphPass VfxPreparation;
        FrameGraphPass DepthVelocity;
        FrameGraphPass DeferredGBufferStandard;
        FrameGraphPass DeferredGBufferExtended;
        FrameGraphPass DeferredDecals;
        FrameGraphPass DeferredLighting;
        FrameGraphPass ForwardOpaqueTail;
        FrameGraphPass Opaque;
        FrameGraphPass ResolveDepth;
        FrameGraphPass Sky;
        FrameGraphPass Transparency;
        FrameGraphPass IrradynTrace;
        FrameGraphPass IrradynComposite;
        FrameGraphPass ToneMap;
        FrameGraphPass Overlays;
        FrameGraphPass Readback;
        FrameGraphPass Presentation;
    };

    [[nodiscard]] StaticSceneFrameGraph BuildStaticSceneFrameGraph();
    [[nodiscard]] StaticSceneFrameGraph BuildStaticSceneFrameGraph(RenderPath path);
} // namespace Keire::RenderBackend
