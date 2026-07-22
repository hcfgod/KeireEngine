#pragma once

#include <compare>
#include <cstdint>
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

    struct FrameGraphResourceDescription final
    {
        std::string Name;
        FrameGraphResourceKind Kind = FrameGraphResourceKind::Texture;
        bool Imported = false;
    };

    struct FrameGraphPassDescription final
    {
        std::string Name;
        std::vector<FrameGraphResource> Reads;
        std::vector<FrameGraphResource> Writes;
    };

    struct FrameGraphResourceLifetime final
    {
        std::uint32_t FirstPass = 0;
        std::uint32_t LastPass = 0;
        bool Used = false;
    };

    struct CompiledFrameGraph final
    {
        std::vector<FrameGraphPass> Order;
        std::vector<FrameGraphResourceLifetime> Lifetimes;
        std::vector<std::string> Diagnostics;
    };

    class FrameGraph final
    {
      public:
        [[nodiscard]] FrameGraphResource AddResource(FrameGraphResourceDescription description);
        [[nodiscard]] FrameGraphPass AddPass(FrameGraphPassDescription description);
        [[nodiscard]] CompiledFrameGraph Compile() const;
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
    };

    [[nodiscard]] StaticSceneFrameGraph BuildStaticSceneFrameGraph();
} // namespace Keire::RenderBackend
