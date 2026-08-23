#include "KeireInternal/UiLayoutInternal.h"

#include "Keire/Ui.h"
#include "KeireInternal/FileSystem.h"

#include <imgui.h>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace Keire::Detail
{
    namespace
    {
        constexpr std::uintmax_t MaximumLayoutBytes = std::uintmax_t{1024} * 1024U;
    }

    void LoadUiLayout(const std::filesystem::path& path)
    {
        if (path.empty() || !std::filesystem::exists(path))
            return;

        const auto size = std::filesystem::file_size(path);
        if (size > MaximumLayoutBytes)
            throw UiError("LoadLayout", "layout file exceeds the 1 MiB safety limit: " + path.string());

        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw UiError("LoadLayout", "cannot open " + path.string());

        const std::string contents{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (!input.good() && !input.eof())
            throw UiError("LoadLayout", "cannot read " + path.string());
        ImGui::LoadIniSettingsFromMemory(contents.data(), contents.size());
    }

    void SaveUiLayout(const std::filesystem::path& path)
    {
        if (path.empty())
            return;

        std::size_t size = 0;
        const char* contents = ImGui::SaveIniSettingsToMemory(&size);
        if (size > MaximumLayoutBytes)
            throw UiError("SaveLayout", "layout data exceeds the 1 MiB safety limit: " + path.string());

        try
        {
            WriteTextFileAtomically(path, std::string_view(contents, size));
        }
        catch (const std::exception& exception)
        {
            throw UiError("SaveLayout", exception.what());
        }
    }
} // namespace Keire::Detail
