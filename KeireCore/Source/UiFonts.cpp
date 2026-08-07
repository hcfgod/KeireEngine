#include "KeireInternal/UiFontInternal.h"

#include <imgui.h>

#include <array>
#include <climits>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

namespace Keire::Detail
{
    namespace
    {
        constexpr std::uintmax_t MaximumFontBytes = 16U * 1024U * 1024U;
        constexpr std::size_t MaximumFonts = 8;
        constexpr std::array<ImWchar, 3> IconGlyphRanges{0xE000, 0xF8FF, 0};

        [[nodiscard]] const char* RoleName(const UiFontRole role) noexcept
        {
            switch (role)
            {
            case UiFontRole::Body:
                return "Body";
            case UiFontRole::Heading:
                return "Heading";
            case UiFontRole::Monospace:
                return "Monospace";
            case UiFontRole::Icons:
                return "Icons";
            }
            return "Unknown";
        }

        [[nodiscard]] void* ReadFont(const std::filesystem::path& path, int& size)
        {
            std::error_code error;
            const auto bytes = std::filesystem::file_size(path, error);
            if (error || !std::filesystem::is_regular_file(path, error) || error || bytes == 0 ||
                bytes > MaximumFontBytes || bytes > static_cast<std::uintmax_t>(INT_MAX))
                throw std::invalid_argument("UI font files must be regular, non-empty files no larger than 16 MiB.");
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
                throw std::invalid_argument("A configured UI font could not be opened.");
            auto* data = ImGui::MemAlloc(static_cast<std::size_t>(bytes));
            if (!data)
                throw std::bad_alloc();
            stream.read(static_cast<char*>(data), static_cast<std::streamsize>(bytes));
            if (!stream)
            {
                ImGui::MemFree(data);
                throw std::invalid_argument("A configured UI font could not be read completely.");
            }
            size = static_cast<int>(bytes);
            return data;
        }
    } // namespace

    void ConfigureUiFonts(const UiSpecification& specification)
    {
        if (specification.Fonts.size() > MaximumFonts)
            throw std::invalid_argument("UiSpecification supports at most eight font roles.");
        std::array<bool, 4> configured{};
        auto& io = ImGui::GetIO();
        for (const auto& font : specification.Fonts)
        {
            const auto role = static_cast<std::size_t>(font.Role);
            if (role >= configured.size() || configured[role] || font.Path.empty() || !std::isfinite(font.SizePixels) ||
                font.SizePixels < 8.0F || font.SizePixels > 96.0F)
                throw std::invalid_argument("A configured UI font role is invalid or duplicated.");
            configured[role] = true;
            int dataSize = 0;
            void* data = ReadFont(font.Path, dataSize);
            ImFontConfig config;
            config.FontDataOwnedByAtlas = true;
            const auto debugName = std::string("Keire.") + RoleName(font.Role);
            (void)std::snprintf(config.Name, sizeof(config.Name), "%s", debugName.c_str());
            const auto ranges = font.Role == UiFontRole::Icons ? IconGlyphRanges.data() : nullptr;
            auto* loaded = io.Fonts->AddFontFromMemoryTTF(data, dataSize, font.SizePixels, &config, ranges);
            if (!loaded)
            {
                ImGui::MemFree(data);
                throw std::invalid_argument("A configured UI font could not be decoded.");
            }
            if (font.Role == UiFontRole::Body)
                io.FontDefault = loaded;
        }
    }
} // namespace Keire::Detail
