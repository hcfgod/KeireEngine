#pragma once

#include "Keire/Api.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace Keire
{
    enum class UiStylePropertyCategory : std::uint8_t
    {
        Layout,
        Size,
        Flex,
        Spacing,
        Position,
        Typography,
        Background,
        Border,
        Effects,
        Transform,
        Clipping,
        Transition,
        Accessibility
    };

    enum class UiStyleValueKind : std::uint8_t
    {
        Number,
        Integer,
        Length,
        Color,
        Keyword,
        Insets,
        Gradient,
        Asset,
        AssetList,
        ShadowList,
        PropertyList,
        DurationList,
        TimingFunctionList,
        String
    };

    struct UiStylePropertyDescriptor
    {
        std::string_view Name;
        std::string_view DisplayName;
        UiStylePropertyCategory Category = UiStylePropertyCategory::Layout;
        UiStyleValueKind ValueKind = UiStyleValueKind::String;
        std::string_view DefaultValue;
        std::string_view Keywords;
        bool Inherited = false;
        bool Animatable = false;
        bool RuntimeSupported = true;
        std::uint32_t MinimumSchemaVersion = 1;
    };

    [[nodiscard]] KEIRE_API std::span<const UiStylePropertyDescriptor> UiStylePropertyDescriptors() noexcept;
    [[nodiscard]] KEIRE_API const UiStylePropertyDescriptor*
    FindUiStylePropertyDescriptor(std::string_view name) noexcept;
    KEIRE_API void ValidateUiStylePropertyValue(std::string_view name, std::string_view value,
                                                std::uint32_t schemaVersion);
} // namespace Keire
