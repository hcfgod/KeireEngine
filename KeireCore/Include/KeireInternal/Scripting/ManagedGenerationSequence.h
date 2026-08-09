#pragma once

#include <cstdint>
#include <filesystem>

namespace Keire::Detail
{
    [[nodiscard]] std::uint64_t NextManagedGeneration(const std::filesystem::path& outputRoot);
}
