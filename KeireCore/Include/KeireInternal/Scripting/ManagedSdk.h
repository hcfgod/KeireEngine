#pragma once

#include "Keire/Scripting/ScriptSystem.h"

#include <filesystem>

namespace Keire::Detail
{
    [[nodiscard]] ManagedSdkConfiguration ReadManagedSdkConfiguration(const std::filesystem::path& projectRoot,
                                                                      ManagedSdkConfiguration fallback);
    void WriteManagedSdkConfiguration(const std::filesystem::path& projectRoot,
                                      const ManagedSdkConfiguration& configuration);
    [[nodiscard]] std::filesystem::path ResolveDotnet(const std::filesystem::path& configured,
                                                      ManagedSdkSelection selection,
                                                      const std::filesystem::path& projectRoot,
                                                      const std::filesystem::path& runtimeRoot);
} // namespace Keire::Detail
