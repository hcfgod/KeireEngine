#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <vector>

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
        Present
    };

    struct FrameGraphResourceDescription final
    {
        std::string Name;
        FrameGraphResourceKind Kind = FrameGraphResourceKind::Texture;
        bool Imported = false;
        std::uint64_t CompatibilityKey = 0;
        std::uint64_t SizeBytes = 0;
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
        FrameGraph Graph;
        CompiledFrameGraph Compiled;
        FrameGraphResource HdrScene;
        FrameGraphResource SampledDepth;
        FrameGraphPass ResourceUploads;
        FrameGraphPass DirectionalShadows;
        FrameGraphPass ForwardPlusCulling;
        FrameGraphPass Opaque;
        FrameGraphPass ResolveDepth;
        FrameGraphPass Sky;
        FrameGraphPass Transparency;
        FrameGraphPass ToneMap;
        FrameGraphPass Overlays;
        FrameGraphPass Readback;
        FrameGraphPass Presentation;
    };

    [[nodiscard]] StaticSceneFrameGraph BuildStaticSceneFrameGraph();
} // namespace Keire::RenderBackend
