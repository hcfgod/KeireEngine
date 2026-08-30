#pragma once

#include "Keire/ECS/Component.h"

#include <array>
#include <string_view>

namespace Keire::Detail
{
    struct RetiredUiComponentType final
    {
        ComponentTypeId Type;
        std::string_view Name;
    };

    inline constexpr std::array RetiredUiComponentTypes{
        RetiredUiComponentType{ComponentTypeId(AssetId(0x4b45495245554943ULL, 0x414e564153000001ULL)), "Canvas"},
        RetiredUiComponentType{ComponentTypeId(AssetId(0x4b45495245554952ULL, 0x4543545452410001ULL)),
                               "Rect Transform"},
        RetiredUiComponentType{ComponentTypeId(AssetId(0x4b45495245554954ULL, 0x4558540000000001ULL)), "UI Text"},
        RetiredUiComponentType{ComponentTypeId(AssetId(0x4b45495245554949ULL, 0x4d41474500000001ULL)), "UI Image"},
        RetiredUiComponentType{ComponentTypeId(AssetId(0x4b45495245554942ULL, 0x5554544f4e000001ULL)), "UI Button"},
        RetiredUiComponentType{ComponentTypeId(AssetId(0x4b4549524555494cULL, 0x41594f5554000001ULL)), "UI Layout"},
        RetiredUiComponentType{ComponentTypeId(AssetId(0x4b45495245554953ULL, 0x4c49444552000001ULL)), "UI Slider"},
        RetiredUiComponentType{ComponentTypeId(AssetId(0x4b45495245554954ULL, 0x4f47474c45000001ULL)), "UI Toggle"},
        RetiredUiComponentType{ComponentTypeId(AssetId(0x4b45495245554949ULL, 0x4e505554464c4401ULL)),
                               "UI Input Field"},
        RetiredUiComponentType{ComponentTypeId(AssetId(0x4b45495245554953ULL, 0x43524f4c4c000001ULL)),
                               "UI Scroll View"},
        RetiredUiComponentType{ComponentTypeId(AssetId(0x4b45495245554941ULL, 0x4343455353000001ULL)),
                               "UI Accessibility"}};

    [[nodiscard]] inline constexpr const RetiredUiComponentType*
    FindRetiredUiComponentType(const ComponentTypeId type) noexcept
    {
        for (const auto& retired : RetiredUiComponentTypes)
            if (retired.Type == type)
                return &retired;
        return nullptr;
    }
} // namespace Keire::Detail
