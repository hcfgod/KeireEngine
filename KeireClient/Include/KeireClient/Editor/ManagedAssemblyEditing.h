#pragma once
#include <cstddef>
#include <filesystem>
#include <span>

namespace KeireEditor
{
    // Checks the draft's original bytes before atomically replacing a validated assembly source.
    void SaveManagedAssemblySettings(const std::filesystem::path& path, std::span<const std::byte> bytes,
                                     std::span<const std::byte> expected);
} // namespace KeireEditor
