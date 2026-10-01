#pragma once

#include "Keire/Math/Math.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace Keire::Detail
{
    [[nodiscard]] inline std::string FootContactDiagnosticEntity()
    {
#if defined(_MSC_VER)
        char value[257]{};
        std::size_t required = 0;
        if (getenv_s(&required, value, sizeof(value), "KEIRE_FOOT_CONTACT_DIAGNOSTICS") != 0 || required == 0)
            return {};
        return value;
#else
        const char* value = std::getenv("KEIRE_FOOT_CONTACT_DIAGNOSTICS");
        if (!value)
            return {};
        const std::string_view name(value);
        return name.size() <= 256 ? std::string(name) : std::string{};
#endif
    }

    // Bone endpoint distance only; this does not measure the skinned sole.
    [[nodiscard]] inline std::optional<float> FootContactPlaneDistance(const Vector3 point, const Vector3 origin,
                                                                       const Vector3 normal) noexcept
    {
        if (!Math::IsFinite(point) || !Math::IsFinite(origin) || !Math::IsFinite(normal))
            return std::nullopt;
        const auto length = std::sqrt(normal.X * normal.X + normal.Y * normal.Y + normal.Z * normal.Z);
        if (!std::isfinite(length) || length <= 0.000001F)
            return std::nullopt;
        const auto value =
            ((point.X - origin.X) * normal.X + (point.Y - origin.Y) * normal.Y + (point.Z - origin.Z) * normal.Z) /
            length;
        return std::isfinite(value) ? std::optional<float>{value} : std::nullopt;
    }

    struct FootContactDiagnostics final
    {
        std::string EntityName = FootContactDiagnosticEntity();
        std::uint64_t Frame = 0;
        std::uint32_t Samples = 0;
        float Elapsed = 0.0F;

        [[nodiscard]] bool Sample(const float deltaSeconds) noexcept
        {
            ++Frame;
            if (Samples >= 3000)
                return false;
            if (std::isfinite(deltaSeconds) && deltaSeconds > 0.0F)
                Elapsed += deltaSeconds;
            if (Samples != 0 && Elapsed < 0.2F)
                return false;
            Elapsed = 0.0F;
            ++Samples;
            return true;
        }
    };
} // namespace Keire::Detail
