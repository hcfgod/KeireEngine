#pragma once

#include "Keire/Core.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace KeireEditor::Detail
{
    [[nodiscard]] inline std::string
    AssetPickerSearchScopeKey(const std::string_view label, const std::optional<Keire::AssetTypeId> expectedType,
                              const std::optional<Keire::ManagedTypeId> expectedManagedType)
    {
        std::string result;
        result.reserve(label.size() + 80);
        result += std::to_string(label.size());
        result += ':';
        result += label;
        result += '|';
        result += expectedType ? expectedType->ToString() : "-";
        result += '|';
        result += expectedManagedType ? expectedManagedType->ToString() : "-";
        return result;
    }

    [[nodiscard]] inline bool
    ActivateAssetPickerSearchScope(std::string& activeScope, std::string& search, const std::string_view label,
                                   const std::optional<Keire::AssetTypeId> expectedType,
                                   const std::optional<Keire::ManagedTypeId> expectedManagedType)
    {
        auto scope = AssetPickerSearchScopeKey(label, expectedType, expectedManagedType);
        if (scope == activeScope)
            return false;
        activeScope = std::move(scope);
        search.clear();
        return true;
    }

    inline void ClearAssetPickerSearchScope(std::string& activeScope, std::string& search) noexcept
    {
        activeScope.clear();
        search.clear();
    }
} // namespace KeireEditor::Detail
