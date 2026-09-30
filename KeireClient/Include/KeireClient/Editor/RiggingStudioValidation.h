#pragma once

#include "Keire/Animation/RiggingSystem.h"
#include "Keire/Assets/Asset.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"

#include <algorithm>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    [[nodiscard]] inline std::string RiggingStudioClipPreviewName(const Keire::AssetId clip,
                                                                  const Keire::Ref<Keire::AssetSystem>& assets)
    {
        if (!assets || !clip || assets->TryGetType(clip) != Keire::AnimationClipAsset::StaticType())
            throw std::invalid_argument(
                "This animation clip is no longer available. Regenerate the model's runtime assets and try again.");
        if (const auto metadata = assets->TryGetMetadata(clip); metadata && !metadata->DisplayName.empty())
            return metadata->DisplayName;
        return "Animation Clip " + clip.ToString();
    }

    [[nodiscard]] inline Keire::AssetId
    ResolveRiggingStudioRevealAsset(const std::span<const Keire::AssetSourceRecord> records,
                                    const Keire::AssetId asset) noexcept
    {
        if (!asset)
            return {};
        for (const auto& record : records)
            if (record.Id == asset)
                return asset;
        // Runtime-only outputs belong to their imported source; they have no editable file record.
        for (const auto& record : records)
            if (std::find(record.SubAssets.begin(), record.SubAssets.end(), asset) != record.SubAssets.end())
                return record.Id;
        return {};
    }

    template <typename T>
    [[nodiscard]] std::string RetargetAssetLoadError(const Keire::AssetHandle<T>& handle, const std::string_view label)
    {
        const auto state = handle.State();
        if (state != Keire::AssetState::Failed && state != Keire::AssetState::Cancelled)
            return {};
        auto message =
            std::string(label) + (state == Keire::AssetState::Cancelled ? " load was cancelled." : " failed to load.");
        const auto diagnostic = handle.Diagnostic();
        if (!diagnostic.Message.empty())
            message += " " + diagnostic.Message;
        message += " Regenerate the affected model's runtime assets, then select the source clip again.";
        return message;
    }

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
        const auto first = result.find_first_not_of(' ');
        result.erase(0, first == std::string::npos ? result.size() : first);
        if (result.empty())
            result = "Animation";
        constexpr std::string_view suffix = " Retargeted";
        const auto truncate = [&result]
        {
            constexpr std::size_t maximum = 230 - suffix.size();
            if (result.size() <= maximum)
                return;
            auto end = maximum;
            // Imported labels are UTF-8; retain whole characters at the portable filename limit.
            while (end > 0 && (static_cast<unsigned char>(result[end]) & 0xC0) == 0x80)
                --end;
            result.resize(end);
        };
        truncate();
        if (!RetargetOutputNameError(result + std::string(suffix)).empty())
        {
            result.insert(result.begin(), '_');
            truncate();
        }
        return result + std::string(suffix);
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
