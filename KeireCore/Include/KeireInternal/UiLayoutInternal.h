#pragma once

#include <filesystem>

namespace Keire::Detail
{
    void LoadUiLayout(const std::filesystem::path& path);
    void SaveUiLayout(const std::filesystem::path& path);
} // namespace Keire::Detail
