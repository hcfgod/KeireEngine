#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Keire::Detail
{
    inline constexpr std::uint32_t ManagedWorkspaceMinimumVisualStudioMajor = 18;

    struct VisualStudioInstallation final
    {
        std::array<std::uint32_t, 4> Version{};
        std::filesystem::path Executable;

        [[nodiscard]] bool operator==(const VisualStudioInstallation&) const = default;
    };

    [[nodiscard]] std::vector<VisualStudioInstallation>
    ParseVisualStudioInstallations(std::string_view document) noexcept;
    [[nodiscard]] std::optional<VisualStudioInstallation>
    SelectCompatibleVisualStudioInstallation(std::span<const VisualStudioInstallation> installations,
                                             std::uint32_t minimumMajor);
    [[nodiscard]] bool IsCompatibleVisualStudioDteMoniker(std::wstring_view moniker,
                                                          std::uint32_t minimumMajor) noexcept;
    [[nodiscard]] std::filesystem::path ResolveCompatibleVisualStudioExecutable(
        std::uint32_t minimumMajor = ManagedWorkspaceMinimumVisualStudioMajor) noexcept;
} // namespace Keire::Detail
