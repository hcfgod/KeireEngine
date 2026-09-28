#pragma once

#include "Keire/Rendering/RenderSystem.h"

#include <cstdint>

namespace Keire::Detail
{
    // Values mirror the managed LightingQuality enum; keep the native environment otherwise unchanged.
    [[nodiscard]] inline bool ApplyManagedLightingQuality(RenderEnvironmentSettings& settings,
                                                          const std::uint8_t preset) noexcept
    {
        if (preset > 3)
            return false;
        settings.RequestedRenderPath = preset == 3 ? RenderPath::ForwardPlus : RenderPath::DeferredHybrid;
        settings.RequestedGlobalIllumination =
            preset == 3 ? GlobalIlluminationMode::Disabled : GlobalIlluminationMode::Irradyn;
        if (preset != 3)
            settings.RequestedIrradynQuality = static_cast<IrradynQuality>(2 - preset);
        return true;
    }
} // namespace Keire::Detail
