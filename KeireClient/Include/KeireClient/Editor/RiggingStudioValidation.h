#pragma once

#include "Keire/Animation/RiggingSystem.h"
#include "Keire/Assets/Asset.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    class RetargetMappingDraft final
    {
      public:
        [[nodiscard]] bool Select(const Keire::AssetId sourceClip, const Keire::AssetId targetModel)
        {
            if (sourceClip == m_SourceClip && targetModel == m_TargetModel)
                return false;
            m_SourceClip = sourceClip;
            m_TargetModel = targetModel;
            Overrides.clear();
            SourceFilter.clear();
            TargetFilter.clear();
            return true;
        }

        std::vector<Keire::AnimationRetargetOverride> Overrides;
        std::string SourceFilter;
        std::string TargetFilter;

      private:
        Keire::AssetId m_SourceClip;
        Keire::AssetId m_TargetModel;
    };

    [[nodiscard]] inline bool RetargetBoneMatchesFilter(std::string_view name, std::string_view filter) noexcept
    {
        const auto lower = [](const unsigned char value)
        { return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value; };
        return filter.empty() || std::search(name.begin(), name.end(), filter.begin(), filter.end(),
                                             [&lower](const unsigned char a, const unsigned char b)
                                             { return lower(a) == lower(b); }) != name.end();
    }

    [[nodiscard]] inline std::string_view RetargetOutputNameError(const std::string_view name) noexcept
    {
        if (name.empty())
            return "Enter a name for the retargeted clip.";
        if (name.size() > 230)
            return "Use a shorter clip name (230 UTF-8 bytes maximum).";
        if (name == "." || name == ".." || name.front() == ' ' || name.back() == ' ' || name.back() == '.')
            return "The clip name cannot begin or end with spaces, or end with a period.";
        if (name.find_first_of("<>:\"/\\|?*") != std::string_view::npos ||
            std::ranges::any_of(name, [](const unsigned char c) { return c < 32; }))
            return "The clip name contains a character that cannot be used in a portable file name.";
        const auto stem = name.substr(0, name.find('.'));
        const auto same = [](const std::string_view value, const std::string_view candidate)
        {
            return value.size() == candidate.size() &&
                   std::equal(value.begin(), value.end(), candidate.begin(), [](const char a, const char b)
                              { return (a >= 'a' && a <= 'z' ? a - 'a' + 'A' : a) == b; });
        };
        const auto matches = [stem, &same](const std::string_view candidate) { return same(stem, candidate); };
        if (matches("CON") || matches("PRN") || matches("AUX") || matches("NUL") ||
            (stem.size() == 4 && stem[3] >= '1' && stem[3] <= '9' &&
             (same(stem.substr(0, 3), "COM") || same(stem.substr(0, 3), "LPT"))))
            return "Choose a clip name that is not a reserved device name.";
        return {};
    }

    [[nodiscard]] inline std::string SuggestedRetargetName(const std::string_view model, const std::string_view clip)
    {
        std::string result = std::string(model) + " " + std::string(clip);
        for (auto& c : result)
            if (static_cast<unsigned char>(c) < 32 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos)
                c = '_';
        return result + " Retargeted";
    }

    [[nodiscard]] inline bool HasPartialRetargetMapping(const Keire::AnimationRetargetDiagnostics& diagnostics) noexcept
    {
        return diagnostics.Compatible() && diagnostics.MappedTrackCount < diagnostics.SourceTrackCount;
    }

    [[nodiscard]] inline bool CanBakeRetarget(const Keire::AnimationRetargetDiagnostics& diagnostics,
                                              const bool reviewedPartialMapping, const bool pendingImportSettings,
                                              const bool sourceImportFailed = false,
                                              const bool targetImportFailed = false) noexcept
    {
        return diagnostics.Compatible() && !pendingImportSettings && !sourceImportFailed && !targetImportFailed &&
               (!HasPartialRetargetMapping(diagnostics) || reviewedPartialMapping);
    }
} // namespace KeireEditor
