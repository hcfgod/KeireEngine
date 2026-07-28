#pragma once

namespace Keire::Detail
{
    [[nodiscard]] constexpr bool TransparentBackToFront(const float leftViewDepth, const float rightViewDepth) noexcept
    {
        // Kéire cameras look down -Z in view space, so more-negative depths are farther from the camera.
        return leftViewDepth < rightViewDepth;
    }
} // namespace Keire::Detail
