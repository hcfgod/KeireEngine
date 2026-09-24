#pragma once

#include <cstdint>
#include <string_view>

namespace Keire::RenderBackend
{
    enum class RuntimeMaterialPassRole : std::uint8_t
    {
        Primary,
        DepthVelocity,
        DeferredGBufferStandard,
        DeferredGBufferExtended,
        ForwardOpaque,
        ForwardTransparent,
        DecalDBuffer,
        VfxBillboard,
        VfxRibbon,
        VfxCpu,
        Unsupported
    };

    enum class MaterialPassTargetLayout : std::uint8_t
    {
        ForwardColor,
        Velocity,
        GBuffer,
        DBuffer,
        Unsupported
    };

    [[nodiscard]] constexpr std::string_view RuntimeMaterialPassName(const RuntimeMaterialPassRole role) noexcept
    {
        switch (role)
        {
        case RuntimeMaterialPassRole::Primary:
            return "primary";
        case RuntimeMaterialPassRole::DepthVelocity:
            return "depthVelocity";
        case RuntimeMaterialPassRole::DeferredGBufferStandard:
            return "deferredGBufferStandard";
        case RuntimeMaterialPassRole::DeferredGBufferExtended:
            return "deferredGBufferExtended";
        case RuntimeMaterialPassRole::ForwardOpaque:
            return "forwardOpaque";
        case RuntimeMaterialPassRole::ForwardTransparent:
            return "forwardTransparent";
        case RuntimeMaterialPassRole::DecalDBuffer:
            return "decalDBuffer";
        case RuntimeMaterialPassRole::VfxBillboard:
            return "vfxBillboard";
        case RuntimeMaterialPassRole::VfxRibbon:
            return "vfxRibbon";
        case RuntimeMaterialPassRole::VfxCpu:
            return "vfxCpu";
        case RuntimeMaterialPassRole::Unsupported:
            break;
        }
        return {};
    }

    [[nodiscard]] constexpr RuntimeMaterialPassRole
    RuntimeMaterialPassRoleFromName(const std::string_view name) noexcept
    {
        for (const auto role :
             {RuntimeMaterialPassRole::Primary, RuntimeMaterialPassRole::DepthVelocity,
              RuntimeMaterialPassRole::DeferredGBufferStandard, RuntimeMaterialPassRole::DeferredGBufferExtended,
              RuntimeMaterialPassRole::ForwardOpaque, RuntimeMaterialPassRole::ForwardTransparent,
              RuntimeMaterialPassRole::DecalDBuffer, RuntimeMaterialPassRole::VfxBillboard,
              RuntimeMaterialPassRole::VfxRibbon, RuntimeMaterialPassRole::VfxCpu})
        {
            if (RuntimeMaterialPassName(role) == name)
                return role;
        }
        return RuntimeMaterialPassRole::Unsupported;
    }

    [[nodiscard]] constexpr MaterialPassTargetLayout
    MaterialPassTargetLayoutForRole(const RuntimeMaterialPassRole role) noexcept
    {
        switch (role)
        {
        case RuntimeMaterialPassRole::Primary:
        case RuntimeMaterialPassRole::ForwardOpaque:
        case RuntimeMaterialPassRole::ForwardTransparent:
        case RuntimeMaterialPassRole::VfxBillboard:
        case RuntimeMaterialPassRole::VfxRibbon:
        case RuntimeMaterialPassRole::VfxCpu:
            return MaterialPassTargetLayout::ForwardColor;
        case RuntimeMaterialPassRole::DepthVelocity:
            return MaterialPassTargetLayout::Velocity;
        case RuntimeMaterialPassRole::DeferredGBufferStandard:
        case RuntimeMaterialPassRole::DeferredGBufferExtended:
            return MaterialPassTargetLayout::GBuffer;
        case RuntimeMaterialPassRole::DecalDBuffer:
            return MaterialPassTargetLayout::DBuffer;
        case RuntimeMaterialPassRole::Unsupported:
            break;
        }
        return MaterialPassTargetLayout::Unsupported;
    }

    [[nodiscard]] constexpr bool IsDeferredGBufferRole(const RuntimeMaterialPassRole role) noexcept
    {
        return role == RuntimeMaterialPassRole::DeferredGBufferStandard ||
               role == RuntimeMaterialPassRole::DeferredGBufferExtended;
    }
} // namespace Keire::RenderBackend
