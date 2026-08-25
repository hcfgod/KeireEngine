#pragma once

namespace Keire::Detail
{
    [[nodiscard]] constexpr bool TransparentBackToFront(const float leftViewDepth, const float rightViewDepth) noexcept
    {
        // Kéire uses +Z forward in view space, so greater visible depths are farther from the camera.
        return leftViewDepth > rightViewDepth;
    }
} // namespace Keire::Detail
