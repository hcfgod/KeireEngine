#pragma once

#include "Keire/Rendering/RenderSystem.h"

#include <string>
#include <string_view>

namespace KeireEditor
{
    [[nodiscard]] inline std::string
    BuildGlobalIlluminationFallbackMessage(const Keire::GlobalIlluminationFallbackReason reason,
                                           const std::string_view effectiveMode)
    {
        const auto fallback = [effectiveMode](const std::string_view explanation)
        { return std::string(explanation) + " Rendering uses " + std::string(effectiveMode) + '.'; };

        switch (reason)
        {
        case Keire::GlobalIlluminationFallbackReason::None:
            return {};
        case Keire::GlobalIlluminationFallbackReason::BakedUnavailable:
            return fallback("Baked indirect lighting is unavailable on the active renderer.");
        case Keire::GlobalIlluminationFallbackReason::RealtimeUnavailable:
            return fallback("Realtime environment lighting is unavailable on the active renderer.");
        case Keire::GlobalIlluminationFallbackReason::IrradynUnavailable:
            return fallback("Irradyn Dynamic GI is unavailable on the active renderer.");
        case Keire::GlobalIlluminationFallbackReason::IrradynRequiresDeferredHybrid:
            return fallback("Irradyn Dynamic GI requires Deferred Hybrid; the selected render path remains active.");
        case Keire::GlobalIlluminationFallbackReason::HybridUnavailable:
            return fallback("Hybrid GI requires both baked lighting and Irradyn on Deferred Hybrid.");
        }
        return fallback("The requested GI mode has an unknown compatibility issue.");
    }
} // namespace KeireEditor
